#pragma once

// Generate-and-select for procedural objects: parameter space -> many candidates -> measurement ->
// ranking -> a deterministic winner.
//
// PROVISIONAL. The Glowmere Valley 2 work has the structurally identical problem for procedural
// hero mushrooms, and the user has asked for one framework rather than two that drift. That agent
// is publishing the canonical interface; this is a minimal implementation written to the shape
// agreed in advance so the tree work is not blocked, and it is expected to be reconciled with
// theirs on merge. The parts below the line are deliberately the scene-agnostic ones: nothing here
// knows what a tree is.
//
// THE ONE RULE THAT IS NOT NEGOTIABLE: NO SCORE COMPONENT MAY BE MONOTONE.
//
// A ranking stage is an optimiser over whatever it measures. Give it "more branches is better" and
// it will return the densest mess the parameter space can produce, having maximised exactly what it
// was asked to. This is the failure the brief names in section 36, and clamping the result
// afterwards does not fix it -- the optimiser still climbs to the clamp and sits there, and every
// candidate at the ceiling scores identically so the ranking stops discriminating.
//
// The answer is that a component is a *band*, not a function that increases: a trapezoid with a
// lower bound, an ideal interval, and an upper bound. "Too little" and "too much" both lose points.
// `MetricBand::validate` refuses an unbounded band, so the rule is enforced at construction rather
// than left as a convention.

#include "core/error.hpp"

// The full header, not the forward declaration the rest of this codebase prefers: `CandidateRecord`
// holds a `json` by value, and a by-value member of a class template needs the definition. Holding
// it behind a pointer to keep the forward declaration would trade a compile-time cost this offline
// path does not care about for an indirection every caller would.
#include <nlohmann/json.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::scene {

// A trapezoidal acceptance band. Full marks inside [idealLow, idealHigh], falling linearly to
// `floorScore` at `min` and `max`, and `floorScore` outside them.
struct MetricBand {
    float min = 0.0f;
    float idealLow = 0.0f;
    float idealHigh = 1.0f;
    float max = 1.0f;
    // What a candidate outside the band scores. Not always zero: a component that hard-zeroes makes
    // the whole weighted sum insensitive to everything else for any candidate that fails it, which
    // turns one component into a filter. A small floor keeps the ranking informative.
    float floorScore = 0.05f;

    [[nodiscard]] float score(float value) const;
    [[nodiscard]] Result<void> validate(std::string_view owner) const;
};

struct ScoreComponent {
    std::string name;
    // Why this number is believed to correlate with the look being aimed at. Carried in the struct
    // rather than in a comment because it is the first thing anyone tuning the evaluator needs, and
    // the contact sheet prints it.
    std::string rationale;
    float weight = 1.0f;
    MetricBand band{};
};

struct ScoredMetric {
    std::string name;
    float value = 0.0f; // what was measured
    float score = 0.0f; // what the band made of it, 0..1
    float weight = 0.0f;
};

struct CandidateRecord {
    std::uint32_t index = 0;
    int generation = 0;
    float score = 0.0f;
    std::vector<ScoredMetric> metrics;
    // The coordinates diversity is judged in. Not the parameters: two very different parameter sets
    // can produce the same-looking object, and it is the looking that must not repeat.
    std::vector<float> features;
    // Everything needed to reproduce this candidate exactly. This is the SAME representation the
    // scene file authors and the editor edits -- not a parallel export format -- so a winning
    // candidate is adopted by pasting it into the scene, and an artist's later edit is expressible
    // as a candidate.
    nlohmann::json parameters;
    double generateMs = 0.0;
    double evaluateMs = 0.0;
    [[nodiscard]] const ScoredMetric* metric(std::string_view name) const;
};

// What the search is searching. A tree, a mushroom, anything with a continuous parameter space and
// measurable structure. The subject owns its parameters, its metrics and its geometry; the search
// owns sampling, scoring, ranking and diversity, and knows nothing about any of them.
class CandidateSubject {
public:
    virtual ~CandidateSubject() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual const std::vector<ScoreComponent>& components() const = 0;
    // How many low-discrepancy coordinates `sample` and `perturb` consume.
    [[nodiscard]] virtual int dimensions() const = 0;
    // Map a point of the unit hypercube into the human-defined design space (brief section 34). The
    // subject decides what trees are allowed to exist; the search only decides where to look.
    [[nodiscard]] virtual nlohmann::json sample(std::span<const float> u) const = 0;
    // A neighbour of `base`. `scale` is the step, 0 to 1 in design-space fractions.
    [[nodiscard]] virtual nlohmann::json perturb(const nlohmann::json& base, std::span<const float> u,
                                                 float scale) const = 0;
    // Build and measure. Fills `out.metrics` with raw values (score/weight are filled by the
    // search) and `out.features`. Returning an error drops the candidate rather than failing the
    // search: a parameter set that cannot be built is a legitimate answer for a region of the space.
    [[nodiscard]] virtual Result<void> measure(const nlohmann::json& parameters, CandidateRecord& out) const = 0;
};

struct SearchSettings {
    int population = 48;    // candidates per generation
    int generations = 3;    // 1 = a plain sweep with no refinement
    int elite = 8;          // how many survivors seed the next generation
    float perturbScale = 0.30f;
    // Feature-space distance below which two candidates count as the same object. 0 disables the
    // filter. Without it the refinement stage converges the whole population onto one local optimum
    // and the contact sheet shows the same tree forty-eight times.
    float diversityRadius = 0.18f;
    std::uint32_t seed = 1;
};

struct SearchResult {
    std::vector<CandidateRecord> ranked; // best first, diversity-filtered
    std::vector<CandidateRecord> all;    // every candidate that built, in generation order
    int rejected = 0;                    // parameter sets that failed to build
    double totalMs = 0.0;
    [[nodiscard]] nlohmann::json report() const;
};

[[nodiscard]] Result<SearchResult> searchCandidates(const CandidateSubject& subject, const SearchSettings& settings);

// Applies the subject's bands to a record's raw metric values and returns the weighted score. Split
// out so a caller can rescore an existing population after retuning the bands, which is the loop
// anyone tuning an aesthetic evaluator actually runs.
float scoreCandidate(const CandidateSubject& subject, CandidateRecord& record);

// Greedy diversity filter over an already-ranked list.
[[nodiscard]] std::vector<CandidateRecord> selectDiverse(std::span<const CandidateRecord> ranked, float radius,
                                                         int limit);

// The scrambled Halton point used for sampling. Exposed because a caller reproducing one candidate
// needs the same coordinates the search used.
[[nodiscard]] std::vector<float> haltonPoint(std::uint32_t index, int dimensions, std::uint32_t seed);

} // namespace avgen::scene
