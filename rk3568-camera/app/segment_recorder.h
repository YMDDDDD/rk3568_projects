#pragma once
#include "config.h"
#include "pts_clock.h"
#include <cstdint>
#include <QObject>
#include <QString>

struct AVPacket;
struct AVFormatContext;
struct AVStream;

// ============================================================================
// 分段 MP4 录制：5 分钟一段，自动轮转，保留最近 120 段（10 小时）
// ============================================================================

class SegmentRecorder : public QObject {
    Q_OBJECT
public:
    explicit SegmentRecorder(QObject *parent = nullptr);
    ~SegmentRecorder() override;

    bool start(const char *basePath = RECORD_BASE_PATH, int segmentSec = SEGMENT_DURATION_SEC);
    bool stop();
    bool isRecording() const { return recording_; }

    // MPP 编码回调中调用（通过 AV_INTERLEAVED_WRITE）
    void writePacket(AVPacket *pkt);

signals:
    void segmentStarted(const QString &filePath);
    void segmentFinished(const QString &filePath);

private:
    bool rotateSegment();
    QString generateFileName();
    void cleanupOldSegments();

    AVFormatContext *outCtx_         = nullptr;
    AVStream        *videoStream_    = nullptr;
    bool             recording_      = false;
    uint64_t         segmentStartUs_ = 0;   // CLOCK_MONOTONIC
    QString          basePath_;
    int              segmentSec_     = SEGMENT_DURATION_SEC;
    int              segmentIndex_   = 0;
    int64_t          nextPts_        = 0;
};
