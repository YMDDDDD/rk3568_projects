#pragma once
#include "config.h"
#include <QObject>
#include <QTimer>
#include <atomic>

// ============================================================================
// 性能监控：FPS / 队列深度 / 编码延迟 / 推理延迟 / CPU温度
// ============================================================================

struct PerfStats {
    float captureFps        = 0;
    float displayFps        = 0;
    float encodeFps         = 0;
    float detectFps         = 0;
    int   displayQueueDepth = 0;
    int   encodeQueueDepth  = 0;
    float encodeLatencyMs   = 0;   // 编码单帧平均耗时
    float detectLatencyMs   = 0;   // 推理单帧平均耗时
    float cpuTemp           = 0;   // 从 /sys/class/thermal/ 读取
    float npuTemp           = 0;
};

class PerfMonitor : public QObject {
    Q_OBJECT
public:
    explicit PerfMonitor(QObject *parent = nullptr);

    void start(int intervalMs = PERF_STATS_INTERVAL_MS);
    void stop();

    // 各线程每帧调用
    void tickCapture();
    void tickDisplay();
    void tickEncode(float latencyMs);
    void tickDetect(float latencyMs);

    // 外部更新队列深度
    void setDisplayQueueDepth(int depth);
    void setEncodeQueueDepth(int depth);

    PerfStats snapshot();

signals:
    void statsUpdated(PerfStats stats);

private:
    void update();
    float readTemp(const char *thermalZone);

    QTimer          timer_;
    std::atomic<int> captureCount_{0};
    std::atomic<int> displayCount_{0};
    std::atomic<int> encodeCount_{0};
    std::atomic<int> detectCount_{0};
    std::atomic<int> encodeLatencySum_{0};
    std::atomic<int> encodeFrameCount_{0};
    std::atomic<int> detectLatencySum_{0};
    std::atomic<int> detectFrameCount_{0};
    std::atomic<int> displayQueueDepth_{0};
    std::atomic<int> encodeQueueDepth_{0};
};
