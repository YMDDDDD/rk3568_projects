#include "detector.h"
#include <spdlog/spdlog.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <rknn_api.h>
}

DetectThread::DetectThread(QObject *parent) : QThread(parent) {}

DetectThread::~DetectThread() {
    stop();
    wait(5000);

    if (fmtCtx_)  avformat_close_input(&fmtCtx_);
    if (rknnCtx_) rknn_destroy(rknnCtx_);
    free(inputBuf_);
}

bool DetectThread::open(const char *device, int width, int height) {
    devicePath_ = device;
    detWidth_   = width;
    detHeight_  = height;

    AVFormatContext *ctx = nullptr;
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "input_format", "v4l2", 0);
    av_dict_set_int(&opts, "framerate", AI_FPS, 0);
    av_dict_set(&opts, "video_size",
                QString("%1x%2").arg(width).arg(height).toUtf8().constData(), 0);
    av_dict_set(&opts, "pixel_format", "nv12", 0);

    int ret = avformat_open_input(&ctx, device, nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        spdlog::error("Cannot open V4L2 device {}: {}", device, ret);
        return false;
    }

    fmtCtx_ = ctx;
    spdlog::info("Detector V4L2 opened: {} {}x{}", device, width, height);
    return true;
}

bool DetectThread::loadModel(const char *rknnPath) {
    FILE *fp = fopen(rknnPath, "rb");
    if (!fp) {
        spdlog::error("Cannot open RKNN model: {}", rknnPath);
        return false;
    }

    fseek(fp, 0, SEEK_END);
    size_t modelSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    auto *modelData = static_cast<uint8_t*>(malloc(modelSize));
    fread(modelData, 1, modelSize, fp);
    fclose(fp);

    int ret = rknn_init(&rknnCtx_, modelData, modelSize, 0, nullptr);
    free(modelData);

    if (ret < 0) {
        spdlog::error("rknn_init failed: {}", (int)ret);
        return false;
    }

    // 查询输入输出属性，分配 buffer
    rknn_input_output_num ioNum;
    ret = rknn_query(rknnCtx_, RKNN_QUERY_IN_OUT_NUM, &ioNum, sizeof(ioNum));
    if (ret != RKNN_SUCC) {
        spdlog::error("rknn_query IN_OUT_NUM failed: {}", (int)ret);
        return false;
    }

    rknn_tensor_attr inputAttr;
    memset(&inputAttr, 0, sizeof(inputAttr));
    inputAttr.index = 0;
    ret = rknn_query(rknnCtx_, RKNN_QUERY_INPUT_ATTR, &inputAttr, sizeof(inputAttr));
    if (ret == RKNN_SUCC) {
        inputSize_ = inputAttr.size;
        inputBuf_  = static_cast<uint8_t*>(malloc(inputSize_));
    }

    spdlog::info("RKNN model loaded: {} (input {} bytes)", rknnPath, inputSize_);
    return true;
}

void DetectThread::stop() {
    running_.store(false, std::memory_order_relaxed);
}

void DetectThread::restart() {
    stop();
    wait(5000);

    if (fmtCtx_) { avformat_close_input(&fmtCtx_); fmtCtx_ = nullptr; }

    open(devicePath_.toUtf8().constData(), detWidth_, detHeight_);
    start();
}

void DetectThread::preprocess(void *nv12Data, uint8_t *inputBuf) {
    // NV12 → 输入格式转换（通常需要转为 RGB 或保持 NV12 送 NPU）
    // 简单实现：直接拷贝（假设模型接受 NV12 输入）
    memcpy(inputBuf, nv12Data, inputSize_);
}

QVector<Detection> DetectThread::postProcess(float *output, int width, int height) {
    Q_UNUSED(width)
    Q_UNUSED(height)
    QVector<Detection> results;

    // TODO: YOLO 后处理解码
    // 1. 解析输出张量（边界框、置信度、类别）
    // 2. NMS 非极大值抑制
    // 3. 映射坐标到 1080p 显示分辨率
    (void)output;

    return results;
}

void DetectThread::run() {
    running_.store(true, std::memory_order_relaxed);
    AVPacket pkt;

    while (running_.load(std::memory_order_relaxed)) {
        if (av_read_frame(fmtCtx_, &pkt) < 0) {
            usleep(5000);
            continue;
        }

        // 跳帧检测
        skipCount_++;
        if (skipCount_ <= DETECT_SKIP_FRAMES) {
            av_packet_unref(&pkt);
            continue;
        }
        skipCount_ = 0;

        // 预处理
        preprocess(pkt.data, inputBuf_);

        // RKNN 推理
        rknn_input inputs[1];
        memset(inputs, 0, sizeof(inputs));
        inputs[0].index  = 0;
        inputs[0].type   = RKNN_TENSOR_UINT8;
        inputs[0].buf    = inputBuf_;
        inputs[0].size   = inputSize_;
        inputs[0].fmt    = RKNN_TENSOR_NHWC;

        int ret = rknn_inputs_set(rknnCtx_, 1, inputs);
        if (ret < 0) {
            spdlog::error("rknn_inputs_set failed: {}", (int)ret);
            av_packet_unref(&pkt);
            continue;
        }

        ret = rknn_run(rknnCtx_, nullptr);
        if (ret < 0) {
            spdlog::error("rknn_run failed: {}", (int)ret);
            av_packet_unref(&pkt);
            continue;
        }

        // 获取输出
        rknn_output outputs[3];  // YOLO 通常 3 个输出层
        memset(outputs, 0, sizeof(outputs));
        for (int i = 0; i < 3; i++) {
            outputs[i].want_float = 1;
            outputs[i].is_prealloc = 0;
        }

        ret = rknn_outputs_get(rknnCtx_, 3, outputs, nullptr);
        if (ret == RKNN_SUCC) {
            // 后处理（取第一个输出作为示例）
            auto results = postProcess(static_cast<float*>(outputs[0].buf),
                                       detWidth_, detHeight_);
            emit detectionReady(results);
        }

        rknn_outputs_release(rknnCtx_, 3, outputs);

        av_packet_unref(&pkt);
        emit heartbeat();
    }

    spdlog::info("Detect thread stopped");
}
