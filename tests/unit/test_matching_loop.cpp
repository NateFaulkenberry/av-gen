// Phase C §26-§29 -- the matching loop, benchmarked as a whole.
//
// The mechanisms all exist on `MatchMotionProvider`: `searchInterval` (§27), `minimumContinuation`
// (§29), `switchMargin` (§28), with `searches`/`continued`/`switches`/`heldByMargin` counters.
// **What C asks for and nobody has done is the measurement.** §27 says, in as many words, "do not
// assume every-frame search is necessary. Benchmark." That is the third section of this phase with
// the same shape -- mechanism present, measurement missing -- after §14 and §24.
//
// And §26 is the first place §16's search plan, §24's horizons and §25's prediction run together,
// so it is the first place their individual numbers can disagree with what the combination does.
// **Three components each validated alone is not a validated system**, so the loop is measured as
// a loop: quality and cost, over a run, with the opposing quantity for each dial.

#include "assets/gltf_loader.hpp"
#include "entity/clip_motion_provider.hpp"
#include "entity/match_motion_provider.hpp"
#include "scene/motion_database.hpp"
#include "scene/motion_pack.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {
namespace fs = std::filesystem;

fs::path alienGlb() {
    return fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
}

struct Fixture {
    scene::Scene sc;
    scene::MotionPack pack;
    scene::MotionDatabase db;
    bool ok = false;

    Fixture() {
        assets::GltfLoadOptions loadOptions;
        loadOptions.loadImages = false;
        if (!fs::exists(alienGlb()) || !assets::loadGltf(alienGlb(), sc, loadOptions).has_value() ||
            sc.rigs.empty()) {
            return;
        }
        scene::Provenance provenance;
        provenance.source = "Glowmere alien pack";
        provenance.sourceFile = "alien-scout.glb";
        provenance.creator = "AV Gen";
        provenance.license = "CC0-1.0";
        provenance.licenseUrl = "https://creativecommons.org/publicdomain/zero/1.0/";
        provenance.redistribution = scene::Redistribution::Allowed;
        provenance.derivedDataAllowed = true;
        provenance.trainingAllowed = true;
        provenance.processing = {"Phase C §26"};
        provenance.toolVersion = "avgen-phase-c";
        scene::PackBuildOptions packOptions;
        packOptions.contactJoints = {scene::ContactJoint{"foot.l", scene::ContactKind::Foot},
                                     scene::ContactJoint{"foot.r", scene::ContactKind::Foot}};
        packOptions.contacts.looping = true;
        packOptions.toolVersion = "avgen-phase-c";
        auto built = scene::buildMotionPack("glowmere-scout", sc.rigs.front().skeleton,
                                            sc.rigs.front().clips, provenance, packOptions);
        if (!built.has_value()) {
            return;
        }
        pack = std::move(*built);
        scene::MotionDatabaseOptions dbOptions;
        dbOptions.sampleRate = 30.0f;
        dbOptions.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
        auto database = scene::buildMotionDatabase(pack, dbOptions);
        if (!database.has_value()) {
            return;
        }
        db = std::move(*database);
        ok = true;
    }
};

struct LoopResult {
    std::uint64_t searches = 0;
    std::uint64_t continued = 0;
    std::uint64_t switches = 0;
    std::uint64_t heldByMargin = 0;
    double microsPerFrame = 0.0;
    int frames = 0;
};

// One run of the loop over a scripted request, driven through `advance` exactly as the provider
// seam is driven in the product (ADR-556: advance and pose are two calls).
LoopResult runLoop(const Fixture& fixture, entity::MatchSettings settings, int frames = 600) {
    entity::MatchMotionProvider provider(&fixture.db, &fixture.sc.rigs.front().clips, "match");
    provider.setSettings(settings);
    provider.resetCounters();

    entity::MotionMemory memory;
    const float dt = 1.0f / 60.0f;
    double time = 0.0;
    double best = std::numeric_limits<double>::max();

    for (int repeat = 0; repeat < 3; ++repeat) {
        entity::MotionMemory local;
        double t = 0.0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int f = 0; f < frames; ++f) {
            entity::MotionRequest request;
            // A request that changes its mind, so hysteresis and the search interval both have
            // something to do. A constant request would let any setting look perfect.
            const float phase = static_cast<float>(f) / 120.0f;
            request.desiredVelocity =
                glm::vec3(std::sin(phase) * 1.2f, 0.0f, std::cos(phase) * 1.2f);
            request.desiredFacing = glm::normalize(request.desiredVelocity);
            entity::MotionMemory next;
            (void)provider.advance(request, local, t, dt, next);
            local = next;
            t += static_cast<double>(dt);
        }
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best,
                        std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(t1 - t0)
                                .count() /
                            frames);
        if (repeat == 0) {
            memory = local;
            time = t;
        }
    }
    (void)memory;
    (void)time;

    LoopResult out;
    const entity::MatchMotionProvider::Counters counters = provider.counters();
    out.searches = counters.searches;
    out.continued = counters.continued;
    out.switches = counters.switches;
    out.heldByMargin = counters.heldByMargin;
    out.microsPerFrame = best;
    out.frames = frames;
    return out;
}

} // namespace

