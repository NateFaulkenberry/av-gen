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
