#include "mainwindow.h"
#include "capture.h"
#include "video_widget.h"
#include "config.h"
#include <QApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    auto logger = spdlog::stdout_color_mt("camera");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::info("=== Perf Test ===");

    MainWindow window;

    CaptureThread capture;
    if (!capture.open(VIDEO0_DEV, MAIN_WIDTH, MAIN_HEIGHT, MAIN_FPS)) {
        spdlog::error("Capture open failed");
        return -1;
    }
    capture.start();

    // 10ms 快速 displayTimer + 批量取帧
    int renderCount = 0, emptyCount = 0;
    double totalRenderMs = 0;
    QElapsedTimer perft;
    perft.start();

    QTimer displayTimer;
    QObject::connect(&displayTimer, &QTimer::timeout, [&]() {
        // 批量取帧直到队列空
        for (int i = 0; i < 4; i++) {
            auto ref = capture.displayQueue().tryPop();
            if (!ref || !ref->mmapAddr) { emptyCount++; break; }

            QElapsedTimer rt; rt.start();
            window.videoWidget()->renderRawNV12(
                static_cast<const uint8_t*>(ref->mmapAddr),
                ref->width, ref->height, ref->stride);
            capture.pool().release(ref);  // 归还 V4L2 buffer
            totalRenderMs += rt.elapsed();
            renderCount++;
        }

        if (perft.elapsed() >= 2000) {
            double avgMs = renderCount > 0 ? totalRenderMs / renderCount : 0;
            spdlog::info("Perf: {} renders in 2s, {} empty polls, avg render {:.1f}ms",
                         renderCount, emptyCount, avgMs);
            renderCount = 0; emptyCount = 0; totalRenderMs = 0;
            perft.restart();
        }
    });
    displayTimer.start(10);  // 10ms 快速轮询

    spdlog::info("Showing window...");
    window.show();
    spdlog::info("Window shown");

    int ret = app.exec();
    capture.stop();
    return ret;
}
