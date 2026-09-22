#pragma once

// Curved trajectories (Phase B §38): where the body will want to be heading shortly, so a turn can
// begin before the corner rather than at it.
//
// **The gap this was written to close is still open, and this module is on the dark side of it**
// (ADR-615). `MotionRequest::futureDirection` and `futureSeconds` were added in B.C with no
// producer and no consumer. The sentence that used to stand here said "Both ends are closed here:
// this produces the direction, and `stepMotion` consumes it." **Neither half is true as built:**
//
//   * `sampleTrajectory` has no caller in `src/` or `tools/` -- only tests;
//   * `stepMotion`, the named consumer, is itself unreachable (`Entity::motionState_` is touched
//     in exactly one place in the tree, and that place is `.reset()`);
//   * nothing anywhere assigns `MotionRequest::futureDirection` -- what this file writes is
//     `TrajectorySample::futureDirection`, a different struct, and nothing copies one to the other;
//   * `MotionRequest::futureSeconds` has exactly one reference in the tree: its own declaration.
//
// The code is correct and tested. It is waiting for a caller, and the ruling is to leave it that
// way and say so here rather than wire it or delete it.
//
// **Deliberately one sample, not a trajectory.** §38 says so outright: Phase C's motion matcher
// will want a real future trajectory with several horizons, and this is the lightweight stand-in
// that does not pretend to be one. A single direction and the time it is measured over is enough to
// start a turn early, and anything richer would be a second answer for Phase C to contradict.
//
// **The failure to watch is not turning late.** A lookahead that turns too early reads as a
// character anticipating a corner the viewer cannot see yet, and an onset count cannot see that --
// it only gets better as the lookahead grows. The measurement that catches it is **path
// deviation**: how far off the intended line the body travels while anticipating. Both are in
// `test_trajectory.cpp`, and the second is why `anticipation` is a dial rather than a constant.

#include <cstddef>
#include <span>

#include <glm/glm.hpp>

namespace avgen::entity {

struct TrajectorySettings {
    // How far ahead to look, in seconds of travel at the current pace. Seconds rather than metres
    // so a running body looks further than a walking one without a second setting -- which is what
    // a body actually does.
    float lookaheadSeconds = 0.45f;
    // How much of the turn to spend on where the body will be going rather than where it is going.
    // **A dial because over-eager anticipation is a real failure**, not because the number is
    // unknown: at 1 the body aims entirely at the future and cuts the corner visibly.
    float anticipation = 0.5f;
    // A corner sharper than this, in degrees, is a turn worth anticipating; below it the lookahead
    // is suppressed so a body on a gentle curve does not weave.
    float minCornerDegrees = 12.0f;

    friend bool operator==(const TrajectorySettings&, const TrajectorySettings&) = default;
};

struct TrajectorySample {
    // Unit, world, horizontal. Where the body wants to go **now**.
    glm::vec3 direction{0.0f};
    // Unit, world, horizontal. Where it will want to go in `seconds`. Zero length means "no
    // opinion" -- the path ends, or the corner is too gentle to anticipate -- which is not the
    // same as "straight ahead" and callers must not treat it as such.
    glm::vec3 futureDirection{0.0f};
    float seconds = 0.0f;
    // The angle between the two, in radians. Reported so a caller can decide how hard to lean
    // without re-deriving it, and so a debug view can say why the body is turning.
    float cornerAngle = 0.0f;
    bool hasFuture = false;
};

// Sample a polyline path from `position`, travelling at `speed`.
//
// `waypoints` is the path ahead, nearest first. An empty or single-point path has no direction and
// says so rather than inventing one.
[[nodiscard]] TrajectorySample sampleTrajectory(const TrajectorySettings& settings,
                                                std::span<const glm::vec3> waypoints,
                                                const glm::vec3& position, float speed);

} // namespace avgen::entity
