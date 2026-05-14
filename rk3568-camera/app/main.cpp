#include "mainwindow.h"
#include "capture.h"
#include "mpp_encoder.h"
#include "rtsp_server.h"
#include "segment_recorder.h"
#include "detector.h"
#include "watchdog.h"
#include "perf_monitor.h"
#include "video_widget.h"
#include "config.h"
#include <QApplication>
#include <QTimer>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <cstdio>

extern "C" {
#include <libavformat/avformat.h>
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    auto logger = spdlog::stdout_color_mt("camera");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::info("=== RK3568 Camera v1.3 ===");

    avformat_network_init();

    MainWindow window;

    // --- 采集 ---
    CaptureThread capture;
    if (!capture.open(VIDEO0_DEV, MAIN_WIDTH, MAIN_HEIGHT, MAIN_FPS)) {
        spdlog::error("Capture open failed");
        return -1;
    }

    // --- MPP 编码器 ---
    MppEncoder encoder;
    bool encOk = encoder.init(MAIN_WIDTH, MAIN_HEIGHT, capture.stride());
    spdlog::info("MPP encoder init: {}", encOk ? "OK" : "FAIL");

    // --- RTSP ---
    RtspServer rtsp;
    rtsp.start(RTSP_PORT, RTSP_MOUNT);

    // --- 分段录像 ---
    SegmentRecorder recorder;

    // 编码 NAL → 文件 + RTSP
    encoder.setNalCallback([&](const uint8_t *data, size_t len, uint64_t /*pts*/) {
        rtsp.feedNALU(data, len, 0);
        if (recorder.isRecording()) recorder.feedNALU(data, len);
    });

    // --- 监控 ---
    PerfMonitor perf;
    perf.start();

    window.setModules(&capture, &rtsp, &recorder, nullptr, nullptr, &perf);
    window.setCaptureRunning(true);

    // YOLO 检测（video1 → 640×640 → RKNN）
    DetectThread detector;
    if (detector.open(VIDEO1_DEV, AI_WIDTH, AI_HEIGHT)) {
        detector.loadModel(RKNN_MODEL_PATH);
        QObject::connect(&detector, &DetectThread::detectionReady,
                         &window, &MainWindow::onDetection);
        detector.start();
        spdlog::info("YOLO detector started");
    } else {
        spdlog::warn("YOLO video1 not available");
    }  // 采集已自动启动，同步按钮状态

    // --- 显示（10ms 快速轮询） ---
    int renderCount = 0;
    QTimer displayTimer;
    QObject::connect(&displayTimer, &QTimer::timeout, [&]() {
        for (int i = 0; i < 4; i++) {
            auto ref = capture.displayQueue().tryPop();
            if (!ref || !ref->mmapAddr) break;
            window.videoWidget()->renderRawNV12(
                static_cast<const uint8_t*>(ref->mmapAddr),
                ref->width, ref->height, ref->stride);
            capture.pool().release(ref);
            perf.tickCapture();
            renderCount++;
        }
    });
    displayTimer.start(10);

    // --- 性能统计 ---
    QTimer perfTimer;
    QObject::connect(&perfTimer, &QTimer::timeout, [&]() {
        spdlog::info("Stats: {} renders", renderCount);
        renderCount = 0;
    });
    perfTimer.start(10000);

    // --- 启动 ---
    if (encOk) encoder.start(capture.encodeQueue(), capture.pool());
    capture.start();
    spdlog::info("All started. RTSP: rtsp://<IP>:{}{}", RTSP_PORT, RTSP_MOUNT);
    window.show();

    int ret = app.exec();

    // --- 清理 ---
    capture.stop();
    encoder.stop();
    recorder.stop();
    detector.stop();
    rtsp.stop();
    avformat_network_deinit();
    return ret;
}
