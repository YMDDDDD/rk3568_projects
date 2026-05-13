#include "perf_monitor.h"
#include <spdlog/spdlog.h>
#include <QFile>
#include <QTextStream>

PerfMonitor::PerfMonitor(QObject *parent) : QObject(parent) {
    QObject::connect(&timer_, &QTimer::timeout, this, &PerfMonitor::update);
}

void PerfMonitor::start(int intervalMs) {
    timer_.start(intervalMs);
    spdlog::info("PerfMonitor started, interval={}ms", intervalMs);
}

void PerfMonitor::stop() {
    timer_.stop();
}

void PerfMonitor::tickCapture()   { captureCount_.fetch_add(1, std::memory_order_relaxed); }
void PerfMonitor::tickDisplay()   { displayCount_.fetch_add(1, std::memory_order_relaxed); }
void PerfMonitor::tickEncode(float latencyMs) {
    encodeCount_.fetch_add(1, std::memory_order_relaxed);
    encodeLatencySum_.fetch_add(static_cast<int>(latencyMs * 1000), std::memory_order_relaxed);
    encodeFrameCount_.fetch_add(1, std::memory_order_relaxed);
}
void PerfMonitor::tickDetect(float latencyMs) {
    detectCount_.fetch_add(1, std::memory_order_relaxed);
    detectLatencySum_.fetch_add(static_cast<int>(latencyMs * 1000), std::memory_order_relaxed);
    detectFrameCount_.fetch_add(1, std::memory_order_relaxed);
}

void PerfMonitor::setDisplayQueueDepth(int depth) { displayQueueDepth_.store(depth, std::memory_order_relaxed); }
void PerfMonitor::setEncodeQueueDepth(int depth)  { encodeQueueDepth_.store(depth, std::memory_order_relaxed); }

PerfStats PerfMonitor::snapshot() {
    PerfStats s;
    s.captureFps        = static_cast<float>(captureCount_.load());
    s.displayFps        = static_cast<float>(displayCount_.load());
    s.encodeFps         = static_cast<float>(encodeCount_.load());
    s.detectFps         = static_cast<float>(detectCount_.load());
    s.displayQueueDepth = displayQueueDepth_.load();
    s.encodeQueueDepth  = encodeQueueDepth_.load();

    int encFrames = encodeFrameCount_.load();
    if (encFrames > 0)
        s.encodeLatencyMs = static_cast<float>(encodeLatencySum_.load()) / encFrames / 1000.0f;

    int detFrames = detectFrameCount_.load();
    if (detFrames > 0)
        s.detectLatencyMs = static_cast<float>(detectLatencySum_.load()) / detFrames / 1000.0f;

    s.cpuTemp = readTemp("/sys/class/thermal/thermal_zone0/temp");
    s.npuTemp = readTemp("/sys/class/thermal/thermal_zone1/temp");

    return s;
}

void PerfMonitor::update() {
    PerfStats stats = snapshot();

    // 清零计数器（原子操作，每周期重置）
    captureCount_.store(0, std::memory_order_relaxed);
    displayCount_.store(0, std::memory_order_relaxed);
    encodeCount_.store(0, std::memory_order_relaxed);
    detectCount_.store(0, std::memory_order_relaxed);
    encodeLatencySum_.store(0, std::memory_order_relaxed);
    encodeFrameCount_.store(0, std::memory_order_relaxed);
    detectLatencySum_.store(0, std::memory_order_relaxed);
    detectFrameCount_.store(0, std::memory_order_relaxed);

    emit statsUpdated(stats);
}

float PerfMonitor::readTemp(const char *thermalZone) {
    QFile f(thermalZone);
    if (f.open(QIODevice::ReadOnly)) {
        QTextStream in(&f);
        int temp;
        in >> temp;
        return static_cast<float>(temp) / 1000.0f;
    }
    return 0.0f;
}
