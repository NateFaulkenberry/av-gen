#pragma once

// The arithmetic behind gpu::FrameTimeline, with no WebGPU in it so the unit tests can pin it.
//
// A frame resolves to N timestamps: slot 0 is the frame origin (the beginning of the first pass)
// and slots 1..N-1 are pass ends, written in submission order. Pass i's cost is the interval
// end[i] - end[i-1], so the passes partition the frame and sum to it exactly.
//
// The one wrinkle is that Metal does not write the end-of-pass timestamp of a render pass that
// issues no draws -- an empty clear resolves as a literal zero. Left alone that turns the next
// pass's interval into a raw counter value: the depth prepass of the world scene once reported
// 228,832,448 ms. So a slot is only believed when it is at least the last believed one; an
// unbelieved slot charges its pass nothing and leaves the boundary where it was, which folds
// that pass's (near-zero) cost into the next one rather than corrupting the frame.

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::gpu {

struct TimelineInterval {
    std::string label;
    double ms = 0.0;
};

struct TimelineSpan {
    std::vector<TimelineInterval> passes; // one per timestamp after the origin, in submission order
    double frameMs = -1.0;                // last believed timestamp minus the origin; -1 = no data
    std::uint32_t unwritten = 0;          // slots the driver did not write
};

// `timestamps` and `labels` are both `count` long; labels[0] names nothing (it is the origin).
[[nodiscard]] TimelineSpan timelineIntervals(const std::uint64_t* timestamps, const std::string* labels,
                                             std::uint32_t count);

} // namespace avgen::gpu
