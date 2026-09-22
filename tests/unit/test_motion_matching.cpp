// Motion matching (Phase C): the database, the cost function, and the provider.
//
// The assertions that matter are the ones about **continuity** and **commitment**, because a naive
// nearest-neighbour search passes a "does it find a similar pose" test and produces a character
// that twitches between unrelated clips forever. §11 calls continuity the most important addition
// beyond nearest neighbour; these tests are what make that claim checkable.

#include "entity/match_motion_provider.hpp"
#include "scene/motion_database.hpp"
#include "scene/motion_pack.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace avgen;
using Catch::Approx;

namespace {

scene::Skeleton bodyRig() {
    scene::Skeleton sk;
    sk.name = "body";
    sk.joints.push_back(scene::Joint{"root.x", -1, scene::Transform{}});
    sk.joints.push_back(scene::Joint{"foot.l", 0, scene::Transform{}});
    sk.joints.push_back(scene::Joint{"foot.r", 0, scene::Transform{}});
    sk.palette = {0, 1, 2};
    sk.inverseBind = {glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
    return sk;
}

// A clip whose feet swing out of phase with each other, so two clips built with different
// amplitudes are genuinely distinguishable in feature space.
scene::AnimationClip gaitClip(std::string name, float amplitude, float travel) {
    scene::AnimationClip clip;
    clip.name = std::move(name);
    const int frames = 31;
    scene::AnimationChannel root;
    root.joint = 0;
    root.path = scene::AnimationPath::Translation;
    root.interpolation = scene::Interpolation::Linear;
    scene::AnimationChannel left = root;
    left.joint = 1;
    scene::AnimationChannel right = root;
    right.joint = 2;
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / 30.0f;
        const float a = 6.283185307179586f * t;
        root.times.push_back(t);
        root.values.emplace_back(0.0f, 0.0f, travel * t, 0.0f);
        left.times.push_back(t);
        left.values.emplace_back(0.0f, 0.1f, (travel * t) + (amplitude * std::sin(a)), 0.0f);
        right.times.push_back(t);
        right.values.emplace_back(0.0f, 0.1f, (travel * t) - (amplitude * std::sin(a)), 0.0f);
    }
    clip.start = 0.0f;
    clip.duration = 1.0f;
    clip.channels = {std::move(root), std::move(left), std::move(right)};
    return clip;
}

scene::MotionPack twoGaitPack() {
    scene::MotionPack pack;
    pack.name = "probe";
    pack.skeleton = bodyRig();
    pack.skeletonDigest = "probe-digest";
    pack.animation = {gaitClip("Walking", 0.30f, 0.0f), gaitClip("Running", 0.75f, 0.0f)};
    for (const scene::AnimationClip& clip : pack.animation) {
        scene::PackClip meta;
        meta.name = clip.name;
        meta.loop = true;
        meta.sampleRate = 30.0f;
        meta.frames = 31;
        pack.clips.push_back(std::move(meta));
    }
    return pack;
}

scene::MotionDatabase buildProbe() {
    scene::MotionDatabaseOptions options;
    options.sampleRate = 30.0f;
    options.config.joints = {"foot.l", "foot.r"};
    options.config.trajectoryTimes = {0.2f, 0.4f};
    auto db = scene::buildMotionDatabase(twoGaitPack(), options);
    REQUIRE(db.has_value());
    return *db;
}

} // namespace

TEST_CASE("the database is built once and reports what it is", "[matching][database]") {
    const scene::MotionDatabase db = buildProbe();
    CHECK(db.stats.clips == 2);
    CHECK(db.sampleCount() == 62); // 31 frames each
    CHECK(db.dimension == db.config.dimension());
    // 2 joints * 6 + 2 trajectory * 4 + 3 root velocity.
    CHECK(db.dimension == 23);
    CHECK(db.stats.totalBytes() > 0);

    // The continuation chain: every sample but the clip ends points at its successor, and a
    // looping clip's last sample points back at its first.
    CHECK(db.sampleNext[0] == 1);
    CHECK(db.sampleNext[30] == 0);   // Walking loops
    CHECK(db.sampleNext[31] == 32);
    CHECK(db.sampleNext[61] == 31);  // Running loops

    // **The rotation-invariant statistic**, which is the one a dead-dimension count cannot supply.
    // These feet genuinely swing, so it is not zero.
    REQUIRE(db.stats.jointRadiusSpread.size() == 2);
    INFO("spread " << db.stats.jointRadiusSpread[0] << ", " << db.stats.jointRadiusSpread[1]);
    CHECK(db.stats.jointRadiusSpread[0] > 0.01f);
}

