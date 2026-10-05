// ADR-1108..1113: the live optimizer's Phase 5 logic, with no device -- contribution, heroes, the A/B metrics (the
// Quality Lab's `ab`), reading the Lab's answer, the search's planning and choosing, and the agent hook's command line.

#include "app/interactive_resolution.hpp"
#include "app/live_optimize.hpp"
#include "app/live_profile.hpp"
#include "app/live_profile_hook.hpp"
#include "entity/entity.hpp"
#include "metrics/ab.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace avgen;
using Catch::Approx;

namespace {

app::LiveProfileEntity ent(const std::string& name, double coverage, double radiusPx, bool caster,
                           scene::Importance importance = scene::Importance::Normal) {
    app::LiveProfileEntity e;
    e.name = name;
    e.projectedArea = coverage;
    e.radiusPx = radiusPx;
    e.castsShadow = caster;
    e.visible = true;
    e.onScreen = true;
    e.hero = importance == scene::Importance::Hero;
    e.importance = scene::importanceName(importance);
    e.leverWeight = scene::importanceLeverWeight(importance);
    e.contribution = app::contributionOf(coverage, e.leverWeight, e.hero);
    if (e.hero) {
        e.haveBox = true;
        e.x0 = 10;
        e.y0 = 10;
        e.x1 = 40;
        e.y1 = 30;
    }
    return e;
}

quality::Frame gradient(std::uint32_t w, std::uint32_t h, int offset = 0) {
    quality::Frame f;
    f.width = w;
    f.height = h;
    f.rgba.resize(static_cast<std::size_t>(w) * h * 4);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            // a pattern with edges: stripes over a gradient
            const int v = static_cast<int>((x * 3 + y) % 160) + ((x / 8) % 2 == 0 ? 60 : 0) + offset;
            const auto b = static_cast<std::uint8_t>(std::clamp(v, 0, 255));
            f.rgba[i] = b;
            f.rgba[i + 1] = b;
            f.rgba[i + 2] = b;
            f.rgba[i + 3] = 255;
        }
    }
    return f;
}

quality::Frame scaled(const quality::Frame& f, double gain) {
    quality::Frame out = f;
    for (std::size_t i = 0; i < out.rgba.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            out.rgba[i + c] = static_cast<std::uint8_t>(std::clamp(std::lround(f.rgba[i + c] * gain), 0L, 255L));
        }
    }
    return out;
}

quality::Frame boxBlur(const quality::Frame& f) {
    quality::Frame out = f;
    for (std::uint32_t y = 1; y + 1 < f.height; ++y) {
        for (std::uint32_t x = 1; x + 1 < f.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                int s = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        s += f.pixel(x + dx, y + dy)[c];
                    }
                }
                out.rgba[(static_cast<std::size_t>(y) * f.width + x) * 4 + c] = static_cast<std::uint8_t>(s / 9);
            }
        }
    }
    return out;
}

app::SearchCandidate single(const std::string& lever, double saving, double ssimMatched, bool result = true,
                            const std::string& risk = "low") {
    app::SearchCandidate c;
    c.lever = lever;
    c.risk = risk;
    c.heroEffect = app::heroEffectOfLever(lever);
    c.admitted = true;
    c.single.levers = {lever};
    c.single.timing.measured = true;
    c.single.timing.baselineMs = 20.0;
    c.single.timing.optimizedMs = 20.0 - saving;
    c.single.timing.savingMs = saving;
    c.single.timing.isResult = result;
    c.single.visual.available = true;
    c.single.visual.ssimMatched = ssimMatched;
    return c;
}

} // namespace

// ---- ADR-1108 ------------------------------------------------------------------------------------------------------

