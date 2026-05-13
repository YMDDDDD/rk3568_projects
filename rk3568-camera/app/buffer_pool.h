#pragma once
#include "config.h"
#include "frame_ref.h"
#include <vector>
#include <memory>
#include <linux/videodev2.h>

// ============================================================================
// DMA-BUF BufferPool：管理 V4L2 mmap buffer，dmabuf fd 零拷贝
// 全程不拷贝帧数据，只传 dmabuf fd + 引用计数
// ============================================================================

class BufferPool {
public:
    BufferPool() = default;
    ~BufferPool();

    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    // V4L2 REQBUFS + QUERYBUF + mmap + EXPBUF
    bool init(int v4l2Fd, uint32_t width, uint32_t height, uint32_t stride);

    // 采集线程: dequeue → 分配 FrameRef
    FrameRefPtr acquire(uint32_t index);

    // 消费者: 减少引用计数，归零时归还 V4L2 (VIDIOC_QBUF)
    void release(FrameRefPtr ref);

    // V4L2 原生 dequeue/queue
    bool dequeueBuffer(uint32_t &index);
    bool queueBuffer(uint32_t index);

    // 流开关
    bool streamOn();
    bool streamOff();

    int  fd() const { return v4l2Fd_; }
    int  numBuffers() const { return numBuffers_; }

private:
    bool exportDmaBuf(uint32_t index);

    int                   v4l2Fd_       = -1;
    int                   numBuffers_   = 0;
    uint32_t              width_        = 0;
    uint32_t              height_       = 0;
    uint32_t              stride_       = 0;  // Y 平面 bytesperline

    // V4L2 原始 buffer 信息
    std::vector<int>      dmabufFds_;    // EXPBUF 导出的 dmabuf fd
    std::vector<void*>    mmapAddrs_;    // mmap 虚拟地址
    std::vector<uint32_t> bufferSizes_;  // buffer 大小

    bool                  streaming_    = false;
};
