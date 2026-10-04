// ADR-1094..1106: the live optimizer's GPU-free parts -- profiles, ceilings, the priority ladder, the minimum level,
// recovery, importance, and the profile hook's command line.

#include "app/interactive_resolution.hpp"
#include "app/live_profile_hook.hpp"
#include "rendering/quality_policy.hpp"
#include "rendering/render_quality.hpp"
#include "scene/importance.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace avgen;
using app::LiveQualityLevel;
using app::QualityProfile;

namespace {
app::InteractiveResolutionSettings quick() {
    app::InteractiveResolutionSettings s;
    s.enabled = true;
    s.targetFps = 60.0;
    s.budgetMs = app::liveBudget(60.0).qualityBudgetMs;
    s.dwellFrames = 8;
    s.windowFrames = 5;
    s.raiseHoldFrames = 24;
    return s;
}
void feed(app::InteractiveResolution& c, int n, double gpu) {
    for (int i = 0; i < n; ++i) {
        c.note(gpu, gpu + 1.0);
    }
}
} // namespace

TEST_CASE("profiles only lower the tier, QUALITY is the tier, PERFORMANCE lowers every lever", "[unit][live-optimizer]") {
    const rendering::QualitySettings tier = rendering::QualitySettings::forTier(rendering::QualityTier::Realtime);
    const auto q = app::applyQualityProfile(tier, QualityProfile::Quality);
    CHECK(q.renderScale == tier.renderScale);
    CHECK(q.volumeResolutionScale == tier.volumeResolutionScale);
    CHECK(q.lodBias == tier.lodBias);
    CHECK(q.shadowCasterMinPixels == tier.shadowCasterMinPixels);
    const auto b = app::applyQualityProfile(tier, QualityProfile::Balanced);
    CHECK(b.renderScale == 1.0f);
    CHECK(b.shadowCasterMinPixels > 0.0f);
    CHECK(b.particleCullDistance > 0.0f);
    const auto p = app::applyQualityProfile(tier, QualityProfile::Performance);
    CHECK(p.renderScale < 1.0f);
    CHECK(p.volumeResolutionScale <= 0.25f);
    CHECK(p.postEffectQuality < 1.0f);
    CHECK(p.lodBias > b.lodBias);
    CHECK(p.drawDistanceScale < 1.0f);
    CHECK(p.cascadeCount <= 2u);
    CHECK(p.particleSpawnScale < 1.0f);
    // Never raises: a base already lower than the profile keeps its own value.
    rendering::QualitySettings low = tier;
    low.renderScale = 0.5f;
    low.volumeResolutionScale = 0.125f;
    low.lodBias = 4.0f;
    const auto pl = app::applyQualityProfile(low, QualityProfile::Performance);
    CHECK(pl.renderScale == 0.5f);
    CHECK(pl.volumeResolutionScale == 0.125f);
    CHECK(pl.lodBias == 4.0f);
    for (const auto token : {"quality", "balanced", "performance"}) {
        const auto parsed = app::qualityProfileFromToken(token);
        REQUIRE(parsed);
        CHECK(app::qualityProfileToken(*parsed) == token);
    }
    CHECK_FALSE(app::qualityProfileFromToken("ultra"));
}

TEST_CASE("the ladder's top three levels are unchanged by the background levers", "[unit][live-optimizer]") {
    for (const auto strategy : {app::LiveQualityStrategy::Balanced, app::LiveQualityStrategy::ResolutionFirst,
                                app::LiveQualityStrategy::EffectsFirst}) {
        const auto& ladder = app::liveQualityLadder(strategy);
        for (std::size_t k = 0; k < 3; ++k) {
            CHECK(ladder[k].lodBias == 1.0f);
            CHECK(ladder[k].shadowCasterMinPixels == 0.0f);
            CHECK(ladder[k].particleCullDistance == 0.0f);
        }
        CHECK(ladder[3].shadowCasterMinPixels > 0.0f);
        CHECK(ladder[4].lodBias >= ladder[3].lodBias);
    }
}

TEST_CASE("the Stage 2 levers are neutral offline and the policy says so", "[unit][live-optimizer]") {
    rendering::QualityPolicy policy = rendering::QualityPolicy::forTier(rendering::QualityTier::Offline);
    CHECK(policy.assertOfflineIsUncompromised());
    policy.settings.lodBias = 2.0f;
    CHECK_FALSE(policy.assertOfflineIsUncompromised());
    policy.settings.lodBias = 1.0f;
    policy.settings.materialProgramsOff = true;
    CHECK_FALSE(policy.assertOfflineIsUncompromised());
}

