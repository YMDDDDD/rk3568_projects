#pragma once
#include "config.h"
#include <QThread>
#include <QVector>
#include <QRect>
#include <QString>

#ifdef DISABLE_RKNN
typedef uint64_t rknn_context;
#else
#include <rknn/rknn_api.h>
#endif

// ============================================================================
// YOLO 检测结果
// ============================================================================

struct Detection {
    QRect    bbox;
    int      classId  = 0;
    QString  className;
    float    confidence = 0.0f;
};

// ============================================================================
// YOLO 推理线程：原生 V4L2 从 /dev/video1 读取 640×640 NV12 → RKNN NPU
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
    void preprocess(void *nv12Data, uint8_t *inputBuf);
    QVector<Detection> postProcess(float *output, int width, int height);

    QString            devicePath_;
    int                v4l2Fd_      = -1;
    void              *mmapAddrs_[4] = {};
    int                bufSizes_[4] = {};
    rknn_context       rknnCtx_     = 0;
    std::atomic<bool>  running_{false};

    uint8_t            *inputBuf_  = nullptr;
    size_t              inputSize_ = 0;

    int                 detWidth_  = 0;
    int                 detHeight_ = 0;
    int                 skipCount_ = 0;
};
