#include "app/live_optimize.hpp"

#include "app/interactive_resolution.hpp"
#include "app/live_profile.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace avgen::app {

using nlohmann::json;

namespace {

json num(double v) { return std::isfinite(v) ? json(v) : json(nullptr); }
json opt(const std::optional<double>& v) { return v && std::isfinite(*v) ? json(*v) : json(nullptr); }

std::string joined(const std::vector<std::string>& v, const char* sep = "+") {
    std::string out;
    for (const auto& s : v) {
        out += out.empty() ? s : sep + s;
    }
    return out;
}

} // namespace

// ---- ADR-1108 ----------------------------------------------------------------------------------------------------

double contributionOf(double coverage, float leverWeight, bool hero) {
    if (hero || leverWeight <= 0.0f) {
        return std::numeric_limits<double>::infinity();
    }
    return coverage / static_cast<double>(leverWeight);
}

ContributionReport analyseContribution(const std::vector<LiveProfileEntity>& entities,
                                       const std::vector<ContributionEmitter>& emitters) {
    ContributionReport r;
    r.available = true;
    r.entities = static_cast<int>(entities.size());
    for (const auto& e : entities) {
        r.visible += e.visible && e.onScreen ? 1 : 0;
        r.casters += e.castsShadow ? 1 : 0;
        r.heroEntities += e.hero ? 1 : 0;
        r.heroesOnScreen += e.hero && e.visible && e.onScreen ? 1 : 0;
        if (e.hero && e.visible && e.haveBox) {
            r.heroRegions.push_back({e.name, e.x0, e.y0, e.x1, e.y1});
        }
    }
    // Top contributors: visible, on screen, by coverage (heroes included and marked).
    std::vector<const LiveProfileEntity*> shown;
    for (const auto& e : entities) {
        if (e.visible && e.onScreen) {
            shown.push_back(&e);
        }
    }
    std::stable_sort(shown.begin(), shown.end(),
                     [](const auto* a, const auto* b) { return a->projectedArea > b->projectedArea; });
    for (std::size_t k = 0; k < std::min<std::size_t>(8, shown.size()); ++k) {
        r.topContributors.push_back(fmt::format("{} ({:.2f}% of the frame, {})", shown[k]->name,
                                                shown[k]->projectedArea * 100.0, shown[k]->importance));
    }
    // The caster floors: exactly the renderer's test (radius px < floor x weight; a hero never).
    for (const double floor : {8.0, 16.0, 24.0, 48.0}) {
        CasterFloorRow row;
        row.floorPx = floor;
        for (const auto& e : entities) {
            if (!e.castsShadow || e.radiusPx <= 0.0) {
                continue; // a camera inside its bounds is never under the floor (radius 0 here)
            }
            if (e.hero) {
                row.heroCastersProtected += e.radiusPx < floor ? 1 : 0;
                continue;
            }
            if (e.radiusPx < floor * static_cast<double>(e.leverWeight)) {
                ++row.casters;
                row.coverage += e.projectedArea;
                if (row.names.size() < 10) {
                    row.names.push_back(e.name);
                }
            }
        }
        r.casterFloors.push_back(std::move(row));
    }
    for (const auto& e : entities) {
        if (!e.hero && e.visible && e.onScreen && e.projectedArea < 0.005) {
            ++r.lodCandidates;
            r.lodCandidateCoverage += e.projectedArea;
            if (r.lodCandidateNames.size() < 10) {
                r.lodCandidateNames.push_back(e.name);
            }
        }
    }
    r.emitters = emitters;
    for (const auto& em : emitters) {
        r.emittersBeyondCull += em.beyondCull ? 1 : 0;
    }
    if (r.heroEntities == 0) {
        // An object, not a backdrop: something that fills a quarter of the frame or more (terrain, water, a sky
        // dome, anything the camera is inside) is the setting a hero stands in, not a hero.
        for (const auto* e : shown) {
            if (r.suggestedHeroes.size() >= 3 || e->projectedArea < 0.01) {
                break;
            }
            if (e->projectedArea < 0.25) {
                r.suggestedHeroes.push_back(e->name);
            }
        }
    }
    r.limits = {
        "coverage is each entity's bounding sphere as a disc on the frame, not its silhouette: it over-reads thin and "
        "hollow objects, and overlapping objects are counted once each",
        "a caster small on screen can cast a large shadow (a low sun); the caster table says what the floor removes, "
        "not what that shadow covered",
        "the camera of the last measured frame; a moving camera changes every row",
        "procedural instances and SDF objects are not entities and are not listed (their LOD and casters are decided on "
        "the GPU)",
    };
    return r;
}

