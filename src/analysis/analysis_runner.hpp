#pragma once

// Background thread: AnalysisStream -> Analyzer -> TripleBuffer<AnalysisFrame>. The render
// thread calls latest() once per frame. Also keeps a short ring of recent frames for the debug UI.

#include "analysis/analyzer.hpp"
#include "analysis/beat_tracker.hpp"
#include "audio/analysis_stream.hpp"
#include "core/triple_buffer.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace avgen::analysis {

class AnalysisRunner {
public:
    AnalysisRunner(AnalyzerConfig config, audio::AnalysisStream& stream, BeatTrackerConfig beatConfig = {});
    ~AnalysisRunner();
    AnalysisRunner(const AnalysisRunner&) = delete;
    AnalysisRunner& operator=(const AnalysisRunner&) = delete;

    void start();
    void stop();
    [[nodiscard]] bool running() const { return running_.load(); }

    // Render-thread side. Returns true if a newer frame is now in latest().
    bool acquire();
    [[nodiscard]] const AnalysisFrame& latest() const { return frames_.front(); }

    // Copy of the last `count` frames (oldest first) for plots. Cheap; called at UI rate.
    [[nodiscard]] std::vector<AnalysisFrame> history(std::size_t count) const;

    // Wall-clock microseconds spent per hop, exponentially averaged. For the performance display.
    [[nodiscard]] double averageHopMicros() const { return hopMicros_.load(); }
    [[nodiscard]] std::uint64_t framesProduced() const { return produced_.load(); }

private:
    void threadMain(std::stop_token token);

    AnalyzerConfig config_;
    audio::AnalysisStream& stream_;
    Analyzer analyzer_;
    BeatTracker beatTracker_; // fills the beat fields of every frame
    TripleBuffer<AnalysisFrame> frames_;
    std::jthread thread_;
    std::atomic<bool> running_{false};
    std::atomic<double> hopMicros_{0.0};
    std::atomic<std::uint64_t> produced_{0};

    mutable std::mutex historyMutex_;
    std::vector<AnalysisFrame> history_;
    std::size_t historyHead_ = 0;
    static constexpr std::size_t kHistorySize = 512;
};

} // namespace avgen::analysis
