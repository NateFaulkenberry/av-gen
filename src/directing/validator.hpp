#pragma once

// The Director's semantic validator (spec §17-§18, ADR-756).
//
// JSON Schema can say a plan is well formed; only the scene can say it is possible. This checks a
// parsed plan against `SceneFacts` and reports, per item: references (who and what), capability
// (can Rook do that), spatial (can he clear it), timing (does it collide), camera (will it own the
// frame), determinism (will it render the same twice), and what this build cannot compile yet.
//
// **It decides what can be built, item by item** (spec §34 step 8, "construct the feasible
// portion"). Every error names the item it concerns; that item is blocked, and so is anything that
// depends on it -- a cue on "rook.backflip_peak" cannot happen when the backflip cannot. Everything
// else is still compiled, and the diff says what was left out and why. Nothing is ever substituted:
// a blocked backflip is reported, never replaced by a jump (spec §1.5); the model may propose that
// as a revision, which the person then sees.

#include "directing/issue.hpp"
#include "directing/plan.hpp"
#include "directing/resolver.hpp"
#include "directing/scene_facts.hpp"

#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_kind.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace avgen::directing {

// What the compiler can do with a camera move on a subject of a given kind. Shared by the validator
// (to report) and the compiler (to act), so the two cannot disagree.
enum class CameraSupport : std::uint8_t {
    Framing,     // a `seq::ShotCamera` move framing a place (a hero, a node): baked, editable
    FollowRig,   // a `scene::CameraRig` following a node (a character): evaluated every frame
    Modifier,    // changes how another move is shot (low_angle), compiles nothing of its own
    Unsupported, // not compilable yet
};
[[nodiscard]] CameraSupport cameraSupport(CameraMove move, SubjectKind subject);

// The semantic activity a performance action needs from the character's card ("run_to" -> "run").
// An action no mapping knows needs an activity of its own name ("backflip" -> "backflip").
[[nodiscard]] std::string activityFor(std::string_view action);
// Whether an action goes over its target (and so must clear it).
[[nodiscard]] bool actionClearsTarget(std::string_view action);

// A cue's effect reference, resolved against the staged effect list (ADR-702): the owner it names,
// the type, and the instance -- empty when the owner has none of that type yet (a window cue then
// makes one from the type's factory). `issue` is set when it cannot be resolved.
struct ResolvedEffect {
    world::EffectOwner owner;
    world::EffectKind kind{};
    std::string id;
    std::optional<Issue> issue;
};
[[nodiscard]] ResolvedEffect resolveEffect(const EffectRef& ref, const Plan& plan, const SceneFacts& facts,
                                           std::string_view location = {});

struct Validation {
    std::vector<Issue> issues;
    std::set<std::string> blocked; // item keys that will not be compiled
    PlanTimes times;               // every time in the plan, placed
    [[nodiscard]] bool isBlocked(const std::string& key) const { return blocked.contains(key); }
    [[nodiscard]] bool hasErrors() const { return directing::hasErrors(issues); }
};

// Resolves the plan's subjects in place, places its times, and checks everything above.
[[nodiscard]] Validation validatePlan(Plan& plan, const SceneFacts& facts);

} // namespace avgen::directing