// ---- ADR-1109 ----------------------------------------------------------------------------------------------------

HeroEffect heroEffectOfLever(std::string_view lever) {
    static constexpr std::array<std::string_view, 4> kExempt{"castercull", "lodbias2", "drawdist75", "particlelod"};
    static constexpr std::array<std::string_view, 10> kImageWide{"volumequarter", "volumesteps", "volumepreview",
                                                                 "posttaps",      "nomotionblur", "nodof",
                                                                 "shadowatlas1k", "scale85",      "scale71", "pcss"};
    if (std::find(kExempt.begin(), kExempt.end(), lever) != kExempt.end()) return HeroEffect::Exempt;
    if (std::find(kImageWide.begin(), kImageWide.end(), lever) != kImageWide.end()) return HeroEffect::ImageWide;
    if (lever == "noprograms") return HeroEffect::Degrades;
    return HeroEffect::Unknown;
}

const char* heroEffectName(HeroEffect e) {
    switch (e) {
    case HeroEffect::Exempt: return "exempt";
    case HeroEffect::ImageWide: return "image-wide";
    case HeroEffect::Degrades: return "degrades";
    case HeroEffect::Unknown: return "unknown";
    }
    return "unknown";
}

std::optional<HeroPolicy> heroPolicyFromToken(std::string_view t) {
    if (t == "protect") return HeroPolicy::Protect;
    if (t == "strict") return HeroPolicy::Strict;
    return std::nullopt;
}

const char* heroPolicyToken(HeroPolicy p) { return p == HeroPolicy::Strict ? "strict" : "protect"; }

int riskRank(std::string_view risk) {
    if (risk == "low") return 0;
    if (risk == "medium") return 1;
    if (risk == "high") return 2;
    return 3;
}

bool leverAdmitted(std::string_view lever, std::string_view risk, HeroPolicy policy, std::string_view maxRisk,
                   std::string* why) {
    const auto refuse = [&](std::string reason) {
        if (why != nullptr) *why = std::move(reason);
        return false;
    };
    LiveQualityRung probe{};
    if (lever.empty() || !applyLeverToCeiling(probe, lever)) {
        return refuse("no form the project can keep (a diagnostic or pass arm)");
    }
    const HeroEffect effect = heroEffectOfLever(lever);
    if (effect == HeroEffect::Degrades || effect == HeroEffect::Unknown) {
        return refuse("it would degrade a hero (hero policy)");
    }
    if (effect == HeroEffect::ImageWide && policy == HeroPolicy::Strict) {
        return refuse("image-wide: it changes the heroes' pixels too (hero policy strict)");
    }
    if (riskRank(risk) > riskRank(maxRisk)) {
        return refuse(fmt::format("risk {} is above the search's {}", risk, maxRisk));
    }
    if (why != nullptr) why->clear();
    return true;
}

// ---- ADR-1110 ----------------------------------------------------------------------------------------------------

