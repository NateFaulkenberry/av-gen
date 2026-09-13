// Phase D's shading decision: which material tier a drawable shades at, and the one policy object
// that defines what editor/realtime/high/offline mean (ADR-133, ADR-134).
//
// Everything here runs without a device, for the reason Phase C's selectors do: a quality policy
// that can only be checked by looking at a picture is one nobody checks, and the failure this
// guards -- risk 4, "offline silently inherits a realtime compromise" -- is a *deliverable*
// failure, not a preview one. It is stated positively (offline forces the top of every ladder) and
// negatively (a widened realtime band cannot reach offline), because a test that would pass with
// the mechanism removed is not a test of the mechanism.

#include "rendering/material_tier.hpp"
#include "rendering/quality_policy.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace avgen::rendering;

namespace {

ImportanceRecord recordOfRadius(float radius, bool hero = false) {
    ImportanceRecord r;
    r.projectedRadius = radius;
    r.projectedArea = radius * radius * 3.14159265f;
    r.triangles = 100;
    r.pixelsPerTriangle = r.projectedArea / 50.0f;
    r.hero = hero;
    return r;
}

} // namespace

TEST_CASE("material tier assignment follows projected radius", "[unit][quality][materialtier]") {
    MaterialTierPolicy policy;
    policy.enabled = true;
    policy.reducedRadius = 64.0f;
    policy.flatRadius = 12.0f;

    CHECK(MaterialTierSelector::select(recordOfRadius(400.0f), policy) == MaterialTier::Full);
    CHECK(MaterialTierSelector::select(recordOfRadius(64.1f), policy) == MaterialTier::Full);
    CHECK(MaterialTierSelector::select(recordOfRadius(64.0f), policy) == MaterialTier::ReducedLights);
    CHECK(MaterialTierSelector::select(recordOfRadius(12.1f), policy) == MaterialTier::ReducedLights);
    CHECK(MaterialTierSelector::select(recordOfRadius(12.0f), policy) == MaterialTier::Flat);
    CHECK(MaterialTierSelector::select(recordOfRadius(0.5f), policy) == MaterialTier::Flat);
}

TEST_CASE("disabled assignment is the exact pre-Phase-D renderer", "[unit][quality][materialtier]") {
    // The rollback the plan asks the tests to assert (§6): everything Full, whatever its size.
    MaterialTierPolicy policy;
    policy.enabled = false;
    policy.reducedRadius = 1e9f;
    policy.flatRadius = 1e9f;
    CHECK(MaterialTierSelector::select(recordOfRadius(0.1f), policy) == MaterialTier::Full);
}

TEST_CASE("a hero is never shaded below its floor", "[unit][quality][materialtier]") {
    MaterialTierPolicy policy;
    policy.enabled = true;
    policy.heroFloor = MaterialTier::Full;
    // A hero the size of a speck still shades at Full: the saving that costs the shot is not one.
    CHECK(MaterialTierSelector::select(recordOfRadius(1.0f, /*hero=*/true), policy) ==
          MaterialTier::Full);
    CHECK(MaterialTierSelector::select(recordOfRadius(1.0f, /*hero=*/false), policy) ==
          MaterialTier::Flat);

    policy.heroFloor = MaterialTier::ReducedLights;
    CHECK(MaterialTierSelector::select(recordOfRadius(1.0f, /*hero=*/true), policy) ==
          MaterialTier::ReducedLights);
}

TEST_CASE("worstTier bounds the ladder without a second band table",
          "[unit][quality][materialtier]") {
    MaterialTierPolicy policy;
    policy.enabled = true;
    policy.flatRadius = 100.0f;
    policy.worstTier = MaterialTier::ReducedLights;
    CHECK(MaterialTierSelector::select(recordOfRadius(1.0f), policy) == MaterialTier::ReducedLights);
}

