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
// **The rate below which a clip reads as stopped rather than as slow** (ADR-622).
//
// One number, in one place, because two things need to agree about it and did not: `rateMin` is
// the floor a rate-matched clip is clamped to, and `test_farm_locomotion`'s "frozen while moving"
// detector had its own literal for the same idea. The farm pack authors `rateMin: 0.005`, which is
// **four times below** what that detector calls frozen -- so a body could be officially playing
// its clip and officially frozen at once, and neither number knew about the other.
//
// Derived from what a viewer can see rather than picked: at 60 fps a rate of 0.02 advances a clip
// by 1.2 frames of clip time per second, which is the slowest advance that still reads as motion
// rather than as a held pose. Below it, a clip is a still that happens to be changing.
//
// **Anything that needs to ask "is this clip advancing enough to see" reads this**, so the answer
// cannot diverge again.
inline constexpr float kVisibleClipRate = 0.02f;

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
    // **Whether a scene actually asked for these, as distinct from inheriting them** (ADR-620).
    //
    // The action tier has always applied `approach` with these numbers, so their defaults are
    // load-bearing there and may not be zeroed -- `approach` with a rate of 0 returns the speed
    // unchanged, which would freeze every action-driven body rather than merely unlimit it.
    //
    // The *behaviour* tier never applied them at all, and switching it on for every body costs
    // a scrub: a rate-limited speed is an integrator, so a shallow body's replay grows from one
    // or two steps to about forty. So the behaviour tier honours them **only where they were
    // authored**, which puts the cost on the bodies whose author asked for the ramp and leaves
    // every other body exactly as it was.
    //
    // Set by the parser when either key is present, and settable directly by a caller building a
    // `GaitSettings` in code -- a test that sets `accel` and not this is asking for the old
    // behaviour, which is a legitimate thing to ask for.
    bool accelAuthored = false;
    float blend = 0.2f;      // cross-fade seconds between gaits; < 0 = leave it to the clip
    // Playback-rate matching: a walk clip authored at 1.6 m/s played at 2.0 m/s slides its feet
    // unless the clip runs 1.25x. Clamped, because a clip at 3x is a cartoon.
    bool matchRate = false;
    float rateMin = 0.6f;
    float rateMax = 1.6f;
    // What a non-locomotion activity plays at, when `matchRate` is on (ADR-213).
    //
    // 1 is right for an asset with a real idle: the clip is already the pose of a standing body and
    // it should run at its authored speed. It is wrong for an asset that has *only* a walk cycle,
    // where `clips` necessarily maps idle onto it -- the body then stands still playing a walk at
    // full rate, which is the whole of what "the animals are sliding" turned out to mean. The nine
    // farm animals ship exactly one clip, called `Walk`, and every activity resolves to it.
    //
    // 0 freezes the clip while the body is not travelling. A statue caught mid-stride is not a good
    // idle, and it is enormously better than feet running on the spot; an asset that wants better
    // needs an idle clip, which is an asset question rather than an engine one.
    //
    // **LABELLED COMPENSATION (ADR-622): a rate floor below `kVisibleClipRate` is standing in for
    // a missing animation.** The farm pack authors `rateMin: 0.005` and `idleRate: 0`, and both
    // exist because these nine assets ship one clip called `Walk` and no idle. The floor is not a
    // tuning preference; it is the smallest crawl that keeps a walk cycle from looking like a held
    // pose on an asset that has nothing else to play. **The actual gap is the missing idle clip**,
    // which is content work. Recording it here so the number is not read as a considered choice
    // about playback and quietly "corrected" by someone who does not know what it is covering for.
    float idleRate = 1.0f;

    // ---- how this body turns (ADR-908) ---------------------------------------------------------
    //
    // Degrees per second: the fastest this body turns, and the rate it pivots at from rest. **0 is
    // "not authored"** and keeps each action verb's long-standing default -- 2.45 rad/s (140 deg/s)
    // for a `move` and 2.5 rad/s for a `face` -- so every body that says nothing turns exactly as it
    // did. Degrees in memory as well as in the file, because every other `turnRate` an author writes
    // in this vocabulary is degrees and a value that round-tripped through radians would not come
    // back bit for bit.
    float turnRate = 0.0f;
    // Metres: the circle this body turns on while it keeps walking. **0 is the old mover**, which
    // turns toward its heading at `turnRate` and travels at the cosine of what is left -- so any turn
    // over 90 degrees is a dead stop and a pivot on the spot, the pattern the GV3 audit traced to this
    // one line. Above 0 a moving body turns no faster than speed / radius, keeps at least
    // `kTurnKeep` of its pace through the turn, and pivots only from rest (`turnCap`, `turnPace`).
    float turnRadius = 0.0f;
    // Metres from the pivot to the feet that step round it, for a body turning on the spot with no
    // clip of its own to turn with (ADR-908, see `playbackRate`). A body turning at w rad/s moves its
    // feet at w * pivotRadius, and its locomotion cycle plays at that speed over `walkSpeed`. 1 m is
    // about half a farm animal at the Glowmere cast scale; a scene can say what its bodies are.
    float pivotRadius = 1.0f;

    // So a writer can tell "the author set nothing" from "the author set the defaults" and emit
    // nothing in the first case.
    friend bool operator==(const GaitSettings&, const GaitSettings&) = default;
};

