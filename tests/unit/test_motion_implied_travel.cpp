// Phase C: a walk authored in place still walks, as far as the database is concerned.
//
// ADR-540: every Glowmere locomotion cycle is authored in place. The root stays put and the planted
// foot slides backward at the walking speed. The database read the root literally, so every walk
// had a root velocity of zero, the same as standing. `agent/anim-cinfra`'s explainer found the
// consequence on the scout: asked to walk at 1.6 m/s, the matcher chose `Fight_leg_kick_1`, whose
// root at least moved.
//
// These cases rebuild that situation in miniature. The real scout is measured by the explainer
// case in `test_motion_explain.cpp` and by `avgen-motion explain`.

#include "entity/match_motion_provider.hpp"
#include "scene/motion_database.hpp"
#include "scene/motion_pack.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <cmath>
#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr float kRate = 30.0f;
constexpr float kWalk = 1.2f; // m/s: a stance foot slides 0.6 m in 0.5 s

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

scene::AnimationChannel track(std::uint32_t joint) {
    scene::AnimationChannel c;
    c.joint = joint;
    c.path = scene::AnimationPath::Translation;
    c.interpolation = scene::Interpolation::Linear;
    return c;
}

// A foot's position at cycle time `u` in [0, 1): planted and sliding back for the first half,
// lifted and swinging forward for the second. This is a treadmill walk.
glm::vec3 foot(float side, float u) {
    if (u < 0.5f) {
        return {side, 0.0f, 0.3f - (kWalk * u)};
    }
    const float s = u - 0.5f;
    return {side, 0.1f * std::sin(6.283185307179586f * s), -0.3f + (kWalk * s)};
}

// One second, looping, root fixed at the origin: the in-place walk.
scene::AnimationClip inPlaceWalk() {
    scene::AnimationClip clip;
    clip.name = "Walking";
    scene::AnimationChannel root = track(0);
    scene::AnimationChannel left = track(1);
    scene::AnimationChannel right = track(2);
    for (int i = 0; i <= 30; ++i) {
        const float t = static_cast<float>(i) / kRate;
        root.times.push_back(t);
        root.values.emplace_back(0.0f, 0.0f, 0.0f, 0.0f);
        left.times.push_back(t);
        left.values.emplace_back(foot(0.1f, std::fmod(t, 1.0f)), 0.0f);
        right.times.push_back(t);
        right.values.emplace_back(foot(-0.1f, std::fmod(t + 0.5f, 1.0f)), 0.0f);
    }
    clip.start = 0.0f;
    clip.duration = 1.0f;
    clip.channels = {std::move(root), std::move(left), std::move(right)};
    return clip;
}

// One second, not looping: the feet stay where they are and the root sways 0.36 m forward, which
// is what the scout's kick does.
scene::AnimationClip kick() {
    scene::AnimationClip clip;
    clip.name = "Kick";
    scene::AnimationChannel root = track(0);
    scene::AnimationChannel left = track(1);
    scene::AnimationChannel right = track(2);
    for (int i = 0; i <= 30; ++i) {
        const float t = static_cast<float>(i) / kRate;
        root.times.push_back(t);
        root.values.emplace_back(0.0f, 0.0f, 0.36f * t, 0.0f);
        left.times.push_back(t);
        left.values.emplace_back(0.1f, 0.0f, 0.0f, 0.0f);
        right.times.push_back(t);
        right.values.emplace_back(-0.1f, 0.3f * std::sin(3.14159265f * t), 0.2f * t, 0.0f);
    }
    clip.start = 0.0f;
    clip.duration = 1.0f;
    clip.channels = {std::move(root), std::move(left), std::move(right)};
    return clip;
}

scene::ContactTrack planted(const char* joint, int index, float from, float to) {
    scene::ContactTrack c;
    c.joint = joint;
    c.kind = scene::ContactKind::Foot;
    c.jointIndex = index;
    c.spans.push_back(scene::ContactSpan{from, to, 1.0f});
    return c;
}

