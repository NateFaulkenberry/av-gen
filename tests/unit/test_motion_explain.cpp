// Phase C §68 (debug visualization), §69 (why did it choose this motion?) and §72 (offline baking).
//
// §69's test is whether each of its named causes can be told apart. So each cause gets a case that
// is constructed to be decided by that cause and nothing else, and the explanation must name it.

#include "entity/match_motion_provider.hpp"
#include "entity/motion_bake.hpp"
#include "scene/motion_match_explain.hpp"

#include "support/motion_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstring>

using namespace avgen;
using scene::MatchCause;

namespace {

scene::MotionDatabase probeDb() {
    auto db = scene::buildMotionDatabase(testsupport::probePack(), testsupport::probeOptions());
    REQUIRE(db.has_value());
    return std::move(*db);
}

scene::MotionQuery queryAt(const scene::MotionDatabase& db, std::uint32_t sample, std::uint32_t current,
                           float nudge = 0.05f) {
    scene::MotionQuery q;
    q.features.assign(db.featuresFor(sample), db.featuresFor(sample) + db.dimension);
    for (std::size_t d = 0; d < q.features.size(); ++d) {
        q.features[d] += nudge * static_cast<float>(static_cast<int>(d % 3) - 1);
    }
    q.current = current;
    return q;
}

// 0..30 are "Walking", 31..61 "Running" in the probe database.
constexpr std::uint32_t kWalk = 7;
constexpr std::uint32_t kRun = 40;

} // namespace

TEST_CASE("§69: the explanation's cost is the search's cost, to the bit", "[motionexplain][phaseC]") {
    const scene::MotionDatabase db = probeDb();
    const scene::MotionCostWeights weights;
    int compared = 0;
    for (std::uint32_t s = 0; s < db.sampleCount(); s += 3) {
        for (const std::uint32_t current : {scene::MotionDatabase::kInvalid, (s + 11) % db.sampleCount()}) {
            const scene::MotionQuery q = queryAt(db, s, current);
            const scene::MotionMatch m = scene::searchMotion(db, q, weights);
            REQUIRE(m.found());
            const scene::MotionMatchExplanation e = scene::explainMotionMatch(db, q, weights);
            CHECK(e.selected.sample == m.sample);
            CHECK(std::bit_cast<std::uint32_t>(e.selected.total) == std::bit_cast<std::uint32_t>(m.cost));
            ++compared;
        }
    }
    CHECK(compared > 30);
}

