#pragma once

// Character intent (Phase D §2.5, §3, §15): what a behaviour wants, in the form the motion system
// can actually use.
//
// **The gap this closes, stated as a measurement rather than a preference.** Every behaviour in
// this engine expresses movement as `EntityState::speed` and `EntityState::yaw` -- a scalar along a
// heading. That polar pair can describe a body walking where it looks and nothing else. ADR-545
// already had to add a *measured* velocity vector because the seam could not otherwise tell a
// strafe from a walk; this is the same correction applied to the **intent** side, which is what
// §15 means by "character movement must become vector-based".
//
// **What it is not.** It is not a second decision layer. `entity::decision` already selects
// behaviours by utility with dwell and margin (ADR-269/333), `IPerception` already filters what a
// character knows (ADR-270/290), `NavGrid` already routes. Phase D's audit found those built; this
// is the one seam between them that was still carrying less than it needed to.
//
// **R3 (ADR-210): intent, never a transform.** Nothing here moves a body. It says what the body
// wants; `stepMotion` decides what it may have, and the mover integrates it.
//
// **R4 (ADR-096): no layer names a clip.** There is no animation name in this struct and there
// must never be one. A behaviour says "go there, at this pace, facing that" and the motion system
// decides what plays.

#include <cstdint>
#include <string_view>

#include <glm/glm.hpp>

namespace avgen::entity {

// What the character is trying to do. Coarse on purpose: this is the vocabulary a *camera*, a
// *debug view* and a *report* share, so it is the level at which "why is this character doing
// that" is answerable (§41). The fine detail is in the fields below and in the behaviour itself.
enum class IntentType : std::uint8_t {
    Idle,
    Wander,
    MoveTo,
    Follow,
    Investigate,
    Observe,
    Flee,
    Avoid,
    Interact,
    // Phase D §3/§24/§67. Added when the first producer needed them, not before.
    Socialize,  // approach / greet / idle near another character
    ReturnTo,   // go back to a designated region (a post, a home)
    React,      // respond to a world event
    Custom,
};
[[nodiscard]] const char* intentTypeName(IntentType type);
// The inverse, for a scene file that names an option's intent. False for an unknown name.
[[nodiscard]] bool intentTypeFromName(std::string_view name, IntentType& out);

// **Who writes it.** Three producers now; ADR-615 recorded this as dark with none.
//
//   * the **HOW** -- `desiredVelocity`, `facing`, `valid` -- is written by the mover that knows it:
//     `ActionKind::Move` and `ActionKind::Face` in `action.cpp`, every step they run (Phase D);
//   * the **WHAT** -- `type`, `targetPosition`, `urgency`, `stoppingDistance` -- is written by the
//     `decide` behaviour from the option it has committed to (Phase D);
//   * the **`motionScript` behaviour** writes an authored one (Phase C §65, with ADR-623).
//
// Find any others with `grep -rn "intent.valid = true" src`. Cleared at the top of every entity
// step on both paths (`update` and `seek`), so a step nobody published one in falls back to the
// polar `speed`/`yaw` reconstruction underneath it. `Entity::advanceMotion` reads it when
// `proceduralMotion` is on; with it off -- every shipping scene -- nothing downstream of the seam
// reads the vector half, which is why producing it changed no rendered frame.
struct CharacterIntent {
    IntentType type = IntentType::Idle;

    // **World space, metres per second.** The whole point: magnitude is pace and direction is
    // heading, so a body circling a target while watching it is expressible and a polar pair
    // cannot express it.
    glm::vec3 desiredVelocity{0.0f};

    // Where the body should face, independently of where it is going. Separate because those are
    // different questions and the engine has already paid once for conflating them (ADR-545).
    glm::vec3 facing{0.0f, 0.0f, 1.0f};
    bool hasFacing = false;

    // What this intent is about, when it is about something. A position rather than a handle at
    // this seam: the motion tier must not be able to follow a reference into the world, which is
    // the same line ADR-300 draws one tier further down.
    glm::vec3 targetPosition{0.0f};
    bool hasTarget = false;
    float stoppingDistance = 0.0f;

    // §14/§39. An avoidance correction supplied from outside -- Phase D's steering, executed by
    // Phase B. **Added to `desiredVelocity`, never blended**: whoever computed it has already
    // decided how hard to avoid, and a second weight here would be a second opinion.
    glm::vec3 steering{0.0f};

    // 0..1. How much this matters, for the acceleration limits to read: a character fleeing may
    // spend more of its budget than one wandering. Not a priority -- the decision layer already
    // owns priority (ADR-269) and this is downstream of that choice being made.
    float urgency = 0.0f;

    // **False means "this frame published no vector intent", not "stand still".** Every behaviour
    // that exists writes the polar pair and nothing else, so the default has to be the one that
    // leaves them working: `Entity::advanceMotion` reconstructs a velocity from `speed` and `yaw`
    // when this is false. A default of "valid, zero" would stop the entire cast dead.
    bool valid = false;

    void clear() { *this = CharacterIntent{}; }
};

} // namespace avgen::entity