AbVisual abVisualFromJson(const json& j) {
    AbVisual v;
    if (!j.is_object() || j.value("schema", std::string()) != "avgen.abdiff/1" || !j.contains("pooled")) {
        v.unavailableReason = "not an avgen.abdiff/1 answer";
        return v;
    }
    const json& p = j["pooled"];
    const auto mean = [&](const char* key, double fallback) {
        if (!p.contains(key) || !p[key].is_object() || !p[key]["mean"].is_number()) return fallback;
        return p[key]["mean"].get<double>();
    };
    const auto worst = [&](const char* key, double fallback) {
        if (!p.contains(key) || !p[key].is_object() || !p[key]["worst"].is_number()) return fallback;
        return p[key]["worst"].get<double>();
    };
    const auto maybe = [&](const char* key) -> std::optional<double> {
        if (!p.contains(key) || !p[key].is_object() || !p[key]["mean"].is_number()) return std::nullopt;
        return p[key]["mean"].get<double>();
    };
    v.available = true;
    v.frames = j.value("frames", 0);
    v.meanAbsDiff = mean("meanAbsDiff", 0);
    v.changedFraction = mean("changedFraction", 0);
    v.psnr = maybe("psnr");
    v.ssim = mean("ssim", 1);
    v.ssimWorst = worst("ssim", 1);
    v.msSsim = mean("msSsim", 1);
    v.edgeDifference = mean("edgeDifference", 0);
    v.edgeStrengthRatio = mean("edgeStrengthRatio", 1);
    v.edgesLost = mean("edgesLost", 0);
    v.lumaDelta = mean("lumaDelta", 0);
    v.ssimMatched = mean("ssimMatched", 1);
    v.meanAbsDiffMatched = mean("meanAbsDiffMatched", 0);
    v.temporalDifference = maybe("temporalDifference");
    v.temporalActivityRatio = maybe("temporalActivityRatio");
    v.haveRegions = j.contains("regions") && j["regions"].value("onFrame", false);
    if (v.haveRegions) {
        v.regionCoverage = j["regions"].value("coverage", 0.0);
        v.regionSsim = mean("regionSsim", 1);
        v.regionSsimMatched = mean("regionSsimMatched", 1);
        v.regionMeanAbsDiff = mean("regionMeanAbsDiff", 0);
        v.regionLumaDelta = mean("regionLumaDelta", 0);
    }
    v.raw = j;
    return v;
}

double visualRankKey(const AbVisual& v) {
    if (!v.available) {
        return std::numeric_limits<double>::infinity(); // unmeasured is never "the least change"
    }
    double key = std::max(0.0, 1.0 - v.ssimMatched);
    if (v.haveRegions) {
        key += std::max(0.0, 1.0 - v.regionSsimMatched);
    }
    return key;
}

bool withinFloor(const AbVisual& v, const AbVisual& floor) {
    if (!v.available || !floor.available) {
        return false;
    }
    // 8-bit output: a difference of a fraction of one step is rounding, whatever the floor says.
    constexpr double kStep = 0.05;
    const auto under = [](double value, double floorValue, double allowance) { return value <= floorValue + allowance; };
    bool ok = under(v.meanAbsDiff, floor.meanAbsDiff, kStep) && under(1.0 - v.ssim, 1.0 - floor.ssim, 1e-4) &&
              under(1.0 - v.ssimMatched, 1.0 - floor.ssimMatched, 1e-4) &&
              under(v.edgeDifference, floor.edgeDifference, 1e-3) && under(v.lumaDelta, floor.lumaDelta, kStep);
    if (v.temporalDifference && floor.temporalDifference) {
        ok = ok && under(*v.temporalDifference, *floor.temporalDifference, kStep);
    }
    if (v.haveRegions && floor.haveRegions) {
        ok = ok && under(1.0 - v.regionSsimMatched, 1.0 - floor.regionSsimMatched, 1e-4);
    }
    return ok;
}

