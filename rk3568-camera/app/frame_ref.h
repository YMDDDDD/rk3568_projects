#pragma once
#include "config.h"
#include <cstdint>
#include <atomic>
#include <memory>

// ============================================================================
// 统一帧描述结构 —— 替代 AVFrame 深拷贝，只传 dmabuf fd + 元数据
// ============================================================================

struct FrameRef {
    int         dmabufFd   = -1;       // DMA-BUF 文件描述符
    uint64_t    pts        = 0;        // CLOCK_MONOTONIC 微秒
    uint32_t    width      = 0;
    uint32_t    height     = 0;
    uint32_t    stride     = 0;        // Y 平面行步长（可能 > width）
    uint32_t    format     = 0;        // V4L2 fourcc
    uint32_t    size       = 0;        // buffer 字节数
    uint32_t    v4l2Index  = 0;        // V4L2 buffer index
    int         v4l2Fd     = -1;       // V4L2 设备 fd
    void       *mmapAddr   = nullptr;  // V4L2 mmap 虚拟地址

    // 引用计数
    void addRef() { refCount.fetch_add(1, std::memory_order_relaxed); }
    void release() { refCount.fetch_sub(1, std::memory_order_release); }
    int  ref() const { return refCount.load(std::memory_order_acquire); }

    void init(int dmaFd, uint64_t p, uint32_t w, uint32_t h, uint32_t s,
              uint32_t fmt, uint32_t sz, uint32_t idx, int vfd, void *mmap, int initRef = 1) {
        this->dmabufFd  = dmaFd;
        this->pts       = p;
        this->width     = w;
        this->height    = h;
        this->stride    = s;
        this->format    = fmt;
        this->size      = sz;
        this->v4l2Index = idx;
        this->v4l2Fd    = vfd;
        this->mmapAddr  = mmap;
        this->refCount.store(initRef, std::memory_order_relaxed);
    }

private:
    std::atomic<int> refCount{0};
};

using FrameRefPtr = std::shared_ptr<FrameRef>;