TEST_CASE("an Optimize lever becomes a ceiling, round-trips as JSON, and a diagnostic arm does not", "[unit][live-optimizer]") {
    app::LiveQualityRung c{};
    CHECK(app::applyLeverToCeiling(c, "volumequarter"));
    CHECK(app::applyLeverToCeiling(c, "castercull"));
    CHECK(app::applyLeverToCeiling(c, "posttaps"));
    CHECK(app::applyLeverToCeiling(c, "particlelod"));
    CHECK_FALSE(app::applyLeverToCeiling(c, "noprograms"));
    CHECK_FALSE(app::applyLeverToCeiling(c, "shadows"));
    CHECK(c.volumeResolutionScale == 0.25f);
    CHECK(c.shadowCasterMinPixels == 24.0f);
    const std::string text = app::ceilingJsonText(c);
    CHECK(text.find("renderScale") == std::string::npos); // only what differs from neutral
    std::string error;
    const auto back = app::ceilingFromJsonText(text, error);
    REQUIRE(back);
    CHECK(error.empty());
    CHECK(*back == c);
    CHECK_FALSE(app::ceilingFromJsonText("{\"bogus\": 1}", error));
    CHECK_FALSE(error.empty());
    // Applied to settings, a ceiling only lowers.
    const rendering::QualitySettings tier = rendering::QualitySettings::forTier(rendering::QualityTier::Realtime);
    const auto q = app::applyCeiling(tier, c);
    CHECK(q.volumeResolutionScale == 0.25f);
    CHECK(q.shadowCasterMinPixels == 24.0f);
    CHECK(q.renderScale == tier.renderScale);
}

TEST_CASE("a degradation priority builds a ladder that gives groups up in order", "[unit][live-optimizer]") {
    std::string error;
    const auto ladder = app::ladderFromPriority({"particles", "volumes", "shadows", "post"}, error);
    REQUIRE(ladder);
    CHECK(error.empty());
    const auto& l = *ladder;
    CHECK(l[0] == app::LiveQualityRung{}); // Ultra gives nothing up
    CHECK(l[1].particleSpawnScale < 1.0f);     // High: the first group, a little
    CHECK(l[1].volumeResolutionScale == 1.0f);
    CHECK(l[2].volumeResolutionScale < 1.0f);  // Medium: the second group too
    CHECK(l[2].particleSpawnScale < l[1].particleSpawnScale);
    // Resolution was not named: the picture stays sharp until the last level, and only a little there.
    for (std::size_t k = 0; k < 4; ++k) {
        CHECK(l[k].renderScale == 1.0f);
    }
    CHECK(l[4].renderScale == 0.71f);
    CHECK_FALSE(app::ladderFromPriority({"particles", "particles"}, error));
    CHECK_FALSE(app::ladderFromPriority({"glitter"}, error));
    CHECK_FALSE(app::ladderFromPriority({}, error));
    // The controller uses it in place of the strategy's table.
    app::InteractiveResolution c;
    auto s = quick();
    s.ladder = ladder;
    c.configure(s);
    CHECK(&c.ladder() != &app::liveQualityLadder(s.strategy));
    CHECK(c.ladder()[2] == l[2]);
}

TEST_CASE("the controller never goes below the project's minimum, and says when that is not enough",
          "[unit][live-optimizer]") {
    app::InteractiveResolution c;
    auto s = quick();
    s.lowestLevel = static_cast<std::size_t>(LiveQualityLevel::Medium);
    c.configure(s);
    feed(c, 600, 60.0);
    CHECK(c.rung() == static_cast<std::size_t>(LiveQualityLevel::Medium));
    CHECK(c.unsustainable());
    feed(c, 60, 5.0); // fits now
    CHECK_FALSE(c.unsustainable());
    // Without a minimum the same frames go to the bottom and only then say so.
    app::InteractiveResolution d;
    d.configure(quick());
    feed(d, 600, 60.0);
    CHECK(d.rung() == app::kLiveQualityLevels - 1);
    CHECK(d.unsustainable());
}