TEST_CASE("contribution: the caster table is the renderer's floor, and a hero caster is protected", "[unit][live-optimize]") {
    std::vector<app::LiveProfileEntity> es{
        ent("hero", 0.20, 6.0, true, scene::Importance::Hero), // tiny on screen, but a hero
        ent("near", 0.30, 300.0, true),
        ent("pebble", 0.0001, 10.0, true),
        ent("far-tree", 0.0004, 20.0, true, scene::Importance::Background), // weight 1.5: 20 < 24 x 1.5 at 24 px
        ent("not-a-caster", 0.0001, 3.0, false),
    };
    const app::ContributionReport r = app::analyseContribution(es, {});
    REQUIRE(r.casterFloors.size() == 4);
    const auto row = [&](double px) {
        return *std::find_if(r.casterFloors.begin(), r.casterFloors.end(), [&](const auto& f) { return f.floorPx == px; });
    };
    CHECK(row(8.0).casters == 0);
    CHECK(row(8.0).heroCastersProtected == 1);
    CHECK(row(16.0).casters == 2);  // pebble (10 < 16) and far-tree (20 < 16 x 1.5)
    CHECK(row(24.0).casters == 2);
    CHECK(row(24.0).coverage == Approx(0.0005));
    CHECK(row(48.0).casters == 2);
    CHECK(r.heroesOnScreen == 1);
    REQUIRE(r.heroRegions.size() == 1);
    CHECK(r.heroRegions[0].name == "hero");
    CHECK(r.lodCandidates == 3); // pebble, far-tree, not-a-caster (the hero is never one)
    CHECK(r.suggestedHeroes.empty()); // a hero is declared
    CHECK(std::isinf(app::contributionOf(0.2, 0.0f, true)));
}

TEST_CASE("contribution: with no hero declared, the largest visible contributors are suggested, not marked",
          "[unit][live-optimize]") {
    std::vector<app::LiveProfileEntity> es{ent("terrain", 1.0, 0.0, true), ent("tree", 0.12, 200.0, true),
                                           ent("rock", 0.02, 50.0, true), ent("speck", 0.001, 4.0, true)};
    const app::ContributionReport r = app::analyseContribution(es, {});
    CHECK(r.heroEntities == 0);
    // The terrain fills the frame (a backdrop, not a hero); the speck is under 1% of it.
    CHECK(r.suggestedHeroes == std::vector<std::string>{"tree", "rock"});
    CHECK(r.heroRegions.empty());
    const nlohmann::json j = app::contributionJson(r);
    CHECK(j["suggestedHeroesNote"].is_string());
}

// ---- ADR-1109 ------------------------------------------------------------------------------------------------------

TEST_CASE("heroes: every lever with a ceiling form has a hero classification, and the policy admits by it",
          "[unit][live-optimize]") {
    for (const char* lever : {"volumequarter", "volumesteps", "volumepreview", "posttaps", "nomotionblur", "nodof",
                              "castercull", "shadowatlas1k", "lodbias2", "drawdist75", "particlelod", "scale85",
                              "scale71", "pcss"}) {
        app::LiveQualityRung c{};
        REQUIRE(app::applyLeverToCeiling(c, lever));
        CHECK(app::heroEffectOfLever(lever) != app::HeroEffect::Unknown);
        CHECK(app::heroEffectOfLever(lever) != app::HeroEffect::Degrades);
    }
    CHECK(app::heroEffectOfLever("noprograms") == app::HeroEffect::Degrades);
    CHECK(app::heroEffectOfLever("castercull") == app::HeroEffect::Exempt);
    CHECK(app::heroEffectOfLever("scale85") == app::HeroEffect::ImageWide);
    std::string why;
    CHECK_FALSE(app::leverAdmitted("noprograms", "high", app::HeroPolicy::Protect, "high", &why));
    CHECK(app::leverAdmitted("scale85", "medium", app::HeroPolicy::Protect, "medium", &why));
    CHECK_FALSE(app::leverAdmitted("scale85", "medium", app::HeroPolicy::Strict, "high", &why));
    CHECK(why.find("strict") != std::string::npos);
    CHECK(app::leverAdmitted("castercull", "low", app::HeroPolicy::Strict, "low", &why));
    CHECK_FALSE(app::leverAdmitted("nodof", "medium", app::HeroPolicy::Protect, "low", &why));
    CHECK_FALSE(app::leverAdmitted("made-up", "low", app::HeroPolicy::Protect, "high", &why));
}

TEST_CASE("heroes: an entity driving a hero node keeps its authored bands under the draw-distance lever",
          "[unit][live-optimize]") {
    const std::vector<std::string> heroes{"ember", "moth"}; // sorted, as Composition keeps them
    CHECK(entity::distanceScaleFor(&heroes, "ember", 0.75f) == 1.0f);
    CHECK(entity::distanceScaleFor(&heroes, "rabbit", 0.75f) == 0.75f);
    CHECK(entity::distanceScaleFor(nullptr, "ember", 0.75f) == 0.75f);
}

