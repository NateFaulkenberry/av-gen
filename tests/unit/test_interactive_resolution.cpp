// The live quality controller's control law (§15-§17, ADR-1080..1085).
//
// This is the half of the feature a benchmark cannot check. A benchmark can say "the frame got
// faster"; it cannot say the ladder settles rather than oscillating, that it declines to act when
// the main thread is what the frame is waiting for, or that it never moves at all while the budget
// is being met. Those are properties of the decision, and the decision is deliberately a pure
// function of the numbers handed to `note()`.
//
// ADR-182 throughout: every "it did not move" assertion is paired with an arm over the same
// controller in which it does move, so a controller wired to return rung 0 unconditionally fails
// this file rather than passing it.

#include "app/interactive_resolution.hpp"
#include "rendering/quality_policy.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace avgen;
using app::LiveQualityLevel;
using app::LiveQualityStrategy;

namespace {

app::InteractiveResolutionSettings fast(LiveQualityStrategy strategy = LiveQualityStrategy::Balanced,
                                        double targetFps = 60.0) {
    app::InteractiveResolutionSettings s;
    s.enabled = true;
    s.targetFps = targetFps;
    s.budgetMs = app::liveBudget(targetFps).qualityBudgetMs;
    s.strategy = strategy;
    // Short enough that a test does not have to feed thousands of frames, long enough that the
    // window still fills before a decision is taken.
    s.dwellFrames = 8;
    s.windowFrames = 5;
    s.raiseHoldFrames = 24;
    return s;
}

std::size_t feed(app::InteractiveResolution& c, int n, double gpuMs, double wallMs) {
    for (int i = 0; i < n; ++i) {
        c.note(gpuMs, wallMs);
    }
    return c.rung();
}

// A scene's GPU cost under a rung, in the shape the projection report measured (§D): a fixed term,
// a per-megapixel term for the scene pass at the output size, and the volume march, motion blur,
// depth of field and shadows as separate terms that each lever removes or shrinks.
struct SceneCost {
    double fixedMs = 2.0;        // vertex work, culling: no lever touches it
    double pixelMsAtFull = 30.0; // the scene pass / SDF march at scale 1.0
    double volumeMsAtFull = 0.0; // the march at the realtime tier's half resolution, scale 1.0
    double motionBlurMsAtFull = 0.0;
    double dofMsAtFull = 0.0;
    double shadowMs = 0.0;       // three cascades in a 2048 atlas; fixed with resolution
};

double cost(const SceneCost& c, const app::LiveQualityRung& rung, float scaleFloor = app::kLiveScaleFloorMin) {
    const double s = app::effectiveRenderScale(rung, scaleFloor);
    const double px = s * s;
    const double volumeRes = std::min(0.5f, rung.volumeResolutionScale) / 0.5;
    const double volume = c.volumeMsAtFull * px * volumeRes * volumeRes * std::min(1.0f, rung.volumeStepScale);
    const double shadows = c.shadowMs * (std::min(3u, rung.cascadeCount) / 3.0) *
                           (rung.shadowResolution < 2048 ? 0.6 : 1.0);
    return c.fixedMs + c.pixelMsAtFull * px + volume + (rung.motionBlur ? c.motionBlurMsAtFull * px : 0.0) +
           (rung.depthOfField ? c.dofMsAtFull * px : 0.0) + shadows;
}

// Close the loop: the cost the controller is told about is the cost of the rung it chose.
void run(app::InteractiveResolution& c, const SceneCost& scene, int frames) {
    for (int i = 0; i < frames; ++i) {
        const double gpu = cost(scene, c.current(), c.settings().scaleFloor);
        c.note(gpu, gpu + 1.0);
    }
}

// The measured scenes at 1920x1080 (evidence-live-projection-2026-10-02.md §B/§D), roughly.
constexpr SceneCost kLiminal{1.7, 36.0, 13.8, 5.1, 0.0, 0.0};
constexpr SceneCost kGlowmere{5.8, 25.5, 8.6, 4.4, 1.4, 0.6};
// Sonic: 17 ms at 1.0 and about 9.2 at 0.5 with everything on (§B), shadows and vertex work fixed.
constexpr SceneCost kSonic{5.0, 9.0, 0.0, 0.0, 0.9, 2.3};

} // namespace

// ---- the budget (ADR-1080) ---------------------------------------------------------------------------

