#pragma once
#include "config.h"
#include "frame_ref.h"
#include "buffer_pool.h"
#include "spsc_queue.h"
#include <QObject>
#include <QTimer>

// ============================================================================
// 主线程采集器：用 QTimer 在主线程事件循环中驱动 V4L2 采集
// （RK ISP 驱动要求 V4L2 操作在主线程）
// ============================================================================

class CaptureThread : public QObject {
    Q_OBJECT
public:
    explicit CaptureThread(QObject *parent = nullptr);
    ~CaptureThread() override;

    bool open(const char *device, uint32_t width, uint32_t height, uint32_t fps);
    void start();
    void start(class MainWindow *window);  // 带渲染目标的 start
    void stop();
    void wait(int timeoutMs = 3000) { Q_UNUSED(timeoutMs); }  // QTimer 不需要等待
    void restart();

    SPSCQueue<FrameRefPtr>& displayQueue() { return displayQueue_; }
    SPSCQueue<FrameRefPtr>& encodeQueue()  { return encodeQueue_;  }
    BufferPool& pool() { return pool_; }
    uint32_t stride() const { return configStride_; }
    bool isRunning() const { return running_; }

signals:
    void error(const QString &msg);
    void heartbeat();

private:
    void tick();  // QTimer 回调

    bool configureFormat(uint32_t width, uint32_t height, uint32_t fps);

    int                       v4l2Fd_ = -1;
    uint32_t                  configWidth_  = 0;
    uint32_t                  configHeight_ = 0;
    uint32_t                  configStride_ = 0;
    uint32_t                  configFps_    = 0;
    BufferPool                pool_;
    SPSCQueue<FrameRefPtr>    displayQueue_{DISPLAY_QUEUE_SIZE};
    SPSCQueue<FrameRefPtr>    encodeQueue_{ENCODE_QUEUE_SIZE};
    QTimer                    timer_;
    bool                      running_ = false;
    QString                   devicePath_;
    int                       frameCount_ = 0;
};
