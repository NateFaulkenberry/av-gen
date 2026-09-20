#pragma once

// The motion controller (Phase B §33-§35): intent in, continuous motion out.
//
// **What it is not.** It does not decide goals -- §33 is explicit, and ADR-091's tiers already say
// who does. It does not select a gait: `Gait::select` owns that, with hysteresis and a dwell timer
// this has no business duplicating (ADR-096). It does not pose anything: that is the provider's
// job and then the layer stack's.
//
// What it owns is the **vector** layer that `Gait` does not have. `Gait::approach` limits how fast
// a *scalar* speed may change, which is the whole model for a body that walks where it looks. A
// body that strafes, backs up, or circles a target while watching it has a velocity and a facing
// that disagree, and "how fast may that change" is two different questions:
//
//   * **along** the current heading -- speeding up and braking, which have different limits because
//     stopping is not the reverse of starting;
//   * **across** it -- turning, which is limited by a turn rate and not by an acceleration, because
//     a body at 5 m/s and a body at 0.5 m/s take about the same time to change direction.
//
// Collapsing those into one "move the velocity vector toward the desired one at `maxAccel`" makes a
// fast body turn slowly and a slow body turn instantly, which is backwards.
//
// **Its memory is a value the entity owns**, exactly as `MotionMemory` is, and for the same reason:
// `EntityWorld::seek` reproduces a frame by replaying the simulation, so anything remembered here
// must be reconstructible by that replay (ADR-541, ADR-560).

#include "entity/motion_provider.hpp"

#include <glm/glm.hpp>

namespace avgen::entity {

// How hard this body may change what it is doing. §34 says these should eventually be
// character-profile data; `GaitSettings` already carries `accel`/`decel` per entity and this reads
// them rather than introducing a second place to author the same number.
struct MotionLimits {
    float maxAcceleration = 6.0f; // m/s^2 along the heading
    float maxDeceleration = 8.0f; // m/s^2, braking
    float maxTurnRate = 3.0f;     // rad/s, how fast the VELOCITY may change direction
    float maxFacingRate = 6.0f;   // rad/s, how fast the BODY may turn to face somewhere
    // Below this speed a body has no meaningful heading, so a direction change is free: a standing
    // body asked to walk east should not have to turn its velocity vector from a direction it does
    // not have. Expressed in m/s because it is about the body, not about the clip.
    float headingFloor = 0.05f;
};

// Everything the controller remembers between steps. A plain value, entity-owned.
struct MotionState {
    glm::vec3 velocity{0.0f};           // world, m/s -- what the body is actually doing
    glm::vec3 previousVelocity{0.0f};   // §35, so acceleration is measured once and not re-derived
    glm::vec3 facing{0.0f, 0.0f, 1.0f}; // world, unit
    bool started = false;               // false until the first step; distinguishes "at rest" from "unknown"

    void reset() { *this = MotionState{}; }
};

// What the controller concluded this step. Everything downstream reads this rather than deriving
// its own answer -- §35's rule, and the one ADR-545 already had to establish for velocity.
struct MotionSolution {
    glm::vec3 velocity{0.0f};
    glm::vec3 facing{0.0f, 0.0f, 1.0f};
    glm::vec3 acceleration{0.0f}; // m/s^2, this step; feeds lean (§19) and stride (§7)
    float speed = 0.0f;           // horizontal magnitude of `velocity`
    float turnRate = 0.0f;        // rad/s, signed, the rate the FACING actually changed at
    // True when the request asked for more change than the limits allowed. **Reported rather than
    // hidden**: a controller that silently clamps is indistinguishable from one whose limits are
    // right, and B.A found exactly that failure in `Gait::playbackRate`, where the shipping cast
    // has been sitting on the clamp floor permanently with nothing saying so.
    bool accelerationLimited = false;
    bool turnLimited = false;
};

// Advance one step. Pure: everything it remembers arrives in `in` and leaves in `next`.
//
// `dt <= 0` is not an error -- `FixedStepClock::tick()` hands out zero on the first tick of every
// render (ADR-521) -- and yields the state unchanged with zero acceleration rather than a division.
[[nodiscard]] MotionSolution stepMotion(const MotionRequest& request, const MotionState& in,
                                        const MotionLimits& limits, float dt, MotionState& next);

} // namespace avgen::entity
