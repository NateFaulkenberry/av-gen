#pragma once

// A scripted performance, compiled to a `seq::Actor` (spec §23-§26, ADR-759).
//
// The actor IS the performance (ADR-758): on an entity's node it takes the body for its span and
// hands it back where it ends. So compiling a performance is computing keys, and every number comes
// from the engine, never from the model:
//
//   * the start MARK is placed from authored facts only -- the character's authored anchor and the
//     target's place -- on the line from the anchor toward the first target, far enough back for the
//     lead-in; never from where the simulation has the character (ADR-758);
//   * speeds are the card's: 95% of the run speed for a run, the walk speed for a walk;
//   * times are distance over speed, placed on the plan's clock from the performance's start;
//   * events a beat `emits` get the time the beat reaches it -- arrival, closest approach, the end.
//
// Ground beats write no clip cues: the performance's speed reaches the gait exactly, so the walk,
// run and idle clips follow the path by construction. One-shots do (Slice 3, on the Motion lead's
// M2-M4): a `jump` flies the engine's one arc (`entity::planJump`, ADR-822) as keys with an airborne
// span, its clip cued `once` and fitted to the arc by its measured takeoff and touchdown
// (`seq::jumpClipCue`); a `land` plays the landing clip once and holds for the character's recovery.

#include "directing/issue.hpp"
#include "directing/plan.hpp"
#include "directing/resolver.hpp"
#include "directing/scene_facts.hpp"
#include "entity/airborne.hpp"
#include "seq/sequence.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace avgen::directing {

// Whether this build compiles an action (Slice 2's set: run_to, walk_to, run_past, walk_past, run,
// walk, hold, look_at). A capability the character has but this does not list is UNSUPPORTED, not
// silently skipped: jump, land and the acrobatic set are Slice 3.
[[nodiscard]] bool beatCompilable(std::string_view action);

// When a performance starts: its first beat's `at`, else the start of the plan's first shot about the
// same subject. Nothing when neither exists.
[[nodiscard]] std::optional<double> performanceStart(const Plan& plan, std::size_t performance, const PlanTimes& times);

// A `jump` beat, planned: the engine's arc from where the body is to where it lands, and what the
// geometry says about it. Computed by `compilePerformance` for every jump beat, and read by the
// validator -- so what is checked is exactly what would be compiled, not a second estimate.
struct JumpOutcome {
    std::size_t beat = 0;
    std::string obstacle;          // the place jumped over; empty for a hop
    float requiredHeight = 0.0f;   // obstacle height + clearance, above its base (over a target)
    float minimumApex = 0.0f;      // the lowest apex above take-off that clears it on this path; 0 = none needed
    float apex = 0.0f;             // the apex flown: max(the character's own, minimumApex)
    float apexLimit = 0.0f;        // the highest the character can leap (ADR-822 `maxApex`)
    float distance = 0.0f;         // take-off to landing, horizontal
    float maxDistance = 0.0f;
    bool groundKnown = false;      // false: the scene has no terrain, so nothing could be checked
    entity::ArcCheck ground;       // the terrain's verdict on the arc
    std::optional<entity::JumpArc> arc; // null: no arc exists (no distance, or an apex below the landing)
    [[nodiscard]] bool feasible() const {
        return arc.has_value() && groundKnown && apex <= apexLimit + 1e-4f && distance <= maxDistance + 1e-4f &&
               ground.clear && std::abs(ground.landingError) < 0.05f;
    }
};

struct CompiledPerformance {
    seq::Actor actor;
    double from = 0.0;
    double to = 0.0;
    std::vector<std::pair<std::string, double>> events; // (name, time) for every `emits`
    std::vector<std::string> summary;                   // one line per beat, for the diff
    std::vector<JumpOutcome> jumps;                     // one per `jump` beat, feasible or not
};

// What moment of a beat its `emits` names, when the beat has several: a jump's `takeoff`, `peak`
// (the default) or `touchdown`. Other beats have one moment and ignore it.
inline constexpr std::string_view kJumpMoments[] = {"takeoff", "peak", "touchdown"};

// Metres above an obstacle a jump over it must pass, when the plan does not say.
inline constexpr float kDefaultJumpClearance = 0.25f;

// For a validated, unblocked scripted performance whose beats are all compilable, the actor. Also
// safe on one the validator is still checking (it probes jumps this way): a beat it cannot place --
// an unknown action, a missing target -- moves nothing.
[[nodiscard]] CompiledPerformance compilePerformance(const Plan& plan, std::size_t performance,
                                                     const SceneFacts& facts, const PlanTimes& times);

} // namespace avgen::directing