TEST_CASE("the batch overload agrees with the single one", "[unit][quality][materialtier]") {
    const MaterialTierPolicy policy = MaterialTierPolicy::forTier(QualityTier::Realtime);
    const ImportanceRecord records[] = {recordOfRadius(500.0f), recordOfRadius(30.0f),
                                        recordOfRadius(3.0f)};
    MaterialTier out[3] = {};
    MaterialTierSelector::select(records, policy, out);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(out[i] == MaterialTierSelector::select(records[i], policy));
    }
    // And the ladder is monotone in size: bigger is never cheaper.
    CHECK(static_cast<int>(out[0]) <= static_cast<int>(out[1]));
    CHECK(static_cast<int>(out[1]) <= static_cast<int>(out[2]));
}

TEST_CASE("the offline policy is uncompromised (§5.9, risk 4)", "[unit][quality][policy]") {
    const QualityPolicy offline = QualityPolicy::forTier(QualityTier::Offline);
    REQUIRE(offline.assertOfflineIsUncompromised());

    // Positively: every ladder returns its top answer, whatever the bands say.
    CHECK(MaterialTierSelector::select(recordOfRadius(0.01f), offline.materialTier) ==
          MaterialTier::Full);
    CHECK(offline.settings.forcedMaterialTier == MaterialTier::Full);
    CHECK_FALSE(offline.settings.materialTiers);
    CHECK(offline.settings.renderScale == 1.0f);
    CHECK(offline.renderSize(1920, 1080).width == 1920);
    CHECK(offline.renderSize(1920, 1080).height == 1080);

    // Negatively: widening a band cannot reach an offline render, because the force flag and not
    // the band is what decides. A test that only checked the bands would pass with the force
    // removed, and the force is the whole mechanism.
    QualityPolicy widened = offline;
    widened.materialTier.reducedRadius = 1e9f;
    widened.materialTier.flatRadius = 1e9f;
    widened.materialTier.enabled = true;
    CHECK(MaterialTierSelector::select(recordOfRadius(0.01f), widened.materialTier) ==
          MaterialTier::Full);
}

TEST_CASE("every tier's policy is internally consistent", "[unit][quality][policy]") {
    for (const QualityTier tier : {QualityTier::Preview, QualityTier::Realtime, QualityTier::High,
                                   QualityTier::Offline}) {
        const QualityPolicy p = QualityPolicy::forTier(tier);
        INFO("tier " << qualityTierName(tier));
        CHECK(p.tier == tier);
        CHECK(p.assertOfflineIsUncompromised());
        // The two statements of "is assignment on" agree. They exist separately because the shader
        // reads one and the selector the other; forTier is what reconciles them, and a drift here
        // is exactly the "why did this look different in a render?" question §5.6 is about.
        CHECK(p.settings.materialTiers == (p.materialTier.enabled && !p.materialTier.forceTopTier));
        if (!p.settings.materialTiers) {
            CHECK(p.settings.forcedMaterialTier == MaterialTier::Full);
        }
        // Bands are ordered, and the light budgets are monotone down the ladder.
        CHECK(p.materialTier.flatRadius <= p.materialTier.reducedRadius);
        CHECK(p.settings.localLightBudget(MaterialTier::Flat) <=
              p.settings.localLightBudget(MaterialTier::ReducedLights));
        CHECK(p.settings.localLightBudget(MaterialTier::ReducedLights) <=
              p.settings.localLightBudget(MaterialTier::Full));
        // §34: a render scale is fixed and positive. Nothing here is adaptive.
        CHECK(p.settings.renderScale > 0.0f);
    }
}

TEST_CASE("render scale is a tier parameter with one rounding rule", "[unit][quality][policy]") {
    QualityPolicy p = QualityPolicy::forTier(QualityTier::Realtime);
    CHECK(p.renderSize(1280, 800).width == 1280);

    p.settings.renderScale = 0.75f;
    CHECK(p.renderSize(1280, 800).width == 960);
    CHECK(p.renderSize(1280, 800).height == 600);

    // Never zero: a one-pixel target is degenerate but renderable; a zero-pixel one is a crash.
    p.settings.renderScale = 0.0001f;
    CHECK(p.renderSize(1280, 800).width >= 1);
    CHECK(p.renderSize(1280, 800).height >= 1);

    // Above 1 is supersampling and is allowed -- an offline render may legitimately ask for it.
    p.settings.renderScale = 2.0f;
    CHECK(p.renderSize(1280, 800).width == 2560);
}