TEST_CASE("the live budget is the target's frame less 12 percent, in one place", "[unit][resolution][live-quality]") {
    const app::LiveBudget b60 = app::liveBudget(60);
    CHECK(b60.targetFps == 60.0);
    CHECK(b60.targetFrameMs == Catch::Approx(16.667).margin(1e-3));
    CHECK(b60.qualityBudgetMs == Catch::Approx(14.667).margin(1e-3));
    CHECK(app::liveBudget(90).qualityBudgetMs == Catch::Approx(9.778).margin(1e-3));
    CHECK(app::liveBudget(120).qualityBudgetMs == Catch::Approx(7.333).margin(1e-3));
    // The brief's "approximately 12%": every target keeps the same share for what the GPU timer misses.
    for (const int fps : app::kLiveTargetChoices) {
        const app::LiveBudget b = app::liveBudget(fps);
        CHECK(b.qualityBudgetMs / b.targetFrameMs == Catch::Approx(1.0 - app::kLiveBudgetHeadroom));
    }
    // Out of range: clamped, never a zero or infinite budget.
    CHECK(app::liveBudget(0).targetFps == app::kLiveTargetFpsMin);
    CHECK(app::liveBudget(100000).targetFps == app::kLiveTargetFpsMax);
}

// ---- the ladders (ADR-1083, ADR-1084) -----------------------------------------------------------------

TEST_CASE("every ladder only ever gives quality up, one deterministic bundle per level",
          "[unit][resolution][live-quality]") {
    for (const auto strategy : {LiveQualityStrategy::Balanced, LiveQualityStrategy::ResolutionFirst,
                                LiveQualityStrategy::EffectsFirst}) {
        const app::LiveQualityLadder& ladder = app::liveQualityLadder(strategy);
        INFO("strategy " << app::liveQualityStrategyToken(strategy));
        CHECK(ladder[0].level == LiveQualityLevel::Ultra);
        CHECK(ladder[0].renderScale == 1.0f);
        CHECK(ladder[0].motionBlur);
        CHECK(ladder[0].depthOfField);
        CHECK(ladder.back().level == LiveQualityLevel::Emergency);
        for (std::size_t k = 1; k < ladder.size(); ++k) {
            const auto& a = ladder[k - 1];
            const auto& b = ladder[k];
            INFO("rung " << k);
            CHECK(static_cast<std::size_t>(b.level) == k);
            CHECK(b.renderScale <= a.renderScale);
            CHECK(b.volumeResolutionScale <= a.volumeResolutionScale);
            CHECK(b.volumeStepScale <= a.volumeStepScale);
            CHECK((!b.motionBlur || a.motionBlur));
            CHECK((!b.depthOfField || a.depthOfField));
            CHECK(b.cascadeCount <= a.cascadeCount);
            CHECK(b.shadowResolution <= a.shadowResolution);
            // Each step gives something up: a rung identical to the one above is a decision that
            // buys nothing and still resets the screen history.
            const bool same = b.renderScale == a.renderScale && b.volumeResolutionScale == a.volumeResolutionScale &&
                              b.volumeStepScale == a.volumeStepScale && b.motionBlur == a.motionBlur &&
                              b.depthOfField == a.depthOfField && b.cascadeCount == a.cascadeCount &&
                              b.shadowResolution == a.shadowResolution &&
                              b.reducedShadowFiltering == a.reducedShadowFiltering;
            CHECK_FALSE(same);
        }
        // Heavy scenes can keep degrading after resolution reaches 0.5 (§20): Emergency gives up
        // more than Low in every ladder.
        CHECK(ladder[4].volumeStepScale < ladder[3].volumeStepScale);
    }
    // The orders differ, which is the point of the hint: resolution first goes lowest on scale and
    // keeps its effects longest; effects first keeps its sharpness longest.
    CHECK(app::liveQualityLadder(LiveQualityStrategy::ResolutionFirst)[4].renderScale <
          app::liveQualityLadder(LiveQualityStrategy::Balanced)[4].renderScale);
    CHECK(app::liveQualityLadder(LiveQualityStrategy::EffectsFirst)[1].renderScale == 1.0f);
    CHECK_FALSE(app::liveQualityLadder(LiveQualityStrategy::EffectsFirst)[1].depthOfField);
    CHECK(app::liveQualityLadder(LiveQualityStrategy::ResolutionFirst)[2].motionBlur);
}

