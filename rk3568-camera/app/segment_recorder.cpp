#include "segment_recorder.h"
#include <spdlog/spdlog.h>
#include <QDateTime>
#include <QDir>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

SegmentRecorder::SegmentRecorder(QObject *parent) : QObject(parent) {}

SegmentRecorder::~SegmentRecorder() {
    stop();
}

bool SegmentRecorder::start(const char *basePath, int segmentSec) {
    if (recording_) return true;

    basePath_    = basePath;
    segmentSec_  = segmentSec;
    segmentIndex_ = 0;

    QDir().mkpath(basePath);

    rotateSegment();

    recording_ = true;
    segmentStartUs_ = PtsClock::nowUs();
    spdlog::info("SegmentRecorder started: {}s/segment, path={}", segmentSec, basePath);
    return true;
}

bool SegmentRecorder::stop() {
    if (!recording_) return false;

    if (outCtx_) {
        av_write_trailer(outCtx_);
        avio_closep(&outCtx_->pb);
        avformat_free_context(outCtx_);
        outCtx_ = nullptr;
    }

    recording_ = false;
    spdlog::info("SegmentRecorder stopped");
    return true;
}

void SegmentRecorder::writePacket(AVPacket *pkt) {
    if (!recording_ || !outCtx_ || !pkt) return;

    // 检查是否需要切分
    uint64_t now = PtsClock::nowUs();
    if (now - segmentStartUs_ >= static_cast<uint64_t>(segmentSec_) * 1000000ULL) {
        rotateSegment();
        segmentStartUs_ = now;
    }

    // 重设 PTS/DTS 时间基
    pkt->stream_index = videoStream_->index;
    pkt->pts = nextPts_;
    pkt->dts = nextPts_;
    nextPts_ += 3000;  // 30fps ≈ 33ms per frame in AVRational{1,3000}

    int ret = av_interleaved_write_frame(outCtx_, pkt);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        spdlog::error("av_interleaved_write_frame failed: {}", errbuf);
    }
}

bool SegmentRecorder::rotateSegment() {
    // 关闭当前段
    if (outCtx_) {
        av_write_trailer(outCtx_);
        avio_closep(&outCtx_->pb);
        avformat_free_context(outCtx_);
        outCtx_ = nullptr;
    }

    QString filePath = generateFileName();
    nextPts_ = 0;

    // 创建新 MP4
    int ret = avformat_alloc_output_context2(&outCtx_, nullptr, "mp4", filePath.toUtf8().constData());
    if (ret < 0 || !outCtx_) {
        spdlog::error("avformat_alloc_output_context2 failed");
        return false;
    }

    videoStream_ = avformat_new_stream(outCtx_, nullptr);
    if (!videoStream_) {
        spdlog::error("avformat_new_stream failed");
        return false;
    }

    videoStream_->time_base = AVRational{1, 90000};

    if (!(outCtx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outCtx_->pb, filePath.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            spdlog::error("avio_open failed");
            return false;
        }
    }

    ret = avformat_write_header(outCtx_, nullptr);
    if (ret < 0) {
        spdlog::error("avformat_write_header failed");
        return false;
    }

    segmentIndex_++;
    emit segmentStarted(filePath);

    // 清理旧段
    cleanupOldSegments();

    spdlog::info("Segment rotated: {}", filePath.toUtf8().constData());
    return true;
}

QString SegmentRecorder::generateFileName() {
    auto now = QDateTime::currentDateTime();
    QString timestamp = now.toString("yyyyMMdd_HHmmss");
    return QString("%1/record_%2_%3.mp4")
        .arg(basePath_)
        .arg(timestamp)
        .arg(segmentIndex_, 4, 10, QChar('0'));
}

void SegmentRecorder::cleanupOldSegments() {
    // 保留最近 MAX_SEGMENT_COUNT 个文件
    QDir dir(basePath_);
    QStringList filters;
    filters << "record_*.mp4";
    dir.setNameFilters(filters);
    dir.setSorting(QDir::Name);

    auto files = dir.entryInfoList();
    if (files.size() <= MAX_SEGMENT_COUNT) return;

    int toRemove = files.size() - MAX_SEGMENT_COUNT;
    for (int i = 0; i < toRemove; i++) {
        QFile::remove(files[i].absoluteFilePath());
    }
}
