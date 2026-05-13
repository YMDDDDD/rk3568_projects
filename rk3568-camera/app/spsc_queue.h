#pragma once
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>

// ============================================================================
// 无锁单生产者单消费者队列
// - DisplayQueue (深度1，满则丢弃旧帧)
// - EncodeQueue  (深度4，满则阻塞等待)
// ============================================================================

template<typename T>
class SPSCQueue {
public:
    explicit SPSCQueue(size_t capacity)
        : capacity_(capacity)
        , mask_(capacity - 1)
    {
        buffer_.resize(capacity);
        for (auto &slot : buffer_) slot = nullptr;
    }

    // 生产者：非阻塞 push，满则覆盖最旧帧（DisplayQueue 用）
    bool tryPush(T item) {
        size_t w = writePos_.load(std::memory_order_relaxed);
        size_t r = readPos_.load(std::memory_order_acquire);

        if ((w - r) >= capacity_) {
            // 队列满，丢弃最旧的一帧
            r++;
            readPos_.store(r, std::memory_order_release);
        }

        buffer_[w & mask_] = item;
        writePos_.store(w + 1, std::memory_order_release);

        std::lock_guard<std::mutex> lk(mtx_);
        cv_.notify_one();
        return true;
    }

    // 生产者：阻塞 push（EncodeQueue 用，满则等待）
    void push(T item) {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait(lk, [this] {
            return (writePos_.load(std::memory_order_relaxed)
                  - readPos_.load(std::memory_order_acquire)) < capacity_;
        });

        size_t w = writePos_.load(std::memory_order_relaxed);
        buffer_[w & mask_] = item;
        writePos_.store(w + 1, std::memory_order_release);
        lk.unlock();
        cv_.notify_one();
    }

    // 消费者：阻塞 pop
    T pop() {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait(lk, [this] {
            return readPos_.load(std::memory_order_acquire)
                 < writePos_.load(std::memory_order_acquire);
        });

        size_t r = readPos_.load(std::memory_order_relaxed);
        T item = buffer_[r & mask_];
        buffer_[r & mask_] = nullptr;
        readPos_.store(r + 1, std::memory_order_release);
        lk.unlock();
        cv_.notify_one();
        return item;
    }

    // 消费者：非阻塞 pop，空则返回 nullptr
    T tryPop() {
        size_t r = readPos_.load(std::memory_order_relaxed);
        size_t w = writePos_.load(std::memory_order_acquire);
        if (r >= w) return nullptr;

        T item = buffer_[r & mask_];
        buffer_[r & mask_] = nullptr;
        readPos_.store(r + 1, std::memory_order_release);

        std::lock_guard<std::mutex> lk(mtx_);
        cv_.notify_one();
        return item;
    }

    size_t sizeApprox() const {
        int64_t w = writePos_.load(std::memory_order_acquire);
        int64_t r = readPos_.load(std::memory_order_acquire);
        int64_t s = w - r;
        return s < 0 ? 0 : static_cast<size_t>(s);
    }

private:
    std::vector<T>              buffer_;
    size_t                      capacity_;
    size_t                      mask_;
    std::atomic<size_t>         writePos_{0};
    std::atomic<size_t>         readPos_{0};
    std::mutex                  mtx_;
    std::condition_variable     cv_;
};
