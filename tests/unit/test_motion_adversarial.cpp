// Phase C §47: adversarial tests, on the golden corpus (§79).
//
// "Do not only test easy cases", and "ensure a broken matcher cannot pass because the expected answer
// happens to be the no-op". So every case below comes in a pair whose answers differ: forward and
// back, left and right, fast and slow, contact on and contact off. A matcher that always returned
// the first sample, the continuation, or the same clip whatever it was asked would fail one arm of
// every pair. And the first clip in the corpus is `Idle`, which is the right answer to exactly one
// case here.

#include "entity/match_motion_provider.hpp"
#include "scene/motion_database.hpp"
#include "support/golden_motion.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <cmath>
#include <limits>
#include <string>

using namespace avgen;

namespace {

struct Golden {
    scene::MotionPack pack = testsupport::goldenPack();
    scene::MotionDatabase db;
    entity::MatchMotionProvider matcher;
    Golden() : Golden(testsupport::goldenOptions()) {}
    explicit Golden(const scene::MotionDatabaseOptions& options) {
        auto built = scene::buildMotionDatabase(pack, options);
        REQUIRE(built.has_value());
        db = std::move(*built);
        matcher.setDatabase(&db);
        matcher.setClips(&pack.animation);
    }
    // The clip a first selection picks for `velocity` (world space, body facing `facing`).
    std::string pick(glm::vec3 velocity, glm::vec3 facing = {0.0f, 0.0f, 1.0f}) const {
        entity::MotionRequest request;
        request.desiredVelocity = velocity;
        request.bodyFacing = facing;
        // Keep facing where the body faces: the question is how to move, not whether to turn.
        // (Since extraction v6 the trajectory's facing is the future facing, so a request left at
        // the default +Z would be asking an east-facing body to turn north.)
        request.desiredFacing = facing;
        entity::MotionMemory memory;
        entity::MotionMemory next;
        const entity::MotionResult r = matcher.advance(request, memory, 0.0, 1.0f / 60.0f, next);
        REQUIRE(r.ok());
        return std::string(r.content);
    }
    // The sample a first selection picks, and how that sample is moving (raw, body frame).
    std::uint32_t pickSample(glm::vec3 velocity) const {
        entity::MotionRequest request;
        request.desiredVelocity = velocity;
        entity::MotionMemory memory;
        entity::MotionMemory next;
        REQUIRE(matcher.advance(request, memory, 0.0, 1.0f / 60.0f, next).ok());
        return next.selection;
    }
    glm::vec3 velocityOf(std::uint32_t sample) const {
        const auto layout = scene::motionFeatureLayout(db.config);
        std::size_t rv = 0;
        while (rv < layout.size() && layout[rv] != scene::MotionFeatureGroup::RootVelocity) {
            ++rv;
        }
        const float* f = db.featuresFor(sample);
        const auto raw = [&](std::size_t d) { return db.scale[d] != 0.0f ? (f[d] / db.scale[d]) + db.mean[d] : f[d]; };
        return {raw(rv), raw(rv + 1), raw(rv + 2)};
    }
};

const glm::vec3 kForward(0.0f, 0.0f, 1.0f);
const glm::vec3 kLeft(1.0f, 0.0f, 0.0f); // +X is the body's left, facing +Z, in this Y-up right-handed frame

} // namespace

TEST_CASE("§47 stationary and near-zero queries find a body at rest, and a walk does not",
          "[adversarial][matching][phaseC]") {
    // **Judged by how the chosen sample moves, not by its clip's name.** The corpus has three places
    // a body stands still: Idle, the start of Start and the end of Stop. A near-zero request is
    // answered correctly by any of them, and 0.05 m/s is honestly nearer the end of Stop than Idle.
    // The first draft asserted "Idle" and failed for exactly that reason. The matcher was right.
    Golden g;
    const glm::vec3 still = g.velocityOf(g.pickSample(glm::vec3(0.0f)));
    const glm::vec3 creep = g.velocityOf(g.pickSample(kForward * 0.05f));
    const glm::vec3 walk = g.velocityOf(g.pickSample(kForward * testsupport::kGoldenWalk));
    WARN(fmt::format("still -> {:.3f} m/s, creep 0.05 -> {:.3f} m/s, walk 1.2 -> {:.3f} m/s", glm::length(still),
                     glm::length(creep), glm::length(walk)));
    CHECK(glm::length(still) < 0.1f);
    CHECK(glm::length(creep) < 0.15f);
    // The other arm: Idle is the first clip, so a matcher returning sample 0 would pass the two
    // above. It fails this.
    CHECK(std::abs(walk.z - testsupport::kGoldenWalk) < 0.15f);
    CHECK(std::abs(walk.x) < 0.1f);
}

