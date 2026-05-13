#include "watchdog.h"
#include "pts_clock.h"
#include <spdlog/spdlog.h>

Watchdog::Watchdog(QObject *parent) : QObject(parent) {
    QObject::connect(&checkTimer_, &QTimer::timeout, this, &Watchdog::check);
    checkTimer_.setInterval(WD_CHECK_INTERVAL_MS);
}

void Watchdog::registerTarget(const QString &name, int timeoutMs,
                               std::function<void()> recoverFn) {
    Target t;
    t.name       = name;
    t.timeoutMs  = timeoutMs;
    t.recoverFn  = std::move(recoverFn);
    targets_.insert(name, t);
    spdlog::info("Watchdog registered: {} timeout={}ms", name.toUtf8().constData(), timeoutMs);
}

void Watchdog::feed(const QString &name) {
    if (targets_.contains(name)) {
        targets_[name].lastHeartbeat = PtsClock::nowMs();
    }
}

void Watchdog::start() {
    running_ = true;
    checkTimer_.start();
    spdlog::info("Watchdog started, monitoring {} targets", targets_.size());
}

void Watchdog::stop() {
    running_ = false;
    checkTimer_.stop();
}

void Watchdog::check() {
    if (!running_) return;

    uint64_t now = PtsClock::nowMs();

    for (auto &target : targets_) {
        uint64_t elapsed = now - target.lastHeartbeat;
        if (elapsed > static_cast<uint64_t>(target.timeoutMs)) {
            spdlog::warn("Watchdog: {} timeout ({}ms, last heartbeat {}ms ago)",
                         target.name.toUtf8().constData(),
                         target.timeoutMs, elapsed);
            emit targetTimeout(target.name);

            if (target.recoverFn) {
                spdlog::info("Watchdog: attempting recovery for {}", target.name.toUtf8().constData());
                target.recoverFn();
                // 重置心跳时间，给恢复留时间
                target.lastHeartbeat = now;
            }
        }
    }
}