TEST_CASE("a raise the next window cannot hold is reverted, and that raise waits longer next time",
          "[unit][live-optimizer]") {
    app::InteractiveResolution c;
    const auto s = quick();
    c.configure(s);
    feed(c, 300, 30.0);
    REQUIRE(c.rung() > 0);
    const std::size_t low = c.rung();
    // Comfortable for long enough to climb one level...
    int frames = 0;
    while (c.rung() == low && frames < 2000) {
        c.note(3.0, 4.0);
        ++frames;
    }
    REQUIRE(c.rung() == low - 1);
    const int firstHold = frames;
    // ...and the level above is over budget: the first decision puts it back.
    feed(c, s.dwellFrames + 1, 20.0);
    CHECK(c.rung() == low);
    CHECK(c.stats().reverts == 1);
    // The same raise now needs twice the comfortable frames. (Cheaper frames than the first climb: the failed raise
    // also taught the controller that level costs more, so 3 ms no longer predicts a fit.)
    frames = 0;
    while (c.rung() == low && frames < 4000) {
        c.note(1.0, 2.0);
        ++frames;
    }
    CHECK(c.rung() == low - 1);
    CHECK(frames > firstHold + s.raiseHoldFrames / 2);
}

TEST_CASE("importance names round-trip and a hero is exempt from every lever", "[unit][live-optimizer]") {
    for (const auto i : {scene::Importance::Hero, scene::Importance::Foreground, scene::Importance::Normal,
                         scene::Importance::Background, scene::Importance::Ambient}) {
        scene::Importance back = scene::Importance::Normal;
        REQUIRE(scene::importanceFromName(scene::importanceName(i), back));
        CHECK(back == i);
    }
    scene::Importance out = scene::Importance::Normal;
    CHECK_FALSE(scene::importanceFromName("vip", out));
    CHECK(scene::importanceLeverWeight(scene::Importance::Hero) == 0.0f);
    CHECK(scene::importanceLeverWeight(scene::Importance::Normal) == 1.0f);
    CHECK(scene::importanceLeverWeight(scene::Importance::Ambient) > scene::importanceLeverWeight(scene::Importance::Background));
}

TEST_CASE("the profile tool's child command line carries the request", "[unit][live-optimizer]") {
    ai::ProfileRequest r;
    r.mode = "live";
    r.targetFps = 90.0;
    r.width = 1280;
    r.height = 720;
    r.start = 60.0;
    r.quality = "balanced";
    r.deep = true;
    r.verifyCandidates = 2;
    const auto argv = app::liveProfileCommand("/bin/avgen", "/tmp/p.json", "/tmp/o.json", r);
    const auto has = [&](const std::string& a, const std::string& b) {
        const auto it = std::find(argv.begin(), argv.end(), a);
        return it != argv.end() && it + 1 != argv.end() && *(it + 1) == b;
    };
    CHECK(argv.front() == "/bin/avgen");
    CHECK(std::find(argv.begin(), argv.end(), "--live-profile") != argv.end());
    CHECK(has("--mode", "live"));
    CHECK(has("--target-fps", "90"));
    CHECK(has("--size", "1280x720"));
    CHECK(has("--start", "60"));
    CHECK(has("--quality", "balanced"));
    CHECK(has("--verify-candidates", "2"));
    CHECK(has("--json", "/tmp/o.json"));
    CHECK(std::find(argv.begin(), argv.end(), "--deep") != argv.end());
    CHECK(std::find(argv.begin(), argv.end(), "--no-text") != argv.end());
}

TEST_CASE("at the bottom of the ladder the unsustainable verdict is judged once per dwell, not every frame",
          "[unit][live-optimizer]") {
    // The Live panel flickered: at the bottom, a median sitting on the budget flipped the verdict every frame.
    app::InteractiveResolution c;
    const auto s = quick();
    c.configure(s);
    feed(c, 600, 60.0);
    REQUIRE(c.rung() == app::kLiveQualityLevels - 1);
    int flips = 0;
    bool last = c.unsustainable();
    for (int i = 0; i < 80; ++i) {
        c.note(s.budgetMs + ((i % 2) == 0 ? 0.4 : -0.4), s.budgetMs + 1.0);
        if (c.unsustainable() != last) {
            ++flips;
            last = c.unsustainable();
        }
    }
    CHECK(flips <= 80 / s.dwellFrames + 1);
}