TEST_CASE("a welded limb is reported however busy its coordinates are", "[matching][database]") {
    // **ADR-553's failure, reproduced deliberately, so the detector is shown to fire.** A foot
    // rigidly attached to a body that rotates has feature coordinates that change on every frame
    // and a distance-from-body that never changes at all. A zero-variance check passes it; this
    // statistic does not.
    scene::MotionPack pack = twoGaitPack();
    for (scene::AnimationClip& clip : pack.animation) {
        // Drop the foot channels: the feet now ride the root rigidly.
        clip.channels.resize(1);
        // ...and rotate the root, so the raw coordinates are busy.
        scene::AnimationChannel spin;
        spin.joint = 0;
        spin.path = scene::AnimationPath::Rotation;
        spin.interpolation = scene::Interpolation::Linear;
        for (int i = 0; i < 31; ++i) {
            const float t = static_cast<float>(i) / 30.0f;
            const glm::quat q = glm::angleAxis(t * 3.0f, glm::vec3(0.0f, 1.0f, 0.0f));
            spin.times.push_back(t);
            spin.values.emplace_back(q.x, q.y, q.z, q.w);
        }
        clip.channels.push_back(std::move(spin));
    }
    // The feet need a non-zero rest offset, or their radius is zero for a different reason.
    pack.skeleton.joints[1].rest.position = glm::vec3(0.2f, 0.0f, 0.0f);
    pack.skeleton.joints[2].rest.position = glm::vec3(-0.2f, 0.0f, 0.0f);

    scene::MotionDatabaseOptions options;
    options.config.joints = {"foot.l", "foot.r"};
    options.config.trajectoryTimes = {};
    auto db = scene::buildMotionDatabase(pack, options);
    REQUIRE(db.has_value());

    INFO("dead dimensions " << db->stats.deadDimensions << ", spread "
                            << db->stats.jointRadiusSpread[0]);
    // The coordinates really are busy -- this is not a database of constants.
    CHECK(db->stats.deadDimensions < db->dimension);
    // And the limb is nonetheless not articulating.
    CHECK(db->stats.jointRadiusSpread[0] < 0.005f);
    CHECK(db->stats.jointRadiusSpread[1] < 0.005f);
}

TEST_CASE("continuity stops the search hopping between clips", "[matching][cost]") {
    // §11, and the reason it is called the most important addition beyond nearest neighbour.
    // Queried with exactly the features of a sample in Walking, with the character already on the
    // sample before it: the continuation must win even though the identical sample exists.
    const scene::MotionDatabase db = buildProbe();
    const std::uint32_t current = 10;
    const std::uint32_t expected = db.sampleNext[current];

    scene::MotionQuery query;
    query.features.assign(db.featuresFor(expected), db.featuresFor(expected) + db.dimension);
    query.current = current;

    scene::MotionCostWeights weights;
    const scene::MotionMatch m = scene::searchMotion(db, query, weights);
    REQUIRE(m.found());
    CHECK(m.sample == expected);

    // **The control arm.** With continuity switched off the same query may legitimately land
    // anywhere that matches as well -- so the assertion above is testing continuity rather than
    // testing that the database contains the sample it was handed.
    weights.continuity = 0.0f;
    weights.transition = 0.0f;
    const scene::MotionMatch naive = scene::searchMotion(db, query, weights);
    REQUIRE(naive.found());
    CHECK(naive.cost <= m.cost);
}

TEST_CASE("the tag filter removes candidates before they are scored", "[matching][cost]") {
    // §14. The saving is the point: filtering is one AND per sample and scoring is `dimension`
    // multiply-adds, so removing a clip before scoring is 23x cheaper than scoring it.
    const scene::MotionDatabase db = buildProbe();
    scene::MotionQuery query;
    query.features.assign(db.featuresFor(5), db.featuresFor(5) + db.dimension);

    const scene::MotionMatch all = scene::searchMotion(db, query, {});
    CHECK(all.rejected == 0);
    CHECK(all.considered > 0);

    // Everything in this pack is cyclic, so requiring OneShot rejects all of it -- and the search
    // says so rather than returning a sample that does not match the filter.
    query.requireTags = static_cast<std::uint32_t>(scene::MotionTag::OneShot);
    const scene::MotionMatch none = scene::searchMotion(db, query, {});
    CHECK(none.rejected == db.sampleCount());
    CHECK(none.considered == 0);
    CHECK_FALSE(none.found());
}

