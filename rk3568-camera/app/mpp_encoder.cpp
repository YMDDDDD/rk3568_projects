#include "mpp_encoder.h"
#include "buffer_pool.h"
#include "config.h"
#include <spdlog/spdlog.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/mpp_task.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/rk_venc_cfg.h>
#include <cstring>
#include <unistd.h>

MppEncoder::MppEncoder(QObject *parent) : QObject(parent) {
    QObject::connect(&timer_, &QTimer::timeout, this, &MppEncoder::tick);
}

MppEncoder::~MppEncoder() {
    stop();
}

bool MppEncoder::init(uint32_t width, uint32_t height, uint32_t stride) {
    MPP_RET ret = mpp_create(&mppCtx_, &mppApi_);
    if (ret != MPP_OK) { spdlog::error("mpp_create: {}", (int)ret); return false; }

    MppPollType timeout = MPP_POLL_BLOCK;
    mppApi_->control(mppCtx_, MPP_SET_OUTPUT_TIMEOUT, &timeout);

    ret = mpp_init(mppCtx_, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
    if (ret != MPP_OK) { spdlog::error("mpp_init: {}", (int)ret); return false; }

    MppEncCfg cfg = nullptr;
    ret = mpp_enc_cfg_init(&cfg);
    if (ret) { spdlog::error("cfg_init: {}", (int)ret); return false; }

    mpp_enc_cfg_set_s32(cfg, "prep:width",       (int32_t)width);
    mpp_enc_cfg_set_s32(cfg, "prep:height",      (int32_t)height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride",  (int32_t)stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride",  (int32_t)height);
    mpp_enc_cfg_set_s32(cfg, "prep:format",      MPP_FMT_YUV420SP);
    mpp_enc_cfg_set_s32(cfg, "prep:range",       MPP_FRAME_RANGE_JPEG);  // Full range SPS VUI
    mpp_enc_cfg_set_s32(cfg, "rc:mode",          MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target",    4 * 1024 * 1024);
    mpp_enc_cfg_set_s32(cfg, "rc:gop",           60);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num",    30);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm",  1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num",   30);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", 1);

    ret = mppApi_->control(mppCtx_, MPP_ENC_SET_CFG, cfg);
    mpp_enc_cfg_deinit(cfg);

    if (ret != MPP_OK) {
        spdlog::error("MPP_ENC_SET_CFG failed: {}", (int)ret);
        return false;
    }

    // 预分配编码 buffer
    int frameSize = (int32_t)(stride * height * 3 / 2);
    ret = mpp_buffer_group_get_internal(&bufGrp_, MPP_BUFFER_TYPE_DRM);
    if (ret) {
        spdlog::error("buffer_group_get_internal failed: {}", (int)ret);
        return false;
    }

    ret = mpp_buffer_get(bufGrp_, &frmBuf_, frameSize);
    if (ret) {
        spdlog::error("mpp_buffer_get failed: {}", (int)ret);
        return false;
    }

    mppReady_ = true;
    spdlog::info("MPP encoder ready: {}x{} @30fps 4Mbps", width, height);
    return true;
}

void MppEncoder::start(SPSCQueue<FrameRefPtr> &encodeQueue, BufferPool &pool) {
    encodeQueue_ = &encodeQueue;
    bufferPool_  = &pool;
    running_ = true;
    timer_.start(33);  // ~30fps
    spdlog::info("MPP encoder timer started");
}

void MppEncoder::stop() {
    running_ = false;
    timer_.stop();

    if (frmBuf_) { mpp_buffer_put(frmBuf_); frmBuf_ = nullptr; }
    if (bufGrp_) { mpp_buffer_group_put(bufGrp_); bufGrp_ = nullptr; }
    if (mppCtx_) {
        mppApi_->reset(mppCtx_);
        mpp_destroy(mppCtx_);
        mppCtx_ = nullptr;
    }
    mppReady_ = false;
    spdlog::info("MPP encoder stopped");
}

void MppEncoder::tick() {
    if (!running_ || !mppReady_) return;

    // 尽量多处理几帧
    for (int i = 0; i < 4; i++) {
        auto ref = encodeQueue_->tryPop();
        if (!ref) break;

        bool ok = encodeOneFrame(ref);
        bufferPool_->release(ref);

        if (ok) {
            emit heartbeat();
        }
    }
}

bool MppEncoder::encodeOneFrame(FrameRefPtr ref) {
    if (!ref || !ref->mmapAddr) return false;

    int stride = ref->stride > 0 ? (int)ref->stride : (int)ref->width;
    int frameSize = stride * ref->height * 3 / 2;

    void *dst = mpp_buffer_get_ptr(frmBuf_);
    if (!dst) return false;
    memcpy(dst, ref->mmapAddr, frameSize);

    // 组装 frame
    MppFrame frame = nullptr;
    mpp_frame_init(&frame);
    mpp_frame_set_width(frame,  ref->width);
    mpp_frame_set_height(frame, ref->height);
    mpp_frame_set_hor_stride(frame, ref->stride);  // V4L2 stride (2112)
    mpp_frame_set_ver_stride(frame, ref->height);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
    mpp_frame_set_buffer(frame, frmBuf_);
    mpp_frame_set_eos(frame, 0);
    mpp_frame_set_color_range(frame, MPP_FRAME_RANGE_JPEG);  // Full range (0-255)

    // 送入编码
    MPP_RET ret = mppApi_->encode_put_frame(mppCtx_, frame);
    mpp_frame_deinit(&frame);
    if (ret != MPP_OK) return false;

    // 获取编码输出
    MppPacket packet = nullptr;
    ret = mppApi_->encode_get_packet(mppCtx_, &packet);

    if (packet) {
        size_t len  = mpp_packet_get_length(packet);
        void  *data = mpp_packet_get_data(packet);
        uint64_t pts = (uint64_t)mpp_packet_get_pts(packet);

        if (data && len > 0 && nalCallback_) {
            nalCallback_((const uint8_t*)data, len, pts);
            emit frameEncoded(len);
        }
        mpp_packet_deinit(&packet);
    }

    return true;
}
