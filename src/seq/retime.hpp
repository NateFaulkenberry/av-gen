#pragma once

// A local retime of one actor (ADR-823): slow motion (or fast) over a window, as stretched keys and
// scaled clip speeds -- the Director's `PlanRetime`. Only this actor changes; the world's clock, the
// music and every other body carry on at their rate (a global time warp is out of scope by design).
//
// The time map, for a window [a, b] played at `rate` (0.5 = half speed):
//     t <= a      ->  t
//     a < t <= b  ->  a + (t - a) / rate
//     t > b       ->  t + (b - a) (1 / rate - 1)
// so everything after the window is pushed later by the time the window gained, and the actor's
// performance still ends in one piece.

#include "core/error.hpp"
#include "seq/sequence.hpp"

namespace avgen::seq {

[[nodiscard]] double retimeMap(double seconds, double windowStart, double windowEnd, float rate);

// Retimes `actor` in place:
//   * keys: a key is inserted at each window edge a key interval straddles (at the actor's own
//     position there), so the motion's speed changes exactly at the edge; then every key moves by
//     the map. Linear keys stay exact; a Smooth key's Catmull-Rom tangents are re-derived from the
//     new neighbours, which is close and not exact;
//   * clip cues: moved by the map; a cue that runs across an edge is split there, the continuation
//     carrying the clip time it had reached (`ClipCue::offsetSeconds`), with its speed multiplied by
//     `rate` inside the window -- a run slowed mid-stride stays mid-stride;
//   * airborne spans and earlier warps: endpoints moved by the map;
//   * `timeWarps` gains the window as it lands on the timeline, so a performance's gait knows.
// Refused, and nothing changed: a rate that is not positive, an empty window, a window that
// overlaps an earlier warp (compose them in one call instead), or a path the window cuts through
// (a path is one arc-length parameterisation; bake it to keys first).
[[nodiscard]] Result<void> retimeActor(Actor& actor, double windowStart, double windowEnd, float rate);

} // namespace avgen::seq