TEST_CASE("§47 a high-speed query finds the run, even beyond the corpus", "[adversarial][matching][phaseC]") {
    Golden g;
    CHECK(g.pick(kForward * testsupport::kGoldenRun) == "Run");
    CHECK(g.pick(kForward * 6.0f) == "Run"); // faster than anything here: the nearest is still the run
    CHECK(g.pick(kForward * testsupport::kGoldenWalk) != "Run");
}

TEST_CASE("§47 reverse and strafe find their own clips, in the body's frame", "[adversarial][matching][phaseC]") {
    Golden g;
    CHECK(g.pick(-kForward * testsupport::kGoldenWalk) == "Back");
    CHECK(g.pick(kLeft * testsupport::kGoldenWalk) == "StrafeLeft");
    CHECK(g.pick(-kLeft * testsupport::kGoldenWalk) == "StrafeRight");
    // Turned a quarter: facing east (+X), the body's own +X (its left) is world -Z. The same answers
    // must come back when the whole question is turned.
    const glm::vec3 east(1.0f, 0.0f, 0.0f);
    CHECK(g.pick(east * testsupport::kGoldenWalk, east) != "Back");
    CHECK(g.pick(-east * testsupport::kGoldenWalk, east) == "Back");
    CHECK(g.pick(glm::vec3(0.0f, 0.0f, -1.0f) * testsupport::kGoldenWalk, east) == "StrafeLeft");
}

TEST_CASE("§47 a sharp reversal is followed at the next search, not held by continuity",
          "[adversarial][matching][phaseC]") {
    // The adversarial part is the hysteresis (§28) and the continuation lock (§29): both exist to
    // hold motion, and neither may hold it against a request that reverses. Walking forward, then
    // asked to go back: within one search interval of the lock expiring, the matcher must be
    // playing Back. The control is the same run without the reversal, which must stay on Walk.
    Golden g;
    const auto run = [&](bool reverse) {
        entity::MotionMemory memory;
        std::string playing;
        for (int f = 0; f <= 60; ++f) {
            entity::MotionRequest request;
            request.desiredVelocity = kForward * ((reverse && f >= 30) ? -testsupport::kGoldenWalk : testsupport::kGoldenWalk);
            entity::MotionMemory next;
            const entity::MotionResult r =
                g.matcher.advance(request, memory, static_cast<double>(f) / 60.0, 1.0f / 60.0f, next);
            REQUIRE(r.ok());
            memory = next;
            playing = std::string(r.content);
        }
        return playing;
    };
    // Walking on stays on forward motion at walking pace (Walk, or the stretch of Start or Stop that
    // is walking), and the reversal lands on Back, the only clip that moves backward.
    const std::string on = run(false);
    CHECK(on != "Back");
    CHECK(on != "Idle");
    CHECK(run(true) == "Back");
}

TEST_CASE("§47 start and stop: standing asked to walk moves, walking asked to stand stops",
          "[adversarial][matching][phaseC]") {
    Golden g;
    const auto afterSwitch = [&](float from, float to) {
        entity::MotionMemory memory;
        std::string playing;
        for (int f = 0; f <= 60; ++f) {
            entity::MotionRequest request;
            request.desiredVelocity = kForward * (f < 30 ? from : to);
            entity::MotionMemory next;
            const entity::MotionResult r =
                g.matcher.advance(request, memory, static_cast<double>(f) / 60.0, 1.0f / 60.0f, next);
            REQUIRE(r.ok());
            memory = next;
            playing = std::string(r.content);
        }
        return playing;
    };
    const std::string started = afterSwitch(0.0f, testsupport::kGoldenWalk);
    const std::string stopped = afterSwitch(testsupport::kGoldenWalk, 0.0f);
    WARN(fmt::format("standing then walking plays {}; walking then standing plays {}", started, stopped));
    CHECK((started == "Walk" || started == "Start"));
    CHECK((stopped == "Idle" || stopped == "Stop"));
}