TEST_CASE("§27: every-frame search is not necessary, measured", "[loop][phaseC][aliens]") {
    Fixture fixture;
    if (!fixture.ok) {
        SKIP("the Glowmere alien is not present");
    }

    WARN("interval   searches  switches  held  us/frame   characters per 60 Hz frame");
    double everyFrame = 0.0;
    double shipping = 0.0;
    std::uint64_t everyFrameSwitches = 0;
    std::uint64_t shippingSwitches = 0;
    for (const float interval : {0.0f, 0.033f, 0.1f, 0.25f}) {
        entity::MatchSettings settings;
        settings.searchInterval = interval;
        const LoopResult r = runLoop(fixture, settings);
        WARN(fmt::format("{:>8.3f}s {:>9} {:>9} {:>5}  {:>8.2f}  {:>10.0f}", interval, r.searches,
                         r.switches, r.heldByMargin, r.microsPerFrame,
                         16666.0 / std::max(r.microsPerFrame, 1e-6)));
        if (interval == 0.0f) {
            everyFrame = r.microsPerFrame;
            everyFrameSwitches = r.switches;
        }
        if (interval == 0.1f) {
            shipping = r.microsPerFrame;
            shippingSwitches = r.switches;
        }
    }

    // **The answer §27 asks for.** Searching every frame costs more; the question is whether it
    // buys anything, and the opposing quantity is how much the motion actually changes.
    WARN(fmt::format("every-frame {:.2f} us/frame vs the shipping 0.1 s interval {:.2f} us/frame "
                     "({:.1f}x); switches {} vs {}",
                     everyFrame, shipping, everyFrame / std::max(shipping, 1e-6),
                     everyFrameSwitches, shippingSwitches));
    CHECK(everyFrame > 0.0);
    CHECK(shipping > 0.0);

    // **`searchInterval` does nothing at its shipping default, and the table is how that surfaced.**
    // 0.000 s, 0.033 s and 0.100 s all produce *exactly* 150 searches and within 1% the same time.
    // The reason is in `advance`: a search needs `due && !locked`, where `due` is
    // `sinceSearch >= searchInterval` (0.1 s) and `locked` is
    // `sinceSearch < minimumContinuation` (**0.2 s**). The lock is always the later of the two, so
    // it decides every time and the interval cannot affect anything below it. Only at 0.250 s --
    // above the lock -- does the interval take over, and the count drops to 120.
    //
    // That is ADR-608's family with a twist: not a dial nobody reads, but **a dial that is read and
    // then shadowed by another one**. Setting it produces no change, so a person tuning it
    // concludes search frequency does not matter -- and the misdiagnosis is the same cost as the
    // dead weights.
    //
    // Asserted as the current behaviour, so that fixing the shadowing fails here and says so
    // rather than silently changing what every number above means.
    CHECK(everyFrameSwitches == shippingSwitches);
    CHECK(std::abs(everyFrame - shipping) < 0.2 * everyFrame);
}

TEST_CASE("§28: hysteresis stops thrashing without refusing to adapt", "[loop][phaseC][aliens]") {
    // §28's two failure modes are opposite and a single number cannot see both: too little and the
    // motion thrashes A/B/A/B; too much and "the system refuses to adapt". So both are measured --
    // switches for the first, and whether the loop still responds at all for the second.
    Fixture fixture;
    if (!fixture.ok) {
        SKIP("the Glowmere alien is not present");
    }

    WARN("margin    searches  switches  heldByMargin");
    std::uint64_t noMarginSwitches = 0;
    std::uint64_t hugeMarginSwitches = 0;
    for (const float margin : {0.0f, 0.05f, 0.5f, 100.0f}) {
        entity::MatchSettings settings;
        settings.switchMargin = margin;
        const LoopResult r = runLoop(fixture, settings);
        WARN(fmt::format("{:>7.2f} {:>9} {:>9} {:>13}", margin, r.searches, r.switches,
                         r.heldByMargin));
        if (margin == 0.0f) {
            noMarginSwitches = r.switches;
        }
        if (margin == 100.0f) {
            hugeMarginSwitches = r.switches;
        }
    }

    // **The margin is now a fraction of the database's measured cost spread, not raw cost units**,
    // and that change is what made the table readable: 0.50 (half the spread) holds 6 of 150,
    // 100.00 (a hundred spreads) holds 147. Before the unit change the same dial held nothing until
    // 100 and then held indiscriminately, because 0.05 against a spread of 69.72 is four parts in
    // ten thousand -- a units mismatch, the fog bank's per-metre density defect in a new place.
    //
    // **Changing the unit makes the default meaningful, not correct.** 0.05 now means "five percent
    // of the good-to-typical gap", which is a number a person can reason about; whether five
    // percent is the right amount still needs a motion-quality metric this phase does not have.
    //
    // At 0.05 it still holds nothing, and that is now a statement about the loop rather than about
    // the units: the continuation is essentially never within five percent of the best match on
    // this corpus.
    CHECK(hugeMarginSwitches < noMarginSwitches);
    // **And it does not refuse to adapt at its shipping value**, which is §28's explicit second
    // warning and the one a single "fewer switches is better" metric would recommend violating
    // (ADR-559: the monotone metric recommends the extreme).
    entity::MatchSettings shipping;
    const LoopResult atShipping = runLoop(fixture, shipping);
    CHECK(atShipping.switches > 0u);
}

