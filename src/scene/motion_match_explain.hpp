#pragma once

// Phase C §68 (debug visualization) and §69 (why did it choose this motion?).
//
// §69 names eight places a bad choice can come from -- features, weights, filtering, trajectory,
// database coverage, continuity, contact state, search approximation -- and asks that an engineer
// be able to tell which. A cost breakdown of the winner cannot do that alone, and
// `MotionCostBreakdown::caveat` says why: a term can be decisive by making ANOTHER candidate
// expensive and read zero on the winner. So an explanation here is **a comparison, not a
// breakdown**: the winner beside the candidates it beat that matter --
//
//   * the continuation (what carrying on would have cost -- continuity and hysteresis),
//   * the best candidate from another clip (the real runner-up -- features and weights),
//   * the best candidate the tag filter removed (filtering),
//   * and, for a staged plan, the exhaustive answer (search approximation) --
//
// each with the same per-term breakdown, and the per-term DIFFERENCE is what decided it.
//
// **Recomputed, not recorded.** `explainMotionMatch` re-runs the search and scores the comparison
// candidates itself, so nothing on the matching path pays for diagnostics and a replay can explain
// any frame after the fact (ADR-360). The cost it computes is checked against the search's own
// (`MotionMatch::cost`) by test, so the explanation cannot drift from the decision it explains.

#include "scene/motion_database.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace avgen::scene {

// The full cost of one candidate under one query, term by term -- the arithmetic `searchMotion`
// applies, without its early out. `total` is summed in the search's own order, so for the sample a
// search chose it equals `MotionMatch::cost` exactly.
struct MotionCandidateCost {
    std::uint32_t sample = MotionDatabase::kInvalid;
    MotionCostBreakdown breakdown;
    float total = 0.0f;
    bool passesFilter = true;
    [[nodiscard]] bool valid() const { return sample != MotionDatabase::kInvalid; }
    // Everything but continuity, transition and style: how well the sample fits the query.
    [[nodiscard]] float featureCost() const { return breakdown.total() - breakdown.continuity - breakdown.transition - breakdown.style; }
};

[[nodiscard]] MotionCandidateCost motionCandidateCost(const MotionDatabase& db, const MotionQuery& query,
                                                      const MotionCostWeights& weights,
                                                      std::uint32_t sample);

struct MotionExplainOptions {
    // When set, the explanation also answers "would the provider have held the continuation":
    // §28's margin, as a fraction of `stats.costSpread`, exactly as `MatchSettings::switchMargin`.
    std::optional<float> switchMargin;
    // When set, the search being explained is this staged plan, and the exhaustive answer is
    // computed beside it so a difference is attributed to the approximation.
    std::optional<MotionSearchPlan> plan;
    // A decision within this fraction of `stats.costSpread` of its runner-up is reported as fragile.
    float nearTie = 0.05f;
    // A winner whose feature cost exceeds this fraction of `stats.costSpread` is reported as a
    // coverage suspect: even the best the database has is far from what was asked.
    float farMatch = 0.5f;
};

// What §69 asks be separable, as the category each finding belongs to.
enum class MatchCause : std::uint8_t {
    Features,
    Weights,
    Filtering,
    Trajectory,
    Coverage,
    Continuity,
    Contact,
    Approximation,
};
[[nodiscard]] const char* matchCauseName(MatchCause cause);

struct MatchFinding {
    MatchCause cause = MatchCause::Features;
    std::string text;
};

struct MotionMatchExplanation {
    std::uint32_t current = MotionDatabase::kInvalid;
    MotionCandidateCost selected;      // what the search returned
    MotionCandidateCost continuation;  // the sample after `current`, when there is one
    MotionCandidateCost runnerUp;      // the best candidate from a clip other than the winner's
    MotionCandidateCost bestFiltered;  // the best candidate the tag filter removed
    MotionCandidateCost exhaustive;    // the linear scan's answer, when a staged plan was explained
    std::uint32_t considered = 0;
    std::uint32_t rejected = 0;
    bool heldByMargin = false;         // only meaningful with `switchMargin`
    float costSpread = 0.0f;
    std::vector<MatchFinding> findings;

    // §68's panel as text: the samples by clip and time, the query's and the winner's trajectory,
    // the cost table with a column per compared candidate, phase and contact state, then the
    // findings and the caveat.
    [[nodiscard]] std::string report(const MotionDatabase& db, const MotionQuery& query) const;
    [[nodiscard]] bool has(MatchCause cause) const;
};

[[nodiscard]] MotionMatchExplanation explainMotionMatch(const MotionDatabase& db,
                                                        const MotionQuery& query,
                                                        const MotionCostWeights& weights,
                                                        const MotionExplainOptions& options = {});

// "clipname @ 1.82s (sample 417)", the label §68 uses.
[[nodiscard]] std::string motionSampleLabel(const MotionDatabase& db, std::uint32_t sample);

} // namespace avgen::scene