std::string describeVisual(const AbVisual& v, const AbVisual& floor, bool inFloor) {
    if (!v.available) {
        return "pictures not compared: " + v.unavailableReason;
    }
    if (inFloor) {
        return "every metric inside the self-difference floor measured on this run (these frames only; not a claim "
               "of equivalence)";
    }
    std::string s = fmt::format("a measured change: SSIM {:.4f} (matched luminance {:.4f}), mean |d| {:.2f} steps, "
                                "luma {:+.2f}, edges {:.1f}% different",
                                v.ssim, v.ssimMatched, v.meanAbsDiff, v.lumaDelta, v.edgeDifference * 100.0);
    if (v.haveRegions) {
        s += fmt::format("; inside the hero regions SSIM {:.4f} (matched {:.4f})", v.regionSsim, v.regionSsimMatched);
    }
    if (floor.available) {
        s += fmt::format("; floor SSIM {:.4f}, mean |d| {:.2f}", floor.ssim, floor.meanAbsDiff);
    }
    return s;
}

// ---- ADR-1111 ----------------------------------------------------------------------------------------------------

bool usableSingle(const SearchCandidate& c) {
    return c.admitted && c.single.timing.measured && c.single.timing.isResult && !c.single.timing.voided &&
           c.single.timing.savingMs > 0.0 && c.single.visual.available;
}

std::vector<ComboPlan> planCombinations(const std::vector<SearchCandidate>& singles, double baselineMs,
                                        double targetMs, std::size_t maxSize) {
    std::vector<const SearchCandidate*> usable;
    for (const auto& c : singles) {
        if (usableSingle(c)) {
            usable.push_back(&c);
        }
    }
    std::vector<ComboPlan> plans;
    const std::size_t n = usable.size();
    // Every subset of size 2..maxSize (singles are already measured as themselves). n is at most 16.
    for (std::uint32_t mask = 1; mask < (1u << n); ++mask) {
        const auto size = static_cast<std::size_t>(__builtin_popcount(mask));
        if (size < 2 || size > maxSize) {
            continue;
        }
        ComboPlan p;
        for (std::size_t k = 0; k < n; ++k) {
            if ((mask & (1u << k)) != 0) {
                p.levers.push_back(usable[k]->lever);
                p.estimatedSavingMs += usable[k]->single.timing.savingMs;
                p.estimatedVisual += visualRankKey(usable[k]->single.visual);
            }
        }
        // Two render-scale levers are one lever at the lower value: the pair is not a combination.
        if (std::count_if(p.levers.begin(), p.levers.end(), [](const auto& l) { return l.rfind("scale", 0) == 0; }) > 1) {
            continue;
        }
        p.estimatedCostMs = baselineMs - p.estimatedSavingMs;
        p.reachesByEstimate = p.estimatedCostMs <= targetMs;
        plans.push_back(std::move(p));
    }
    std::stable_sort(plans.begin(), plans.end(), [](const ComboPlan& a, const ComboPlan& b) {
        if (a.reachesByEstimate != b.reachesByEstimate) return a.reachesByEstimate;
        if (a.reachesByEstimate) return a.estimatedVisual < b.estimatedVisual;
        return a.estimatedCostMs < b.estimatedCostMs;
    });
    return plans;
}