TEST_CASE("§47 contact transition: with the contact term on, the planted foot decides the tie",
          "[adversarial][matching][phaseC]") {
    // Two clips with identical motion whose contact labels disagree: `WalkSwapped` records the right
    // foot planted where `Walk` records the left. Listed FIRST, so a tie goes to it. From a `Walk`
    // sample with the left foot planted, a matcher that honours contacts must choose a `Walk` sample
    // (the labels agree), and a matcher that ignores them takes the tie, which is `WalkSwapped`.
    // So the same query gives different answers with the term on and off, and neither is the no-op.
    const auto build = [](float contactWeight) {
        scene::MotionPack pack;
        pack.name = "contacts";
        pack.skeleton = testsupport::goldenRig();
        pack.skeletonDigest = scene::skeletonDigest(pack.skeleton);
        const auto line = [](float t) { return glm::vec3(0.0f, 0.0f, testsupport::kGoldenWalk * t); };
        testsupport::addGoldenClip(pack, {"WalkSwapped", line, testsupport::noTurn, true, true});
        testsupport::addGoldenClip(pack, {"Walk", line, testsupport::noTurn, true, false});
        scene::MotionDatabaseOptions options = testsupport::goldenOptions();
        options.config.contactWeight = contactWeight;
        auto db = scene::buildMotionDatabase(pack, options);
        REQUIRE(db.has_value());
        return std::pair{std::move(pack), std::move(*db)};
    };
    const auto choose = [](const scene::MotionDatabase& db) {
        // Sample 31 + 6 is `Walk` at 0.2 s, left foot planted. The query is its own feature vector
        // with continuity off, so pose, trajectory and velocity tie exactly between the two clips.
        const std::uint32_t current = 31u + 6u;
        scene::MotionQuery query;
        query.features.assign(db.featuresFor(current), db.featuresFor(current) + db.dimension);
        scene::MotionCostWeights weights;
        weights.continuity = 0.0f;
        weights.transition = 0.0f;
        const scene::MotionMatch m = scene::searchMotion(db, query, weights);
        REQUIRE(m.found());
        return db.clipNames[db.sampleClip[m.sample]];
    };
    const auto on = build(1.0f);
    const auto off = build(0.0f);
    REQUIRE(on.second.clipNames[on.second.sampleClip[37u]] == "Walk");
    CHECK(choose(on.second) == "Walk");
    CHECK(choose(off.second) == "WalkSwapped");
}

TEST_CASE("§47 an incompatible candidate set declines rather than picking anything",
          "[adversarial][matching][phaseC]") {
    Golden g;
    scene::MotionQuery query;
    query.features.assign(g.db.dimension, 0.0f);
    query.requireTags = static_cast<std::uint32_t>(scene::MotionTag::Airborne); // nothing here flies
    const scene::MotionMatch m = scene::searchMotion(g.db, query, scene::MotionCostWeights{});
    CHECK_FALSE(m.found());
    // The control: the same query with no filter finds something.
    query.requireTags = 0;
    CHECK(scene::searchMotion(g.db, query, scene::MotionCostWeights{}).found());
}

TEST_CASE("§47 empty and single-candidate databases", "[adversarial][matching][phaseC]") {
    // Empty: the search finds nothing and the provider declines, so the chain falls through.
    scene::MotionDatabase empty;
    empty.config = testsupport::goldenOptions().config;
    empty.dimension = empty.config.dimension();
    scene::MotionQuery query;
    query.features.assign(empty.dimension, 0.0f);
    CHECK_FALSE(scene::searchMotion(empty, query, scene::MotionCostWeights{}).found());

    // One sample: every query gets it, at a finite cost, and a far query costs more than a near
    // one, so the cost is still measuring something.
    Golden g;
    scene::MotionDatabase one;
    one.config = g.db.config;
    one.dimension = g.db.dimension;
    const std::uint32_t pick = 40u;
    one.features.assign(g.db.featuresFor(pick), g.db.featuresFor(pick) + g.db.dimension);
    one.sampleClip = {0u};
    one.sampleTime = {g.db.sampleTime[pick]};
    one.samplePhase = {0.0f};
    one.sampleTags = {0u};
    one.sampleNext = {scene::MotionDatabase::kInvalid};
    one.mean = g.db.mean;
    one.scale = g.db.scale;
    one.clipNames = {"only"};
    scene::MotionQuery near;
    near.features = one.features;
    scene::MotionQuery far;
    far.features.assign(one.dimension, 5.0f);
    const scene::MotionMatch a = scene::searchMotion(one, near, scene::MotionCostWeights{});
    const scene::MotionMatch b = scene::searchMotion(one, far, scene::MotionCostWeights{});
    REQUIRE(a.found());
    REQUIRE(b.found());
    CHECK(a.sample == 0u);
    CHECK(b.sample == 0u);
    CHECK(a.cost < b.cost);
    CHECK(std::isfinite(b.cost));
}