// ---- walk-through turns (ADR-908) -----------------------------------------------------------------
//
// The rule every mover that honours a turn radius shares. It is three questions, and it is written
// once so a wandering animal and an alien on an errand cannot turn by two different rules. A mover
// asks them in this order each step: the pace first, from the heading error it has, then the turn,
// capped by the pace it had and the pace it now has.
//
//   * **how much of its pace does it keep** (`turnPace`): at rest, the cosine of what is left to
//     turn -- so from rest it turns in place until the way is within 90 degrees and then walks out
//     of the turn. Moving, never less than `kTurnKeep` of it: it slows into a sharp turn and never
//     stops for one. That floor is the whole difference from the old mover, whose pace was the
//     cosine everywhere and which therefore stopped dead for every turn over 90 degrees.
//   * **can it get there at all** (`insideTurn`): a point `d` metres away and `e` radians off the
//     heading lies inside the circle the body turns on when d < 2 R |sin e|. Walking on, the body
//     would orbit it for ever. The caller decides what that means: a wander's destination was only
//     ever somewhere to walk toward, and it counts as reached; an errand's target is the point of
//     the errand, and the mover brakes to rest and pivots once.
//   * **how fast may it turn** (`turnCap`): a body that will be at rest this step may pivot, at
//     `rate`. One that will be moving turns no faster than the slower of its two paces over the
//     radius -- which is what a circle of that radius at that pace *is* -- so it traces a turn at
//     least `radius` wide whether it is speeding up or slowing down, and a body that wants to turn
//     tighter has to come to rest first. A body leaving rest toward a way within 90 degrees is
//     already moving on the step it leaves, so it walks out on the circle rather than pivoting a
//     sliver first.
//
// With `radius` 0 every answer is the old mover's: `rate`, the cosine, never inside.
struct TurnSettings {
    float rate = 0.0f;   // rad/s
    float radius = 0.0f; // metres; 0 = pivot and go
};
// m/s below which a body is at rest for the purpose of turning: it may pivot.
inline constexpr float kTurnRestSpeed = 0.05f;
// The least fraction of its pace a moving body keeps through a turn.
inline constexpr float kTurnKeep = 0.5f;
[[nodiscard]] float turnPace(const TurnSettings& turning, float pace, float error);
[[nodiscard]] bool insideTurn(const TurnSettings& turning, float pace, float distance, float error);
// `before` is the pace the body had coming into the step, `after` the pace it has leaving it.
[[nodiscard]] float turnCap(const TurnSettings& turning, float before, float after);

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
    // for rate matching, or when the gait has no authored speed to match against. `turnRate` (rad/s,
    // signed) is read only for a body turning on the spot with no idle clip to turn with (ADR-908).
    [[nodiscard]] static float playbackRate(const GaitSettings& settings, Activity activity, float speed,
                                            float turnRate = 0.0f);

    // How far the feet are from the ground they are crossing, as a ratio: 1 means the clip is being
    // played at exactly the speed it was authored for, 3 means the body is covering three metres for
    // every metre of stride and the character is moonwalking.
    //
    // This exists because the mismatch is invisible in every number the engine already prints. A
    // scene authors a travel speed on a *behaviour* and a stride speed on a *gait*, they are set by
    // different people at different times, and nothing compares them -- Glowmere's wanderer explores
    // at 5 m/s against a walk clip authored for 1.6, and the only symptom is that the animation
    // looks wrong in a way nobody can name. Returns 1 when there is nothing to compare, so a silent
    // answer means "no opinion" rather than "fine".
    //
    // Note that this cannot be derived from the clip. The three clips this engine ships with are
    // Mixamo in-place takes whose root returns exactly where it started -- net xz displacement
    // measured at 0.0000 over all three -- so the speed a walk cycle *means* is not in the file and
    // has to be authored. That is why `GaitSettings` carries it, and why it has to be checked.
    [[nodiscard]] static float footSlip(const GaitSettings& settings, Activity activity, float speed);

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
