#pragma once
#include "config.h"
#include <cstdint>
#include <memory>

// ============================================================================
// 统一帧描述结构 —— 只传 dmabuf fd + 元数据
// 引用计数由 std::shared_ptr 管理，析构自动归还 V4L2 buffer
// ============================================================================

struct FrameRef {
    int         dmabufFd   = -1;
    uint64_t    pts        = 0;
    uint32_t    width      = 0;
    uint32_t    height     = 0;
    uint32_t    stride     = 0;
    uint32_t    format     = 0;
    uint32_t    size       = 0;
    uint32_t    v4l2Index  = 0;
    int         v4l2Fd     = -1;
    void       *mmapAddr   = nullptr;
};

using FrameRefPtr = std::shared_ptr<FrameRef>;