TEST_CASE("§69: each cause is named when it is the one that decided", "[motionexplain][phaseC]") {
    const scene::MotionDatabase db = probeDb();
    const scene::MotionCostWeights weights;
    const auto walkTag = static_cast<std::uint32_t>(scene::MotionTag::Walk);
    const auto runTag = static_cast<std::uint32_t>(scene::MotionTag::Run);
    REQUIRE((db.sampleTags[kWalk] & walkTag) != 0u);
    REQUIRE((db.sampleTags[kRun] & runTag) != 0u);

    SECTION("filtering") {
        // Asks for a running pose but only admits walks: the best sample is filtered out.
        scene::MotionQuery q = queryAt(db, kRun, scene::MotionDatabase::kInvalid);
        q.requireTags = walkTag;
        const scene::MotionMatchExplanation e = scene::explainMotionMatch(db, q, weights);
        REQUIRE(e.selected.valid());
        CHECK(db.sampleClip[e.selected.sample] == 0u);
        REQUIRE(e.bestFiltered.valid());
        CHECK(db.sampleClip[e.bestFiltered.sample] == 1u);
        CHECK(e.has(MatchCause::Filtering));
        // And a query the filter does not bind on does not blame it.
        const scene::MotionMatchExplanation free =
            scene::explainMotionMatch(db, queryAt(db, kWalk, scene::MotionDatabase::kInvalid), weights);
        CHECK_FALSE(free.has(MatchCause::Filtering));
    }
    SECTION("continuity, and the margin that would have held it") {
        // Walking now, and the query looks like a run: the matcher switches, and says what paid for it.
        const scene::MotionQuery q = queryAt(db, kRun, kWalk);
        scene::MotionExplainOptions options;
        options.switchMargin = 100.0f; // larger than any saving: the provider would hold
        const scene::MotionMatchExplanation e = scene::explainMotionMatch(db, q, weights, options);
        REQUIRE(e.continuation.valid());
        CHECK(e.continuation.sample == kWalk + 1u);
        CHECK(e.selected.sample != e.continuation.sample);
        CHECK(e.has(MatchCause::Continuity));
        CHECK(e.heldByMargin);
        options.switchMargin = 0.0f;
        CHECK_FALSE(scene::explainMotionMatch(db, q, weights, options).heldByMargin);
    }
    SECTION("weights") {
        // A walking pose moving at running speed. The root-velocity weight decides which half of the
        // query wins; the explanation must say it was the weight, not the features.
        scene::MotionQuery q = queryAt(db, kWalk, scene::MotionDatabase::kInvalid, 0.0f);
        const std::vector<scene::MotionFeatureGroup> layout = scene::motionFeatureLayout(db.config);
        for (std::size_t d = 0; d < layout.size(); ++d) {
            if (layout[d] == scene::MotionFeatureGroup::RootVelocity) {
                q.features[d] = db.featuresFor(kRun)[d];
            }
        }
        scene::MotionDatabase heavy = db;
        heavy.config.rootVelocityWeight = 50.0f;
        const scene::MotionMatch atOne = scene::searchMotion(db, q, weights);
        const scene::MotionMatch atFifty = scene::searchMotion(heavy, q, weights);
        // The precondition that makes this a test of the weight: it flips the answer.
        REQUIRE(db.sampleClip[atOne.sample] != heavy.sampleClip[atFifty.sample]);
        const scene::MotionMatchExplanation e = scene::explainMotionMatch(heavy, q, weights);
        INFO(e.report(heavy, q));
        CHECK(e.has(MatchCause::Weights));
        CHECK_FALSE(scene::explainMotionMatch(db, queryAt(db, kWalk, scene::MotionDatabase::kInvalid), weights)
                        .has(MatchCause::Weights));
    }
    SECTION("search approximation") {
        scene::MotionSearchPlan coarse;
        coarse.stride = 16;
        coarse.shortlist = 1;
        coarse.neighbourhood = 0;
        int differing = 0;
        for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
            const scene::MotionQuery q = queryAt(db, s, scene::MotionDatabase::kInvalid, 0.0f);
            scene::MotionExplainOptions staged;
            staged.plan = coarse;
            const scene::MotionMatchExplanation e = scene::explainMotionMatch(db, q, weights, staged);
            const bool differs = scene::searchMotionStaged(db, q, weights, coarse).sample !=
                                 scene::searchMotion(db, q, weights).sample;
            CHECK(e.has(MatchCause::Approximation) == differs);
            differing += differs ? 1 : 0;
            // An exhaustive search never blames an approximation.
            CHECK_FALSE(scene::explainMotionMatch(db, q, weights).has(MatchCause::Approximation));
        }
        CHECK(differing > 0);
    }
    SECTION("coverage") {
        scene::MotionQuery q = queryAt(db, kWalk, scene::MotionDatabase::kInvalid, 0.0f);
        for (float& v : q.features) {
            v += 8.0f; // nowhere near anything in the database
        }
        CHECK(scene::explainMotionMatch(db, q, weights).has(MatchCause::Coverage));
        CHECK_FALSE(scene::explainMotionMatch(db, queryAt(db, kWalk, scene::MotionDatabase::kInvalid, 0.0f),
                                              weights)
                        .has(MatchCause::Coverage));
    }
}

TEST_CASE("§68: the debug panel names both samples, the costs and the caveat", "[motionexplain][phaseC]") {
    const scene::MotionDatabase db = probeDb();
    const scene::MotionQuery q = queryAt(db, kRun, kWalk);
    const scene::MotionMatchExplanation e = scene::explainMotionMatch(db, q, scene::MotionCostWeights{});
    const std::string text = e.report(db, q);
    INFO(text);
    CHECK(text.find("Current:   Walking @ 0.23s (sample 7)") != std::string::npos);
    CHECK(text.find("Selected:  Running") != std::string::npos);
    CHECK(text.find("Trajectory +0.20s") != std::string::npos);
    CHECK(text.find("TOTAL") != std::string::npos);
    CHECK(text.find("carry on") != std::string::npos);
    CHECK(text.find(scene::MotionCostBreakdown::caveat()) != std::string::npos);
}

