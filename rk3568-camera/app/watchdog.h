#pragma once
#include "config.h"
#include <QObject>
#include <QTimer>
#include <QMap>
#include <functional>

// ============================================================================
// Watchdog：线程心跳监控 + 超时自动恢复
// 每个被监控线程需定期调用 feed(name)，超时则触发 recoverFn 尝试恢复
// ============================================================================

class Watchdog : public QObject {
    Q_OBJECT
public:
    explicit Watchdog(QObject *parent = nullptr);

    void registerTarget(const QString &name, int timeoutMs,
                        std::function<void()> recoverFn);
    void feed(const QString &name);
    void start();
    void stop();

signals:
    void targetTimeout(const QString &name);

private:
    void check();

    struct Target {
        QString              name;
        int                  timeoutMs;
        uint64_t             lastHeartbeat = 0;
        std::function<void()> recoverFn;
    };

    QMap<QString, Target> targets_;
    QTimer                checkTimer_;
    bool                  running_ = false;
};