TEST_CASE("§29: the minimum continuation holds motion without freezing it",
          "[loop][phaseC][aliens]") {
    // The freeze is the failure ADR-610 found on continuity and ADR-611 named: a loop that never
    // changes reports zero switches, which is the best score on the only obvious metric.
    Fixture fixture;
    if (!fixture.ok) {
        SKIP("the Glowmere alien is not present");
    }
    entity::MatchSettings brief;
    brief.minimumContinuation = 0.0f;
    entity::MatchSettings locked;
    locked.minimumContinuation = 5.0f; // longer than the run
    const LoopResult free = runLoop(fixture, brief);
    const LoopResult held = runLoop(fixture, locked);
    WARN(fmt::format("continuation 0 s: {} searches, {} switches; 5 s: {} searches, {} switches",
                     free.searches, free.switches, held.searches, held.switches));
    CHECK(held.searches < free.searches);
    CHECK(free.switches > 0u); // the companion: the unlocked loop actually moved
}

// ---- §32: root-motion continuity across a switch -----------------------------------------------

TEST_CASE("§32: the provider seam inertializes, measured against the threshold fixed before the fix",
          "[loop][phaseC][aliens]") {
    // **ADR-612 recorded the defect; this measures the fix against the bar ADR-612 set while no fix
    // existed to flatter.** The bar: *a transition may not move a foot further than the foot moves
    // anyway between two ordinary frames.* It is re-derived here from the same clip rather than
    // copied as a literal, because a threshold pasted as a number stops tracking the content it was
    // a property of.
    //
    // **Three things about this instrument, stated before its output.**
    //
    // 1. **It is not ADR-612's instrument.** That figure came from §30's search loop, which drives
    //    the matcher with a target sample's own feature vector and poses the feet itself. This
    //    drives `MatchMotionProvider::advance`/`pose` -- the shipping path, with a request instead
    //    of a feature vector -- so the before-figures are not required to agree, and the comparison
    //    that means anything is the one taken *within* this harness, between the blend off and the
    //    blend on. Both arms are run here for exactly that reason.
    // 2. **It is measured at 1/30 s per step**, which is the interval ADR-612's 0.0510 m was
    //    measured over. Running the loop at 60 Hz and comparing per-frame distances against a 30 Hz
    //    budget would halve every number for free.
    // 3. **The jump at the transition is not the only thing that can go wrong, so it is not the
    //    only thing measured.** A blend can pass a "no teleport" test by smearing the same
    //    displacement over the following frames, so the worst single frame of the whole run is
    //    reported beside it. A fix that moves the defect is not a fix either.
    Fixture fixture;
    if (!fixture.ok) {
        SKIP("the Glowmere alien is not present");
    }
    const scene::SkinnedRig& rig = fixture.sc.rigs.front();
    const int footL = rig.skeleton.find("foot.l");
    const int footR = rig.skeleton.find("foot.r");
    REQUIRE(footL >= 0);
    REQUIRE(footR >= 0);

    // ---- the threshold, re-derived from the content ------------------------------------------
    const scene::AnimationClip* walk = nullptr;
    for (const scene::AnimationClip& c : rig.clips) {
        if (c.name == "Walking") {
            walk = &c;
            break;
        }
    }
    REQUIRE(walk != nullptr);
    const float step = entity::kBlendBudgetFrameSeconds;
    scene::Pose pose;
    std::vector<glm::mat4> model;
    std::vector<float> ordinary;
    glm::vec3 previousFoot{0.0f};
    const int walkFrames = static_cast<int>(walk->length() / step);
    for (int f = 0; f <= walkFrames; ++f) {
        scene::setRestPose(rig.skeleton, pose);
        scene::sampleClip(*walk, walk->start + (static_cast<float>(f) * step), pose);
        scene::poseToModel(rig.skeleton, pose, model);
        const glm::vec3 p = glm::vec3(model[static_cast<std::size_t>(footL)][3]);
        if (f > 0) {
            ordinary.push_back(glm::length(p - previousFoot));
        }
        previousFoot = p;
    }
    REQUIRE(ordinary.size() > 10u);
    std::vector<float> sorted = ordinary;
    std::sort(sorted.begin(), sorted.end());
    const float threshold = sorted[sorted.size() / 2];
    const float worstOrdinary = sorted.back();

    // ---- the run -------------------------------------------------------------------------------
    struct Run {
        double meanAtSwitch = 0.0;
        double worstAtSwitch = 0.0;
        double worstFrame = 0.0;
        int switches = 0;
        int frames = 0;
    };
    const auto drive = [&](entity::MatchSettings settings) {
        entity::MatchMotionProvider provider(&fixture.db, &rig.clips, "match");
        provider.setSettings(settings);
        provider.resetCounters();

        entity::MotionMemory memory;
        scene::Pose out;
        std::vector<glm::mat4> matrices;
        glm::vec3 lastL{0.0f};
        glm::vec3 lastR{0.0f};
        bool have = false;
        Run run;
        const int frames = 1800; // 60 s at 1/30 s
        double t = 0.0;
        for (int f = 0; f < frames; ++f) {
            entity::MotionRequest request;
            // The same scripted change of mind §27-§29 use, written against *time* rather than
            // against a frame count so that the script is the same motion at any step size.
            const auto phase = static_cast<float>(t * 0.5);
            request.desiredVelocity =
                glm::vec3(std::sin(phase) * 1.2f, 0.0f, std::cos(phase) * 1.2f);
            request.desiredFacing = glm::normalize(request.desiredVelocity);
            const std::uint64_t before = provider.counters().switches;
            entity::MotionMemory next;
            const entity::MotionResult advanced =
                provider.advance(request, memory, t, step, next);
            memory = next;
            t += static_cast<double>(step);
            if (!advanced.ok()) {
                continue;
            }
            const bool switched = provider.counters().switches > before;
            if (!provider.pose(memory, rig.skeleton, out).ok()) {
                continue;
            }
            scene::poseToModel(rig.skeleton, out, matrices);
            const glm::vec3 l = glm::vec3(matrices[static_cast<std::size_t>(footL)][3]);
            const glm::vec3 r = glm::vec3(matrices[static_cast<std::size_t>(footR)][3]);
            if (have) {
                const double moved =
                    std::max(glm::length(l - lastL), glm::length(r - lastR));
                run.worstFrame = std::max(run.worstFrame, moved);
                ++run.frames;
                if (switched) {
                    run.meanAtSwitch += moved;
                    run.worstAtSwitch = std::max(run.worstAtSwitch, moved);
                    ++run.switches;
                }
            }
            lastL = l;
            lastR = r;
            have = true;
        }
        if (run.switches > 0) {
            run.meanAtSwitch /= run.switches;
        }
        return run;
    };

    entity::MatchSettings shipping;
    entity::MatchSettings shippingOff = shipping;
    shippingOff.inertializeHalflife = 0.0f;
    // **The stress arm, and why it is here rather than in a follow-up.** ADR-612's 0.3566 m was
    // taken by a loop that searched every step with no continuation lock and no switch margin, and
    // drove the query with a different clip's feature vector. The shipping loop has all three
    // guards, so if the before-figure here comes out smaller the guards are the obvious suspect --
    // and "the obvious suspect" is a hypothesis, not a finding, until the guards are removed and
    // the number moves. So they are removed, and the number is reported either way.
    //
    // The halflife is re-derived for this configuration rather than carried over: with no
    // continuation lock the soonest a switch can follow a switch is the next frame, and a halflife
    // derived against 0.2 s of room would be answering a question this arm is not asking.
    entity::MatchSettings loose;
    loose.searchInterval = 0.0f;
    loose.minimumContinuation = 0.0f;
    loose.switchMargin = 0.0f;
    loose.inertializeHalflife = entity::derivedInertializeHalflife(
        loose.minimumContinuation, entity::kBlendBudgetFrameSeconds);
    entity::MatchSettings looseOff = loose;
    looseOff.inertializeHalflife = 0.0f;

    // **How deep the memory has to be is a measurement, not a preference.** Each level keeps one
    // more interrupted transition decaying instead of dropping it, and each costs two integers and
    // a clock in `MotionMemory`. The depths are run side by side so the table says what each one
    // bought rather than leaving the shipping value to look inevitable.
    entity::MatchSettings depth1 = shipping;
    depth1.blendSlots = 1;
    entity::MatchSettings depth2 = shipping;
    depth2.blendSlots = 2;

    const Run off = drive(shippingOff);
    const Run onDepth1 = drive(depth1);
    const Run onDepth2 = drive(depth2);
    const Run on = drive(shipping); // the shipping depth
    const Run stressOff = drive(looseOff);
    const Run stressOn = drive(loose);

    WARN(fmt::format("§32 threshold, re-derived: foot.l moves a median {:.4f} m and at most "
                     "{:.4f} m between two ordinary 1/30 s frames of 'Walking'",
                     threshold, worstOrdinary));
    WARN(fmt::format("§32 derived halflife {:.4f} s (continuation {:.2f} s, budget frame {:.4f} s)",
                     entity::MatchSettings{}.inertializeHalflife,
                     entity::MatchSettings::kDefaultContinuation,
                     entity::kBlendBudgetFrameSeconds));
    WARN(fmt::format("{:<22} {:>9} {:>14} {:>14} {:>14}", "arm", "switches", "mean at switch",
                     "worst at switch", "worst frame"));
    WARN(fmt::format("{:<22} {:>9} {:>12.4f} m {:>12.4f} m {:>12.4f} m", "blend OFF (before)",
                     off.switches, off.meanAtSwitch, off.worstAtSwitch, off.worstFrame));
    WARN(fmt::format("{:<22} {:>9} {:>12.4f} m {:>12.4f} m {:>12.4f} m", "blend ON, 1 slot",
                     onDepth1.switches, onDepth1.meanAtSwitch, onDepth1.worstAtSwitch,
                     onDepth1.worstFrame));
    WARN(fmt::format("{:<22} {:>9} {:>12.4f} m {:>12.4f} m {:>12.4f} m", "blend ON, 2 slots",
                     onDepth2.switches, onDepth2.meanAtSwitch, onDepth2.worstAtSwitch,
                     onDepth2.worstFrame));
    WARN(fmt::format("{:<22} {:>9} {:>12.4f} m {:>12.4f} m {:>12.4f} m", "blend ON, 3 slots",
                     on.switches, on.meanAtSwitch, on.worstAtSwitch, on.worstFrame));
    WARN(fmt::format("{:<22} {:>9} {:>12.4f} m {:>12.4f} m {:>12.4f} m", "no hysteresis, OFF",
                     stressOff.switches, stressOff.meanAtSwitch, stressOff.worstAtSwitch,
                     stressOff.worstFrame));
    WARN(fmt::format("{:<22} {:>9} {:>12.4f} m {:>12.4f} m {:>12.4f} m", "no hysteresis, ON",
                     stressOn.switches, stressOn.meanAtSwitch, stressOn.worstAtSwitch,
                     stressOn.worstFrame));
    WARN(fmt::format("§32 halflife for the stress arm: {:.4f} s (continuation 0 s)",
                     loose.inertializeHalflife));
    WARN(fmt::format("§32 VERDICT: mean jump at a transition {:.4f} m against a threshold of "
                     "{:.4f} m -- {:.2f}x the bar, {:.2f}x a normal frame step; before was "
                     "{:.4f} m ({:.1f}x)",
                     on.meanAtSwitch, threshold, on.meanAtSwitch / threshold,
                     on.meanAtSwitch / threshold, off.meanAtSwitch, off.meanAtSwitch / threshold));

    // The loop actually transitioned, in both arms, and the arms saw the same transitions: the
    // blend is in `pose`, so it cannot change what `advance` selected.
    CHECK(on.switches > 20);
    CHECK(off.switches == on.switches);
    // **The threshold, asserted rather than reported.** ADR-612 fixed it before any fix existed so
    // that it could not be relaxed once one did.
    CHECK(on.meanAtSwitch <= static_cast<double>(threshold));
    // And the anti-smear companion: the worst single frame of the whole run, which a blend that
    // merely spread the teleport out would fail while passing the line above.
    CHECK(on.worstFrame <= off.worstFrame);
    // The stress arm is **reported, not held to the bar**: it is a configuration nothing ships,
    // and its purpose is to explain the difference between this harness's before-figure and
    // ADR-612's rather than to be passed.
    //
    // **It does not come out clean, and the assertion says which half it is.** With a search on
    // every frame the mean at a switch goes *up* -- the chain keeps three offsets alive at once
    // and their combined decay adds a little motion to the typical re-selection, which in that
    // regime is a one-frame nudge rather than a transition. What it buys is the other end: the
    // worst single frame of the run halves. Both are asserted, in the direction each actually
    // goes, rather than one of them being left out of the record.
    WARN(fmt::format("§32 in the no-hysteresis regime the blend is a TRADE, not a win: mean at a "
                     "switch {:+.1f}% ({:.4f} -> {:.4f} m, {:.1f}% of the bar), worst frame "
                     "{:+.1f}% ({:.4f} -> {:.4f} m)",
                     100.0 * (stressOn.meanAtSwitch - stressOff.meanAtSwitch) /
                         std::max(stressOff.meanAtSwitch, 1e-9),
                     stressOff.meanAtSwitch, stressOn.meanAtSwitch,
                     100.0 * (stressOn.meanAtSwitch - stressOff.meanAtSwitch) / threshold,
                     100.0 * (stressOn.worstFrame - stressOff.worstFrame) /
                         std::max(stressOff.worstFrame, 1e-9),
                     stressOff.worstFrame, stressOn.worstFrame));
    CHECK(stressOn.worstFrame < stressOff.worstFrame);
    // The cost of the mean going the wrong way there, stated as a bound rather than as a hope: it
    // is small against the quantity the threshold is about.
    CHECK(stressOn.meanAtSwitch - stressOff.meanAtSwitch < 0.05 * static_cast<double>(threshold));
}

