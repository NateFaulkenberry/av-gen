#pragma once

// What the response model reads from one analysis hop, and where it comes from (ADR-1580).
//
// `sonic::ResponseModel` reads a hop's causal onsets (ADR-1060): per hit class (low, onset) whether it fired and its
// strength, and the hop's broadband onset ratio, level, bass level and onset register. A `ResponseFeed` is exactly
// those. By default every hop's feed is the mix's (`fromCausal(frame.causal)`).
//
// A project's `feeds` block may route a hit class (low, onset) and the level family (bass, level, levelSlow, intensity,
// sustain, transient, flux: one source, so that their adaptive loudness reference, ADR-1510, is the loudness of the
// signal they read) to a stem or a live input source. A source's hops run on its own clock (a stem at its own rate, a
// live channel on the input's frame clock), so they are FOLDED into the mix's hops by time: a source hop belongs to the
// first mix hop at or after it. Events in (previous mix hop, this hop] are OR-ed at their strongest; continuous values
// are the newest source hop at or before this one.
//
// An inactive source (outside a stem's span, an absent or disconnected channel, a stalled one) feeds no hit, and its
// levels read below the gate: a dead channel is not silently replaced by the mix.

#include "analysis/causal_onsets.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace avgen::analysis {

struct ResponseFeed {
    bool valid = false;                           // the levels are measured (the detector ran)
    std::array<bool, kHitClassCount> hit{};       // decided on this hop (only ever on a valid one)
    std::array<float, kHitClassCount> strength{};
    float onsetRatio = 0.0f; // the broadband (Onset) ratio
    float levelDb = -120.0f;
    float bassDb = -120.0f;
    float midDb = -120.0f;  // ADR-1723: part of the level family (routed with it)
    float highDb = -120.0f; // ADR-1723
    std::array<float, 3> onsetRegister{}; // ADR-1604: the register of the onset hit (when hit[Onset])
    [[nodiscard]] bool operator==(const ResponseFeed&) const = default;

    // A hop's onsets (the mix's or a source's).
    [[nodiscard]] static ResponseFeed fromCausal(const CausalOnsets& c);
    // A source that has no data now: no hit, the levels at -120 dBFS (below any gate), measured.
    [[nodiscard]] static ResponseFeed inactive();
};

// One source hop on the project clock.
struct TimedFeed {
    double seconds = 0.0;
    ResponseFeed feed;
};

// Where each part of the response reads from: -1 the mix, otherwise an index into the fed sources.
struct FeedRouting {
    std::array<int, kHitClassCount> hit{-1, -1};
    int levels = -1;
    [[nodiscard]] bool any() const;
    [[nodiscard]] bool operator==(const FeedRouting&) const = default;
};

// Folds one source's hops into a mix hop. Keeps the newest hop seen; `fold` is called with increasing `to`.
class FeedFolder {
public:
    // Up to how far behind a mix hop the newest source hop may be and still count as now (a live source's thread may
    // trail the mix's by a hop or two; a stem's hop can be longer than the mix's).
    static constexpr double kStaleSeconds = 0.25;

    // Folds `hops` (in time order, all at or before `to`) into the hop at `to`. `active` false (a stem outside its
    // span) reads inactive whatever the hops say.
    [[nodiscard]] ResponseFeed fold(std::span<const TimedFeed> hops, double to, bool active = true);
    void reset() { *this = FeedFolder{}; }

private:
    TimedFeed newest_;
    bool have_ = false;
};

// A live path's feeds (ADR-1580): the render thread asks for the feed of each mix hop it steps, in order.
class FeedProvider {
public:
    virtual ~FeedProvider() = default;
    // The feed for the mix hop at `seconds` (the input's frame clock) whose own causal onsets are `mix`. False: no
    // feed is routed, step on the mix's onsets.
    virtual bool feedAt(double seconds, const CausalOnsets& mix, ResponseFeed& out) = 0;
};

// The feed the response model steps on: the mix's (`fromCausal`), with each routed part replaced by its source's.
[[nodiscard]] ResponseFeed mergeFeeds(const ResponseFeed& mix, std::span<const ResponseFeed> sources,
                                      const FeedRouting& routing);

} // namespace avgen::analysis
