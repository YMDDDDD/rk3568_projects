#pragma once
#include "config.h"
#include "pts_clock.h"
#include <cstdint>
#include <cstdio>
#include <QObject>
#include <QString>

// ============================================================================
// 分段录像：直接存 .h264 裸流（VLC 可直接播放），5 分钟一段自动轮转
// ============================================================================

class SegmentRecorder : public QObject {
    Q_OBJECT
public:
    explicit SegmentRecorder(QObject *parent = nullptr);
    ~SegmentRecorder() override;

    bool start(const char *basePath = RECORD_BASE_PATH, int segmentSec = SEGMENT_DURATION_SEC);
    bool stop();
    bool isRecording() const { return recording_; }

    // 编码回调中调用，直接写 H.264 NAL 数据
    void feedNALU(const uint8_t *data, size_t len);

    // 录制文件列表（用于回放界面）
    static QStringList listFiles(const char *basePath = RECORD_BASE_PATH);

signals:
    void segmentStarted(const QString &filePath);
    void segmentFinished(const QString &filePath);

private:
    void rotateSegment();
    QString generateFileName();
    void cleanupOldSegments();

    FILE    *currentFile_    = nullptr;
    bool     recording_      = false;
    uint64_t segmentStartUs_ = 0;
    QString  basePath_;
    int      segmentSec_     = SEGMENT_DURATION_SEC;
    int      segmentIndex_   = 0;
    QString  currentFilePath_;
};
