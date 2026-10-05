#pragma once

// ADR-1108..1113: the live optimizer's Phase 5 -- perceptual, scene-aware optimization (docs/live-optimizer/
// 03-phase5-plan.md). This header is the LOGIC, with no device in it: the contribution analysis over the profile's
// per-entity rows (1108), the hero classification of every lever (1109), what an ORIGINAL vs OPTIMIZED comparison
// holds and how the Quality Lab's answer is read (1110), the combination search's planning and choosing (1111), the
// Critic's optional answer (1112) and the writers. The GPU side that fills these is `live_optimize_run.cpp`.
//
// The owner's rules, enforced by the types below:
//   * ESTIMATED and MEASURED live in separate fields. A combination's saving summed from measured singles is an
//     estimate until the combination itself has been measured, and is labelled so.
//   * Nothing says two pictures are equivalent. `withinFloor` -- every metric inside the self-difference floor
//     measured on this run -- is the strongest statement, and it is a statement about these frames only.
//   * The Critic is optional: `critic` is empty when it was not asked for or not reachable, and nothing else reads it.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::app {

struct LiveProfileEntity;
struct LiveProfileCandidate;

// ---- ADR-1108: contribution -----------------------------------------------------------------------------------------

struct ScreenBox {
    std::string name;
    std::uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0; // pixels of the output, [x0,x1) x [y0,y1)
};

struct ContributionEmitter {
    std::string name;
    double distance = 0.0;
    double reach = 0.0;      // the emitter's largest extent
    std::string importance;
    float leverWeight = 1.0f;
    bool hero = false;
    bool enabled = true;
    bool beyondCull = false; // what `particlelod` (60 m, weighted by importance) stops
};

struct CasterFloorRow {
    double floorPx = 0.0;
    int casters = 0;              // non-hero casters under the floor: what `shadowCasterMinPixels = floorPx` removes
    int heroCastersProtected = 0; // hero casters that WOULD be under it, kept because a hero is exempt
    double coverage = 0.0;        // the removed casters' summed screen coverage (bounding discs; an over-estimate)
    std::vector<std::string> names; // the first few
};

struct ContributionReport {
    bool available = false;
    int entities = 0, visible = 0, casters = 0, heroEntities = 0, heroesOnScreen = 0;
    std::vector<std::string> topContributors; // "name (coverage %, importance)"
    std::vector<CasterFloorRow> casterFloors; // at 8, 16, 24 (the castercull arm) and 48 px
    int lodCandidates = 0;                    // visible non-hero entities under 0.5% of the frame
    double lodCandidateCoverage = 0.0;
    std::vector<std::string> lodCandidateNames;
    std::vector<ContributionEmitter> emitters;
    int emittersBeyondCull = 0;
    std::vector<ScreenBox> heroRegions;       // on-screen heroes' boxes, for the A/B's region metrics
    std::vector<std::string> suggestedHeroes; // when the scene declares none: the largest visible contributors
    std::vector<std::string> limits;
};

// Coverage is the bounding sphere's disc over the frame; the radius in pixels is the caster floor's own formula
// (radius x height / (2 tan(fovY/2) x distance)), so the caster table predicts exactly what the floor removes.
[[nodiscard]] ContributionReport analyseContribution(const std::vector<LiveProfileEntity>& entities,
                                                     const std::vector<ContributionEmitter>& emitters);
// The contribution of one entity: its coverage over its lever weight (a hero's is +infinity: never low).
[[nodiscard]] double contributionOf(double coverage, float leverWeight, bool hero);

// ---- ADR-1109: heroes --------------------------------------------------------------------------------------------

enum class HeroEffect : std::uint8_t {
    Exempt,    // a per-object lever the engine skips heroes for (geometry, shadow casting, particles, animation)
    ImageWide, // changes every pixel the same way, the heroes' included (resolution, fog, post, shadow atlas)
    Degrades,  // reduces a hero's own representation (the `noprograms` diagnostic arm)
    Unknown,   // not a lever this table knows: treated as Degrades
};
[[nodiscard]] HeroEffect heroEffectOfLever(std::string_view lever);
[[nodiscard]] const char* heroEffectName(HeroEffect effect);

enum class HeroPolicy : std::uint8_t {
    Protect, // the default: Degrades/Unknown never proposed; ImageWide admitted, its hero-region change measured
    Strict,  // ImageWide excluded too
};
[[nodiscard]] std::optional<HeroPolicy> heroPolicyFromToken(std::string_view token);
[[nodiscard]] const char* heroPolicyToken(HeroPolicy policy);

[[nodiscard]] int riskRank(std::string_view risk); // low 0, medium 1, high 2, anything else 3
// Whether the optimizer may try `lever`: it has a form the project can keep (a ceiling), the hero policy admits its
// effect, and its risk is at most `maxRisk`. `why` says what refused it.
[[nodiscard]] bool leverAdmitted(std::string_view lever, std::string_view risk, HeroPolicy policy,
                                 std::string_view maxRisk, std::string* why = nullptr);

// ---- ADR-1110: ORIGINAL vs OPTIMIZED --------------------------------------------------------------------------------

// The Quality Lab's pooled answer (avgen.abdiff/1), as the optimizer reads it.
struct AbVisual {
    bool available = false;
    std::string unavailableReason;
    int frames = 0;
    double meanAbsDiff = 0, changedFraction = 0, ssim = 1, ssimWorst = 1, msSsim = 1, edgeDifference = 0,
           edgeStrengthRatio = 1, edgesLost = 0, lumaDelta = 0, ssimMatched = 1, meanAbsDiffMatched = 0;
    std::optional<double> psnr;                       // none when every pair was identical
    std::optional<double> temporalDifference, temporalActivityRatio;
    bool haveRegions = false;
    double regionCoverage = 0, regionSsim = 1, regionSsimMatched = 1, regionMeanAbsDiff = 0, regionLumaDelta = 0;
    nlohmann::json raw; // the whole answer, kept for the record
};
[[nodiscard]] AbVisual abVisualFromJson(const nlohmann::json& abdiff);

