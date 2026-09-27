#pragma once

// A plan's set pieces against the scene (ADR-929).
//
// A `PlanSetPiece` is the request -- "an abduction near the lantern on bar 89, two animals, coming in
// from the east" -- and `stage::SetPieceSpec` is what a template instances. This is the one place the
// first becomes the second: the place resolved through the plan's subjects, the named animals through
// theirs, the craft through the scene's staging actors, the time through the plan's placed times, and
// the beam's authored colours read off the parameter bases so a coloured set piece can put them back.
// Shared by the validator (which reports what stops it) and the compiler (which builds it), so the two
// cannot disagree about where a set piece is or when.
//
// It also holds the checks that are about set pieces TOGETHER: one craft in two places, travel the
// craft cannot make in the gap, and two set pieces that would read as the same shot.

#include "directing/issue.hpp"
#include "directing/plan.hpp"
#include "directing/resolver.hpp"
#include "directing/scene_facts.hpp"
#include "stage/setpiece.hpp"

#include <optional>
#include <string>
#include <vector>

namespace avgen::directing {

struct ResolvedSetPiece {
    std::string key;
    std::string plan;                               // the plan it belongs to
    std::optional<stage::SetPieceSpec> spec;        // absent when it cannot be built
    std::optional<stage::SetPieceTimeline> timeline;
    std::optional<float> framingMetres;
};

// Resolves set piece `index` of `plan`, appending every finding about it -- errors and warnings, each
// naming the item -- to `issues`. `times` are the plan's placed times.
[[nodiscard]] ResolvedSetPiece resolveSetPiece(const Plan& plan, std::size_t index, const SceneFacts& facts,
                                               const PlanTimes& times, std::vector<Issue>& issues);

// The set pieces of the project's OTHER plans whose scenarios are still in the scene, resolved against
// the same facts: what this plan's craft timelines and repetition checks must also respect.
[[nodiscard]] std::vector<ResolvedSetPiece> otherPlansSetPieces(const Plan& plan, const SceneFacts& facts);

// The checks across set pieces, appended to `issues`: per craft, in time order, an overlap (with
// `stage::kCraftHandoverSeconds` between one leaving and the next taking it) or a gap the craft could
// not cross at its cruise speed is an error on the later one; two within `kSamePlaceMetres` of each
// other (a flyby only against another flyby: it happens at no place), or two of one template framed
// within `kSameFramingFraction` of the same distance, are a REPETITION warning. `mine` are this plan's;
// `others` only ever make a finding against one of `mine`.
inline constexpr float kSamePlaceMetres = 40.0f;
inline constexpr float kSameFramingFraction = 0.15f;
void checkSetPiecesTogether(const std::vector<ResolvedSetPiece>& mine, const std::vector<ResolvedSetPiece>& others,
                            const SceneFacts& facts, std::vector<Issue>& issues);

// What `director.inspect_capabilities` lists under "setPieces" (the plan schema promises it): every
// template with its moments and every slot -- name, default, range, unit, what a viewer would call it,
// and whether it is a knob the Parameters panel shows or folded into the beats -- and every staging
// actor that could fly one, with whether it has the beam an abduction and a survey need.
[[nodiscard]] nlohmann::json setPieceCatalog(const SceneFacts& facts);

// "setpiece/<key>/<moment>" when that names a moment of one of `plan`'s set pieces: the set piece's
// index; nothing otherwise.
[[nodiscard]] std::optional<std::size_t> setPieceOfEvent(const Plan& plan, std::string_view event);

} // namespace avgen::directing
