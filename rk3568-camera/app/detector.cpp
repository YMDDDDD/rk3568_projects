#include "detector.h"
#include <spdlog/spdlog.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <cstring>

extern "C" {
#include <rknn_api.h>
}

DetectThread::DetectThread(QObject *parent) : QThread(parent) {}

DetectThread::~DetectThread() {
    stop();
    wait(5000);
    if (rknnCtx_) rknn_destroy(rknnCtx_);
    free(inputBuf_);
}

bool DetectThread::open(const char *device, int width, int height) {
    devicePath_ = device;
    detWidth_   = width;
    detHeight_  = height;

    int fd = ::open(device, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        spdlog::error("Detector: open {} failed: {}", device, strerror(errno));
        return false;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctl(fd, VIDIOC_G_FMT, &fmt);
    fmt.fmt.pix_mp.width       = (uint32_t)width;
    fmt.fmt.pix_mp.height      = (uint32_t)height;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field       = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes  = 1;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        spdlog::error("Detector: S_FMT failed: {}", strerror(errno));
        ::close(fd); return false;
    }
    int bpl = (int)fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
    spdlog::info("Detector: {} {}x{} bpl={}", device, width, height, bpl);

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count  = 4;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        spdlog::error("Detector: REQBUFS failed");
        ::close(fd); return false;
    }

    for (uint32_t i = 0; i < 4; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane  planes[1];
        memset(&buf, 0, sizeof(buf));
        memset(&planes, 0, sizeof(planes));
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.index    = i;
        buf.length   = 1;
        buf.m.planes = planes;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            spdlog::error("Detector: QUERYBUF[{}] failed", i);
            ::close(fd); return false;
        }
        mmapAddrs_[i] = mmap(NULL, planes[0].length, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, planes[0].m.mem_offset);
        bufSizes_[i]  = planes[0].length;
        ioctl(fd, VIDIOC_QBUF, &buf);
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctl(fd, VIDIOC_STREAMON, &type);

    v4l2Fd_ = fd;
    spdlog::info("Detector: V4L2 ready");
    return true;
}

bool DetectThread::loadModel(const char *rknnPath) {
    FILE *fp = fopen(rknnPath, "rb");
    if (!fp) { spdlog::error("Cannot open RKNN model: {}", rknnPath); return false; }
    fseek(fp, 0, SEEK_END);
    size_t s = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *d = (uint8_t*)malloc(s);
    fread(d, 1, s, fp);
    fclose(fp);
    if (rknn_init(&rknnCtx_, d, s, 0, nullptr) < 0) { free(d); return false; }
    free(d);
    rknn_input_output_num io;
    if (rknn_query(rknnCtx_, RKNN_QUERY_IN_OUT_NUM, &io, sizeof(io)) == RKNN_SUCC) {
        rknn_tensor_attr a; memset(&a, 0, sizeof(a)); a.index = 0;
        rknn_query(rknnCtx_, RKNN_QUERY_INPUT_ATTR, &a, sizeof(a));
        inputSize_ = a.size;
        inputBuf_  = (uint8_t*)malloc(inputSize_);
    }
    spdlog::info("Detector: model loaded, input {} bytes", inputSize_);
    return true;
}

void DetectThread::stop() {
    running_.store(false, std::memory_order_relaxed);
}

void DetectThread::restart() {
    stop(); wait(5000);
    if (v4l2Fd_ >= 0) { ::close(v4l2Fd_); v4l2Fd_ = -1; }
    open(devicePath_.toUtf8().constData(), detWidth_, detHeight_);
    start();
}

void DetectThread::preprocess(void *nv12Data, uint8_t *inputBuf) {
    if (inputBuf && nv12Data) memcpy(inputBuf, nv12Data, inputSize_);
}

QVector<Detection> DetectThread::postProcess(float *output, int width, int height) {
    Q_UNUSED(width) Q_UNUSED(height) Q_UNUSED(output)
    return {};
}

void DetectThread::run() {
    running_.store(true, std::memory_order_relaxed);
    while (running_.load(std::memory_order_relaxed)) {
        struct v4l2_buffer buf;
        struct v4l2_plane  planes[1];
        memset(&buf, 0, sizeof(buf));
        memset(&planes, 0, sizeof(planes));
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.length   = 1;
        buf.m.planes = planes;

        if (ioctl(v4l2Fd_, VIDIOC_DQBUF, &buf) < 0) { usleep(5000); continue; }
        int idx = buf.index;
        uint8_t *nv12 = (uint8_t*)mmapAddrs_[idx];

        skipCount_++;
        if (skipCount_ <= DETECT_SKIP_FRAMES) { ioctl(v4l2Fd_, VIDIOC_QBUF, &buf); continue; }
        skipCount_ = 0;

        if (rknnCtx_ && inputBuf_) {
            preprocess(nv12, inputBuf_);
            rknn_input inputs[1] = {};
            inputs[0].index = 0; inputs[0].type = RKNN_TENSOR_UINT8;
            inputs[0].buf = inputBuf_; inputs[0].size = inputSize_;
            inputs[0].fmt = RKNN_TENSOR_NHWC;
            rknn_inputs_set(rknnCtx_, 1, inputs);
            rknn_run(rknnCtx_, nullptr);

            rknn_output outputs[3] = {};
            for (int i = 0; i < 3; i++) { outputs[i].want_float = 1; outputs[i].is_prealloc = 0; }
            rknn_outputs_get(rknnCtx_, 3, outputs, nullptr);
            auto results = postProcess((float*)outputs[0].buf, detWidth_, detHeight_);
            rknn_outputs_release(rknnCtx_, 3, outputs);
            if (!results.isEmpty()) emit detectionReady(results);
        }

        ioctl(v4l2Fd_, VIDIOC_QBUF, &buf);
        emit heartbeat();
    }

    if (v4l2Fd_ >= 0) {
        enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ioctl(v4l2Fd_, VIDIOC_STREAMOFF, &t);
        ::close(v4l2Fd_); v4l2Fd_ = -1;
    }
    spdlog::info("Detect thread stopped");
}
