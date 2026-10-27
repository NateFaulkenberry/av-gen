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

#include "analysis/beat_tracker.hpp"
#include "analysis/response_feed.hpp"
#include "analysis/stem_analysis.hpp"
#include "audio/analysis_stream.hpp"
#include "core/spsc_queue.hpp"
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

// A published frame: the newest StemFrame plus the serial of the frame that raised the onset it carries (0 = none).
struct LiveStemFrame {
    StemFrame frame;
    std::uint64_t serial = 0;
    std::uint64_t stamp = 0;
    // ADR-1581: on the beat feed's source, its causal beat tracker after this hop (as the mix's runner stamps its
    // frames); `tracked` false on every other source.
    BeatState beat;
    bool tracked = false;
};

// The render thread's half of the event carry for one source: fires a carried event once.
struct LiveStemLatch {
    std::uint64_t fired = 0;
    std::uint64_t serial = 0;
    void apply(LiveStemFrame& f);
};

class ChannelAnalysis {
public:
    struct Source {
        audio::AnalysisStream* stream = nullptr; // null: the source's channel is absent; it is never active
        bool pair = false;
        bool hits = false;
        float gainDb = 0.0f; // ADR-1582: the trim, before analysis
        bool beat = false;   // ADR-1581: run the causal beat tracker (the beat feed's source)
        bool feed = false;   // ADR-1580: queue every hop's response feed (a response feed's source; needs `hits`)
    };
    // Room for about 5 s of one source's hops (the Sonic timbre queue's size).
    static constexpr std::size_t kFeedCapacity = 512;
    // `previous` (stopped here) hands over the state of every source whose stream and tier are unchanged, so editing
    // one source does not reset the others' analysis (its trim, beat and feed roles are taken from `sources`). Its other
    // sources' streams may already be gone: they are not read.
    ChannelAnalysis(std::vector<Source> sources, std::uint32_t sampleRate, AnalyzerConfig base = {},
                    std::unique_ptr<ChannelAnalysis> previous = nullptr, BeatTrackerConfig beatConfig = {});
    ~ChannelAnalysis();
    ChannelAnalysis(const ChannelAnalysis&) = delete;
    ChannelAnalysis& operator=(const ChannelAnalysis&) = delete;

    void start();
    void stop();
    [[nodiscard]] std::size_t size() const { return slots_.size(); }

    // Render thread: whether a newer frame of `index` is in `latest(index)`.
    bool acquire(std::size_t index);
    [[nodiscard]] const LiveStemFrame& latest(std::size_t index) const { return slots_[index]->frames.front(); }
    // ADR-1582: any thread; the analysis thread applies it at its next read, without a reset.
    void setGainDb(std::size_t index, float gainDb);
    // ADR-1580: the render thread's end of a fed source's queue: the oldest hop not yet taken, on the input's frame
    // clock (seconds). False when empty (or the source is not fed).
    bool popFeed(std::size_t index, TimedFeed& out) { return slots_[index]->feeds.pop(out); }
    [[nodiscard]] std::uint64_t feedsDropped() const { return feedsDropped_.load(std::memory_order_relaxed); }

    // The analysis thread's own cost, for the profiler: microseconds per hop of one source (mean of the last pass),
    // and the worst single pass over every source.
    [[nodiscard]] double hopMicros() const { return hopMicros_.load(std::memory_order_relaxed); }
    [[nodiscard]] double passMaxMicros() const { return passMax_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t hopsProduced() const { return hops_.load(std::memory_order_relaxed); }
    // Microseconds the thread has spent analysing (not sleeping) since it started: its CPU share over a wall interval.
    [[nodiscard]] double busyMicros() const { return busy_.load(std::memory_order_relaxed); }

private:
    struct Slot {
        Source source;
        std::unique_ptr<StemAnalyzer> analyzer;
        TripleBuffer<LiveStemFrame> frames;
        std::uint64_t serial = 0;
        bool pending = false;
        float pendingStrength = 0.0f;
        std::uint64_t pendingStamp = 0;
        std::atomic<std::uint64_t> consumed{0};
        std::vector<float> read, mono, side;
        std::atomic<float> gainDb{0.0f};      // ADR-1582: wanted; `appliedGainDb` is what the analyzer runs
        float appliedGainDb = 0.0f;
        std::unique_ptr<BeatTracker> tracker; // ADR-1581
        SpscQueue<TimedFeed, kFeedCapacity> feeds; // ADR-1580
    };
    void threadMain(std::stop_token token);
    bool step(Slot& slot);

    std::vector<std::unique_ptr<Slot>> slots_;
    std::uint32_t hopSize_ = 512;
    std::jthread thread_;
    std::atomic<double> hopMicros_{0.0};
    std::atomic<double> passMax_{0.0};
    std::atomic<std::uint64_t> hops_{0};
    std::atomic<double> busy_{0.0};
    std::atomic<std::uint64_t> feedsDropped_{0};
    BeatTrackerConfig beatConfig_;
};

// ADR-1580: the live response feeds -- each fed source's queued hops folded into the mix's hops by the input's frame
// clock (both streams come from the same capture callback, so their clocks agree). Render thread only. A source hop
// that arrives after the mix hop it belongs to was stepped folds into the next one (its thread can trail by a hop).
class LiveFeedMixer final : public FeedProvider {
public:
    // `slots[i]` is the ChannelAnalysis index of fed source i; `routing` indexes into `slots`.
    LiveFeedMixer(ChannelAnalysis& channels, std::vector<std::size_t> slots, FeedRouting routing);
    bool feedAt(double seconds, const CausalOnsets& mix, ResponseFeed& out) override;
    [[nodiscard]] const FeedRouting& routing() const { return routing_; }

private:
    ChannelAnalysis& channels_;
    std::vector<std::size_t> slots_;
    FeedRouting routing_;
    std::vector<FeedFolder> folders_;
    std::vector<std::vector<TimedFeed>> pending_;
    std::vector<ResponseFeed> now_;
};

} // namespace avgen::analysis
