#pragma once
#include "perf_monitor.h"
#include "detector.h"
#include <QMainWindow>
#include <QLabel>
#include <QToolBar>
#include <QAction>
#include <QTimer>

class VideoWidget;
class CaptureThread;
class MppEncoder;
class RtspServer;
class SegmentRecorder;
class DetectThread;
class Watchdog;

// ============================================================================
// Qt 主窗口
// ============================================================================

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    VideoWidget* videoWidget() const { return videoWidget_; }
    void setModules(CaptureThread *capture, RtspServer *rtsp,
                    SegmentRecorder *recorder, DetectThread *detector,
                    Watchdog *watchdog, PerfMonitor *perf);

public slots:
    void onDetection(QVector<Detection> results);
    void onStatsUpdated(PerfStats stats);

private slots:
    void onCaptureToggle(bool checked);
    void onRtspToggle(bool checked);
    void onRecordToggle(bool checked);
    void onAIToggle(bool checked);

private:
    void setupUI();
    void updateStatusBar();
    void updatePerfLabels(PerfStats stats);

    // 控件
    VideoWidget *videoWidget_ = nullptr;

    QLabel *statusLabel_   = nullptr;
    QLabel *fpsLabel_      = nullptr;
    QLabel *detectLabel_   = nullptr;
    QLabel *latencyLabel_  = nullptr;
    QLabel *rtspLabel_     = nullptr;
    QLabel *recordLabel_   = nullptr;

    QToolBar *toolbar_     = nullptr;
    QAction  *actCapture_  = nullptr;
    QAction  *actRtsp_     = nullptr;
    QAction  *actRecord_   = nullptr;
    QAction  *actAI_       = nullptr;

    // 后台模块（由 main 注入或自行创建）
    CaptureThread   *capture_  = nullptr;
    MppEncoder      *encoder_  = nullptr;
    RtspServer      *rtsp_     = nullptr;
    SegmentRecorder *recorder_ = nullptr;
    DetectThread    *detector_ = nullptr;
    Watchdog        *watchdog_ = nullptr;
    PerfMonitor     *perf_     = nullptr;

    // 状态
    bool captureActive_  = false;
    bool rtspActive_     = false;
    bool recordActive_   = false;
    bool aiActive_       = false;
};
