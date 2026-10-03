#pragma once

// Background thread: AnalysisStream -> Analyzer -> TripleBuffer<AnalysisFrame>. The render
// thread calls latest() once per frame. Also keeps a short ring of recent frames for the debug UI.

#include "analysis/analyzer.hpp"
#include "analysis/beat_tracker.hpp"
#include "analysis/causal_onsets.hpp"
#include "audio/analysis_stream.hpp"
#include "core/triple_buffer.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace avgen::analysis {

// Something that wants every frame the runner produces, on the analysis thread, in order (ADR-1025: the live
// Sonic path's timbre stage). `onFrame` runs after the beat fields are filled and before the frame is published.
// It must not block or take long: it shares the thread with the analyzer.
class FrameTap {
public:
    virtual ~FrameTap() = default;
    virtual void onFrame(const AnalysisFrame& frame) = 0;
};

class AnalysisRunner {
public:
    AnalysisRunner(AnalyzerConfig config, audio::AnalysisStream& stream, BeatTrackerConfig beatConfig = {});
    ~AnalysisRunner();
    AnalysisRunner(const AnalysisRunner&) = delete;
    AnalysisRunner& operator=(const AnalysisRunner&) = delete;

    // Installs the tap every produced frame is shown to (null: none). Only before start(): the thread reads
    // the pointer without synchronisation. The tap must outlive the runner.
    void setTap(FrameTap* tap) { tap_ = tap; }

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
    CausalOnsetDetector causal_; // ADR-1060: kick/snare/hat live, into the band-onset fields
    // ADR-1060: the event carry. `pending_` holds the newest event of each kind until a frame holding it has been
    // acquired (`consumed_`, the serial of the newest acquired frame, written by the render thread in acquire()).
    struct Pending {
        bool onset = false, beat = false, low = false, mid = false, high = false;
        float onsetStrength = 0.0f, lowStrength = 0.0f, midStrength = 0.0f, highStrength = 0.0f;
        std::uint64_t onsetStamp = 0, beatStamp = 0, lowStamp = 0, midStamp = 0, highStamp = 0;
    } pending_;
    std::uint64_t serial_ = 0;
    std::atomic<std::uint64_t> consumed_{0};
    void carryEvents(AnalysisFrame& frame);
    TripleBuffer<AnalysisFrame> frames_;
    FrameTap* tap_ = nullptr;
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
