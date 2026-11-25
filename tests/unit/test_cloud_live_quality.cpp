// ADR-2134: the cloud march follows the live quality ladder. ADR-2135: a ladder at its lowest level and still over the
// target says so once.
//
// The cloud Environment (ADR-2100) read its steps, light taps, resolution and erosion octaves straight from the scene,
// so every live level cost the same march (Neon Dreams at 5K: ~180 ms at Emergency). These cases pin the levers'
// arithmetic, that no tier and no render can move them, and the ladders' rows.

#include "app/interactive_resolution.hpp"
#include "rendering/cloud_march_quality.hpp"
#include "rendering/quality_policy.hpp"
#include "rendering/render_quality.hpp"
#include "scene/clouds.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace avgen;
using app::LiveQualityStrategy;

namespace {

// Neon Dreams' offline cloud preset (examples/neon-dreams/build.py CLOUD_QUALITY["offline"])
scene::Clouds neonDreamsOffline() {
    scene::Clouds c;
    c.steps = 128;
    c.lightSteps = 6;
    c.resolution = 0.5f;
    c.stepGrowth = 0.02f;
    c.minStep = 3.0f;
    c.erosionOctaves = 3;
    return c;
}

bool same(const rendering::CloudMarchQuality& m, const scene::Clouds& c) {
    // exactly the authored numbers, not ones recomputed through a lever at 1
    return m.resolution == std::clamp(c.resolution, 0.25f, 1.0f) && m.steps == c.steps && m.lightSteps == c.lightSteps &&
           m.stepGrowth == c.stepGrowth && m.erosionOctaves == c.erosionOctaves;
}

} // namespace

TEST_CASE("the cloud march is exactly as authored at every tier and with no settings", "[unit][clouds][adr2134]") {
    const scene::Clouds c = neonDreamsOffline();
    CHECK(same(rendering::cloudMarchQuality(c, nullptr), c));
    for (const auto tier : {rendering::QualityTier::Preview, rendering::QualityTier::Realtime,
                            rendering::QualityTier::High, rendering::QualityTier::Offline}) {
        const auto q = rendering::QualitySettings::forTier(tier);
        CHECK(q.cloudResolutionScale == 1.0f);
        CHECK(q.cloudStepScale == 1.0f);
        CHECK(q.cloudLightStepScale == 1.0f);
        CHECK(q.cloudErosionOctaves == 3u);
        CHECK(same(rendering::cloudMarchQuality(c, &q), c));
    }
    // an odd authored growth survives bit for bit (no pow(1 + g, 1) - 1 round trip)
    scene::Clouds odd = c;
    odd.stepGrowth = 0.0337f;
    const auto offline = rendering::QualitySettings::forTier(rendering::QualityTier::Offline);
    CHECK(rendering::cloudMarchQuality(odd, &offline).stepGrowth == 0.0337f);
}

TEST_CASE("the cloud levers lower the march and the fewer steps still reach as far", "[unit][clouds][adr2134]") {
    const scene::Clouds c = neonDreamsOffline();
    rendering::QualitySettings q;
    q.cloudResolutionScale = 0.6f;
    q.cloudStepScale = 0.25f;
    q.cloudLightStepScale = 0.34f;
    q.cloudErosionOctaves = 2;
    const rendering::CloudMarchQuality m = rendering::cloudMarchQuality(c, &q);
    CHECK(std::abs(m.resolution - 0.3f) < 1e-6f);
    CHECK(m.steps == 32);
    CHECK(m.lightSteps == 2);
    CHECK(m.erosionOctaves == 2);
    // the v2 march's reach is (1 + growth)^steps times where it starts: the same with 32 steps as with 128
    const double before = std::pow(1.0 + c.stepGrowth, c.steps);
    const double after = std::pow(1.0 + static_cast<double>(m.stepGrowth), m.steps);
    CHECK(std::abs(after / before - 1.0) < 1e-4);

    // the even march (ADR-2109) divides its span by the count, so its growth is not touched
    scene::Clouds even = c;
    even.evenSteps = true;
    CHECK(rendering::cloudMarchQuality(even, &q).stepGrowth == c.stepGrowth);

    // floors: never under 8 view steps, a light march kept at a tap, the analytic shadow (0 taps) left analytic,
    // at least one octave, and the resolution never under an eighth
    scene::Clouds small = c;
    small.steps = 12;
    small.lightSteps = 1;
    small.resolution = 0.25f;
    q.cloudResolutionScale = 0.1f;
    q.cloudLightStepScale = 0.1f;
    q.cloudErosionOctaves = 0;
    const auto s = rendering::cloudMarchQuality(small, &q);
    CHECK(s.steps == 8);
    CHECK(s.lightSteps == 1);
    CHECK(s.resolution >= 0.125f);
    CHECK(s.erosionOctaves == 1);
    small.lightSteps = 0;
    CHECK(rendering::cloudMarchQuality(small, &q).lightSteps == 0);
    // an authored count already at a lever's floor is not raised
    small.steps = 8;
    CHECK(rendering::cloudMarchQuality(small, &q).steps == 8);
    small.erosionOctaves = 1;
    q.cloudErosionOctaves = 2;
    CHECK(rendering::cloudMarchQuality(small, &q).erosionOctaves == 1);
}

TEST_CASE("an offline render may not carry a cloud lever", "[unit][clouds][adr2134]") {
    auto offline = rendering::QualityPolicy::forTier(rendering::QualityTier::Offline);
    REQUIRE(offline.assertOfflineIsUncompromised());
    const auto fails = [&](auto&& set) {
        auto p = offline;
        set(p.settings);
        return !p.assertOfflineIsUncompromised();
    };
    CHECK(fails([](rendering::QualitySettings& q) { q.cloudResolutionScale = 0.8f; }));
    CHECK(fails([](rendering::QualitySettings& q) { q.cloudStepScale = 0.5f; }));
    CHECK(fails([](rendering::QualitySettings& q) { q.cloudLightStepScale = 0.5f; }));
    CHECK(fails([](rendering::QualitySettings& q) { q.cloudErosionOctaves = 2; }));
}

