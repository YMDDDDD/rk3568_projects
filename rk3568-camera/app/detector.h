#pragma once
#include "config.h"
#include <QThread>
#include <QVector>
#include <QRect>
#include <QString>

// RKNN 前向声明
#ifndef DISABLE_RKNN
// rknn_api.h defines rknn_context as uint64_t, don't re-declare
#include <rknn/rknn_api.h>
#endif

struct AVFormatContext;

// ============================================================================
// YOLO 检测结果
// ============================================================================

struct Detection {
    QRect    bbox;         // 边界框（原始坐标，需映射到显示分辨率）
    int      classId  = 0;
    QString  className;
    float    confidence = 0.0f;
};

// ============================================================================
// YOLO 推理线程：独立从 /dev/video1 读取 640×640 NV12 → RKNN NPU 推理
// ============================================================================

class DetectThread : public QThread {
    Q_OBJECT
public:
    explicit DetectThread(QObject *parent = nullptr);
    ~DetectThread() override;

    bool open(const char *device, int width, int height);
    bool loadModel(const char *rknnPath);
    void stop();
    void restart();

signals:
    void detectionReady(QVector<Detection> results);
    void heartbeat();

protected:
    void run() override;

private:
    bool initV4L2();
    void preprocess(void *nv12Data, uint8_t *inputBuf);
    QVector<Detection> postProcess(float *output, int width, int height);

    QString            devicePath_;
    AVFormatContext   *fmtCtx_     = nullptr;
    rknn_context       rknnCtx_    = 0;
    std::atomic<bool>  running_{false};

    uint8_t            *inputBuf_  = nullptr;    // RKNN 输入 buffer
    size_t              inputSize_ = 0;

    int                 detWidth_  = 0;
    int                 detHeight_ = 0;
    int                 skipCount_ = 0;
};
