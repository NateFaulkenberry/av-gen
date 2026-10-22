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
#include <utility>

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
    // ADR-1341: 30 fps is offered, and is a real budget (33.3 ms frame, 29.3 ms for the GPU), not clamped.
    CHECK(app::kLiveTargetChoices.front() == 30);
    CHECK(app::liveBudget(30).targetFps == 30.0);
    CHECK(app::liveBudget(30).qualityBudgetMs == Catch::Approx(29.333).margin(1e-3));
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
    CHECK(c.gpuCostMs() == 9.0);
    // The control: the same span with frames arriving at it is over budget and moves.
    app::InteractiveResolution d;
    d.configure(fast(LiveQualityStrategy::ResolutionFirst, 60));
    CHECK(feed(d, 400, 16.0, 16.7) > 0);
}

TEST_CASE("vsync-quantised intervals do not hide a GPU-bound frame", "[unit][resolution][live-quality]") {
    // Measured on Sonic at Medium: a 13.6 ms span while the intervals alternated 8.3 and 25 ms (a
    // 9.3 ms median, a 14.5 ms mean). The cost is the span; a median interval would read it as 9 ms
    // and hold a 90 fps target it is missing by half.
    app::InteractiveResolution c;
    app::InteractiveResolutionSettings s = fast(LiveQualityStrategy::EffectsFirst, 90);
    s.windowFrames = 21; // the production window is 20; a multiple of the 3-frame pattern keeps the mean exact
    s.dwellFrames = 21;
    c.configure(s);
    int i = 0;
    for (; i < 400 && c.rung() == 0; ++i) {
        c.note(13.6, i % 3 == 2 ? 25.0 : 8.3);
    }
    CHECK(c.rung() > 0);
    CHECK(c.gpuCostMs() == 13.6);
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

// ---- ADR-1107: the raise margin and the frame cap ------------------------------------------------------------------

TEST_CASE("the raise margin is 0.9: a level predicted at 85% of the budget is climbed back to",
          "[unit][resolution][live-quality]") {
    // REPORT §4 (30-live-quality): at 0.8 the level that would hold 60 sat within 20% of the budget and was never
    // climbed back to. The same controller at 0.8 is the paired arm (ADR-182): it must stay down.
    CHECK(app::InteractiveResolutionSettings{}.raiseMargin == Catch::Approx(0.9));
    const auto settle = [](double margin) {
        app::InteractiveResolutionSettings s = fast(LiveQualityStrategy::Balanced, 60);
        s.raiseMargin = margin;
        app::InteractiveResolution c;
        c.configure(s);
        // Over budget: it drops.
        for (int i = 0; i < 200 && c.rung() == 0; ++i) {
            c.note(16.0, 17.0);
        }
        const std::size_t low = c.rung();
        REQUIRE(low > 0);
        // The lower level measured at 8 ms: the step's ratio is learned, and 8 x ratio is still over the budget.
        feed(c, s.dwellFrames + s.windowFrames, 8.0, 9.0);
        REQUIRE(c.rung() == low);
        // The content gets lighter: the level above is now predicted at 85% of the budget.
        const double predictedAbove = 0.85 * s.budgetMs;
        const double now = predictedAbove / c.stepRatio(low);
        feed(c, 2000, now, now + 1.0);
        return std::pair{low, c.rung()};
    };
    const auto [low9, at9] = settle(0.9);
    CHECK(at9 < low9);
    const auto [low8, at8] = settle(0.8);
    CHECK(at8 == low8);
}

TEST_CASE("the frame cap's period: whole vsyncs at or above the target rate, off above the refresh",
          "[unit][resolution][live-quality]") {
    using app::liveFrameCapPeriodMs;
    // Off.
    CHECK(liveFrameCapPeriodMs(false, 60, 120) == 0.0);
    CHECK(liveFrameCapPeriodMs(true, 0, 120) == 0.0);
    CHECK(liveFrameCapPeriodMs(true, 90, 60) == 0.0);   // the display is already the lower ceiling
    CHECK(liveFrameCapPeriodMs(true, 120, 60) == 0.0);
    // 60 on 120 Hz: every second vsync.
    CHECK(liveFrameCapPeriodMs(true, 60, 120) == Catch::Approx(1000.0 / 60.0));
    // 60 on 60 Hz and on 59.94 Hz: every vsync (Fifo did not hold the loop there, ADR-1107).
    CHECK(liveFrameCapPeriodMs(true, 60, 60) == Catch::Approx(1000.0 / 60.0));
    CHECK(liveFrameCapPeriodMs(true, 60, 59.94) == Catch::Approx(1000.0 / 59.94));
    // 60 on 144 Hz: two vsyncs (72 fps), never three (48, below the target).
    CHECK(liveFrameCapPeriodMs(true, 60, 144) == Catch::Approx(2000.0 / 144.0));
    // 90 on 120 Hz: one vsync, not an uneven 1-2 alternation; 30 on 120 Hz: four.
    CHECK(liveFrameCapPeriodMs(true, 90, 120) == Catch::Approx(1000.0 / 120.0));
    CHECK(liveFrameCapPeriodMs(true, 30, 120) == Catch::Approx(4000.0 / 120.0));
    // Refresh unknown: the target's own period.
    CHECK(liveFrameCapPeriodMs(true, 60, 0) == Catch::Approx(1000.0 / 60.0));
}

TEST_CASE("the frame cap's steps hold a fixed grid: no drift from oversleeping, no burst after a stall",
          "[unit][resolution][live-quality]") {
    using app::livePaceStep;
    const double period = 1000.0 / 60.0;
    // No period: never waits.
    CHECK(livePaceStep(100.0, 90.0, 0.0).waitMs == 0.0);
    // The first frame: no wait, the grid starts one period on.
    auto first = livePaceStep(1000.0, 0.0, period);
    CHECK(first.waitMs == 0.0);
    CHECK(first.nextDeadlineMs == Catch::Approx(1000.0 + period));
    // Early: wait for the deadline; the next deadline is a period after it, not after now.
    auto early = livePaceStep(1010.0, 1000.0 + period, period);
    CHECK(early.waitMs == Catch::Approx(period - 10.0));
    CHECK(early.nextDeadlineMs == Catch::Approx(1000.0 + 2 * period));
    // A little late (under half a period): no wait, the grid kept.
    auto late = livePaceStep(1000.0 + period + 3.0, 1000.0 + period, period);
    CHECK(late.waitMs == 0.0);
    CHECK(late.nextDeadlineMs == Catch::Approx(1000.0 + 2 * period));
    // A stall: the grid restarts from now rather than running frames back to back to catch up.
    auto stall = livePaceStep(1000.0 + period + 100.0, 1000.0 + period, period);
    CHECK(stall.waitMs == 0.0);
    CHECK(stall.nextDeadlineMs == Catch::Approx(1000.0 + period + 100.0 + period));

    // Closed loop: 9 ms of work, every wake 0.4 ms late. 600 frames take 600 periods, not 600 x (period + 0.4).
    double now = 0.0;
    double deadline = 0.0;
    double start = -1.0;
    for (int i = 0; i < 601; ++i) {
        const auto step = livePaceStep(now, deadline, period);
        deadline = step.nextDeadlineMs;
        now += step.waitMs > 0.0 ? step.waitMs + 0.4 : 0.0;
        if (i == 0) {
            start = now;
        }
        now += 9.0;
    }
    CHECK((now - 9.0 - start) == Catch::Approx(600 * period + 0.4).margin(0.01));
}

// ---- ADR-1362: paced to the target, misses decide ------------------------------------------------------------------
//
// Measured on THE RIFT LIVE at a 30 fps target with the cap on (docs/development/live-controller-30fps.md): the GPU
// span read 24-26 ms at High and 29-30 ms at Emergency, every frame on time. The clock governor slows the GPU until the
// busy time fills most of the period, so the span reads the clock, not the level. The model below is that: a frame on
// time reads 86-92% of the period whatever the level costs; a frame over the period reads its cost and misses.

namespace {

struct PacedLevel {
    double costMs = 20.0;   // what the level costs at full clock
    int missEvery = 0;      // 0: never misses; n: every n-th frame misses (a level on the edge)
};

struct PacedScene {
    std::array<PacedLevel, app::kLiveQualityLevels> levels{};
    double periodMs = 1000.0 / 30.0;
    double refreshHz = 60.0;
};

struct PacedRun {
    std::uint64_t frames = 0;
    std::uint64_t misses = 0;
    std::array<std::uint64_t, app::kLiveQualityLevels> framesAt{};
};

// One closed-loop run. `paced = false` feeds the same frames to a controller that was never told about the cap: the
// pre-ADR-1362 controller, which is the paired arm (ADR-182).
PacedRun runPaced(app::InteractiveResolution& c, const PacedScene& scene, int frames, bool paced = true,
                  std::uint32_t seed = 1) {
    PacedRun out;
    std::uint32_t rng = seed;
    const auto jitter = [&] { // deterministic, in [0, 1)
        rng = rng * 1664525u + 1013904223u;
        return static_cast<double>(rng >> 8) / static_cast<double>(1u << 24);
    };
    std::array<int, app::kLiveQualityLevels> nth{};
    for (int i = 0; i < frames; ++i) {
        if (paced) {
            c.setPacing(scene.periodMs, scene.refreshHz);
        }
        const std::size_t r = c.rung();
        const PacedLevel& level = scene.levels[r];
        const bool overPeriod = level.costMs > scene.periodMs;
        const bool edgeMiss = level.missEvery > 0 && ++nth[r] % level.missEvery == 0;
        const bool miss = overPeriod || edgeMiss;
        const double gpu = miss ? std::max(level.costMs, scene.periodMs * 1.02) // saturated: the span is the cost
                                : scene.periodMs * (0.86 + 0.06 * jitter()); // the governor's fill
        const double wall = miss ? scene.periodMs + 1000.0 / scene.refreshHz : scene.periodMs + 0.6 * jitter();
        ++out.frames;
        out.misses += miss ? 1 : 0;
        ++out.framesAt[r];
        c.note(gpu, wall);
    }
    return out;
}

app::InteractiveResolutionSettings live(double targetFps) {
    app::InteractiveResolutionSettings s; // the production law: dwell 30, window 20, hold 120, 3 misses in 90
    s.enabled = true;
    s.targetFps = targetFps;
    s.budgetMs = app::liveBudget(targetFps).qualityBudgetMs;
    s.strategy = LiveQualityStrategy::EffectsFirst;
    return s;
}

// THE RIFT LIVE at 30, roughly: Ultra is over the frame, High holds most frames but misses some, Medium and below hold.
PacedScene riftAt30() {
    PacedScene s;
    s.levels = {PacedLevel{38.0, 0}, PacedLevel{31.0, 12}, PacedLevel{25.0, 0}, PacedLevel{20.0, 0},
                PacedLevel{16.0, 0}};
    return s;
}

} // namespace

TEST_CASE("a paced deadline is missed by a frame that lands a refresh late, not by the cap's wake-up jitter",
          "[unit][resolution][live-quality][adr1362]") {
    app::InteractiveResolution c;
    c.configure(live(30));
    // 30 on 60 Hz: 33-39 ms is on time (the main thread's interval jitters around the vsync); 50 ms is a lost refresh.
    c.setPacing(1000.0 / 30.0, 60.0);
    for (int i = 0; i < 10; ++i) {
        c.note(20.0, i % 2 == 0 ? 33.3 : 39.0);
    }
    CHECK(c.stats().missedDeadlines == 0);
    c.note(20.0, 50.0);
    CHECK(c.stats().missedDeadlines == 1);
    // 30 on 120 Hz, the editor's own screen: 38 ms is jitter (measured: no frame lost), 42 ms is a quarter period late.
    app::InteractiveResolution b;
    b.configure(live(30));
    b.setPacing(1000.0 / 30.0, 120.0);
    b.note(20.0, 38.4);
    CHECK(b.stats().missedDeadlines == 0);
    b.note(20.0, 42.5);
    CHECK(b.stats().missedDeadlines == 1);
    // 60 on 120 Hz: 25 ms is a miss, 16.7-19 ms is not.
    app::InteractiveResolution d;
    d.configure(live(60));
    d.setPacing(1000.0 / 60.0, 120.0);
    d.note(10.0, 19.0);
    d.note(10.0, 25.0);
    CHECK(d.stats().missedDeadlines == 1);
    // Not paced: nothing is a deadline (the uncapped law reads the span and the interval as before).
    app::InteractiveResolution e;
    e.configure(live(30));
    e.note(20.0, 50.0);
    CHECK(e.stats().missedDeadlines == 0);
}

TEST_CASE("paced: a level a little over the frame on a fine refresh grid counts its stretched frames as the GPU's misses",
          "[unit][resolution][live-quality][adr1363]") {
    // ADR-1363. 30 on 120 Hz: High costs 34.8 ms of GPU, every frame arrives at ~35.5 ms -- never a quarter period late,
    // so ADR-1362's rule alone counted nothing and the level held at ~28 fps (Cymatic World, Feline Footwear, measured).
    const double period = 1000.0 / 30.0;
    app::InteractiveResolution c;
    c.configure(live(30));
    for (int i = 0; i < 600 && c.rung() == 0; ++i) {
        c.setPacing(period, 120.0);
        c.note(34.8 + 0.4 * (i % 3), 35.2 + 0.3 * (i % 4));
    }
    CHECK(c.stats().missedDeadlines > 0);
    CHECK(c.rung() >= 1);
    // Frames on time with the span at the period: no miss (the governor's fill, or a level exactly at the edge).
    app::InteractiveResolution onTime;
    onTime.configure(live(30));
    for (int i = 0; i < 600; ++i) {
        onTime.setPacing(period, 120.0);
        onTime.note(34.0, period + 0.3);
    }
    CHECK(onTime.stats().missedDeadlines == 0);
    CHECK(onTime.rung() == 0);
    // Stretched intervals with the GPU well inside the period: the cap's wake-up or the main thread, not the level.
    app::InteractiveResolution jitter;
    jitter.configure(live(30));
    for (int i = 0; i < 600; ++i) {
        jitter.setPacing(period, 120.0);
        jitter.note(29.0, 36.5);
    }
    CHECK(jitter.stats().missedDeadlines == 0);
    CHECK(jitter.rung() == 0);
}

TEST_CASE("paced: a span the clock governor inflates to the budget does not move the ladder when frames are on time",
          "[unit][resolution][live-quality][adr1362]") {
    // Every level holds; the span reads 86-92% of the period (28.7-30.7 ms: over the 29.3 ms budget about a third of
    // the time) because the GPU clocks down into the idle part of each frame.
    PacedScene scene;
    scene.levels = {PacedLevel{20.0, 0}, PacedLevel{17.0, 0}, PacedLevel{14.0, 0}, PacedLevel{12.0, 0},
                    PacedLevel{10.0, 0}};
    app::InteractiveResolution c;
    c.configure(live(30));
    runPaced(c, scene, 6000);
    CHECK(c.rung() == 0);
    CHECK(c.stats().drops == 0);
    CHECK_FALSE(c.unsustainable());
    // The paired arm: the same frames, the cap not declared -- the old law reads the span against the budget and walks
    // a scene that holds every level down the ladder.
    app::InteractiveResolution d;
    d.configure(live(30));
    runPaced(d, scene, 6000, false);
    CHECK(d.rung() > 0);
}

TEST_CASE("paced: a level whose frames are on time is climbed back to, slowly, although its span reads at the budget",
          "[unit][resolution][live-quality][adr1362]") {
    const PacedScene scene = riftAt30();
    app::InteractiveResolution c;
    const app::InteractiveResolutionSettings s = live(30);
    c.configure(s);
    // Down from Ultra (over the frame) on its misses.
    PacedScene heavy = scene;
    heavy.levels[1] = heavy.levels[2] = PacedLevel{36.0, 0}; // a heavy passage: High and Medium miss too
    for (int i = 0; i < 1000 && c.rung() < 3; ++i) {
        runPaced(c, heavy, 1, true, 7 + static_cast<std::uint32_t>(i));
    }
    REQUIRE(c.rung() == 3); // Low
    // The passage ends. Medium holds with every frame on time while its span reads 86-92% of the period. It is
    // climbed to -- but not before the hold: a dwell, then `raiseHoldFrames` frames with no miss in the window.
    const std::uint64_t raisesBefore = c.stats().raises;
    runPaced(c, scene, s.dwellFrames + s.raiseHoldFrames - 10);
    CHECK(c.rung() == 3);
    runPaced(c, scene, 2 * (s.dwellFrames + s.raiseHoldFrames));
    CHECK(c.rung() <= 2);
    CHECK(c.stats().raises > raisesBefore);
    // The paired arm: told nothing about the cap, the same frames never climb back to Medium.
    app::InteractiveResolution d;
    d.configure(s);
    runPaced(d, heavy, 200, false);
    runPaced(d, scene, 3000, false);
    CHECK(d.rung() >= 3);
}

TEST_CASE("paced: real misses still descend, and misses the GPU did not cause do not",
          "[unit][resolution][live-quality][adr1362]") {
    // Ultra misses every frame: down, one level per decision, until the frames are on time.
    app::InteractiveResolution c;
    c.configure(live(30));
    runPaced(c, riftAt30(), 600);
    CHECK(c.rung() >= 1);
    CHECK(c.stats().drops >= 1);
    // A level that misses one frame in twelve is left within a few seconds even though most of its frames are on
    // time and its span reads under the budget between misses.
    app::InteractiveResolution e;
    e.configure(live(30));
    PacedScene edge;
    edge.levels = {PacedLevel{31.0, 12}, PacedLevel{25.0, 0}, PacedLevel{20.0, 0}, PacedLevel{16.0, 0},
                   PacedLevel{12.0, 0}};
    runPaced(e, edge, 300); // ten seconds
    CHECK(e.rung() >= 1);
    // The rate that decides: a level losing one frame in sixty (1.7%) is left within half a minute; one losing one in
    // six hundred (a hitch every twenty seconds) is kept.
    PacedScene rare = edge;
    rare.levels[0] = PacedLevel{30.0, 60};
    app::InteractiveResolution r1;
    r1.configure(live(30));
    runPaced(r1, rare, 900);
    CHECK(r1.rung() >= 1);
    rare.levels[0] = PacedLevel{30.0, 600};
    app::InteractiveResolution r2;
    r2.configure(live(30));
    runPaced(r2, rare, 3000);
    CHECK(r2.rung() == 0);
    CHECK(r2.stats().drops == 0);
    // Misses with the GPU finishing early (a main-thread stall, another process): held, not degraded (§8).
    app::InteractiveResolution f;
    f.configure(live(30));
    for (int i = 0; i < 2000; ++i) {
        f.setPacing(1000.0 / 30.0, 60.0);
        f.note(12.0, i % 10 == 0 ? 50.0 : 33.4);
    }
    CHECK(f.rung() == 0);
    CHECK(f.stats().lateNotGpu > 0);
    CHECK_FALSE(f.unsustainable());
    // Nor at a span the clock governor holds at 85-90% of the period: measured, the late frames of a light scene at
    // 60 fps (the cap's wake-up, the main thread) sat there, and a GPU-share test would have called them the GPU's.
    app::InteractiveResolution h;
    h.configure(live(30));
    for (int i = 0; i < 2000; ++i) {
        h.setPacing(1000.0 / 30.0, 60.0);
        h.note(29.5, i % 10 == 0 ? 50.0 : 33.4);
    }
    CHECK(h.rung() == 0);
    CHECK(h.stats().lateNotGpu > 0);
    // The control: the same late frames with a span that reached the period -- the span arriving two frames after the
    // late frame, as FrameTimeline delivers it -- do go down.
    app::InteractiveResolution g;
    g.configure(live(30));
    for (int i = 0; i < 2000 && g.rung() == 0; ++i) {
        g.setPacing(1000.0 / 30.0, 60.0);
        g.note(i % 10 == 2 ? 36.0 : 29.5, i % 10 == 0 ? 50.0 : 33.4);
    }
    CHECK(g.rung() > 0);
}

TEST_CASE("paced: a GPU stall is not the level's cost, a level far over the period is",
          "[unit][resolution][live-quality][adr1362]") {
    // Measured on Neon Vector World at 60 fps: single 40 ms spans (2.4x the period) with a 14 ms median -- a compile,
    // an upload, another process -- each making two or three frames late. No level fixes that.
    app::InteractiveResolution c;
    c.configure(live(60));
    for (int i = 0; i < 3600; ++i) {
        c.setPacing(1000.0 / 60.0, 120.0);
        const int k = i % 300; // a stall every five seconds, three late frames each
        const bool stall = k >= 100 && k < 103;
        c.note(k == 101 ? 40.0 : 14.0, stall ? 26.0 : 16.7);
    }
    CHECK(c.rung() == 0);
    CHECK(c.stats().drops == 0);
    CHECK(c.stats().missedDeadlines > 0);
    // The control: a level that is far over every frame (a 40 ms span at a 16.7 ms period, so the median is over the
    // period too) goes down although every span is a "stall" by size.
    app::InteractiveResolution d;
    d.configure(live(60));
    for (int i = 0; i < 600 && d.rung() == 0; ++i) {
        d.setPacing(1000.0 / 60.0, 120.0);
        d.note(40.0, 41.7);
    }
    CHECK(d.rung() > 0);
}

TEST_CASE("paced: under a steady load it settles on the highest level that holds, without hunting",
          "[unit][resolution][live-quality][adr1362]") {
    // Three minutes at 30 fps on THE RIFT's shape: High misses one frame in twelve, Medium holds.
    app::InteractiveResolution c;
    c.configure(live(30));
    const PacedRun run = runPaced(c, riftAt30(), 30 * 180);
    INFO("ended at " << app::liveQualityLevelName(c.level()) << "; drops " << c.stats().drops << " raises "
                     << c.stats().raises << " reverts " << c.stats().reverts << "; misses " << run.misses);
    CHECK(c.level() == LiveQualityLevel::Medium);
    // Most of the run at Medium, the highest level that holds.
    CHECK(run.framesAt[2] > run.frames * 8 / 10);
    // Each probe of High that misses comes back down and the next waits twice as long (ADR-1104, up to 8x): a handful
    // of probes in three minutes, not a level change every few seconds.
    CHECK(c.stats().raises <= 6);
    CHECK(c.stats().reverts + 1 >= c.stats().raises);
    // What the viewer sees: under 1% of frames missed over the whole run, the descent from Ultra included.
    CHECK(run.misses * 100 < run.frames);
}

TEST_CASE("paced at 60 on a 120 Hz display: the same law, a level that holds is kept",
          "[unit][resolution][live-quality][adr1362]") {
    PacedScene scene;
    scene.periodMs = 1000.0 / 60.0;
    scene.refreshHz = 120.0;
    scene.levels = {PacedLevel{19.0, 0}, PacedLevel{15.5, 9}, PacedLevel{12.0, 0}, PacedLevel{10.0, 0},
                    PacedLevel{8.0, 0}};
    app::InteractiveResolution c;
    c.configure(live(60));
    const PacedRun run = runPaced(c, scene, 60 * 120);
    CHECK(c.level() == LiveQualityLevel::Medium);
    CHECK(run.framesAt[2] > run.frames * 7 / 10);
    CHECK(c.stats().raises <= 6);
}

TEST_CASE("paced: when the frame gets cheaper underneath it, forgetting the backoff climbs back sooner (ADR-1493)",
          "[unit][resolution][live-quality][adr1493]") {
    // A live scene opens on the material interpreter (every level but Emergency over the frame), then the compiled
    // variant lands and every level holds. The backoff the interpreter's misses earned would make the climb wait up to
    // 16x the hold at each level.
    PacedScene interpreted;
    interpreted.levels = {PacedLevel{45.0, 0}, PacedLevel{42.0, 0}, PacedLevel{40.0, 0}, PacedLevel{37.0, 0},
                          PacedLevel{25.0, 0}};
    PacedScene compiled;
    compiled.levels = {PacedLevel{30.0, 0}, PacedLevel{24.0, 0}, PacedLevel{20.0, 0}, PacedLevel{17.0, 0},
                       PacedLevel{14.0, 0}};
    const auto framesToTop = [&](bool forget) {
        app::InteractiveResolution c;
        c.configure(live(30));
        runPaced(c, interpreted, 900); // about the 30 s a cold variant can take
        REQUIRE(c.rung() == 4);
        if (forget) {
            c.forgetBackoff();
        }
        CHECK(c.rung() == 4); // the level in force is kept: forgetting is not a jump
        int frames = 0;
        while (c.rung() != 0 && frames < 30000) {
            runPaced(c, compiled, 1);
            ++frames;
        }
        return frames;
    };
    const int kept = framesToTop(false);
    const int forgotten = framesToTop(true);
    CHECK(forgotten < kept);
    CHECK(forgotten <= 4 * 300); // four climbs at about the ordinary hold, not a minute each
}

// ADR-1600: a drop carries the misses that decided it. recentMisses() restarts on the new level, so the controller keeps
// the count the level being left had when the move was taken.
TEST_CASE("paced: a drop remembers the misses that decided it", "[unit][resolution][live-quality][adr1600]") {
    app::InteractiveResolution g;
    g.configure(live(30));
    for (int i = 0; i < 2000 && g.rung() == 0; ++i) {
        g.setPacing(1000.0 / 30.0, 60.0);
        g.note(i % 10 == 2 ? 36.0 : 29.5, i % 10 == 0 ? 50.0 : 33.4);
    }
    REQUIRE(g.rung() > 0);
    CHECK(g.missesAtLastMove() >= live(30).missesToDrop);
    CHECK(g.recentMisses() == 0); // the new level's own window starts empty
    CHECK(g.gpuAtLastMove() > 0.0);
}
