#include "capture.h"
#include "pts_clock.h"
#include <spdlog/spdlog.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <linux/videodev2.h>

CaptureThread::CaptureThread(QObject *parent) : QObject(parent) {
    timer_.setInterval(33);  // ~30fps
    QObject::connect(&timer_, &QTimer::timeout, this, &CaptureThread::tick);
}

CaptureThread::~CaptureThread() {
    stop();
}

bool CaptureThread::open(const char *device, uint32_t width, uint32_t height, uint32_t fps) {
    devicePath_ = device;
    configWidth_  = width;
    configHeight_ = height;
    configFps_    = fps;

    v4l2Fd_ = ::open(device, O_RDWR | O_NONBLOCK);
    if (v4l2Fd_ < 0) {
        spdlog::error("Cannot open {}: {}", device, strerror(errno));
        return false;
    }

    if (!configureFormat(width, height, fps)) return false;
    if (!pool_.init(v4l2Fd_, width, height, configStride_)) return false;
    if (!pool_.streamOn()) return false;

    spdlog::info("Capture opened: {} {}x{}@{}fps (main thread QTimer)", device, width, height, fps);
    return true;
}

bool CaptureThread::configureFormat(uint32_t width, uint32_t height, uint32_t fps) {
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if (ioctl(v4l2Fd_, VIDIOC_G_FMT, &fmt) < 0) {
        spdlog::error("VIDIOC_G_FMT failed: {}", strerror(errno));
        return false;
    }

    fmt.fmt.pix_mp.width       = width;
    fmt.fmt.pix_mp.height      = height;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field       = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes  = 1;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = width * height * 3 / 2;

    if (ioctl(v4l2Fd_, VIDIOC_S_FMT, &fmt) < 0) {
        spdlog::error("VIDIOC_S_FMT failed: {}", strerror(errno));
        return false;
    }

    // 读取实际 stride（可能 > width，如 2112 vs 1920）
    configStride_ = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
    if (configStride_ == 0) configStride_ = width;
    spdlog::info("Format set: {}x{} stride={} NV12 @ {}fps", width, height, configStride_, fps);
    return true;
}

void CaptureThread::start() {
    running_ = true;
    frameCount_ = 0;
    timer_.start();
    spdlog::info("Capture timer started");
}

void CaptureThread::stop() {
    running_ = false;
    timer_.stop();
    pool_.streamOff();
    ::close(v4l2Fd_);
    v4l2Fd_ = -1;
    spdlog::info("Capture stopped ({} frames)", frameCount_);
}

void CaptureThread::restart() {
    stop();
    open(devicePath_.toUtf8().constData(), configWidth_, configHeight_, configFps_);
    start();
}

void CaptureThread::tick() {
    if (!running_) return;

    // 尽量多取几帧（避免积压）
    for (int i = 0; i < 4; i++) {
        uint32_t index;
        if (!pool_.dequeueBuffer(index)) break;

        frameCount_++;
        if (frameCount_ == 1 || frameCount_ % 30 == 0) {
            spdlog::info("Capture: frame {} idx={}", frameCount_, index);
        }

        auto ref = pool_.acquire(index);
        if (!ref) continue;

        ref->pts = PtsClock::nowUs();
        displayQueue_.tryPush(ref);
        encodeQueue_.push(ref);
    }

    emit heartbeat();
}
