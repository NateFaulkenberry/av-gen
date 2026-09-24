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
// No clip cues are written. The performance's speed reaches the gait exactly (`exactSpeed`), so the
// walk, run and idle clips follow the path by construction; clip cues are for one-shots (Slice 3).

#include "directing/issue.hpp"
#include "directing/plan.hpp"
#include "directing/resolver.hpp"
#include "directing/scene_facts.hpp"
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

struct CompiledPerformance {
    seq::Actor actor;
    double from = 0.0;
    double to = 0.0;
    std::vector<std::pair<std::string, double>> events; // (name, time) for every `emits`
    std::vector<std::string> summary;                   // one line per beat, for the diff
};

// Requires a validated, unblocked scripted performance whose beats are all compilable.
[[nodiscard]] CompiledPerformance compilePerformance(const Plan& plan, std::size_t performance,
                                                     const SceneFacts& facts, const PlanTimes& times);

} // namespace avgen::directing