// ---- ADR-1110: the Lab's ab metrics --------------------------------------------------------------------------------

TEST_CASE("ab: identical frames differ in nothing", "[unit][live-optimize][quality]") {
    const quality::Frame a = gradient(64, 48);
    const quality::AbFrame r = quality::abCompare(a, a, {{"hero", 8, 8, 40, 40}}, &a, &a);
    CHECK(r.meanAbsDiff == 0.0);
    CHECK(r.changedFraction == 0.0);
    CHECK(std::isinf(r.psnr));
    CHECK(r.ssim == Approx(1.0));
    CHECK(r.edgeDifference == 0.0);
    CHECK(r.lumaDelta == 0.0);
    CHECK(r.temporalDifference == 0.0);
    CHECK(r.haveRegions);
    CHECK(r.regionSsim == Approx(1.0));
}

TEST_CASE("ab: an exposure change moves luminance and the raw metrics, not structure at matched luminance",
          "[unit][live-optimize][quality]") {
    const quality::Frame a = gradient(64, 48);
    const quality::Frame b = scaled(a, 0.8);
    const quality::AbFrame r = quality::abCompare(a, b);
    CHECK(r.lumaDelta < -10.0);
    CHECK(r.meanAbsDiff > 10.0);
    CHECK(r.meanAbsDiffMatched < r.meanAbsDiff / 4.0);
    CHECK(r.ssimMatched > 0.99);
    CHECK(r.ssimMatched >= r.ssim);
}

TEST_CASE("ab: a blur softens edges and loses some, at unchanged luminance", "[unit][live-optimize][quality]") {
    const quality::Frame a = gradient(64, 48);
    const quality::Frame b = boxBlur(a);
    const quality::AbFrame r = quality::abCompare(a, b);
    CHECK(std::abs(r.lumaDelta) < 2.0);
    CHECK(r.edgeStrengthRatio < 0.9);
    CHECK(r.edgeDifference > 0.1);
    CHECK(r.ssim < 0.99);
}

TEST_CASE("ab: a change outside the hero box leaves the box's numbers alone", "[unit][live-optimize][quality]") {
    const quality::Frame a = gradient(64, 48);
    quality::Frame b = a;
    for (std::uint32_t y = 30; y < 48; ++y) {
        for (std::uint32_t x = 40; x < 64; ++x) {
            b.rgba[(static_cast<std::size_t>(y) * 64 + x) * 4] = 0;
        }
    }
    const quality::AbFrame r = quality::abCompare(a, b, {{"hero", 0, 0, 32, 24}});
    CHECK(r.meanAbsDiff > 0.0);
    CHECK(r.haveRegions);
    CHECK(r.regionMeanAbsDiff == 0.0);
    CHECK(r.regionSsim == Approx(1.0));
    CHECK(r.regionCoverage == Approx(32.0 * 24.0 / (64.0 * 48.0)));
}

TEST_CASE("ab: the temporal difference sees a sequence that stopped moving", "[unit][live-optimize][quality]") {
    std::vector<quality::Frame> moving{gradient(32, 32, 0), gradient(32, 32, 20), gradient(32, 32, 40)};
    std::vector<quality::Frame> frozen{moving[0], moving[0], moving[0]};
    const quality::AbSequence s = quality::abCompareSequence(moving, frozen);
    REQUIRE(s.haveTemporal);
    CHECK(s.temporalDifference.mean > 5.0);
    CHECK(s.temporalActivityRatio.mean == Approx(0.0));
    const quality::AbSequence same = quality::abCompareSequence(moving, moving);
    CHECK(same.temporalDifference.mean == 0.0);
}

TEST_CASE("ab: the optimizer reads the Lab's answer, and unchanged is never mistaken for missing",
          "[unit][live-optimize]") {
    nlohmann::json j{{"schema", "avgen.abdiff/1"},
                     {"frames", 4},
                     {"pooled",
                      {{"meanAbsDiff", {{"mean", 1.5}, {"worst", 2.0}}},
                       {"psnr", {{"mean", nullptr}, {"worst", nullptr}}},
                       {"ssim", {{"mean", 0.97}, {"worst", 0.95}}},
                       {"ssimMatched", {{"mean", 0.98}, {"worst", 0.96}}},
                       {"regionSsimMatched", {{"mean", 0.99}, {"worst", 0.99}}}}},
                     {"regions", {{"onFrame", true}, {"coverage", 0.1}}}};
    const app::AbVisual v = app::abVisualFromJson(j);
    REQUIRE(v.available);
    CHECK(v.frames == 4);
    CHECK_FALSE(v.psnr.has_value());
    CHECK(v.ssimWorst == Approx(0.95));
    CHECK(app::visualRankKey(v) == Approx(0.02 + 0.01));
    CHECK_FALSE(app::abVisualFromJson(nlohmann::json{{"schema", "other"}}).available);
    CHECK(std::isinf(app::visualRankKey(app::AbVisual{})));
}