TEST_CASE("a rung is a set of ceilings on the live tier, and Ultra is the tier exactly",
          "[unit][resolution][live-quality]") {
    const auto base = rendering::QualitySettings::forTier(rendering::QualityTier::Realtime);
    rendering::QualitySettings current = base;
    current.antialiasFloor = rendering::kLiveAntialiasFloor; // owned by someone else; must survive

    const auto& balanced = app::liveQualityLadder(LiveQualityStrategy::Balanced);
    const auto ultra = app::applyLiveRung(current, base, balanced[0], app::kLiveScaleFloorMin);
    CHECK(ultra.renderScale == 1.0f);
    CHECK(ultra.volumeResolutionScale == base.volumeResolutionScale);
    CHECK(ultra.volumeStepScale == base.volumeStepScale);
    CHECK(ultra.cascadeCount == base.cascadeCount);
    CHECK(ultra.shadowResolution == base.shadowResolution);
    CHECK(ultra.softShadows == base.softShadows);
    CHECK(ultra.motionBlur);
    CHECK(ultra.depthOfField);
    CHECK(ultra.antialiasFloor == rendering::kLiveAntialiasFloor);

    const auto low = app::applyLiveRung(current, base, balanced[3], app::kLiveScaleFloorMin);
    CHECK(low.renderScale == 0.5f);
    CHECK(low.volumeResolutionScale == 0.25f);
    CHECK(low.volumeStepScale == 0.5f);
    CHECK_FALSE(low.motionBlur);
    CHECK_FALSE(low.depthOfField);
    CHECK(low.cascadeCount == 2u);
    CHECK(low.antialiasFloor == rendering::kLiveAntialiasFloor);

    const auto emergency = app::applyLiveRung(low, base, balanced[4], app::kLiveScaleFloorMin);
    CHECK(emergency.shadowResolution == 1024u);
    CHECK_FALSE(emergency.softShadows);
    // And back: climbing restores the tier's values, not the last rung's.
    const auto back = app::applyLiveRung(emergency, base, balanced[0], app::kLiveScaleFloorMin);
    CHECK(back.shadowResolution == base.shadowResolution);
    CHECK(back.softShadows == base.softShadows);
    CHECK(back.shadowPcfTaps == base.shadowPcfTaps);
    CHECK(back.volumeStepScale == base.volumeStepScale);
    CHECK(back.motionBlur);

    // Never above the base: a Preview tier's quarter-resolution volume stays a quarter at Ultra, and
    // a quality arm's scale stays where it was put.
    auto preview = rendering::QualitySettings::forTier(rendering::QualityTier::Preview);
    preview.renderScale = 0.71f;
    const auto previewUltra = app::applyLiveRung(preview, preview, balanced[0], app::kLiveScaleFloorMin);
    CHECK(previewUltra.volumeResolutionScale == 0.25f);
    CHECK(previewUltra.cascadeCount == preview.cascadeCount);
    CHECK(previewUltra.renderScale == 0.71f);

    // The floor holds the scale and leaves the other reductions alone.
    const auto floored = app::applyLiveRung(current, base, balanced[4], 0.71f);
    CHECK(floored.renderScale == 0.71f);
    CHECK_FALSE(floored.motionBlur);
}

TEST_CASE("the offline promise covers the live gates", "[unit][resolution][live-quality]") {
    auto offline = rendering::QualityPolicy::forTier(rendering::QualityTier::Offline);
    REQUIRE(offline.assertOfflineIsUncompromised());
    // ADR-182: the predicate must be able to fail on exactly the fields this work added.
    offline.settings.motionBlur = false;
    CHECK_FALSE(offline.assertOfflineIsUncompromised());
    offline.settings.motionBlur = true;
    offline.settings.depthOfField = false;
    CHECK_FALSE(offline.assertOfflineIsUncompromised());
    // Every tier leaves the gates open: only the live ladder closes them.
    for (const auto tier : {rendering::QualityTier::Preview, rendering::QualityTier::Realtime,
                            rendering::QualityTier::High, rendering::QualityTier::Offline}) {
        const auto q = rendering::QualitySettings::forTier(tier);
        CHECK(q.motionBlur);
        CHECK(q.depthOfField);
    }
}

