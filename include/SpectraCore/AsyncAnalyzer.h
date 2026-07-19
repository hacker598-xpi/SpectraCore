#pragma once

#include "Analyzer.h"
#include <functional>
#include <thread>
#include <atomic>

namespace SpectraCore {

template <size_t N, size_t MaxLogBands = 128>
class AsyncAnalyzer {
public:
    using AnalyzerType = Analyzer<N, MaxLogBands>;
    using Spectrum = typename AnalyzerType::Spectrum;

    AsyncAnalyzer() = default;
    ~AsyncAnalyzer() { stop(); }

    AnalyzerType& analyzer() noexcept { return analyzer_; }

    void start(std::function<void(const Spectrum&)> callback) {
        if (running_.exchange(true)) return;
        worker_ = std::thread([this, cb = std::move(callback)]() {
            while (running_.load(std::memory_order_relaxed)) {
                analyzer_.waitForFrame();
                if (!running_.load(std::memory_order_relaxed)) break;
                analyzer_.processReadyFrame();
                cb(analyzer_.getSpectrum());
            }
        });
    }

    void stop() noexcept {
        if (running_.exchange(false)) {
            analyzer_.notifyAll();
            if (worker_.joinable()) worker_.join();
        }
    }

private:
    AnalyzerType analyzer_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}
