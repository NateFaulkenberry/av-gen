#pragma once

// ADR-710: where the volumetric march puts its samples along one camera ray.
//
// The CPU transliteration of `shaders/march_schedule.wgsl`, which `fs_volume` calls once per
// pixel. It is a pure function of the per-slot ray intervals (`mediumInterval`, ADR-562 §4 / the
// bound of ADR-566), the ray's length, the authored step count and whether the environment fog can
// be non-zero anywhere on the ray. `tests/unit/test_march_schedule.cpp` asserts its properties and
// `tests/rendering/test_march_schedule_parity_gpu.cpp` asserts the shader computes the same thing.
//
// The rule, in one sentence: **the authored step count is spent INSIDE the union of the media's
// intervals**, and the environment fog outside them keeps the grid it always had.
//
//   * No medium on the ray, environment fog on: one segment, `steps` samples over `[0, maxDistance]`
//     -- the legacy march, sample for sample.
//   * No medium on the ray, environment fog off: no segments. Nothing on this ray has density, so
//     the march has nothing to integrate and returns (0, 1) without looping.
//   * Media on the ray: each merged interval is one segment, and all of them together receive
//     `steps` samples, spaced evenly by length, never coarser than the legacy spacing. With the
//     environment fog on the intervals are first widened to the legacy grid's cell edges, so the
//     gaps between them are whole legacy cells marched exactly as before; with it off the gaps are
//     empty air and are skipped.

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>

namespace avgen::world {

// Two intervals per slot -- a column and a cap (ADR-710) -- over `kMaxMedia` (4) slots; merged, at
// most eight spans, with a gap before, between and after each.
inline constexpr std::size_t kMarchMaxIntervals = 8;
inline constexpr std::size_t kMarchMaxSegments = 17;

struct MarchSegment {
    float start = 0.0f; // metres along the ray where the segment begins
    float step = 0.0f;  // metres between samples; sample i is at start + (i + jitter) * step
    int count = 0;      // samples in the segment
    bool medium = false; // true for a medium interval, false for an environment-fog gap
};

struct MarchSchedule {
    std::array<MarchSegment, kMarchMaxSegments> segments{};
    std::uint32_t segmentCount = 0;
};

// `intervals` are ray intervals (tEnter, tExit) -- slot s's column at 2s and its cap at 2s + 1 in the
// march; an interval with tExit <= tEnter is empty (a missed or unused slot, or a kind with no cap).
// They are merged as a UNION, never a hull: the spacing is the union's length over `steps`, and only
// a union's length varies continuously from ray to ray (see the shader for the seam a hull drew).
// `ambient` is whether the environment fog's density can be non-zero (`volumeDensity > 0`).
[[nodiscard]] MarchSchedule marchSchedule(const std::array<glm::vec2, kMarchMaxIntervals>& intervals,
                                          float maxDistance, int steps, bool ambient);

// Total samples a schedule takes; the march's cost is proportional to it.
[[nodiscard]] int marchSampleCount(const MarchSchedule& schedule);

} // namespace avgen::world