TEST_CASE("§32: what the blend costs the presentation half", "[loop][phaseC][aliens]") {
    // **Measure the cost before moving a shipping default.** `pose` runs once per drawn frame per
    // character, and inertialization makes it sample the clips at both ends of every live
    // transition instead of sampling one clip once -- up to six extra `sampleClip` calls at three
    // slots. That is a real multiplier on the presentation half and it is not allowed to be a
    // surprise.
    Fixture fixture;
    if (!fixture.ok) {
        SKIP("the Glowmere alien is not present");
    }
    const scene::SkinnedRig& rig = fixture.sc.rigs.front();
    const auto cost = [&](std::size_t slots, float halflife) {
        entity::MatchSettings settings;
        settings.blendSlots = std::max<std::size_t>(slots, 1);
        settings.inertializeHalflife = halflife;
        entity::MatchMotionProvider provider(&fixture.db, &rig.clips, "match");
        provider.setSettings(settings);
        // A memory with every slot live, which is the worst case rather than the average one.
        entity::MotionMemory memory;
        memory.selection = 0;
        memory.generation = 1;
        for (std::size_t i = 0; i < entity::MotionMemory::kBlendSlots; ++i) {
            memory.pushBlend(static_cast<std::uint32_t>(10u + i),
                             fixture.db.sampleTime[10u + i], static_cast<std::uint32_t>(40u + i),
                             fixture.db.sampleTime[40u + i], entity::MotionMemory::kBlendSlots);
            memory.blends[0].elapsed = 0.01f * static_cast<float>(i);
        }
        scene::Pose out;
        double best = std::numeric_limits<double>::max();
        for (int repeat = 0; repeat < 5; ++repeat) {
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 2000; ++i) {
                (void)provider.pose(memory, rig.skeleton, out);
            }
            const auto t1 = std::chrono::steady_clock::now();
            best = std::min(
                best,
                std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(t1 - t0)
                        .count() /
                    2000.0);
        }
        return best;
    };
    const double none = cost(1, 0.0f);
    const double one = cost(1, entity::MatchSettings{}.inertializeHalflife);
    const double three = cost(3, entity::MatchSettings{}.inertializeHalflife);
    WARN(fmt::format("§32 pose cost per character per drawn frame: {:.2f} us unblended, {:.2f} us "
                     "at 1 slot ({:.2f}x), {:.2f} us at 3 live slots ({:.2f}x). Every slot live, "
                     "which is not a rare case: see the run average below.",
                     none, one, one / none, three, three / none));
    WARN(fmt::format("§32 at 60 Hz that is {:.0f} characters per frame unblended and {:.0f} with "
                     "three live slots, posing only",
                     16666.0 / none, 16666.0 / three));

    // **The worst case above is not the bill.** A slot is only sampled while it is live, and a
    // blend at the shipping halflife is spent in about a quarter of a second, so most drawn frames
    // pay for fewer than three. The average over a real run is the number that decides whether
    // Glowmere can afford this, and it is measured rather than interpolated between the two above.
    const auto averageOverRun = [&](float halflife) {
        entity::MatchSettings settings;
        settings.inertializeHalflife = halflife;
        entity::MatchMotionProvider provider(&fixture.db, &rig.clips, "match");
        provider.setSettings(settings);
        entity::MotionMemory memory;
        scene::Pose out;
        const float step = entity::kBlendBudgetFrameSeconds;
        double t = 0.0;
        double total = 0.0;
        int posed = 0;
        for (int f = 0; f < 1800; ++f) {
            entity::MotionRequest request;
            const auto phase = static_cast<float>(t * 0.5);
            request.desiredVelocity =
                glm::vec3(std::sin(phase) * 1.2f, 0.0f, std::cos(phase) * 1.2f);
            request.desiredFacing = glm::normalize(request.desiredVelocity);
            entity::MotionMemory next;
            (void)provider.advance(request, memory, t, step, next);
            memory = next;
            t += static_cast<double>(step);
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = provider.pose(memory, rig.skeleton, out).ok();
            const auto t1 = std::chrono::steady_clock::now();
            if (ok) {
                total +=
                    std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(t1 - t0)
                        .count();
                ++posed;
            }
        }
        return posed > 0 ? total / posed : 0.0;
    };
    const double runOff = averageOverRun(0.0f);
    const double runOn = averageOverRun(entity::MatchSettings{}.inertializeHalflife);
    WARN(fmt::format("§32 averaged over the shipping loop: {:.2f} us unblended, {:.2f} us blended "
                     "({:.2f}x) -- {:.0f} characters per 60 Hz frame, posing only",
                     runOff, runOn, runOn / std::max(runOff, 1e-9), 16666.0 / runOn));
    CHECK(none > 0.0);
    CHECK(three >= none);
    CHECK(runOn >= runOff);
}