void chooseCombination(OptimizationReport& o) {
    o.reached = false;
    o.chosen.clear();
    o.chosenIndex = -1;
    if (o.alreadyUnder) {
        o.reached = true;
        o.decision = fmt::format("already under the target: {:.2f} ms measured against {:.2f} ms; nothing needs to "
                                 "change",
                                 o.baselineMs, o.targetMs);
        return;
    }
    double bestKey = std::numeric_limits<double>::infinity();
    int bestSaving = -1;
    for (std::size_t i = 0; i < o.measuredCombos.size(); ++i) {
        const AbComparison& c = o.measuredCombos[i];
        if (!c.timing.measured || c.timing.voided || !c.visual.available) {
            continue;
        }
        const bool reaches = c.timing.isResult && c.timing.optimizedMs <= o.targetMs;
        const double key = visualRankKey(c.visual);
        if (reaches && key < bestKey) {
            bestKey = key;
            o.chosenIndex = static_cast<int>(i);
        }
        if (bestSaving < 0 || c.timing.savingMs > o.measuredCombos[static_cast<std::size_t>(bestSaving)].timing.savingMs) {
            bestSaving = static_cast<int>(i);
        }
    }
    if (o.chosenIndex >= 0) {
        const AbComparison& c = o.measuredCombos[static_cast<std::size_t>(o.chosenIndex)];
        o.reached = true;
        o.chosen = c.levers;
        o.decision = fmt::format("{} reaches the target MEASURED: {:.2f} -> {:.2f} ms (target {:.2f}), the least "
                                 "measured visual change of the combinations that reach it (rank key {:.4f})",
                                 joined(c.levers), c.timing.baselineMs, c.timing.optimizedMs, o.targetMs, bestKey);
    } else if (bestSaving >= 0) {
        const AbComparison& c = o.measuredCombos[static_cast<std::size_t>(bestSaving)];
        o.decision = fmt::format("NO admitted combination reaches the target: the largest measured saving is {} "
                                 "({:.2f} -> {:.2f} ms, target {:.2f}). The live controller's levels are what remain{}",
                                 joined(c.levers), c.timing.baselineMs, c.timing.optimizedMs, o.targetMs,
                                 o.maxRisk == "high" ? std::string()
                                                     : std::string(" (or a riskier search: --optimize-risk high; the "
                                                                   "hero policy is never relaxed for you)"));
    } else {
        o.decision = "nothing was measured that saves time outside the noise; no change is recommended";
    }
}

std::vector<std::string> lowRiskSet(const std::vector<SearchCandidate>& candidates) {
    std::vector<std::string> out;
    for (const auto& c : candidates) {
        if (usableSingle(c) && c.risk == "low" && c.heroEffect != HeroEffect::Degrades &&
            c.heroEffect != HeroEffect::Unknown) {
            out.push_back(c.lever);
        }
    }
    return out;
}

// ---- writers -------------------------------------------------------------------------------------------------------

json contributionJson(const ContributionReport& c) {
    if (!c.available) {
        return nullptr;
    }
    json floors = json::array();
    for (const auto& f : c.casterFloors) {
        floors.push_back({{"floorPx", f.floorPx}, {"casters", f.casters}, {"heroCastersProtected", f.heroCastersProtected},
                          {"coverage", f.coverage}, {"names", f.names},
                          {"lever", f.floorPx == 24.0 ? json("castercull") : json(nullptr)}});
    }
    json emitters = json::array();
    for (const auto& e : c.emitters) {
        emitters.push_back({{"name", e.name}, {"distance", e.distance}, {"reach", e.reach}, {"importance", e.importance},
                            {"hero", e.hero}, {"enabled", e.enabled}, {"beyondParticleCull", e.beyondCull}});
    }
    json boxes = json::array();
    for (const auto& b : c.heroRegions) {
        boxes.push_back({{"name", b.name}, {"x0", b.x0}, {"y0", b.y0}, {"x1", b.x1}, {"y1", b.y1}});
    }
    return {{"basis", "measured geometry at the last measured frame (bounding spheres); see limits"},
            {"entities", c.entities},
            {"visible", c.visible},
            {"shadowCasters", c.casters},
            {"heroEntities", c.heroEntities},
            {"heroesOnScreen", c.heroesOnScreen},
            {"topContributors", c.topContributors},
            {"lowContributionShadowCasters", floors},
            {"lodCandidates", {{"under", 0.005}, {"count", c.lodCandidates}, {"coverage", c.lodCandidateCoverage},
                               {"names", c.lodCandidateNames}, {"lever", "lodbias2"}}},
            {"particleEmitters", emitters},
            {"emittersBeyondParticleCull", c.emittersBeyondCull},
            {"heroRegions", boxes},
            {"suggestedHeroes", c.suggestedHeroes},
            {"suggestedHeroesNote", c.suggestedHeroes.empty()
                                        ? json(nullptr)
                                        : json("the scene declares no hero; these are the largest visible contributors. "
                                               "Suggested only: nothing is marked")},
            {"limits", c.limits}};
}

