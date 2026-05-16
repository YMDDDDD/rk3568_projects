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

bool MppEncoder::init(uint32_t width, uint32_t height, uint32_t stride, BufferPool &pool) {
    MPP_RET ret = mpp_create(&mppCtx_, &mppApi_);
    if (ret != MPP_OK) { spdlog::error("mpp_create: {}", (int)ret); return false; }

    MppPollType timeout = MPP_POLL_BLOCK;
    mppApi_->control(mppCtx_, MPP_SET_OUTPUT_TIMEOUT, &timeout);

    ret = mpp_init(mppCtx_, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
    if (ret != MPP_OK) { spdlog::error("mpp_init: {}", (int)ret); return false; }

    MppEncCfg cfg = nullptr;
    ret = mpp_enc_cfg_init(&cfg);
    spdlog::info("cfg_init ret={}", (int)ret);
    if (ret) { spdlog::error("cfg_init: {}", (int)ret); return false; }

    mpp_enc_cfg_set_s32(cfg, "prep:width",       (int32_t)width);
    mpp_enc_cfg_set_s32(cfg, "prep:height",      (int32_t)height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride",  (int32_t)stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride",  (int32_t)height);
    mpp_enc_cfg_set_s32(cfg, "prep:format",      MPP_FMT_YUV420SP);
    mpp_enc_cfg_set_s32(cfg, "rc:mode",          MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target",    4 * 1024 * 1024);
    mpp_enc_cfg_set_s32(cfg, "rc:gop",           60);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num",    30);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm",  1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num",   30);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", 1);

    ret = mppApi_->control(mppCtx_, MPP_ENC_SET_CFG, cfg);
    mpp_enc_cfg_deinit(cfg);
    spdlog::info("SET_CFG ret={}", (int)ret);
    if (ret != MPP_OK) {
        spdlog::error("SET_CFG failed, encode may use defaults");
    }

    int frameSize = (int32_t)(width * height * 3 / 2);
    int nbuf = pool.numBuffers();
    importedBufs_.resize(nbuf, nullptr);

    for (int i = 0; i < nbuf; i++) {
        int fd = pool.dmabufFd(i);
        if (fd < 0) {
            spdlog::error("BufferPool dmabufFd[{}] invalid", i);
            return false;
        }

        MppBuffer mbuf = nullptr;
        MppBufferInfo info;
        memset(&info, 0, sizeof(info));
        info.type  = MPP_BUFFER_TYPE_EXT_DMA;
        info.fd    = fd;
        info.size  = (size_t)frameSize;
        info.index = i;

        ret = mpp_buffer_import(&mbuf, &info);
        if (ret != MPP_OK) {
            spdlog::error("mpp_buffer_import[{}] failed: {}", i, (int)ret);
            return false;
        }
        importedBufs_[i] = mbuf;
    }

    mppReady_ = true;
    spdlog::info("MPP encoder ready: {}x{} @30fps 4Mbps, {} dma-bufs imported (zero-copy)",
                 width, height, nbuf);
    return true;
}

void MppEncoder::start(SPSCQueue<FrameRefPtr> &encodeQueue, BufferPool &pool) {
    encodeQueue_ = &encodeQueue;
    bufferPool_  = &pool;
    running_ = true;
    timer_.start(33);
    spdlog::info("MPP encoder timer started");
}

void MppEncoder::stop() {
    running_ = false;
    timer_.stop();

    for (auto &buf : importedBufs_) {
        if (buf) { mpp_buffer_put(buf); buf = nullptr; }
    }
    importedBufs_.clear();

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

    for (int i = 0; i < 4; i++) {
        auto ref = encodeQueue_->tryPop();
        if (!ref) break;

        bool ok = encodeOneFrame(ref);
        if (ok) {
            emit heartbeat();
        }
    }
}

bool MppEncoder::encodeOneFrame(FrameRefPtr ref) {
    if (!ref || ref->v4l2Index >= importedBufs_.size()) return false;

    uint32_t idx = ref->v4l2Index;
    MppBuffer mbuf = importedBufs_[idx];
    if (!mbuf) return false;

    int w = (int)ref->width, h = (int)ref->height;

    MppFrame frame = nullptr;
    mpp_frame_init(&frame);
    mpp_frame_set_width(frame,  w);
    mpp_frame_set_height(frame, h);
    mpp_frame_set_hor_stride(frame, w);
    mpp_frame_set_ver_stride(frame, h);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
    mpp_frame_set_buffer(frame, mbuf);
    mpp_frame_set_eos(frame, 0);

    MPP_RET ret = mppApi_->encode_put_frame(mppCtx_, frame);
    mpp_frame_deinit(&frame);
    if (ret != MPP_OK) return false;

    MppPacket packet = nullptr;
    ret = mppApi_->encode_get_packet(mppCtx_, &packet);

    if (packet) {
        size_t len  = mpp_packet_get_length(packet);
        void  *data = mpp_packet_get_data(packet);
        uint64_t pts = (uint64_t)mpp_packet_get_pts(packet);

        static int encCount = 0;
        encCount++;
        if (encCount <= 3 || encCount % 30 == 0)
            spdlog::info("Enc: frame {} len={}", encCount, len);

        if (data && len > 0 && nalCallback_) {
            nalCallback_((const uint8_t*)data, len, pts);
            emit frameEncoded(len);
        }
        mpp_packet_deinit(&packet);
    }

    return true;
}