TEST_CASE("§32 companion: a database sample time is already absolute", "[loop][phaseC][aliens]") {
    // **Why this exists next to §32.** ADR-612's before-figure was taken by §30's harness, which
    // poses a database sample with `sampleClip(clip, clip.start + db.sampleTime[s], pose)`.
    // `buildMotionDatabase` writes `sampleTime` as `clip.start + f * dt` -- already absolute -- and
    // `MatchMotionProvider::pose` reads it as absolute. Two conventions, one name, which is the
    // hazard this phase has now paid for four times.
    //
    // **It is only a defect if `clip.start` is non-zero**, and whether it is is a property of the
    // file rather than of anybody's reasoning, so it is asserted rather than assumed. Blender's
    // glTF exporter writes the frame range it was given (see `AnimationClip::start`), so it can be
    // either.
    Fixture fixture;
    if (!fixture.ok) {
        SKIP("the Glowmere alien is not present");
    }
    const scene::SkinnedRig& rig = fixture.sc.rigs.front();
    float worstStart = 0.0f;
    for (const scene::AnimationClip& c : rig.clips) {
        worstStart = std::max(worstStart, c.start);
    }
    WARN(fmt::format("largest clip.start on this rig: {:.4f} s; clip[0] '{}' start {:.4f} "
                     "duration {:.4f}; db.sampleTime[0] {:.4f}",
                     worstStart, rig.clips.front().name, rig.clips.front().start,
                     rig.clips.front().duration, fixture.db.sampleTime[0]));
    // The convention, asserted: the first sample of a clip is that clip's own start.
    for (std::uint32_t s = 1; s < fixture.db.sampleCount(); ++s) {
        if (fixture.db.sampleClip[s] != fixture.db.sampleClip[s - 1u]) {
            const scene::AnimationClip& c = rig.clips[fixture.db.sampleClip[s]];
            CHECK(fixture.db.sampleTime[s] == Approx(c.start));
            break;
        }
    }
    CHECK(fixture.db.sampleTime[0] == Approx(rig.clips[fixture.db.sampleClip[0]].start));
}

