#pragma once
#include <cstdint>
#include <cstddef>

// ============================================================================
// 编译期配置常量
// ============================================================================

// --- 采集参数 ---
constexpr uint32_t MAIN_WIDTH    = 1920;
constexpr uint32_t MAIN_HEIGHT   = 1080;
constexpr uint32_t MAIN_FPS      = 30;
constexpr uint32_t MAIN_FORMAT   = 0x3231564E;    // V4L2_PIX_FMT_NV12
constexpr const char* VIDEO0_DEV = "/dev/video0";

constexpr uint32_t AI_WIDTH      = 640;
constexpr uint32_t AI_HEIGHT     = 640;
constexpr uint32_t AI_FPS        = 30;
constexpr const char* VIDEO1_DEV = "/dev/video1";

// --- V4L2 BufferPool ---
constexpr int      V4L2_BUFFER_COUNT = 8;           // V4L2 DMA buffer 数量

// --- 队列深度 ---
constexpr size_t   DISPLAY_QUEUE_SIZE = 1;           // 显示只保留最新帧
constexpr size_t   ENCODE_QUEUE_SIZE  = 4;           // 编码缓冲4帧防丢

// --- YOLO 跳帧 ---
constexpr int      DETECT_SKIP_FRAMES = 2;           // 每3帧推理1次

// --- 分段录像 ---
constexpr int      SEGMENT_DURATION_SEC = 300;       // 5分钟/段
constexpr int      MAX_SEGMENT_COUNT    = 120;       // 保留最近10小时

// --- RTSP ---
constexpr int      RTSP_PORT       = 8554;
constexpr const char* RTSP_MOUNT   = "/live";

// --- Watchdog ---
constexpr int      WD_CAPTURE_TIMEOUT_MS  = 2000;
constexpr int      WD_ENCODER_TIMEOUT_MS  = 3000;
constexpr int      WD_DETECTOR_TIMEOUT_MS = 5000;
constexpr int      WD_CHECK_INTERVAL_MS   = 1000;

// --- 性能监控 ---
constexpr int      PERF_STATS_INTERVAL_MS = 1000;

// --- 录制路径 ---
constexpr const char* RECORD_BASE_PATH = "/data/record";

// --- 模型路径 ---
constexpr const char* RKNN_MODEL_PATH = "model/yolov5s.rknn";
constexpr const char* LABEL_FILE_PATH = "model/coco_labels.txt";

// --- 日志 ---
constexpr const char* LOG_FILE_PATH   = "/var/log/camera.log";
constexpr int         LOG_MAX_SIZE_MB = 10;
constexpr int         LOG_MAX_FILES   = 3;
