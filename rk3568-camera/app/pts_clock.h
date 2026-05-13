#pragma once
#include <cstdint>
#include <time.h>

// ============================================================================
// 全系统统一时间戳 (CLOCK_MONOTONIC)
// 所有模块——采集、显示、编码、RTSP、录制——使用此时间基准
// ============================================================================

class PtsClock {
public:
    static uint64_t nowUs() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL
             + static_cast<uint64_t>(ts.tv_nsec) / 1000ULL;
    }

    static uint64_t nowMs() {
        return nowUs() / 1000ULL;
    }

    static uint64_t deltaUs(uint64_t prevPts) {
        uint64_t now = nowUs();
        return (now > prevPts) ? (now - prevPts) : 0;
    }
};