TEST_CASE("the strategy and level tokens round-trip, and nothing else parses", "[unit][resolution][live-quality]") {
    for (const auto s : {LiveQualityStrategy::Balanced, LiveQualityStrategy::ResolutionFirst,
                         LiveQualityStrategy::EffectsFirst}) {
        CHECK(app::liveQualityStrategyFromToken(app::liveQualityStrategyToken(s)) == s);
    }
    CHECK_FALSE(app::liveQualityStrategyFromToken("Glowmere").has_value());
    CHECK_FALSE(app::liveQualityStrategyFromToken("").has_value());
    for (std::size_t i = 0; i < app::kLiveQualityLevels; ++i) {
        const auto level = static_cast<LiveQualityLevel>(i);
        CHECK(app::liveQualityLevelFromToken(app::liveQualityLevelToken(level)) == level);
    }
    CHECK_FALSE(app::liveQualityLevelFromToken("auto").has_value());
}

// ---- the controller -----------------------------------------------------------------------------------

TEST_CASE("a frame inside the budget never moves the ladder", "[unit][resolution]") {
    app::InteractiveResolution c;
    c.configure(fast());
    CHECK(feed(c, 300, 10.0, 12.0) == 0);
    CHECK(c.scale() == 1.0f);
    CHECK(c.stats().drops == 0);
    CHECK(c.stats().framesReduced == 0);

    // The control: the same controller, the same number of frames, over budget.
    app::InteractiveResolution d;
    d.configure(fast());
    CHECK(feed(d, 300, 60.0, 62.0) > 0);
}

TEST_CASE("the target, not a constant, decides what is over budget", "[unit][resolution][live-quality]") {
    // 12 ms: inside a 60 fps budget (14.7), over a 90 fps one (9.8). The old controller's 16.67 ms
    // constant could not tell these apart -- the reason Sonic sat at 55 fps with judder on 120 Hz.
    app::InteractiveResolution at60;
    at60.configure(fast(LiveQualityStrategy::Balanced, 60));
    CHECK(feed(at60, 300, 12.0, 13.0) == 0);
    app::InteractiveResolution at90;
    at90.configure(fast(LiveQualityStrategy::Balanced, 90));
    CHECK(feed(at90, 300, 12.0, 13.0) > 0);
}

TEST_CASE("each measured scene settles inside its budget without hunting", "[unit][resolution][live-quality]") {
    struct Case {
        const char* name;
        SceneCost scene;
        LiveQualityStrategy strategy;
        double fps;
    };
    const Case cases[] = {
        {"liminal 60", kLiminal, LiveQualityStrategy::ResolutionFirst, 60},
        {"liminal 90", kLiminal, LiveQualityStrategy::ResolutionFirst, 90},
        {"glowmere 60", kGlowmere, LiveQualityStrategy::Balanced, 60},
        {"sonic 60", kSonic, LiveQualityStrategy::EffectsFirst, 60},
        {"sonic 90", kSonic, LiveQualityStrategy::EffectsFirst, 90},
    };
    for (const Case& k : cases) {
        app::InteractiveResolution c;
        c.configure(fast(k.strategy, k.fps));
        run(c, k.scene, 4000);
        const double settled = cost(k.scene, c.current());
        INFO(k.name << ": settled at " << app::liveQualityLevelName(c.level()) << " scale " << c.scale() << " -> "
                    << settled << " ms against " << c.settings().budgetMs << "; drops " << c.stats().drops
                    << " raises " << c.stats().raises);
        // Inside the budget -- or at the bottom of the ladder, with nothing left to give.
        CHECK((settled <= c.settings().budgetMs || c.level() == LiveQualityLevel::Emergency));
        // It stopped: four drops walk the whole ladder, and a hunting controller would be in the
        // hundreds over 4,000 frames.
        CHECK(c.stats().drops + c.stats().raises <= 6);
        // And not further than it needed: the rung above does not fit comfortably.
        if (c.rung() > 0) {
            CHECK(cost(k.scene, app::liveQualityLadder(k.strategy)[c.rung() - 1]) > c.settings().budgetMs * 0.8);
        }
    }
}