// The search's rank key, and only that (ADR-250: no metric decides): whole-frame 1 - SSIM at matched luminance, plus
// the same inside the hero regions when heroes are on screen (so a hero's pixels count twice).
[[nodiscard]] double visualRankKey(const AbVisual& v);
// Every metric of `v` inside the self-difference floor `floor` (with a small allowance for 8-bit rounding).
[[nodiscard]] bool withinFloor(const AbVisual& v, const AbVisual& floor);

struct AbTiming {
    bool measured = false;
    double baselineMs = 0, optimizedMs = 0, savingMs = 0, savingPercent = 0, noiseFloorPercent = 0;
    double baselineGpuMs = 0, optimizedGpuMs = 0, gpuSavingMs = 0;
    bool isResult = false;  // the frame-cost saving is outside the noise floor
    bool voided = false;    // the machine drifted more than the effect
    int pairs = 0;
    std::string verdict;
};

struct CriticAnswer {
    bool asked = false;
    bool available = false;
    std::string unavailableReason;
    std::string originalJob, optimizedJob;
    nlohmann::json comparison; // `critic compare --json`, as the Critic said it
};

struct AbComparison {
    std::string label;                // "original vs optimized: scale85+volumequarter"
    std::vector<std::string> levers;  // what OPTIMIZED applies, as ceilings
    AbTiming timing;                  // MEASURED
    AbVisual visual;                  // MEASURED (the Lab)
    bool inFloor = false;             // visual within the self-difference floor
    std::string visualVerdict;
    std::string framesDir;            // ORIGINAL / OPTIMIZED PNGs a person can open
    CriticAnswer critic;              // optional; never read by the optimizer
};
[[nodiscard]] std::string describeVisual(const AbVisual& v, const AbVisual& floor, bool inFloor);

// ---- ADR-1111: the search ------------------------------------------------------------------------------------------

struct SearchCandidate {
    std::string lever, title, risk;
    HeroEffect heroEffect = HeroEffect::Unknown;
    double estimatedLowMs = 0, estimatedHighMs = 0; // the rules' ESTIMATE, carried for reference
    bool admitted = false;
    std::string refusal;
    AbComparison single; // MEASURED when admitted
};

struct ComboPlan {
    std::vector<std::string> levers;
    double estimatedSavingMs = 0;  // ESTIMATED: the measured singles' savings summed (assumed additive)
    double estimatedCostMs = 0;    // ESTIMATED: baseline - that
    double estimatedVisual = 0;    // ESTIMATED: the singles' rank keys summed
    bool reachesByEstimate = false;
};

// Combinations of up to `maxSize` usable singles (admitted, measured, a saving outside the noise), ranked: those that
// reach `targetMs` by estimate first, by estimated visual change; then the rest by estimated cost.
[[nodiscard]] std::vector<ComboPlan> planCombinations(const std::vector<SearchCandidate>& singles, double baselineMs,
                                                      double targetMs, std::size_t maxSize = 3);
[[nodiscard]] bool usableSingle(const SearchCandidate& c);

struct OptimizationReport {
    bool ran = false;
    double targetFps = 60, budgetMs = 16.67, margin = 0.9, targetMs = 15.0;
    double baselineMs = 0; // MEASURED: the pipelined frame cost, max(GPU, CPU work) median
    bool alreadyUnder = false;
    HeroPolicy heroPolicy = HeroPolicy::Protect;
    std::string maxRisk = "medium";
    AbVisual floor;        // ORIGINAL vs ORIGINAL: what the renderer's own repeatability costs every metric
    std::vector<SearchCandidate> candidates;
    std::vector<ComboPlan> plans;           // ESTIMATED
    std::vector<AbComparison> measuredCombos; // MEASURED
    // The choice: the measured combination (or single) reaching the target with the least visual rank key.
    bool reached = false;
    std::vector<std::string> chosen;
    int chosenIndex = -1; // into measuredCombos, or -1
    std::string decision; // in words
    std::vector<std::string> lowRisk; // low-risk, hero-safe singles with a measured saving outside the noise
    std::vector<std::string> notes;
};

// Picks from the measured combinations: reaching `targetMs` (measured), then the least rank key. When none reaches,
// the largest measured saving is reported and `reached` stays false.
void chooseCombination(OptimizationReport& report);
[[nodiscard]] std::vector<std::string> lowRiskSet(const std::vector<SearchCandidate>& candidates);

// ---- writers ---------------------------------------------------------------------------------------------------------

[[nodiscard]] nlohmann::json contributionJson(const ContributionReport& c);
[[nodiscard]] nlohmann::json comparisonJson(const AbComparison& c);
[[nodiscard]] nlohmann::json optimizationJson(const OptimizationReport& o);
[[nodiscard]] std::string contributionText(const ContributionReport& c);
[[nodiscard]] std::string comparisonText(const AbComparison& c, const AbVisual& floor);
[[nodiscard]] std::string optimizationText(const OptimizationReport& o);

// `critic` command lines (ADR-1112): a frame run submitted, and two jobs compared.
[[nodiscard]] std::vector<std::string> criticSequenceCommand(const std::string& critic, const std::string& url,
                                                             const std::string& dir, double fps,
                                                             const std::string& label, const std::string& compareTo);
[[nodiscard]] std::vector<std::string> criticCompareCommand(const std::string& critic, const std::string& url,
                                                            const std::string& a, const std::string& b);

} // namespace avgen::app