namespace {

json visualJson(const AbVisual& v) {
    if (!v.available) {
        return {{"available", false}, {"reason", v.unavailableReason}};
    }
    json j{{"available", true},
           {"basis", "MEASURED by the Quality Lab (avgen_quality ab)"},
           {"frames", v.frames},
           {"pixel", {{"meanAbsDiff", v.meanAbsDiff}, {"changedFraction", v.changedFraction}, {"psnr", opt(v.psnr)}}},
           {"structural", {{"ssim", v.ssim}, {"ssimWorstFrame", v.ssimWorst}, {"msSsim", v.msSsim}}},
           {"edge", {{"difference", v.edgeDifference}, {"strengthRatio", v.edgeStrengthRatio}, {"lost", v.edgesLost}}},
           {"luminance", {{"delta", v.lumaDelta}, {"ssimMatched", v.ssimMatched},
                          {"meanAbsDiffMatched", v.meanAbsDiffMatched}}},
           {"temporal", {{"difference", opt(v.temporalDifference)}, {"activityRatio", opt(v.temporalActivityRatio)}}},
           {"rankKey", num(visualRankKey(v))}};
    if (v.haveRegions) {
        j["heroRegions"] = {{"coverage", v.regionCoverage}, {"ssim", v.regionSsim},
                            {"ssimMatched", v.regionSsimMatched}, {"meanAbsDiff", v.regionMeanAbsDiff},
                            {"lumaDelta", v.regionLumaDelta}};
    } else {
        j["heroRegions"] = nullptr;
    }
    return j;
}

json timingJson(const AbTiming& t) {
    if (!t.measured) {
        return {{"measured", false}};
    }
    return {{"measured", true},
            {"basis", "MEASURED: counterbalanced A/B pairs in this process; frame cost = max(GPU span, CPU work)"},
            {"baselineMs", t.baselineMs},
            {"optimizedMs", t.optimizedMs},
            {"savingMs", t.savingMs},
            {"savingPercent", t.savingPercent},
            {"noiseFloorPercent", t.noiseFloorPercent},
            {"gpu", {{"baselineMs", t.baselineGpuMs}, {"optimizedMs", t.optimizedGpuMs}, {"savingMs", t.gpuSavingMs}}},
            {"isResult", t.isResult},
            {"void", t.voided},
            {"pairs", t.pairs},
            {"verdict", t.verdict}};
}

json criticJson(const CriticAnswer& c) {
    if (!c.asked) {
        return nullptr;
    }
    return {{"basis", "the Creative Critic's own answer (optional; the optimizer does not read it)"},
            {"available", c.available},
            {"reason", c.unavailableReason},
            {"originalJob", c.originalJob},
            {"optimizedJob", c.optimizedJob},
            {"comparison", c.comparison}};
}

} // namespace

json comparisonJson(const AbComparison& c) {
    return {{"label", c.label},
            {"levers", c.levers},
            {"timing", timingJson(c.timing)},
            {"visual", visualJson(c.visual)},
            {"withinSelfDifferenceFloor", c.inFloor},
            {"visualVerdict", c.visualVerdict},
            {"frames", c.framesDir},
            {"critic", criticJson(c.critic)}};
}

