#include "mainwindow.h"
#include "video_widget.h"
#include "capture.h"
#include "rtsp_server.h"
#include "segment_recorder.h"
#include "detector.h"
#include "watchdog.h"
#include "perf_monitor.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QAction>
#include <QLabel>
#include <QDockWidget>
#include <QGroupBox>
#include <QFileInfo>
#include <spdlog/spdlog.h>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUI();
    resize(1024, 640);
    setWindowTitle("RK3568 Camera");
    spdlog::info("MainWindow created");
}

MainWindow::~MainWindow() {}

void MainWindow::setCaptureRunning(bool on) {
    captureActive_ = on;
    actCapture_->setChecked(on);
    actRtsp_->setEnabled(on);
    actRecord_->setEnabled(on);
    actAI_->setEnabled(on);
}

void MainWindow::setModules(CaptureThread *capture, RtspServer *rtsp,
                             SegmentRecorder *recorder, DetectThread *detector,
                             Watchdog *watchdog, PerfMonitor *perf) {
    capture_  = capture;
    rtsp_     = rtsp;
    recorder_ = recorder;
    detector_ = detector;
    watchdog_ = watchdog;
    perf_     = perf;
}

void MainWindow::setupUI() {
    videoWidget_ = new VideoWidget(this);
    setCentralWidget(videoWidget_);

    // 工具栏
    toolbar_ = addToolBar("控制");
    toolbar_->setMovable(false);

    actCapture_ = toolbar_->addAction("开始采集");
    actCapture_->setCheckable(true);
    connect(actCapture_, &QAction::toggled, this, &MainWindow::onCaptureToggle);

    toolbar_->addSeparator();

    actRtsp_ = toolbar_->addAction("RTSP推流");
    actRtsp_->setCheckable(true);
    actRtsp_->setEnabled(false);
    connect(actRtsp_, &QAction::toggled, this, &MainWindow::onRtspToggle);

    actRecord_ = toolbar_->addAction("录像");
    actRecord_->setCheckable(true);
    actRecord_->setEnabled(false);
    connect(actRecord_, &QAction::toggled, this, &MainWindow::onRecordToggle);

    toolbar_->addSeparator();

    actAI_ = toolbar_->addAction("YOLO检测");
    actAI_->setCheckable(true);
    actAI_->setEnabled(false);
    connect(actAI_, &QAction::toggled, this, &MainWindow::onAIToggle);

    // 右侧 Dock：性能监控面板
    auto *dock = new QDockWidget("状态", this);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);

    auto *panel = new QWidget(dock);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *perfGroup = new QGroupBox("性能", panel);
    auto *perfLayout = new QVBoxLayout(perfGroup);
    fpsLabel_     = new QLabel("采集: -- fps");
    detectLabel_  = new QLabel("检测目标: 0");
    latencyLabel_ = new QLabel("推理: --ms 编码: --ms");
    perfLayout->addWidget(fpsLabel_);
    perfLayout->addWidget(detectLabel_);
    perfLayout->addWidget(latencyLabel_);

    auto *serviceGroup = new QGroupBox("服务", panel);
    auto *svcLayout = new QVBoxLayout(serviceGroup);
    rtspLabel_  = new QLabel("RTSP: 未启动");
    recordLabel_ = new QLabel("录像: 未启动");
    svcLayout->addWidget(rtspLabel_);
    svcLayout->addWidget(recordLabel_);

    layout->addWidget(perfGroup);
    layout->addWidget(serviceGroup);

    // 录像文件列表
    auto *recGroup = new QGroupBox("录像回放", panel);
    auto *recLayout = new QVBoxLayout(recGroup);
    recList_ = new QListWidget(panel);
    recList_->setAlternatingRowColors(true);
    recLayout->addWidget(recList_);
    layout->addWidget(recGroup);

    layout->addStretch();

    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    // 状态栏
    statusLabel_ = new QLabel("就绪");
    statusBar()->addPermanentWidget(statusLabel_);

    refreshRecordings();
}

void MainWindow::onCaptureToggle(bool checked) {
    if (!capture_) {
        spdlog::error("Capture thread not initialized");
        actCapture_->setChecked(false);
        return;
    }

    if (checked) {
        capture_->start();
        captureActive_ = true;
        actRtsp_->setEnabled(true);
        actRecord_->setEnabled(true);
        actAI_->setEnabled(true);
        statusLabel_->setText("采集中...");
        spdlog::info("Capture started");
    } else {
        capture_->stop();
        captureActive_ = false;
        actRtsp_->setEnabled(false);
        actRecord_->setEnabled(false);
        actAI_->setEnabled(false);
        statusLabel_->setText("已停止");
        spdlog::info("Capture stopped");
    }
}

void MainWindow::onRtspToggle(bool checked) {
    if (!rtsp_ && !encoder_) return;

    if (checked) {
        if (!rtsp_->isRunning()) rtsp_->start();
        rtspActive_ = true;
        rtspLabel_->setText("RTSP: ● 运行中");
        spdlog::info("RTSP streaming started");
    } else {
        rtsp_->stop();
        rtspActive_ = false;
        rtspLabel_->setText("RTSP: 未启动");
        spdlog::info("RTSP streaming stopped");
    }
}

void MainWindow::refreshRecordings() {
    if (!recList_) return;
    recList_->clear();
    auto files = SegmentRecorder::listFiles();
    for (const auto &path : files) {
        QFileInfo fi(path);
        QString label = fi.fileName();
        auto *item = new QListWidgetItem(label);
        item->setToolTip(path);
        recList_->addItem(item);
    }
}

void MainWindow::onRecordToggle(bool checked) {
    if (!recorder_) return;

    if (checked) {
        recorder_->start();
        recordActive_ = true;
        recordLabel_->setText("录像: ● 录制中");
        spdlog::info("Recording started");
    } else {
        recorder_->stop();
        recordActive_ = false;
        recordLabel_->setText("录像: 未启动");
        spdlog::info("Recording stopped");
    }
    refreshRecordings();
}

void MainWindow::onAIToggle(bool checked) {
    if (!detector_) return;

    if (checked) {
        detector_->start();
        aiActive_ = true;
        statusLabel_->setText("YOLO 检测运行中");
        spdlog::info("YOLO detection started");
    } else {
        detector_->stop();
        aiActive_ = false;
        videoWidget_->setDetections({});
        statusLabel_->setText("检测已停止");
        spdlog::info("YOLO detection stopped");
    }
}

void MainWindow::onDetection(QVector<Detection> results) {
    videoWidget_->setDetections(std::move(results));
    detectLabel_->setText(QString("检测目标: %1").arg(results.size()));
}

void MainWindow::onStatsUpdated(PerfStats stats) {
    fpsLabel_->setText(QString("采集: %1 | 显示: %2 | 编码: %3 | 推理: %4 fps")
        .arg(stats.captureFps, 0, 'f', 1)
        .arg(stats.displayFps, 0, 'f', 1)
        .arg(stats.encodeFps, 0, 'f', 1)
        .arg(stats.detectFps, 0, 'f', 1));

    latencyLabel_->setText(QString("编码: %1ms | 推理: %2ms | 温度: %3°C")
        .arg(stats.encodeLatencyMs, 0, 'f', 1)
        .arg(stats.detectLatencyMs, 0, 'f', 1)
        .arg(stats.cpuTemp, 0, 'f', 1));
}