TEST_CASE("the matcher continues between searches instead of scanning every frame",
          "[matching][provider]") {
    // §27/§29. This is what makes motion matching affordable: the scan runs ten times a second
    // and the other fifty frames are an array read. A provider that searched every frame would
    // pass every quality test and cost six times as much.
    const scene::MotionDatabase db = buildProbe();
    const scene::MotionPack pack = twoGaitPack();
    entity::MatchMotionProvider provider(&db, &pack.animation, "match");
    entity::MatchSettings settings;
    settings.searchInterval = 0.1f;
    settings.minimumContinuation = 0.0f;
    provider.setSettings(settings);

    entity::MotionRequest request;
    request.desiredVelocity = glm::vec3(0.0f, 0.0f, 1.2f);
    entity::MotionMemory memory;
    for (int i = 0; i < 60; ++i) {
        entity::MotionMemory next;
        const entity::MotionResult r =
            provider.advance(request, memory, static_cast<double>(i) / 60.0, 1.0f / 60.0f, next);
        REQUIRE(r.ok());
        memory = next;
    }
    const entity::MatchMotionProvider::Counters& c = provider.counters();
    INFO("searches " << c.searches << " continued " << c.continued << " scored " << c.scored);
    // One second at 60 Hz with a 0.1 s interval: about ten searches, not sixty.
    CHECK(c.searches >= 8);
    CHECK(c.searches <= 13);
    CHECK(c.continued >= 45);
    // And it really did scan on those searches, rather than reporting searches it did not run.
    CHECK(c.scored > 0);
}

TEST_CASE("an airborne request falls through rather than guessing", "[matching][provider]") {
    // §35. The database is ground locomotion; a body in the air is the clip player's problem, and
    // declining is what lets ADR-541's chain do its job.
    const scene::MotionDatabase db = buildProbe();
    const scene::MotionPack pack = twoGaitPack();
    entity::MatchMotionProvider provider(&db, &pack.animation, "match");
    entity::MotionRequest request;
    request.mode = entity::MovementMode::Airborne;
    entity::MotionMemory memory;
    entity::MotionMemory next;
    const entity::MotionResult r = provider.advance(request, memory, 0.0, 1.0f / 60.0f, next);
    CHECK_FALSE(r.ok());
    CHECK(r.status == entity::MotionStatus::Unsupported);
}

TEST_CASE("the matcher keeps nothing, so a replay reproduces a play", "[matching][provider][determinism]") {
    // ADR-556's contract. Everything the matcher remembers is in `MotionMemory`; the counters are
    // diagnostics about the provider and not about any character.
    const scene::MotionDatabase db = buildProbe();
    const scene::MotionPack pack = twoGaitPack();
    entity::MatchMotionProvider provider(&db, &pack.animation, "match");
    entity::MotionRequest request;
    request.desiredVelocity = glm::vec3(0.0f, 0.0f, 1.0f);

    const auto run = [&](int steps) {
        entity::MotionMemory m;
        for (int i = 0; i < steps; ++i) {
            entity::MotionMemory next;
            (void)provider.advance(request, m, static_cast<double>(i) / 60.0, 1.0f / 60.0f, next);
            m = next;
        }
        return m;
    };
    const entity::MotionMemory a = run(45);
    const entity::MotionMemory b = run(45);
    CHECK(a.selection == b.selection);
    CHECK(a.localTime == Approx(b.localTime));
    CHECK(a.phase == Approx(b.phase));
    CHECK(a.generation == b.generation);
    // And it went somewhere, so this is not two identical answers from a provider that never ran.
    CHECK(a.generation > 0);
}

TEST_CASE("the matcher poses from the sample it chose", "[matching][provider]") {
    const scene::MotionDatabase db = buildProbe();
    const scene::MotionPack pack = twoGaitPack();
    entity::MatchMotionProvider provider(&db, &pack.animation, "match");
    entity::MotionMemory memory;
    // Sample 7 is t = 0.233 s, where sin(2*pi*t) is 0.99 and the feet are at their swing
    // extremes. The first draft used sample 15 -- t = 0.5 s, where sin is **zero** and the feet
    // cross -- and measured a 5e-8 separation. The arithmetic was wrong, not the provider.
    memory.selection = 7;
    memory.generation = 1;
    // A hand-built memory says which database it indexes, as one `advance` settled would (§40).
    memory.database = db.identity;
    scene::Pose pose;
    const scene::Skeleton sk = bodyRig();
    const entity::MotionResult r = provider.pose(memory, sk, pose);
    REQUIRE(r.ok());
    REQUIRE(pose.size() == sk.jointCount());
    // At the swing extreme the two feet are a stride apart, so "it posed something" is
    // answerable rather than being true of any pose.
    CHECK(std::abs(pose.local[1].position.z - pose.local[2].position.z) > 0.05f);

    // A memory pointing past the end is refused rather than read -- the second lock on ADR-556's
    // door, in case a clip provider's memory ever reaches this provider.
    memory.selection = 99999;
    CHECK_FALSE(provider.pose(memory, sk, pose).ok());
}