TEST_CASE("a heavy scene keeps degrading after the scale reaches 0.5", "[unit][resolution][live-quality]") {
    // Liminal at 90 fps: 0.5 alone is ~16 ms. The old ladder stopped there; this one goes on to
    // Emergency's smaller scale and quarter-step volumes.
    app::InteractiveResolution c;
    c.configure(fast(LiveQualityStrategy::ResolutionFirst, 90));
    run(c, kLiminal, 3000);
    CHECK(c.level() == LiveQualityLevel::Emergency);
    CHECK(c.scale() < 0.5f);
    CHECK(cost(kLiminal, app::liveQualityLadder(LiveQualityStrategy::ResolutionFirst)[3]) > c.settings().budgetMs);
}

TEST_CASE("the floor holds the scale while the effects still go", "[unit][resolution][adr1024][live-quality]") {
    app::InteractiveResolutionSettings s = fast(LiveQualityStrategy::ResolutionFirst, 120);
    s.scaleFloor = 0.71f;
    app::InteractiveResolution c;
    c.configure(s);
    feed(c, 2000, 80.0, 82.0); // far over any budget
    CHECK(c.level() == LiveQualityLevel::Emergency);
    CHECK(c.scale() == 0.71f);
    CHECK_FALSE(c.current().motionBlur);

    // The control: without the raised floor the same frames go below it.
    app::InteractiveResolution d;
    d.configure(fast(LiveQualityStrategy::ResolutionFirst, 120));
    feed(d, 2000, 80.0, 82.0);
    CHECK(d.scale() < 0.71f);
}

TEST_CASE("a CPU-bound frame is not made smaller", "[unit][resolution]") {
    app::InteractiveResolution c;
    c.configure(fast());
    // A wall clock far above the GPU frame: the GPU is over budget, so the drop branch is entered,
    // but two thirds of the frame is the main thread (§8: a warning, not a reason to degrade).
    CHECK(feed(c, 300, 20.0, 60.0) == 0);
    CHECK(c.stats().heldByCpu > 0);
    // The control: the same GPU cost, GPU-bound, does move.
    app::InteractiveResolution d;
    d.configure(fast());
    CHECK(feed(d, 300, 20.0, 21.0) > 0);
}

TEST_CASE("frames that overlap on the GPU are not read as over budget", "[unit][resolution][live-quality]") {
    // Measured on Liminal at Low: a 12 ms GPU span at a 9 ms frame interval -- consecutive frames
    // overlapping on the GPU. Frames arriving every 9 ms fit a 60 fps budget whatever the span says.
    app::InteractiveResolution c;
    c.configure(fast(LiveQualityStrategy::ResolutionFirst, 60));
    CHECK(feed(c, 400, 16.0, 9.0) == 0);
    CHECK(c.medianGpuMs() == 9.0);
    // The control: the same span with frames arriving at it is over budget and moves.
    app::InteractiveResolution d;
    d.configure(fast(LiveQualityStrategy::ResolutionFirst, 60));
    CHECK(feed(d, 400, 16.0, 16.7) > 0);
}

TEST_CASE("one stalled frame does not move the ladder", "[unit][resolution]") {
    app::InteractiveResolution c;
    c.configure(fast());
    for (int i = 0; i < 300; ++i) {
        c.note(i % 50 == 25 ? 400.0 : 9.0, i % 50 == 25 ? 402.0 : 10.0);
    }
    CHECK(c.rung() == 0);
    CHECK(c.stats().drops == 0);
}

TEST_CASE("a frame the GPU timeline could not report is not a free frame", "[unit][resolution]") {
    app::InteractiveResolution c;
    c.configure(fast());
    feed(c, 200, 60.0, 62.0);
    const std::size_t dropped = c.rung();
    REQUIRE(dropped > 0);
    // No timestamps for a long stretch: the ladder holds rather than reading "0 ms" and climbing.
    feed(c, 500, -1.0, 16.0);
    CHECK(c.rung() == dropped);
}

TEST_CASE("the ladder climbs back only after a sustained comfortable stretch", "[unit][resolution][live-quality]") {
    app::InteractiveResolution c;
    app::InteractiveResolutionSettings s = fast();
    c.configure(s);
    feed(c, 300, 60.0, 62.0);
    REQUIRE(c.rung() > 0);
    const std::size_t low = c.rung();
    // The frame gets cheap. One window of good frames is not enough: the climb waits for
    // `raiseHoldFrames` consecutive comfortable frames after the dwell.
    feed(c, s.dwellFrames + s.raiseHoldFrames / 2, 2.0, 3.0);
    CHECK(c.rung() == low);
    // A bad stretch resets the streak.
    feed(c, s.windowFrames, 13.5, 14.0);
    feed(c, s.raiseHoldFrames - 2, 2.0, 3.0);
    CHECK(c.rung() == low);
    // Sustained: it climbs, one rung per hold, all the way back.
    feed(c, 2000, 2.0, 3.0);
    CHECK(c.rung() == 0);
    CHECK(c.stats().raises == low);
}