json optimizationJson(const OptimizationReport& o) {
    json cands = json::array();
    for (const auto& c : o.candidates) {
        json cj{{"lever", c.lever},
                {"title", c.title},
                {"risk", c.risk},
                {"heroEffect", heroEffectName(c.heroEffect)},
                {"estimatedSavingMs", {{"low", c.estimatedLowMs}, {"high", c.estimatedHighMs}}},
                {"admitted", c.admitted},
                {"refusal", c.refusal},
                {"usable", usableSingle(c)}};
        if (c.admitted) {
            cj["measured"] = comparisonJson(c.single);
        }
        cands.push_back(std::move(cj));
    }
    json plans = json::array();
    for (const auto& p : o.plans) {
        plans.push_back({{"levers", p.levers},
                         {"estimatedSavingMs", p.estimatedSavingMs},
                         {"estimatedCostMs", p.estimatedCostMs},
                         {"estimatedRankKey", p.estimatedVisual},
                         {"reachesByEstimate", p.reachesByEstimate},
                         {"basis", "ESTIMATED: measured singles summed, assumed additive"}});
    }
    json combos = json::array();
    for (const auto& c : o.measuredCombos) {
        combos.push_back(comparisonJson(c));
    }
    return {{"targetFps", o.targetFps},
            {"budgetMs", o.budgetMs},
            {"margin", o.margin},
            {"targetMs", o.targetMs},
            {"baselineMs", o.baselineMs},
            {"baselineBasis", "MEASURED: median of max(GPU span, CPU work) per frame, headless"},
            {"alreadyUnder", o.alreadyUnder},
            {"heroPolicy", heroPolicyToken(o.heroPolicy)},
            {"maxRisk", o.maxRisk},
            {"selfDifferenceFloor", visualJson(o.floor)},
            {"candidates", cands},
            {"plans", plans},
            {"measuredCombos", combos},
            {"reached", o.reached},
            {"chosen", o.chosen},
            {"decision", o.decision},
            {"lowRisk", o.lowRisk},
            {"notes", o.notes}};
}

std::string contributionText(const ContributionReport& c) {
    std::string t;
    const auto line = [&]<typename... A>(fmt::format_string<A...> f, A&&... args) {
        t += fmt::format(f, std::forward<A>(args)...);
        t += '\n';
    };
    line("CONTRIBUTION (ADR-1108; bounding spheres at the last measured frame)");
    line("  {} entities, {} visible on screen, {} shadow casters; heroes: {} ({} on screen)", c.entities, c.visible,
         c.casters, c.heroEntities, c.heroesOnScreen);
    for (const auto& s : c.topContributors) {
        line("    {}", s);
    }
    line("  low-contribution shadow casters (non-hero, under the floor; heroes kept):");
    for (const auto& f : c.casterFloors) {
        line("    {:>3.0f} px: {:4} caster(s), {:.2f}% of the frame{}{}", f.floorPx, f.casters, f.coverage * 100.0,
             f.heroCastersProtected > 0 ? fmt::format(", {} hero caster(s) protected", f.heroCastersProtected)
                                        : std::string(),
             f.floorPx == 24.0 ? "   <- castercull" : "");
    }
    line("  LOD candidates (non-hero, under 0.5% of the frame): {} ({:.2f}% of the frame together)", c.lodCandidates,
         c.lodCandidateCoverage * 100.0);
    line("  particle emitters: {} ({} beyond the particle cull at 60 m, weighted)", c.emitters.size(),
         c.emittersBeyondCull);
    if (!c.suggestedHeroes.empty()) {
        line("  no hero is declared; the largest visible contributors would be: {}", joined(c.suggestedHeroes, ", "));
    }
    return t;
}

std::string comparisonText(const AbComparison& c, const AbVisual& floor) {
    std::string t;
    const auto line = [&]<typename... A>(fmt::format_string<A...> f, A&&... args) {
        t += fmt::format(f, std::forward<A>(args)...);
        t += '\n';
    };
    line("ORIGINAL vs OPTIMIZED: {} (ADR-1110)", joined(c.levers, ", "));
    if (c.timing.measured) {
        line("  MEASURED time: {:.2f} -> {:.2f} ms ({:+.2f} ms, {:+.1f}%), noise floor {:.1f}% over {} pair(s): {}",
             c.timing.baselineMs, c.timing.optimizedMs, -c.timing.savingMs, -c.timing.savingPercent,
             c.timing.noiseFloorPercent, c.timing.pairs, c.timing.verdict);
    }
    line("  MEASURED picture: {}", describeVisual(c.visual, floor, c.inFloor));
    if (c.visual.available && c.visual.temporalDifference) {
        line("    temporal difference {:.2f} steps, activity ratio {:.3f}; edges lost {:.1f}%", *c.visual.temporalDifference,
             c.visual.temporalActivityRatio.value_or(1.0), c.visual.edgesLost * 100.0);
    }
    if (!c.framesDir.empty()) {
        line("  frames: {}", c.framesDir);
    }
    if (c.critic.asked) {
        line("  Critic: {}", c.critic.available ? "answered (see the record)" : "unavailable -- " + c.critic.unavailableReason);
    }
    return t;
}

