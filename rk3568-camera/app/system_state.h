#pragma once
#include <QObject>
#include <atomic>
#include <cstdint>

// ============================================================================
// 系统状态标志位（可叠加）
// ============================================================================

enum SystemFlag : quint32 {
    FLAG_IDLE      = 0,
    FLAG_PREVIEW   = 1 << 0,   // 采集+显示运行中
    FLAG_STREAMING = 1 << 1,   // RTSP 推流中
    FLAG_RECORDING = 1 << 2,   // 本地录制中
    FLAG_AI        = 1 << 3,   // YOLO 检测中
    FLAG_ERROR     = 1 << 4,   // 异常状态
};

// ============================================================================
// 系统状态机：标志位管理 + 状态转换通知
// ============================================================================

class SystemState : public QObject {
    Q_OBJECT
public:
    explicit SystemState(QObject *parent = nullptr) : QObject(parent) {}

    void set(quint32 flag) {
        quint32 old = m_flags.load(std::memory_order_relaxed);
        quint32 nxt = old | flag;
        while (!m_flags.compare_exchange_weak(old, nxt,
                std::memory_order_release, std::memory_order_relaxed)) {
            nxt = old | flag;
        }
        if (old != nxt) {
            emit changed(old, nxt);
        }
    }

    void clear(quint32 flag) {
        quint32 old = m_flags.load(std::memory_order_relaxed);
        quint32 nxt = old & ~flag;
        while (!m_flags.compare_exchange_weak(old, nxt,
                std::memory_order_release, std::memory_order_relaxed)) {
            nxt = old & ~flag;
        }
        if (old != nxt) {
            emit changed(old, nxt);
        }
    }

    bool test(quint32 flag) const {
        return (m_flags.load(std::memory_order_acquire) & flag) != 0;
    }

    quint32 flags() const {
        return m_flags.load(std::memory_order_acquire);
    }

signals:
    void changed(quint32 oldFlags, quint32 newFlags);

private:
    std::atomic<quint32> m_flags{FLAG_IDLE};
};
