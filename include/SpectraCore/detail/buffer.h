#pragma once
#include <array>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace SpectraCore::detail {

template <size_t N>
class FrameDoubleBuffer {
public:
    FrameDoubleBuffer() noexcept {
        for (auto& b : buffers_) b.fill(0.0f);
        bufferBusy_[0] = bufferBusy_[1] = false;
    }

    float* getFillBuffer() noexcept {
        int idx = writeIdx_.load(std::memory_order_relaxed);
        while (bufferBusy_[idx].load(std::memory_order_acquire))
            std::this_thread::yield();
        return buffers_[idx].data();
    }

    void submitFill() noexcept {
        int idx = writeIdx_.load(std::memory_order_relaxed);
        bufferBusy_[idx].store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lk(cv_mut_);
            readyIdx_.store(idx, std::memory_order_release);
        }
        cv_.notify_one();
        writeIdx_.store(1 - idx, std::memory_order_relaxed);
    }

    bool hasReadyFrame() const noexcept {
        return readyIdx_.load(std::memory_order_acquire) >= 0;
    }

    struct ReadyFrame { int index; const float* data; };
    ReadyFrame getReadyFrame() noexcept {
        int idx = readyIdx_.exchange(-1, std::memory_order_acquire);
        if (idx < 0) return {-1, nullptr};
        return {idx, buffers_[idx].data()};
    }

    void markProcessed(int idx) noexcept {
        bufferBusy_[idx].store(false, std::memory_order_release);
    }

    void waitUntilReady() const noexcept {
        std::unique_lock<std::mutex> lk(cv_mut_);
        cv_.wait(lk, [this] { return readyIdx_.load(std::memory_order_acquire) >= 0; });
    }

    void notifyAll() noexcept {
        std::lock_guard<std::mutex> lk(cv_mut_);
        cv_.notify_all();
    }

private:
    std::array<std::array<float, N>, 2> buffers_;
    std::atomic<int> writeIdx_{0};
    std::atomic<int> readyIdx_{-1};
    std::atomic<bool> bufferBusy_[2]{false, false};
    mutable std::mutex cv_mut_;
    mutable std::condition_variable cv_;
};

}
