#pragma once

// The locomotion plan (Phase B §8-§11): starting, stopping, turning and strafing.
//
// **What this is on top of, and why it is not a second gait machine.** `Gait::select` already
// chooses the clip *family* -- idle, walk, run, turn -- with a speed band and a dwell timer, and
// ADR-096 settled that it owns that choice. It has no notion of the *transitional* states, and
// those are what §8 and §9 are about: a body does not go from standing to walking, it goes from
// standing to **starting** to walking, and the difference is the whole of what makes a character
// read as having mass.
//
// So this reads the gait's answer and adds a phase to it. It never contradicts the gait, and two
// answers to "which clip family" is what ADR-260 exists to prevent.
//
// **Hysteresis in both dimensions, because one is not enough.** `GaitSettings` already paid for
// that lesson: a speed band alone does not stop flicker in time, and a dwell alone does not stop it
// in speed. Every threshold here comes in a pair with a minimum dwell beside it.
//
// **Memory is a value the entity owns**, exactly as `MotionMemory` and `MotionState` are, so
// `EntityWorld::seek` reconstructs it by replay (ADR-541, ADR-554).

#include "entity/locomotion.hpp"

#include <cstdint>

#include <glm/glm.hpp>

namespace avgen::entity {

// `LocomotionPhase` lives in `locomotion.hpp`: the seam carries it, and a planner that owned
// the type the seam publishes would make the seam depend on the planner.


struct LocomotionPlanSettings {
    // §8. How long a start lasts at most, and the fraction of target speed that ends it early.
    // Both, because a body that reaches its pace early should stop "starting", and one that never
    // reaches it should not be starting forever.
    float startSeconds = 0.45f;
    float startCompleteFraction = 0.85f;
    // §9. A stop begins when the *desired* speed falls below this fraction of the current speed --
    // which is a braking test rather than a slow test, so a body that was always slow does not
    // spend its life stopping.
    float stopTriggerFraction = 0.35f;
    float stopSeconds = 0.5f;
    // Below this the body is standing, in metres per second.
    float standSpeed = 0.12f;
    // §11. How far apart the heading and the facing must be before it is a strafe rather than a
    // walk, in degrees. 35 is about where a viewer stops reading it as "walking with a slight
    // lean" and starts reading it as "moving sideways".
    float strafeEnterDegrees = 35.0f;
    float strafeExitDegrees = 22.0f;
    // §10. Turning on the spot: above this |rad/s| with no meaningful travel.
    float turnEnterRate = 0.45f;
    float turnExitRate = 0.2f;
    // The floor under every transition, in seconds. One number rather than one per edge: the
    // failure it prevents is the same everywhere, and six tunable dwells is five more places to be
    // wrong.
    float minDwell = 0.18f;

    friend bool operator==(const LocomotionPlanSettings&, const LocomotionPlanSettings&) = default;
};

// Everything the planner remembers. A plain value; the entity owns it and a seek replays it.
struct LocomotionPlanState {
    LocomotionPhase phase = LocomotionPhase::Idle;
    // The timeline second the phase was entered. A **time**, never an accumulator, for the reason
    // ADR-086 gives and the reason `decision.hpp` counts in ticks: an elapsed count integrated at
    // one frame rate expires on a different instant at another, so a replay would not land where
    // the play did.
    double entered = 0.0;
    // The speed the body was asked for when a stop began, so "how far through the stop am I" is
    // answerable without a second accumulator.
    float stopFromSpeed = 0.0f;
    bool started = false;

    void reset() { *this = LocomotionPlanState{}; }
};

// What the plan concluded. Read by motion selection and by the stride and lean layers.
struct LocomotionPlan {
    LocomotionPhase phase = LocomotionPhase::Idle;
    // 0..1 through the current phase, where a phase has a natural length (`Starting`, `Stopping`).
    // 0 for the phases that do not. This is what a stride ramp reads: §8's "strideRamp" is this
    // number, not a second clock.
    float progress = 0.0f;
    // §8's stride ramp and §9's brake, as one number: how much of the authored stride this phase
    // wants. A start eases the stride in from a stand; a stop eases it out. 1 elsewhere, so a
    // layer can multiply unconditionally.
    float strideScale = 1.0f;
    // Signed radians between where the body is going and where it is facing. The measurement §11
    // is about, carried so that nothing downstream re-derives it.
    float strafeAngle = 0.0f;
    bool changed = false; // true on the frame the phase changed, for an inertializer to key on
};

// One step. Pure: everything remembered arrives in `in` and leaves in `next`.
//
// `speed` is the MEASURED ground speed and `desiredSpeed` what the mover asked for -- ADR-545's
// pair, and the difference between them is what tells a start from a stop.
[[nodiscard]] LocomotionPlan planLocomotion(const LocomotionPlanSettings& settings,
                                            const LocomotionPlanState& in, Activity gait,
                                            float speed, float desiredSpeed, float turnRate,
                                            float strafeAngle, double time,
                                            LocomotionPlanState& next);

} // namespace avgen::entity
