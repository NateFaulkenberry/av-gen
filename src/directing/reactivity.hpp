#pragma once

// ADR-924 and ADR-926: a plan's routes and sources, validated and compiled.
//
// **Validation (ADR-926).** Every route is put through the liveness registry (ADR-902) with the
// scene's facts -- the same rules the bind, the project load and `--audit-routes` apply -- and a
// route a rule shows cannot reach the picture is refused (DEAD_TARGET): the item is blocked and the
// diff says why. What reaches the picture in a way nobody meant is flagged: a phase rate
// (PHASE_RATE_TRAP), an event through a chain that swallows it, a one-frame event or an unsmoothed
// level used as a depth, a hue driven by the audio (a colour keyed by section reads as intent, one
// driven by the audio reads as noise), a multiply whose rest output darkens its target between
// hits (ROUTE_HAZARD). And the plan as a whole is read for the failure the owner named -- "everything
// pulses to the beat":
//
//   * ONE_SOURCE     three or more routes, and 60% or more of them follow one signal;
//   * ONE_PHASE      three or more routes that share a source reach their targets at one instant
//                    (the same delay plus attack), or every route of the plan does;
//   * OVER_SATURATED one entity (a hero, a layer, a particle system) answers more than three signals
//                    -- it cannot read as answering any one -- or the routes stacked on one target can
//                    push it past its safe range (the reactive catalogue's, ADR-925).
//
// A plan source must be pure in time (LFO, noise, timeline), so a seek replays every route that reads
// it (ADR-901); an envelope or a random source is refused (NON_DETERMINISTIC). A route that reads a
// blocked source is blocked with it.
//
// **Compilation (ADR-924).** A route becomes a `params::ModRoute` appended to the staged route list,
// stamped `planItem = "<planId>/<key>"`; a source becomes an entry of the staged rack. Both are
// recorded in `produced` (domains `modulation.route` and `modulation.source`), fingerprinted like
// every other piece of content a plan makes, so a revision replaces them and never overwrites one a
// person has edited since.

#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "directing/scene_facts.hpp"
#include "directing/validator.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace avgen::directing {

// The flags' thresholds, named so a test and a panel can say them.
inline constexpr std::size_t kOneSourceMinimumRoutes = 3;
inline constexpr float kOneSourceShare = 0.6f;
inline constexpr std::size_t kOnePhaseMinimumRoutes = 3;
inline constexpr std::size_t kSaturatedSources = 3; // more distinct signals than this on one entity

// Checks the plan's routes and sources and adds what it finds to `v` (blocking refused items).
// Called by `validatePlan` after the subjects and times are resolved.
void validateReactivity(Plan& plan, const SceneFacts& facts, Validation& v);

// What a route does to a target resting at `base`: the lowest and highest value it can write, over
// every value its source can take and every depth it can be given. `sourceMin`/`sourceMax` are the
// source's declared range; an event source's rest is its minimum.
struct Excursion {
    float low = 0.0f;
    float high = 0.0f;
    float rest = 0.0f; // what it writes with its source at rest (at the source's minimum)
};
[[nodiscard]] Excursion routeExcursion(const params::ModRoute& route, float base, float sourceMin, float sourceMax);

// The owners a target belongs to, for saturation: its hero, else its owner in the catalogue; a
// material's every surface. Targets the catalogue does not know are owned by their path's node.
[[nodiscard]] std::vector<std::string> targetOwners(const ReactiveCatalog& catalog, const std::string& path);

// ---- compilation --------------------------------------------------------------------------------

struct ReactivityCompileSink {
    std::function<void(const std::string& item, ContentDomain domain, const std::string& id)> record;
    std::function<void(char sign, const std::string& item, std::string text)> line;
    std::function<char(const std::string& item)> sign; // '+' new, '~' replaces the last revision's
};
void compileReactivity(const Plan& plan, const Validation& v, Staging& staged, const ReactivityCompileSink& sink);

// A staged route's or source's content, for `produced` (nothing when it is gone), and its removal.
[[nodiscard]] std::optional<nlohmann::json> reactivityContent(const ContentRef& ref, const Staging& staged);
bool removeReactivityContent(const ContentRef& ref, Staging& staged);

// One line for the diff and the panel: "audio.onsetLow -> nodes/elder-2-gills/emissiveBoost: add 0.60,
// delay 0 ms, attack 5 ms, decay 300 ms, depth section.energy 0.30..1.08".
[[nodiscard]] std::string describeRoute(const params::ModRoute& route);

} // namespace avgen::directing