// ---- §78: the two adversarial search tests the spec spells out -------------------------------------

namespace {

// A database of two samples whose features are written by hand, so each differs from the query in
// exactly the dimensions the case names.
scene::MotionDatabase handBuilt(const scene::MotionFeatureConfig& config) {
    scene::MotionDatabase db;
    db.config = config;
    db.dimension = config.dimension();
    db.features.assign(2u * db.dimension, 0.0f);
    db.sampleClip = {0u, 1u};
    db.sampleTime = {0.0f, 0.0f};
    db.samplePhase = {0.0f, 0.0f};
    db.sampleTags = {0u, 0u};
    db.sampleNext = {scene::MotionDatabase::kInvalid, scene::MotionDatabase::kInvalid};
    db.mean.assign(db.dimension, 0.0f);
    db.scale.assign(db.dimension, 1.0f);
    db.clipNames = {"A", "B"};
    return db;
}

std::size_t firstOf(const scene::MotionFeatureConfig& config, scene::MotionFeatureGroup group) {
    const auto layout = scene::motionFeatureLayout(config);
    for (std::size_t d = 0; d < layout.size(); ++d) {
        if (layout[d] == group) {
            return d;
        }
    }
    FAIL("no dimension in that group");
    return 0;
}

} // namespace

TEST_CASE("§78 excellent pose and terrible trajectory, against slightly worse pose and excellent trajectory",
          "[adversarial][matching][phaseC]") {
    // A: the pose is exact and the future trajectory is 1.0 off. B: the pose is 0.3 off and the
    // trajectory is exact. At equal weights, B wins (0.09 < 1.0), so trajectory features matter. The
    // configured weighting must be able to flip it: with the trajectory weight at 0.05, A wins.
    scene::MotionFeatureConfig config = testsupport::goldenOptions().config;
    config.contactWeight = 0.0f;
    scene::MotionDatabase db = handBuilt(config);
    const std::size_t pose = firstOf(config, scene::MotionFeatureGroup::JointPosition);
    const std::size_t traj = firstOf(config, scene::MotionFeatureGroup::TrajectoryPosition);
    db.features[(0u * db.dimension) + traj] = 1.0f; // A: trajectory wrong
    db.features[(1u * db.dimension) + pose] = 0.3f; // B: pose slightly wrong
    scene::MotionQuery query;
    query.features.assign(db.dimension, 0.0f);
    CHECK(db.clipNames[scene::searchMotion(db, query, {}).sample] == "B");
    db.config.trajectoryPositionWeight = 0.05f;
    CHECK(db.clipNames[scene::searchMotion(db, query, {}).sample] == "A");
}

TEST_CASE("§78 good pose and wrong contact, against slightly worse pose and correct contact",
          "[adversarial][matching][phaseC]") {
    // A: the pose is exact and a foot the query has planted is recorded lifted. B: the pose is 0.3 off
    // and the contact agrees. With the contact term weighted in, B wins. With it off, A wins. That
    // demonstrates the contact weighting works, in both directions.
    scene::MotionFeatureConfig config = testsupport::goldenOptions().config;
    config.contactWeight = 1.0f;
    scene::MotionDatabase db = handBuilt(config);
    const std::size_t pose = firstOf(config, scene::MotionFeatureGroup::JointPosition);
    const std::size_t contact = firstOf(config, scene::MotionFeatureGroup::Contact);
    scene::MotionQuery query;
    query.features.assign(db.dimension, 0.0f);
    query.features[contact] = 1.0f;                       // the query's left foot is planted
    db.features[(0u * db.dimension) + contact] = 0.0f;    // A says lifted
    db.features[(1u * db.dimension) + contact] = 1.0f;    // B says planted
    db.features[(1u * db.dimension) + pose] = 0.3f;       // and B's pose is a little off
    CHECK(db.clipNames[scene::searchMotion(db, query, {}).sample] == "B");
    db.config.contactWeight = 0.05f;
    CHECK(db.clipNames[scene::searchMotion(db, query, {}).sample] == "A");
}

