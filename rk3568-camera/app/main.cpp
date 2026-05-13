#include "mainwindow.h"
#include "capture.h"
#include "video_widget.h"
#include "config.h"
#include <QApplication>
#include <QTimer>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    auto logger = spdlog::stdout_color_mt("camera");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::info("=== Capture + Display ===");

    MainWindow window;

    CaptureThread capture;
    if (!capture.open(VIDEO0_DEV, MAIN_WIDTH, MAIN_HEIGHT, MAIN_FPS)) {
        spdlog::error("Capture open failed");
        return -1;
    }
    capture.start();

    std::vector<uint8_t> compactBuf;

    QTimer displayTimer;
    QObject::connect(&displayTimer, &QTimer::timeout, [&]() {
        auto ref = capture.displayQueue().tryPop();
        if (!ref || !ref->mmapAddr) return;

        int w = ref->width, h = ref->height, s = ref->stride > 0 ? (int)ref->stride : w;
        int total = w * h * 3 / 2;
        if ((int)compactBuf.size() < total) compactBuf.resize(total);

        uint8_t *dst = compactBuf.data();
        const uint8_t *src = (const uint8_t*)ref->mmapAddr;

        // Y: 逐行拷贝去 padding
        for (int row = 0; row < h; row++) {
            memcpy(dst, src, w);
            dst += w;
            src += s;
        }
        // UV: 也逐行拷贝（UV 行 stride = Y 的 stride，每行 w 字节有效数据）
        const uint8_t *uvSrc = (const uint8_t*)ref->mmapAddr + s * h;
        for (int row = 0; row < h / 2; row++) {
            memcpy(dst, uvSrc, w);
            dst += w;
            uvSrc += s;
        }

        window.videoWidget()->renderRawNV12(compactBuf.data(), w, h);
    });
    displayTimer.start(33);

    spdlog::info("Showing window...");
    window.show();
    spdlog::info("Window shown");

    int ret = app.exec();
    capture.stop();
    return ret;
}
