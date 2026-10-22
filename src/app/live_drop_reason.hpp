#pragma once

// ADR-1600: why the live quality controller dropped a level.
//
// The controller (ADR-1362) drops on deadline misses confirmed by GPU spans; the trace and the Live panel said THAT it
// dropped, never why. A drop reason is two facts a performer can act on: how many frames missed in the window that
// decided it, and which GPU pass grew the most at the level being left -- its median over the most recent frames
// against its median over the level's earlier frames. A pass that grew is the content that got heavier (the song
// reached a dense section, a volume filled the frame); no pass growing means the misses were not the GPU's content.
//
// Pure: fed one frame's pass list at a time, reset on every level change (a level change moves every pass, which is
// not growth), so it is a unit test rather than a benchmark.

#include "gpu/timeline_math.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace avgen::app {

class PassGrowthTracker {
public:
    // `recentFrames`: how many of the latest frames are "now"; `capacity`: how many frames are kept at most (the
    // controller's miss window, ADR-1362's 300).
    explicit PassGrowthTracker(std::size_t recentFrames = 60, std::size_t capacity = 300);

    // One frame's passes (labels may repeat; they are summed). A label absent from a frame cost 0 in it.
    void note(const std::vector<gpu::TimelineInterval>& passes);
    // A level change: the frames before it describe a different picture.
    void reset();
    [[nodiscard]] std::size_t frames() const { return frames_; }

    struct Growth {
        bool valid = false; // false: too few frames at this level to compare, or nothing grew
        std::string label;
        double beforeMs = 0.0; // median over the level's earlier frames
        double nowMs = 0.0;    // median over the most recent frames
        [[nodiscard]] double grownMs() const { return nowMs - beforeMs; }
    };
    // The pass whose recent median exceeds its earlier median by the most. With fewer than 2 x recentFrames frames the
    // split is half and half; with fewer than 8 frames there is no answer.
    [[nodiscard]] Growth largestGrowth() const;

private:
    std::size_t recentFrames_;
    std::size_t capacity_;
    std::size_t frames_ = 0;
    // Per label, its last `capacity_` per-frame values, oldest first (0 on a frame it did not run).
    std::vector<std::pair<std::string, std::vector<double>>> series_;
};

struct LiveDropReason {
    std::string fromLevel;
    std::string toLevel;
    int misses = 0;        // deadline misses in the window that decided it (paced); 0 unpaced
    int missWindow = 0;    // that window, in frames
    double gpuCostMs = -1.0; // the controller's GPU reading at the decision (unpaced: what decided it)
    bool revert = false;     // a raise on probation that missed, undone (ADR-1104/1362): one miss is enough
    PassGrowthTracker::Growth growth;
    // One line, no commas (it is also a CSV field): "High->Medium: 4 misses in 300 frames; scene +3.12 ms (12.40->15.52)".
    [[nodiscard]] std::string text() const;
};

} // namespace avgen::app