// ---- §49: search correctness, the cases not already named above -----------------------------------

TEST_CASE("§49 a sample's own features find that sample, at zero cost, and the obviously bad never wins",
          "[adversarial][matching][phaseC]") {
    Golden g;
    int exact = 0;
    for (std::uint32_t s = 0; s < g.db.sampleCount(); s += 7) {
        scene::MotionQuery query;
        query.features.assign(g.db.featuresFor(s), g.db.featuresFor(s) + g.db.dimension);
        const scene::MotionMatch m = scene::searchMotion(g.db, query, {});
        REQUIRE(m.found());
        // The known best: a query that IS a sample returns a sample at zero cost. It may be a
        // duplicate of s (a loop's first and last frames are one instant), so the cost is what is
        // checked, not the index.
        CHECK(m.cost <= 1e-6f);
        exact += m.sample == s ? 1 : 0;
    }
    CHECK(exact > 0);
    // The obviously bad: a sample pushed far from everything is never chosen for a query taken
    // from the rest of the corpus.
    scene::MotionDatabase db = g.db;
    const std::uint32_t bad = 5u;
    for (std::size_t d = 0; d < db.dimension; ++d) {
        db.features[(static_cast<std::size_t>(bad) * db.dimension) + d] = 50.0f;
    }
    for (std::uint32_t s = 31; s < db.sampleCount(); s += 11) {
        scene::MotionQuery query;
        query.features.assign(db.featuresFor(s), db.featuresFor(s) + db.dimension);
        CHECK(scene::searchMotion(db, query, {}).sample != bad);
    }
}

TEST_CASE("§49 a candidate whose root teleports is not chosen for a smooth walk",
          "[adversarial][matching][phaseC]") {
    // `Glitch` is the walk with its root jumping 0.5 m forward at 0.5 s: a discontinuity in root
    // motion, the §32 failure in the content rather than in a transition. Listed before `Walk`, so a
    // tie would go to it. Around the jump its velocity and trajectory are wild, and a walking query
    // must land on smooth motion.
    scene::MotionPack pack;
    pack.name = "glitch";
    pack.skeleton = testsupport::goldenRig();
    pack.skeletonDigest = scene::skeletonDigest(pack.skeleton);
    testsupport::addGoldenClip(pack, {"Glitch",
                                      [](float t) {
                                          return glm::vec3(0.0f, 0.0f,
                                                           (testsupport::kGoldenWalk * t) + (t >= 0.5f ? 0.5f : 0.0f));
                                      },
                                      testsupport::noTurn});
    testsupport::addGoldenClip(pack, {"Walk", [](float t) { return glm::vec3(0.0f, 0.0f, testsupport::kGoldenWalk * t); },
                                      testsupport::noTurn});
    auto db = scene::buildMotionDatabase(pack, testsupport::goldenOptions());
    REQUIRE(db.has_value());
    // The subject exists: the glitch sample right before the jump is moving far faster than a walk.
    const auto layout = scene::motionFeatureLayout(db->config);
    std::size_t rv = 0;
    while (layout[rv] != scene::MotionFeatureGroup::RootVelocity) {
        ++rv;
    }
    const auto rawZ = [&](std::uint32_t s) {
        const float* f = db->featuresFor(s);
        return (f[rv + 2] / db->scale[rv + 2]) + db->mean[rv + 2];
    };
    REQUIRE(rawZ(14u) > 5.0f); // Glitch at 14/30 s steps across the jump
    // Every walking query, taken from the smooth clip, is answered with a sample moving at a walk.
    for (std::uint32_t s = 31; s < db->sampleCount(); ++s) {
        scene::MotionQuery query;
        query.features.assign(db->featuresFor(s), db->featuresFor(s) + db->dimension);
        const scene::MotionMatch m = scene::searchMotion(*db, query, {});
        REQUIRE(m.found());
        CHECK(std::abs(rawZ(m.sample) - testsupport::kGoldenWalk) < 0.2f);
    }
}
