#include "segment_recorder.h"
#include <spdlog/spdlog.h>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

SegmentRecorder::SegmentRecorder(QObject *parent) : QObject(parent) {}

SegmentRecorder::~SegmentRecorder() { stop(); }

bool SegmentRecorder::start(const char *basePath, int segmentSec) {
    if (recording_) return true;

    basePath_    = basePath;
    segmentSec_  = segmentSec;
    segmentIndex_ = 0;

    QDir().mkpath(basePath);

    rotateSegment();

    recording_ = true;
    segmentStartUs_ = PtsClock::nowUs();
    spdlog::info("Recorder: {}s/segment, path={}", segmentSec, basePath);
    return true;
}

bool SegmentRecorder::stop() {
    if (!recording_) return false;

    if (currentFile_) {
        fclose(currentFile_);
        currentFile_ = nullptr;
        emit segmentFinished(currentFilePath_);
        spdlog::info("Recorder: {} saved", currentFilePath_.toStdString());
    }

    recording_ = false;
    spdlog::info("Recorder stopped");
    return true;
}

void SegmentRecorder::feedNALU(const uint8_t *data, size_t len) {
    if (!recording_ || !currentFile_ || !data || len == 0) return;

    // 检查是否需要切分
    uint64_t now = PtsClock::nowUs();
    if (now - segmentStartUs_ >= static_cast<uint64_t>(segmentSec_) * 1000000ULL) {
        rotateSegment();
        segmentStartUs_ = now;
    }

    fwrite(data, 1, len, currentFile_);
}

void SegmentRecorder::rotateSegment() {
    if (currentFile_) {
        fclose(currentFile_);
        currentFile_ = nullptr;
        emit segmentFinished(currentFilePath_);
        spdlog::info("Recorder segment done: {}", currentFilePath_.toStdString());
    }

    currentFilePath_ = generateFileName();
    currentFile_ = fopen(currentFilePath_.toUtf8().constData(), "wb");
    if (!currentFile_) {
        spdlog::error("Recorder: cannot create {}", currentFilePath_.toStdString());
        return;
    }

    segmentIndex_++;
    emit segmentStarted(currentFilePath_);

    cleanupOldSegments();

    spdlog::info("Recorder: {}", currentFilePath_.toStdString());
}

QString SegmentRecorder::generateFileName() {
    auto now = QDateTime::currentDateTime();
    QString ts = now.toString("yyyyMMdd_HHmmss");
    return QString("%1/record_%2_%3.h264")
        .arg(basePath_)
        .arg(ts)
        .arg(segmentIndex_, 4, 10, QChar('0'));
}

void SegmentRecorder::cleanupOldSegments() {
    QDir dir(basePath_);
    QStringList filters;
    filters << "record_*.h264";
    dir.setNameFilters(filters);
    dir.setSorting(QDir::Name);

    auto files = dir.entryInfoList();
    if (files.size() <= MAX_SEGMENT_COUNT) return;

    int toRemove = files.size() - MAX_SEGMENT_COUNT;
    for (int i = 0; i < toRemove; i++) {
        QFile::remove(files[i].absoluteFilePath());
        spdlog::info("Recorder: removed old {}", files[i].fileName().toStdString());
    }
}

// static
QStringList SegmentRecorder::listFiles(const char *basePath) {
    QDir dir(basePath);
    QStringList filters;
    filters << "record_*.h264";
    dir.setNameFilters(filters);
    dir.setSorting(QDir::Name | QDir::Reversed);  // 新的在前面

    QStringList result;
    for (const auto &fi : dir.entryInfoList()) {
        result.append(fi.absoluteFilePath());
    }
    return result;
}