TEST_CASE("§69: the provider's own query is what gets explained", "[motionexplain][phaseC]") {
    const scene::MotionPack pack = testsupport::probePack();
    const scene::MotionDatabase db = probeDb();
    entity::MatchMotionProvider provider(&db, &pack.animation, "match");
    entity::MatchSettings settings;
    settings.switchMargin = 0.0f; // so a search's winner is what advance selects
    provider.setSettings(settings);
    entity::MotionMemory memory;
    double time = 0.0;
    int checked = 0;
    for (int f = 0; f < 600; ++f) {
        entity::MotionRequest request;
        request.desiredVelocity = glm::vec3(0.0f, 0.0f, (f / 60) % 2 == 0 ? 1.2f : 3.0f);
        const auto query = provider.queryFor(request, memory);
        REQUIRE(query.has_value());
        const std::uint64_t searches = provider.counters().searches;
        const std::uint64_t held = provider.counters().heldByMargin;
        entity::MotionMemory next;
        REQUIRE(provider.advance(request, memory, time, 1.0f / 60.0f, next).ok());
        if (provider.counters().searches > searches && provider.counters().heldByMargin == held) {
            const scene::MotionMatchExplanation e = scene::explainMotionMatch(db, *query, settings.weights);
            CHECK(e.selected.sample == next.selection);
            ++checked;
        }
        memory = next;
        time += 1.0 / 60.0;
    }
    CHECK(checked > 10);
}

TEST_CASE("§72: a baked session replays the session's poses, and bakes identically twice",
          "[motionexplain][bake][phaseC]") {
    const scene::MotionPack pack = testsupport::probePack();
    const scene::MotionDatabase db = probeDb();
    const entity::MatchMotionProvider provider(&db, &pack.animation, "match");
    const entity::MotionScript script = [](std::uint32_t step, double) {
        entity::MotionRequest r;
        r.desiredVelocity = glm::vec3(0.0f, 0.0f, step < 45 ? 1.2f : 3.0f);
        return r;
    };
    entity::MotionBakeOptions options;
    options.seconds = 3.0f;
    const auto baked = entity::bakeMotionSession(provider, pack.skeleton, script, options);
    REQUIRE(baked.has_value());
    REQUIRE(baked->steps == 91);
    CHECK(baked->declined == 0);
    REQUIRE(baked->clip.channels.size() == pack.skeleton.joints.size() * 3u);
    // The session switched clips at least once, so the bake is not one clip replayed.
    bool sawWalk = false;
    bool sawRun = false;
    for (const entity::MotionMemory& m : baked->memories) {
        sawWalk = sawWalk || db.sampleClip[m.selection] == 0u;
        sawRun = sawRun || db.sampleClip[m.selection] == 1u;
    }
    CHECK(sawWalk);
    CHECK(sawRun);

    // At every key, the baked clip is the provider's pose from the memory the session held.
    scene::Pose fromBake;
    scene::Pose fromProvider;
    float worst = 0.0f;
    for (std::uint32_t i = 0; i < baked->steps; ++i) {
        scene::setRestPose(pack.skeleton, fromBake);
        // At the key's own time. `static_cast<float>(i) / rate` is not that time: the bake keys
        // `float(i * double(dt))`, which at i = 90 is 3.00000016 against 3.0. The difference put the
        // sample a hair into the next segment, and across a clip wrap, where the root jumps a
        // cycle's travel between keys, that hair read as 2e-5 m of disagreement.
        scene::sampleClip(baked->clip, baked->clip.channels[0].times[i], fromBake);
        REQUIRE(provider.pose(baked->memories[i], pack.skeleton, fromProvider).ok());
        for (std::size_t j = 0; j < fromBake.local.size(); ++j) {
            worst = std::max(worst, glm::length(fromBake.local[j].position - fromProvider.local[j].position));
        }
    }
    CHECK(worst < 1e-5f);

    const auto again = entity::bakeMotionSession(provider, pack.skeleton, script, options);
    REQUIRE(again.has_value());
    for (std::size_t c = 0; c < baked->clip.channels.size(); ++c) {
        const auto& a = baked->clip.channels[c].values;
        const auto& b = again->clip.channels[c].values;
        REQUIRE(a.size() == b.size());
        CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(glm::vec4)) == 0);
    }

    // A provider with nothing to play still yields a clip of the session's length, and says so.
    const entity::MatchMotionProvider empty;
    const auto declined = entity::bakeMotionSession(empty, pack.skeleton, script, options);
    REQUIRE(declined.has_value());
    CHECK(declined->declined == declined->steps);
}