std::string optimizationText(const OptimizationReport& o) {
    std::string t;
    const auto line = [&]<typename... A>(fmt::format_string<A...> f, A&&... args) {
        t += fmt::format(f, std::forward<A>(args)...);
        t += '\n';
    };
    line("OPTIMIZATION SEARCH (ADR-1111): target {:.0f} fps, {:.2f} ms x {:.2f} = {:.2f} ms; hero policy {}, risk <= {}",
         o.targetFps, o.budgetMs, o.margin, o.targetMs, heroPolicyToken(o.heroPolicy), o.maxRisk);
    line("  baseline MEASURED {:.2f} ms{}", o.baselineMs, o.alreadyUnder ? " (already under the target)" : "");
    if (o.floor.available) {
        line("  self-difference floor (ORIGINAL twice): SSIM {:.5f}, mean |d| {:.3f} steps", o.floor.ssim,
             o.floor.meanAbsDiff);
    }
    line("  singles:");
    for (const auto& c : o.candidates) {
        if (!c.admitted) {
            line("    {:<14} not tried: {}", c.lever, c.refusal);
            continue;
        }
        const auto& m = c.single;
        line("    {:<14} MEASURED {:+.2f} ms ({}), rank key {:.4f}, heroes {}{}", c.lever, -m.timing.savingMs,
             m.timing.verdict, visualRankKey(m.visual), heroEffectName(c.heroEffect),
             usableSingle(c) ? "" : "  [not usable]");
    }
    if (!o.plans.empty()) {
        line("  combinations, ESTIMATED from the singles (top {}):", std::min<std::size_t>(5, o.plans.size()));
        for (std::size_t k = 0; k < std::min<std::size_t>(5, o.plans.size()); ++k) {
            const auto& p = o.plans[k];
            line("    {:<34} est. {:.2f} ms, est. rank key {:.4f}{}", joined(p.levers), p.estimatedCostMs,
                 p.estimatedVisual, p.reachesByEstimate ? "  reaches by estimate" : "");
        }
    }
    for (const auto& c : o.measuredCombos) {
        line("  MEASURED {:<30} {:.2f} -> {:.2f} ms, rank key {:.4f}{}", joined(c.levers), c.timing.baselineMs,
             c.timing.optimizedMs, visualRankKey(c.visual), c.inFloor ? " (inside the floor)" : "");
    }
    line("  DECISION: {}", o.decision);
    if (!o.lowRisk.empty()) {
        line("  low-risk set (measured saving, low risk, hero-safe): {}", joined(o.lowRisk, ", "));
    }
    for (const auto& n : o.notes) {
        line("  * {}", n);
    }
    return t;
}

std::vector<std::string> criticSequenceCommand(const std::string& critic, const std::string& url, const std::string& dir,
                                               double fps, const std::string& label, const std::string& compareTo) {
    std::vector<std::string> argv{critic};
    if (!url.empty()) {
        argv.insert(argv.end(), {"--url", url});
    }
    argv.insert(argv.end(), {"submit", "--mode", "preview", "--sequence", dir, "--fps", fmt::format("{}", fps), "--label",
                             label, "--wait", "--json", "--no-autostart", "--session", "avgen-live-optimizer"});
    if (!compareTo.empty()) {
        argv.insert(argv.end(), {"--compare-to", compareTo});
    }
    return argv;
}

std::vector<std::string> criticCompareCommand(const std::string& critic, const std::string& url, const std::string& a,
                                              const std::string& b) {
    std::vector<std::string> argv{critic};
    if (!url.empty()) {
        argv.insert(argv.end(), {"--url", url});
    }
    argv.insert(argv.end(), {"compare", a, b, "--json"});
    return argv;
}

} // namespace avgen::app