TEST_CASE("an effects-only rung is measured on the way down, so the climb cannot hunt",
          "[unit][resolution][live-quality]") {
    // A scene where High's effects are the whole difference: Ultra over the 14.7 ms budget, High
    // (same scale, no DoF, two cascades, quarter volumes) under 80% of it -- so a controller
    // predicting the climb from the pixel ratio (1.0) would climb, miss, drop and climb again for
    // ever. The ratio it measured on the way down forbids the climb.
    const SceneCost scene{8.0, 0.0, 2.0, 0.0, 3.0, 3.0};
    app::InteractiveResolution c;
    c.configure(fast(LiveQualityStrategy::EffectsFirst, 60));
    const auto& ladder = app::liveQualityLadder(LiveQualityStrategy::EffectsFirst);
    REQUIRE(cost(scene, ladder[0]) > c.settings().budgetMs);
    REQUIRE(cost(scene, ladder[1]) < c.settings().budgetMs * c.settings().raiseMargin);
    run(c, scene, 5000);
    CHECK(c.level() == LiveQualityLevel::High);
    CHECK(c.stats().drops == 1);
    CHECK(c.stats().raises == 0);
    CHECK(c.stepRatio(1) == Catch::Approx(cost(scene, ladder[0]) / cost(scene, ladder[1])).epsilon(0.01));
}

TEST_CASE("an unmeasured effects-only step is taken alone, not skipped", "[unit][resolution][live-quality]") {
    // Far over budget at Ultra; the first step (effects only) has no pixel ratio to predict by. The
    // controller takes it alone and measures it, rather than jumping two rungs on a guess of zero.
    app::InteractiveResolution c;
    c.configure(fast(LiveQualityStrategy::EffectsFirst, 60));
    feed(c, 8, 30.0, 31.0);
    CHECK(c.rung() == 1);
    CHECK(c.stats().drops == 1);
}

TEST_CASE("a disabled controller is the identity", "[unit][resolution]") {
    app::InteractiveResolution c;
    app::InteractiveResolutionSettings s = fast();
    s.enabled = false;
    c.configure(s);
    CHECK(feed(c, 500, 80.0, 81.0) == 0);
    CHECK(c.stats().framesSeen == 0);
}

TEST_CASE("a decision is never taken on the previous rung's measurements", "[unit][resolution]") {
    app::InteractiveResolution c;
    app::InteractiveResolutionSettings s = fast();
    s.dwellFrames = 1; // asks for less than the window; configure() must not allow it
    c.configure(s);
    CHECK(c.settings().dwellFrames >= c.settings().windowFrames);
    // 25 ms at Ultra, and every reduced rung cheap. Fed the new cost after the first drop: a
    // controller reading its stale window would drop a second time.
    app::InteractiveResolution d;
    d.configure(fast());
    std::size_t firstDrop = 0;
    for (int i = 0; i < 400; ++i) {
        const double gpu = d.rung() == 0 ? 25.0 : 10.0;
        d.note(gpu, gpu + 1.0);
        if (firstDrop == 0 && d.rung() > 0) {
            firstDrop = d.rung();
        }
    }
    CHECK(d.stats().drops == 1);
    CHECK(d.rung() == firstDrop);
}

TEST_CASE("a new strategy is a new ladder: the controller starts again at the top", "[unit][resolution][live-quality]") {
    app::InteractiveResolution c;
    c.configure(fast(LiveQualityStrategy::Balanced));
    feed(c, 300, 60.0, 62.0);
    REQUIRE(c.rung() > 0);
    c.configure(fast(LiveQualityStrategy::ResolutionFirst));
    CHECK(c.rung() == 0);
    // A new budget alone does not throw the learned ladder away.
    feed(c, 300, 60.0, 62.0);
    const std::size_t at = c.rung();
    REQUIRE(at > 0);
    c.configure(fast(LiveQualityStrategy::ResolutionFirst, 90));
    CHECK(c.rung() == at);
}