TEST_CASE("ab: within the floor only when every metric is inside the measured self-difference",
          "[unit][live-optimize]") {
    app::AbVisual floor;
    floor.available = true;
    app::AbVisual same = floor;
    CHECK(app::withinFloor(same, floor));
    app::AbVisual changed = floor;
    changed.ssimMatched = 0.99;
    CHECK_FALSE(app::withinFloor(changed, floor));
    app::AbVisual luma = floor;
    luma.lumaDelta = 1.0;
    CHECK_FALSE(app::withinFloor(luma, floor));
    CHECK_FALSE(app::withinFloor(same, app::AbVisual{})); // no floor measured: no claim
    CHECK(app::describeVisual(same, floor, true).find("not a claim of equivalence") != std::string::npos);
}

// ---- ADR-1111 ------------------------------------------------------------------------------------------------------

TEST_CASE("search: plans are ESTIMATED from usable singles only, and two render scales are not a combination",
          "[unit][live-optimize]") {
    std::vector<app::SearchCandidate> singles{single("scale85", 3.0, 0.97, true, "medium"),
                                              single("scale71", 5.0, 0.93, true, "medium"),
                                              single("volumequarter", 2.0, 0.995),
                                              single("posttaps", 0.1, 0.999, false), // inside the noise
                                              single("castercull", 0.5, 0.9999)};
    const auto plans = app::planCombinations(singles, 20.0, 15.0, 3);
    for (const auto& p : plans) {
        CHECK(std::find(p.levers.begin(), p.levers.end(), "posttaps") == p.levers.end());
        CHECK_FALSE((std::find(p.levers.begin(), p.levers.end(), "scale85") != p.levers.end() &&
                     std::find(p.levers.begin(), p.levers.end(), "scale71") != p.levers.end()));
    }
    REQUIRE_FALSE(plans.empty());
    CHECK(plans.front().reachesByEstimate);
    // Of the plans that reach 15 ms by estimate, the least estimated change: scale85 + volumequarter (exactly 15 ms,
    // key 0.03 + 0.005), ahead of the same plus castercull (one ten-thousandth more) and anything with scale71.
    CHECK(plans.front().levers == std::vector<std::string>{"scale85", "volumequarter"});
    CHECK(plans.front().estimatedCostMs == Approx(15.0));
}

TEST_CASE("search: the choice is the MEASURED combination that reaches with the least change; an estimate never "
          "decides",
          "[unit][live-optimize]") {
    app::OptimizationReport o;
    o.targetMs = 15.0;
    o.baselineMs = 20.0;
    app::AbComparison reachesByEstimateOnly = single("a", 6.0, 0.999).single;
    reachesByEstimateOnly.levers = {"a", "b"};
    reachesByEstimateOnly.timing.optimizedMs = 15.6; // measured: misses
    app::AbComparison reachesUgly = single("c", 6.0, 0.90).single;
    reachesUgly.levers = {"c"};
    app::AbComparison reachesClean = single("d", 5.5, 0.97).single;
    reachesClean.levers = {"d", "e"};
    reachesClean.timing.optimizedMs = 14.5;
    o.measuredCombos = {reachesByEstimateOnly, reachesUgly, reachesClean};
    app::chooseCombination(o);
    CHECK(o.reached);
    CHECK(o.chosen == std::vector<std::string>{"d", "e"});
    // Nothing reaches: say so, name the largest measured saving, choose nothing.
    o.measuredCombos = {reachesByEstimateOnly};
    app::chooseCombination(o);
    CHECK_FALSE(o.reached);
    CHECK(o.chosen.empty());
    CHECK(o.decision.find("NO admitted combination") != std::string::npos);
    // Already under: nothing to do.
    o.alreadyUnder = true;
    app::chooseCombination(o);
    CHECK(o.reached);
    CHECK(o.chosen.empty());
}

