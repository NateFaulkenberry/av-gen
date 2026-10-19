#pragma once

// One analysis thread for every live input source (stems-multichannel, ADR-1521).
//
// Each source is an `AnalysisStream` the capture callback writes (audio::AudioInput::sourceStream): mono samples, or
// for a linked pair interleaved (mid, side) frames. The thread steps every source's `StemAnalyzer` -- the same code an
// offline stem runs -- and publishes each source's newest `StemFrame` through a triple buffer the render thread reads.
// One thread rather than one per source: measured per-hop cost ~16 us per source (the hits tier ~110 us), so eight
// sources are about 1 ms of a 10.7 ms hop, and one thread keeps eight or twenty-four idle sources from waking on their
// own timers (report §7).
//
// Events are carried until the render thread has taken a frame that holds them (the ADR-1060 pattern): a source's
// onset between two render frames is not lost, and `LiveStemLatch` fires it exactly once.

#include "analysis/stem_analysis.hpp"
#include "audio/analysis_stream.hpp"
#include "core/triple_buffer.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace avgen::analysis {

// A published frame: the newest StemFrame plus, per event kind (onset, kick, snare, hat), the serial of the frame
// that raised the event it carries (0 = none).
struct LiveStemFrame {
    StemFrame frame;
    std::uint64_t serial = 0;
    std::array<std::uint64_t, 4> stamps{};
};

// The render thread's half of the event carry for one source: fires a carried event once.
struct LiveStemLatch {
    std::array<std::uint64_t, 4> fired{};
    std::uint64_t serial = 0;
    void apply(LiveStemFrame& f);
};

class ChannelAnalysis {
public:
    struct Source {
        audio::AnalysisStream* stream = nullptr; // null: the source's channel is absent; it is never active
        bool pair = false;
        bool hits = false;
    };
    // `previous` (stopped here) hands over the state of every source whose stream and tier are unchanged, so editing
    // one source does not reset the others' analysis. Its other sources' streams may already be gone: they are not read.
    ChannelAnalysis(std::vector<Source> sources, std::uint32_t sampleRate, AnalyzerConfig base = {},
                    std::unique_ptr<ChannelAnalysis> previous = nullptr);
    ~ChannelAnalysis();
    ChannelAnalysis(const ChannelAnalysis&) = delete;
    ChannelAnalysis& operator=(const ChannelAnalysis&) = delete;

    void start();
    void stop();
    [[nodiscard]] std::size_t size() const { return slots_.size(); }

    // Render thread: whether a newer frame of `index` is in `latest(index)`.
    bool acquire(std::size_t index);
    [[nodiscard]] const LiveStemFrame& latest(std::size_t index) const { return slots_[index]->frames.front(); }

    // The analysis thread's own cost, for the profiler: microseconds per hop of one source (mean of the last pass),
    // and the worst single pass over every source.
    [[nodiscard]] double hopMicros() const { return hopMicros_.load(std::memory_order_relaxed); }
    [[nodiscard]] double passMaxMicros() const { return passMax_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t hopsProduced() const { return hops_.load(std::memory_order_relaxed); }

private:
    struct Slot {
        Source source;
        std::unique_ptr<StemAnalyzer> analyzer;
        TripleBuffer<LiveStemFrame> frames;
        std::uint64_t serial = 0;
        std::array<bool, 4> pending{};
        std::array<float, 4> pendingStrength{};
        std::array<std::uint64_t, 4> pendingStamp{};
        std::atomic<std::uint64_t> consumed{0};
        std::vector<float> read, mono, side;
    };
    void threadMain(std::stop_token token);
    bool step(Slot& slot);

    std::vector<std::unique_ptr<Slot>> slots_;
    std::uint32_t hopSize_ = 512;
    std::jthread thread_;
    std::atomic<double> hopMicros_{0.0};
    std::atomic<double> passMax_{0.0};
    std::atomic<std::uint64_t> hops_{0};
};

} // namespace avgen::analysis