TEST_CASE("every ladder thins the cloud march by level, Ultra as authored", "[unit][resolution][clouds][adr2134]") {
    for (const auto strategy :
         {LiveQualityStrategy::Balanced, LiveQualityStrategy::ResolutionFirst, LiveQualityStrategy::EffectsFirst}) {
        const auto& ladder = app::liveQualityLadder(strategy);
        CHECK(ladder[0].cloudResolutionScale == 1.0f);
        CHECK(ladder[0].cloudStepScale == 1.0f);
        CHECK(ladder[0].cloudLightStepScale == 1.0f);
        CHECK(ladder[0].cloudErosionOctaves == 3u);
        for (std::size_t k = 1; k < ladder.size(); ++k) {
            CHECK(ladder[k].cloudResolutionScale <= ladder[k - 1].cloudResolutionScale);
            CHECK(ladder[k].cloudStepScale <= ladder[k - 1].cloudStepScale);
            CHECK(ladder[k].cloudLightStepScale <= ladder[k - 1].cloudLightStepScale);
            CHECK(ladder[k].cloudErosionOctaves <= ladder[k - 1].cloudErosionOctaves);
        }
        // the lowest level is cheaper than Neon Dreams' own `--quality live` preset (0.4 of 0.5's resolution, 40 of
        // 128 steps, 3 of 6 light taps, 2 octaves) on every axis
        const auto& last = ladder.back();
        CHECK(last.cloudResolutionScale < 0.8f);
        CHECK(last.cloudStepScale < 40.0f / 128.0f);
        CHECK(last.cloudLightStepScale < 0.5f);
        CHECK(last.cloudErosionOctaves <= 2u);
    }
    // a rung is a ceiling: it never raises what the base asked for
    const auto& balanced = app::liveQualityLadder(LiveQualityStrategy::Balanced);
    rendering::QualitySettings base;
    CHECK(app::applyLiveRung(base, base, balanced[0], 0.38f).cloudStepScale == 1.0f);
    CHECK(app::applyLiveRung(base, base, balanced[4], 0.38f).cloudStepScale == balanced[4].cloudStepScale);
    base.cloudStepScale = 0.1f;
    base.cloudErosionOctaves = 1;
    CHECK(app::applyLiveRung(base, base, balanced[2], 0.38f).cloudStepScale == 0.1f);
    CHECK(app::applyLiveRung(base, base, balanced[2], 0.38f).cloudErosionOctaves == 1u);
    // the priority ladder gives the clouds up with the volumes group
    std::string error;
    const auto prioritised = app::ladderFromPriority({"volumes"}, error);
    REQUIRE(prioritised);
    CHECK((*prioritised)[0].cloudStepScale == 1.0f);
    CHECK((*prioritised)[1].cloudStepScale < 1.0f);
    CHECK((*prioritised)[2].cloudStepScale < (*prioritised)[1].cloudStepScale);
}

TEST_CASE("the cloud levers are A/B levers and project ceilings that round-trip", "[unit][resolution][clouds][adr2134]") {
    app::LiveQualityRung c;
    for (const char* lever : {"cloudres", "cloudsteps", "cloudlight", "cloudoctaves"}) {
        CHECK(app::applyLeverToCeiling(c, lever));
    }
    CHECK(c.cloudResolutionScale == 0.6f);
    CHECK(c.cloudStepScale == 0.5f);
    CHECK(c.cloudLightStepScale == 0.5f);
    CHECK(c.cloudErosionOctaves == 2u);
    std::string error;
    const auto back = app::ceilingFromJsonText(app::ceilingJsonText(c), error);
    REQUIRE(back);
    CHECK(*back == c);
    const auto q = app::applyCeiling(rendering::QualitySettings{}, *back);
    CHECK(q.cloudStepScale == 0.5f);
    CHECK(q.cloudErosionOctaves == 2u);
}

TEST_CASE("a ladder at its lowest level and still over budget says so once", "[unit][resolution][adr2135]") {
    app::InteractiveResolutionSettings s;
    s.enabled = true;
    s.budgetMs = 10.0;
    s.windowFrames = 4;
    s.dwellFrames = 4;
    app::InteractiveResolution r;
    r.configure(s);
    CHECK_FALSE(r.takeUnsustainableNotice());
    int notices = 0;
    for (int i = 0; i < 400; ++i) {
        (void)r.note(90.0, 90.0); // nine times the budget, as Neon Dreams' clouds were
        notices += r.takeUnsustainableNotice() ? 1 : 0;
    }
    CHECK(r.rung() == app::kLiveQualityLevels - 1);
    CHECK(r.unsustainable());
    CHECK(notices == 1);
    // it clears and comes back: still said only once
    for (int i = 0; i < 40; ++i) {
        (void)r.note(5.0, 5.0);
        notices += r.takeUnsustainableNotice() ? 1 : 0;
    }
    for (int i = 0; i < 400; ++i) {
        (void)r.note(90.0, 90.0);
        notices += r.takeUnsustainableNotice() ? 1 : 0;
    }
    CHECK(notices == 1);
    // a new ladder is a new run: it may say it again
    s.strategy = LiveQualityStrategy::ResolutionFirst;
    r.configure(s);
    for (int i = 0; i < 400; ++i) {
        (void)r.note(90.0, 90.0);
        notices += r.takeUnsustainableNotice() ? 1 : 0;
    }
    CHECK(notices == 2);
}