TEST_CASE("search: the low-risk set is measured, low risk and hero-safe", "[unit][live-optimize]") {
    std::vector<app::SearchCandidate> c{single("castercull", 0.4, 0.9999), single("volumequarter", 2.0, 0.99),
                                        single("scale85", 3.0, 0.97, true, "medium"),
                                        single("lodbias2", 0.05, 1.0, false)};
    CHECK(app::lowRiskSet(c) == std::vector<std::string>{"castercull", "volumequarter"});
}

TEST_CASE("estimated and measured stay in separate fields of the record", "[unit][live-optimize]") {
    app::OptimizationReport o;
    o.ran = true;
    o.plans = {app::ComboPlan{{"a", "b"}, 4.0, 16.0, 0.02, false}};
    o.measuredCombos = {single("a", 2.0, 0.99).single};
    const nlohmann::json j = app::optimizationJson(o);
    CHECK(j["plans"][0]["basis"].get<std::string>().find("ESTIMATED") == 0);
    CHECK(j["plans"][0].contains("estimatedSavingMs"));
    CHECK_FALSE(j["plans"][0].contains("savingMs"));
    CHECK(j["measuredCombos"][0]["timing"]["basis"].get<std::string>().find("MEASURED") == 0);
    CHECK(j["measuredCombos"][0]["visual"]["basis"].get<std::string>().find("MEASURED") == 0);
}

// ---- ADR-1110..1113: the command lines -----------------------------------------------------------------------------

TEST_CASE("phase 5 flags parse, and are refused where they cannot mean anything", "[unit][live-optimize]") {
    auto a = app::parseLiveProfileArgs({"avgen", "--live-profile", "--optimize", "--hero-policy", "strict",
                                        "--optimize-risk", "low", "--ab-frames", "8", "--compare", "scale85,nodof"});
    REQUIRE(a.error.empty());
    CHECK(a.options.optimize);
    CHECK(a.options.heroPolicy == "strict");
    CHECK(a.options.optimizeRisk == "low");
    CHECK(a.options.abFrames == 8);
    CHECK(a.options.compare == "scale85,nodof");
    CHECK_FALSE(app::parseLiveProfileArgs({"avgen", "--live-profile", "--optimize", "--mode", "live"}).error.empty());
    CHECK_FALSE(app::parseLiveProfileArgs({"avgen", "--live-profile", "--hero-policy", "loose"}).error.empty());
    CHECK_FALSE(app::parseLiveProfileArgs({"avgen", "--live-profile", "--ab-critic"}).error.empty());
}

TEST_CASE("the agent hook asks the child for the search, the comparison and the Critic", "[unit][live-optimize]") {
    ai::ProfileRequest r;
    r.optimize = true;
    r.maxRisk = "low";
    r.heroPolicy = "strict";
    r.critic = true;
    auto argv = app::liveProfileCommand("/bin/avgen", "/tmp/p.json", "/tmp/o.json", r);
    const auto has = [&](const std::string& f, const std::string& v) {
        const auto it = std::find(argv.begin(), argv.end(), f);
        return it != argv.end() && it + 1 != argv.end() && *(it + 1) == v;
    };
    CHECK(std::find(argv.begin(), argv.end(), "--optimize") != argv.end());
    CHECK(has("--optimize-risk", "low"));
    CHECK(has("--hero-policy", "strict"));
    CHECK(std::find(argv.begin(), argv.end(), "--ab-critic") != argv.end());
    ai::ProfileRequest b;
    b.compare = "project";
    argv = app::liveProfileCommand("/bin/avgen", "/tmp/p.json", "/tmp/o.json", b);
    CHECK(has("--compare", "project"));
    CHECK(std::find(argv.begin(), argv.end(), "--optimize") == argv.end());
    CHECK(std::find(argv.begin(), argv.end(), "--ab-critic") == argv.end());
    const auto critic = app::criticSequenceCommand("/c/critic", "", "/tmp/frames", 60.0, "original", "");
    CHECK(std::find(critic.begin(), critic.end(), "--sequence") != critic.end());
    CHECK(std::find(critic.begin(), critic.end(), "--no-autostart") != critic.end());
}
