#include "buffer_pool.h"
#include <spdlog/spdlog.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

BufferPool::~BufferPool() {
    if (streaming_) streamOff();

    for (int i = 0; i < numBuffers_; i++) {
        if (mmapAddrs_[i] && mmapAddrs_[i] != MAP_FAILED) {
            munmap(mmapAddrs_[i], bufferSizes_[i]);
        }
        if (dmabufFds_[i] >= 0) {
            close(dmabufFds_[i]);
        }
    }

    if (v4l2Fd_ >= 0) {
        struct v4l2_requestbuffers reqbufs = {};
        reqbufs.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        reqbufs.memory = V4L2_MEMORY_MMAP;
        reqbufs.count  = 0;
        ioctl(v4l2Fd_, VIDIOC_REQBUFS, &reqbufs);
    }
}

bool BufferPool::init(int v4l2Fd, uint32_t width, uint32_t height, uint32_t stride) {
    v4l2Fd_  = v4l2Fd;
    width_   = width;
    height_  = height;
    stride_  = stride;
    numBuffers_ = V4L2_BUFFER_COUNT;

    spdlog::info("BufferPool: REQBUFS count={}...", numBuffers_);
    // REQBUFS
    struct v4l2_requestbuffers reqbufs = {};
    reqbufs.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = V4L2_MEMORY_MMAP;
    reqbufs.count  = numBuffers_;
    if (ioctl(v4l2Fd_, VIDIOC_REQBUFS, &reqbufs) < 0) {
        spdlog::error("VIDIOC_REQBUFS failed: {}", strerror(errno));
        return false;
    }
    numBuffers_ = reqbufs.count;
    spdlog::info("V4L2 REQBUFS: {} buffers allocated", numBuffers_);

    dmabufFds_.resize(numBuffers_, -1);
    mmapAddrs_.resize(numBuffers_, nullptr);
    bufferSizes_.resize(numBuffers_, 0);

    // QUERYBUF + mmap + EXPBUF（单 plane 模式：RK NV12 连续存储）
    for (uint32_t i = 0; i < numBuffers_; i++) {
        struct v4l2_buffer buf = {};
        struct v4l2_plane  planes[1] = {};
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.index    = i;
        buf.length   = 1;
        buf.m.planes = planes;

        if (ioctl(v4l2Fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            spdlog::error("QUERYBUF idx={} failed: {}", i, strerror(errno));
            return false;
        }
        spdlog::info("BufferPool: buf[{}] QUERYBUF OK, length={}", i, planes[0].length);

        bufferSizes_[i] = planes[0].length;

        void *addr = mmap(nullptr, planes[0].length,
                          PROT_READ | PROT_WRITE, MAP_SHARED,
                          v4l2Fd_, planes[0].m.mem_offset);
        if (addr == MAP_FAILED) {
            spdlog::error("mmap idx={} failed: {}", i, strerror(errno));
            return false;
        }
        mmapAddrs_[i] = addr;
        spdlog::info("BufferPool: buf[{}] mmap OK addr={}", i, addr);

        struct v4l2_exportbuffer expbuf = {};
        expbuf.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        expbuf.index = i;
        expbuf.plane = 0;
        if (ioctl(v4l2Fd_, VIDIOC_EXPBUF, &expbuf) < 0) {
            spdlog::error("EXPBUF idx={} failed: {}", i, strerror(errno));
            return false;
        }
        dmabufFds_[i] = expbuf.fd;
    }

    // VIDIOC_QBUF 所有 buffer 入队
    for (uint32_t i = 0; i < numBuffers_; i++) {
        queueBuffer(i);
    }

    spdlog::info("BufferPool initialized: {} buffers, {}x{}", numBuffers_, width_, height_);
    return true;
}

FrameRefPtr BufferPool::acquire(uint32_t index) {
    if (index >= static_cast<uint32_t>(numBuffers_)) return nullptr;

    auto *raw = new FrameRef();
    raw->dmabufFd  = dmabufFds_[index];
    raw->width     = width_;
    raw->height    = height_;
    raw->stride    = stride_;
    raw->format    = MAIN_FORMAT;
    raw->size      = bufferSizes_[index];
    raw->v4l2Index = index;
    raw->v4l2Fd    = v4l2Fd_;
    raw->mmapAddr  = mmapAddrs_[index];

    // 自定义 deleter：shared_ptr 引用计数归零时自动 QBUF 归还 V4L2
    auto deleter = [this](FrameRef *ref) {
        if (ref) {
            this->queueBuffer(ref->v4l2Index);
            delete ref;
        }
    };

    return FrameRefPtr(raw, deleter);
}

bool BufferPool::dequeueBuffer(uint32_t &index) {
    struct v4l2_buffer buf = {};
    struct v4l2_plane  planes[1] = {};
    buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory   = V4L2_MEMORY_MMAP;
    buf.length   = 1;
    buf.m.planes = planes;

    if (ioctl(v4l2Fd_, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) return false;
        spdlog::error("VIDIOC_DQBUF failed: {}", strerror(errno));
        return false;
    }

    index = buf.index;
    return true;
}

bool BufferPool::queueBuffer(uint32_t index) {
    struct v4l2_buffer buf = {};
    struct v4l2_plane  planes[1] = {};
    buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory   = V4L2_MEMORY_MMAP;
    buf.index    = index;
    buf.length   = 1;
    buf.m.planes = planes;

    if (ioctl(v4l2Fd_, VIDIOC_QBUF, &buf) < 0) {
        spdlog::error("VIDIOC_QBUF idx={} failed: {}", index, strerror(errno));
        return false;
    }
    return true;
}

bool BufferPool::streamOn() {
    if (streaming_) return true;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(v4l2Fd_, VIDIOC_STREAMON, &type) < 0) {
        spdlog::error("VIDIOC_STREAMON failed: {}", strerror(errno));
        return false;
    }
    streaming_ = true;
    spdlog::info("V4L2 stream ON");
    return true;
}

bool BufferPool::streamOff() {
    if (!streaming_) return true;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(v4l2Fd_, VIDIOC_STREAMOFF, &type) < 0) {
        spdlog::error("VIDIOC_STREAMOFF failed: {}", strerror(errno));
        return false;
    }
    streaming_ = false;
    spdlog::info("V4L2 stream OFF");
    return true;
}
