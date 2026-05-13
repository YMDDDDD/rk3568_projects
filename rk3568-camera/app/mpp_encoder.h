#pragma once
#include "frame_ref.h"
#include "spsc_queue.h"
#include <QObject>
#include <QTimer>
#include <functional>
#include <cstdint>

class BufferPool;

// MPP 不透明类型前向声明（与 rk_mpi.h 兼容）
struct MppApi_t;
typedef struct MppApi_t* MppApiPtr;
typedef void* MppCtx;
typedef void* MppBuffer;
typedef void* MppBufferGroup;
typedef void* MppFrame;
typedef void* MppPacket;

// ============================================================================
// MPP 硬件编码器（主线程 QTimer 驱动）
// ============================================================================

class MppEncoder : public QObject {
    Q_OBJECT
public:
    using NalCallback = std::function<void(const uint8_t* data, size_t len, uint64_t pts)>;

    explicit MppEncoder(QObject *parent = nullptr);
    ~MppEncoder() override;

    bool init(uint32_t width, uint32_t height, uint32_t stride);
    void start(SPSCQueue<FrameRefPtr> &encodeQueue, BufferPool &pool);
    void stop();
    void setNalCallback(NalCallback cb) { nalCallback_ = std::move(cb); }
    bool isRunning() const { return running_; }

signals:
    void heartbeat();
    void frameEncoded(size_t bytes);

private:
    void tick();
    bool encodeOneFrame(FrameRefPtr ref);

    MppCtx              mppCtx_     = nullptr;
    MppApiPtr           mppApi_     = nullptr;
    MppBufferGroup      bufGrp_     = nullptr;
    MppBuffer           frmBuf_     = nullptr;

    SPSCQueue<FrameRefPtr> *encodeQueue_ = nullptr;
    BufferPool           *bufferPool_  = nullptr;
    NalCallback           nalCallback_;
    QTimer                timer_;
    bool                  running_     = false;
    bool                  mppReady_    = false;
};