TEST_CASE("§32's other provider: the clip provider is the one the product can actually reach",
          "[loop][phaseC][aliens]") {
    // **Read this before the number, because the number is not the finding.**
    //
    // `MatchMotionProvider` appears nowhere in `src/` outside its own two files.
    // `Composition::AnimationSink::buildChain` adds exactly one provider, `clipProvider_`, so a
    // body with `proceduralMotion` on is posed by the **clip** provider and the matcher is
    // unreachable from the product. Whatever §32 did for the matcher, it did not do for the
    // provider Glowmere would actually run.
    //
    // And `ClipMotionProvider`'s header states, as a design position, that it does not blend
    // "because those are the player's job and duplicating them here would be a second answer to a
    // question already settled". **That rationale is void in the case that matters**: when the
    // provider poses the body, `AnimationPlayer::evaluate` is not called at all
    // (`SkinnedRig::evaluate` takes the external pose instead), so there is no player to defer to
    // and no second answer to duplicate -- there is no first answer.
    //
    // This measures what that costs, against the same bar, so the decision about whether to change
    // that position is made with a figure rather than from the argument alone.
    Fixture fixture;
    if (!fixture.ok) {
        SKIP("the Glowmere alien is not present");
    }
    const scene::SkinnedRig& rig = fixture.sc.rigs.front();
    const int footL = rig.skeleton.find("foot.l");
    const int footR = rig.skeleton.find("foot.r");
    REQUIRE(footL >= 0);
    REQUIRE(footR >= 0);

    // The bar, re-derived from the same clip and the same frame length as §32's.
    const scene::AnimationClip* walk = nullptr;
    for (const scene::AnimationClip& c : rig.clips) {
        if (c.name == "Walking") {
            walk = &c;
            break;
        }
    }
    REQUIRE(walk != nullptr);
    const float step = entity::kBlendBudgetFrameSeconds;
    {
        scene::Pose pose;
        std::vector<glm::mat4> model;
        std::vector<float> steps;
        glm::vec3 previous{0.0f};
        const int frames = static_cast<int>(walk->length() / step);
        for (int f = 0; f <= frames; ++f) {
            scene::setRestPose(rig.skeleton, pose);
            scene::sampleClip(*walk, walk->start + (static_cast<float>(f) * step), pose);
            scene::poseToModel(rig.skeleton, pose, model);
            const glm::vec3 p = glm::vec3(model[static_cast<std::size_t>(footL)][3]);
            if (f > 0) {
                steps.push_back(glm::length(p - previous));
            }
            previous = p;
        }
        std::sort(steps.begin(), steps.end());
        const float threshold = steps[steps.size() / 2];

        // Three entries, as `buildChain` builds them: idle, walk, run.
        const auto measure = [&](float halflife) {
        entity::ClipMotionProvider provider(&rig.clips, "clip");
        provider.setInertializeHalflife(halflife);
        const auto entry = [&](const char* name, float authored) {
            const int index = scene::findClip(rig.clips, name);
            if (index < 0) {
                return false;
            }
            entity::ClipEntry e;
            e.clip = static_cast<std::uint32_t>(index);
            e.mode = entity::MovementMode::Ground;
            e.authoredSpeed = authored;
            e.loop = true;
            provider.addEntry(std::move(e));
            return true;
        };
        REQUIRE(entry("Idle", 0.0f));
        REQUIRE(entry("Walking", 1.0f));
        REQUIRE(entry("Running", 3.0f));

        // A body that changes gait, which is what ADR-612 says Glowmere's aliens do constantly.
        // The dwell is `GaitSettings::minDwell`'s default, so the changes are no more frequent
        // than the tier above would allow.
        entity::MotionMemory memory;
        scene::Pose out;
        std::vector<glm::mat4> matrices;
        glm::vec3 lastL{0.0f};
        glm::vec3 lastR{0.0f};
        bool have = false;
        double total = 0.0;
        double worst = 0.0;
        double worstFrame = 0.0;
        int changes = 0;
        double t = 0.0;
        const float speeds[3] = {0.0f, 1.0f, 3.0f};
        for (int f = 0; f < 1800; ++f) {
            entity::MotionRequest request;
            const float want = speeds[(f / 15) % 3]; // a gait change every 0.5 s
            request.desiredVelocity = glm::vec3(0.0f, 0.0f, want);
            request.desiredFacing = glm::vec3(0.0f, 0.0f, 1.0f);
            entity::MotionMemory next;
            if (!provider.advance(request, memory, t, step, next).ok()) {
                continue;
            }
            const bool changed = next.generation != memory.generation;
            memory = next;
            t += static_cast<double>(step);
            if (!provider.pose(memory, rig.skeleton, out).ok()) {
                continue;
            }
            scene::poseToModel(rig.skeleton, out, matrices);
            const glm::vec3 l = glm::vec3(matrices[static_cast<std::size_t>(footL)][3]);
            const glm::vec3 r = glm::vec3(matrices[static_cast<std::size_t>(footR)][3]);
            if (have) {
                const double moved = std::max(glm::length(l - lastL), glm::length(r - lastR));
                // **The anti-smear figure.** An inertialized pose at the instant of a change is
                // exactly the outgoing pose, so the foot is held for one frame and the
                // change-frame metric reads near zero -- as it would for a blend that merely
                // deferred the same displacement. This counts it wherever it ends up.
                worstFrame = std::max(worstFrame, moved);
                if (changed) {
                    total += moved;
                    worst = std::max(worst, moved);
                    ++changes;
                }
            }
            lastL = l;
            lastR = r;
            have = true;
        }
        const double mean = changes > 0 ? total / changes : 0.0;
        return std::tuple<double, double, int, double>{mean, worst, changes, worstFrame};
        };

        const auto [beforeMean, beforeWorst, beforeChanges, beforeWorstFrame] = measure(0.0f);
        entity::ClipMotionProvider defaults;
        const auto [afterMean, afterWorst, afterChanges, afterWorstFrame] =
            measure(defaults.inertializeHalflife());

        WARN(fmt::format("§32 CLIP PROVIDER halflife {:.4f} s, derived from the gait tier's "
                         "minimum dwell of {:.2f} s",
                         defaults.inertializeHalflife(), entity::GaitSettings{}.minDwell));
        WARN(fmt::format("§32 CLIP PROVIDER before: {} gait changes, mean foot jump {:.4f} m, "
                         "worst {:.4f} m -- {:.1f}x the {:.4f} m bar",
                         beforeChanges, beforeMean, beforeWorst, beforeMean / threshold,
                         threshold));
        WARN(fmt::format("§32 CLIP PROVIDER after:  {} gait changes, mean foot jump {:.4f} m, "
                         "worst {:.4f} m -- {:.2f}x the bar ({:+.1f}% mean)",
                         afterChanges, afterMean, afterWorst, afterMean / threshold,
                         100.0 * (afterMean - beforeMean) / std::max(beforeMean, 1e-9)));
        WARN(fmt::format("§32 CLIP PROVIDER worst single frame anywhere in the run: {:.4f} m "
                         "before, {:.4f} m after ({:+.1f}%) -- {:.2f}x the bar. The means above "
                         "are not readable without this.",
                         beforeWorstFrame, afterWorstFrame,
                         100.0 * (afterWorstFrame - beforeWorstFrame) /
                             std::max(beforeWorstFrame, 1e-9),
                         afterWorstFrame / threshold));
        WARN("§32: this provider, not the matcher, is the one `buildChain` installs. The matcher "
             "is not constructed anywhere in src/. See ADR-613's consequences.");
        CHECK(beforeChanges > 10);
        CHECK(beforeChanges == afterChanges); // the blend is in `pose`; it changes no selection
        // The defect, recorded in the direction it actually went before the fix...
        CHECK(beforeMean > static_cast<double>(threshold));
        // ...and the same bar ADR-612 fixed, applied to the provider the product installs.
        CHECK(afterMean <= static_cast<double>(threshold));
        CHECK(afterWorstFrame < beforeWorstFrame);
    }
}