scene::MotionPack pack() {
    scene::MotionPack p;
    p.name = "implied";
    p.skeleton = bodyRig();
    p.skeletonDigest = "implied-digest";
    p.animation = {inPlaceWalk(), kick()};
    scene::PackClip walk;
    walk.name = "Walking";
    walk.loop = true;
    walk.sampleRate = kRate;
    walk.frames = 31;
    walk.contacts = {planted("foot.l", 1, 0.0f, 0.5f), planted("foot.r", 2, 0.5f, 1.0f)};
    scene::PackClip hit;
    hit.name = "Kick";
    hit.loop = false;
    hit.sampleRate = kRate;
    hit.frames = 31;
    hit.contacts = {planted("foot.l", 1, 0.0f, 1.0f)};
    p.clips = {std::move(walk), std::move(hit)};
    return p;
}

scene::MotionDatabase build(const scene::MotionPack& p) {
    scene::MotionDatabaseOptions options;
    options.sampleRate = kRate;
    options.config.joints = {"foot.l", "foot.r"};
    options.config.trajectoryTimes = {0.2f, 0.4f};
    auto db = scene::buildMotionDatabase(p, options);
    REQUIRE(db.has_value());
    return *db;
}

float raw(const scene::MotionDatabase& db, std::uint32_t s, std::size_t d) {
    const float stored = db.featuresFor(s)[d];
    return db.scale[d] != 0.0f ? (stored / db.scale[d]) + db.mean[d] : stored;
}

std::size_t rootVelocityDim(const scene::MotionDatabase& db) {
    const auto layout = scene::motionFeatureLayout(db.config);
    for (std::size_t d = 0; d < layout.size(); ++d) {
        if (layout[d] == scene::MotionFeatureGroup::RootVelocity) {
            return d;
        }
    }
    FAIL("no root velocity block");
    return 0;
}

} // namespace

TEST_CASE("an in-place walk carries the speed its planted feet imply", "[motionfeatures][implied][phaseC]") {
    const scene::MotionPack p = pack();
    const scene::MotionDatabase db = build(p);
    const std::size_t rv = rootVelocityDim(db);
    // The subject exists: the walk's root really does not move.
    REQUIRE(p.animation[0].channels[0].values.back().z == 0.0f);

    float worst = 0.0f;
    std::uint32_t walkSamples = 0;
    for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
        if (db.sampleClip[s] != 0u) {
            continue;
        }
        ++walkSamples;
        worst = std::max(worst, std::abs(raw(db, s, rv + 2u) - kWalk));
    }
    REQUIRE(walkSamples == 31u);
    WARN(fmt::format("in-place walk: every sample within {:.2e} m/s of the {:.2f} m/s its feet imply",
                     worst, kWalk));
    CHECK(worst < 1e-3f);

    // The kick's root travels (0.36 m in a second, beyond this rig's zero rest height), so it keeps
    // its root's own answer and gains nothing from its feet.
    for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
        if (db.sampleClip[s] == 1u) {
            CHECK(std::abs(raw(db, s, rv + 2u) - 0.36f) < 1e-3f);
        }
    }
}

TEST_CASE("asked to walk, the matcher chooses the walk and not the kick", "[matching][implied][phaseC]") {
    // The scout's finding, in miniature. With the walk read as standing still, a request to walk
    // at 1.6 m/s was closer to the kick's 0.36 m/s than to the walk's 0, and the kick won.
    //
    // The second arm keeps the fix honest: asked to stand, the answer must not be the walk. Here
    // the kick is the slowest thing on offer.
    const scene::MotionPack p = pack();
    const scene::MotionDatabase db = build(p);
    entity::MatchMotionProvider provider(&db, &p.animation, "match");
    const auto chosen = [&](float speed) {
        entity::MotionRequest request;
        request.desiredVelocity = glm::vec3(0.0f, 0.0f, speed);
        entity::MotionMemory memory;
        entity::MotionMemory next;
        const entity::MotionResult r = provider.advance(request, memory, 0.0, 1.0f / 60.0f, next);
        REQUIRE(r.ok());
        return std::string(r.content);
    };
    CHECK(chosen(1.6f) == "Walking");
    CHECK(chosen(0.0f) == "Kick");
}
