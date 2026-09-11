#pragma once

// Gait: how a travelling speed becomes an animation state, and how fast a body may change that
// speed (ADR-096, the brief's §7).
//
// `LocomotionState` and `Activity` already carried the state machine the brief asks for, and every
// behaviour already picks a gait from its speed. What was missing is the two things that make the
// result watchable rather than merely correct:
//
//   * **Hysteresis.** A character crossing the walk/run threshold once a frame -- a wanderer
//     steering round a tree, a walker on a slope, anything whose speed sits on the boundary --
//     flickers between two clips several times a second. Selecting on one threshold is the defect;
//     separate enter and exit thresholds plus a minimum dwell is the fix, and both are needed:
//     the speed band stops the flicker in speed and the dwell stops it in time, which is where an
//     accelerating body crosses the band legitimately fast.
//
//   * **Acceleration.** A body that reaches full speed in one frame and stops in one frame reads
//     as a sprite, not a character. `approach()` is the whole model: a limit on how fast the speed
//     may rise and a different limit on how fast it may fall, integrated against the real dt so a
//     frame-rate change does not change the trajectory.
//
// Everything here is a pure function of its arguments and three bytes of remembered state. No wall
// clock, no frame counting, no randomness: the same speeds through the same settings give the same
// gaits, which is what an offline render and a scrub need (ADR-091).

#include "entity/locomotion.hpp"

#include <cstdint>

namespace avgen::entity {

// How this particular body moves. Authored per entity in the scene file (addendum §26/§27) and
// never in code: a deer, a robot and a person cross from walking to running at different speeds,
// and the numbers are the only thing that differs between them.
struct GaitSettings {
    float walkSpeed = 1.6f;  // metres per second the walk clip was authored at
    float runSpeed = 4.0f;   // metres per second the run clip was authored at
    // The hysteresis bands. `enter` is crossed going up, `exit` coming down, and `enter > exit`
    // is the whole point: equal thresholds are the flicker.
    float moveEnter = 0.15f; // standing becomes travelling above this
    float moveExit = 0.05f;  // travelling becomes standing below this
    float runEnter = 3.0f;   // walking becomes running above this
    float runExit = 2.2f;    // running becomes walking below this
    float turnEnter = 0.35f; // standing becomes turning-in-place above this |rad/s|
    // Seconds a gait must hold before it may change again. Bounds the switch rate no matter what
    // the speed does, which the speed band alone cannot: a body accelerating hard crosses a 0.8
    // m/s band in a fifth of a second.
    float minDwell = 0.25f;
    float accel = 6.0f;      // m/s^2 the body may gain speed at
    float decel = 8.0f;      // m/s^2 it may lose it at
    float blend = 0.2f;      // cross-fade seconds between gaits; < 0 = leave it to the clip
    // Playback-rate matching: a walk clip authored at 1.6 m/s played at 2.0 m/s slides its feet
    // unless the clip runs 1.25x. Clamped, because a clip at 3x is a cartoon.
    bool matchRate = false;
    float rateMin = 0.6f;
    float rateMax = 1.6f;

    // So a writer can tell "the author set nothing" from "the author set the defaults" and emit
    // nothing in the first case.
    friend bool operator==(const GaitSettings&, const GaitSettings&) = default;
};

[[nodiscard]] bool gaitLocomotor(Activity activity);

// The state machine's three bytes. Owned by the entity, reset on a seek with everything else.
class Gait {
public:
    // The activity to *play*, given what the behaviour or action layer proposed, how fast the body
    // is actually going, and how fast it is turning. A proposal the gait has no opinion about
    // (Observe, React -- a character attending to something or flinching) passes straight through
    // and does not disturb the remembered gait, so returning to travel returns to the gait it left.
    [[nodiscard]] Activity select(const GaitSettings& settings, Activity proposed, float speed,
                                  float turnRate, double dt);

    // Clip seconds per timeline second for `activity` at `speed`. 1 when the settings do not ask
    // for rate matching, or when the gait has no authored speed to match against.
    [[nodiscard]] static float playbackRate(const GaitSettings& settings, Activity activity, float speed);

    // The speed the body is allowed to have after `dt` seconds of wanting `desired`. Separate
    // limits up and down because stopping is not the reverse of starting.
    [[nodiscard]] static float approach(float current, float desired, float accel, float decel, double dt);

    void reset();
    [[nodiscard]] Activity current() const { return gait_; }
    // Seconds the current gait has held. Zero on the frame it changed.
    [[nodiscard]] double dwell() const { return dwell_; }
    // How many times the gait has changed since the last reset. The flicker test reads this.
    [[nodiscard]] std::uint32_t changes() const { return changes_; }

private:
    Activity gait_ = Activity::Idle;
    double dwell_ = 0.0;
    std::uint32_t changes_ = 0;
    bool moving_ = false;
    bool running_ = false;
};

} // namespace avgen::entity
