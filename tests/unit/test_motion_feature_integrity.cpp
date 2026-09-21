// Phase C: the motion database's feature values checked against what the clip actually does.
//
// Every case here was a defect in feature *values*. The shape of the database was never wrong,
// only what went into it. A structural test cannot see that kind of defect, so each case
// reconstructs the raw value of one dimension and compares it with the motion that produced it.

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

// A one-second clip whose root travels along +Z by `rootZ(t)` and whose feet swing about it.
template <typename RootZ>
scene::AnimationClip travellingClip(std::string name, RootZ rootZ) {
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
        const float t = static_cast<float>(i) / kRate;
        const float swing = 0.3f * std::sin(6.283185307179586f * t);
        root.times.push_back(t);
        root.values.emplace_back(0.0f, 0.0f, rootZ(t), 0.0f);
        // The feet are children of the root, so their translation is relative to it.
        left.times.push_back(t);
        left.values.emplace_back(0.1f, 0.0f, swing, 0.0f);
        right.times.push_back(t);
        right.values.emplace_back(-0.1f, 0.0f, -swing, 0.0f);
    }
    clip.start = 0.0f;
    clip.duration = 1.0f;
    clip.channels = {std::move(root), std::move(left), std::move(right)};
    return clip;
}

scene::MotionPack packOf(scene::AnimationClip clip, bool loop) {
    scene::MotionPack pack;
    pack.name = "integrity";
    pack.skeleton = bodyRig();
    pack.skeletonDigest = "integrity-digest";
    scene::PackClip meta;
    meta.name = clip.name;
    meta.loop = loop;
    meta.sampleRate = kRate;
    meta.frames = 31;
    pack.animation = {std::move(clip)};
    pack.clips = {std::move(meta)};
    return pack;
}

scene::MotionDatabase build(const scene::MotionPack& pack) {
    scene::MotionDatabaseOptions options;
    options.sampleRate = kRate;
    options.config.joints = {"foot.l", "foot.r"};
    options.config.trajectoryTimes = {0.2f, 0.4f};
    auto db = scene::buildMotionDatabase(pack, options);
    REQUIRE(db.has_value());
    return *db;
}

// The raw (unnormalised) value of dimension `d` of sample `s`.
float raw(const scene::MotionDatabase& db, std::uint32_t s, std::size_t d) {
    const float stored = db.featuresFor(s)[d];
    return db.scale[d] != 0.0f ? (stored / db.scale[d]) + db.mean[d] : stored;
}

struct Dims {
    std::size_t trajectory = 0;  // first trajectory dimension
    std::size_t rootVelocity = 0;
};

Dims dimsOf(const scene::MotionDatabase& db) {
    const auto layout = scene::motionFeatureLayout(db.config);
    Dims out;
    out.trajectory = layout.size();
    out.rootVelocity = layout.size();
    for (std::size_t d = 0; d < layout.size(); ++d) {
        if (out.trajectory == layout.size() &&
            layout[d] == scene::MotionFeatureGroup::TrajectoryPosition) {
            out.trajectory = d;
        }
        if (out.rootVelocity == layout.size() &&
            layout[d] == scene::MotionFeatureGroup::RootVelocity) {
            out.rootVelocity = d;
        }
    }
    REQUIRE(out.trajectory < layout.size());
    REQUIRE(out.rootVelocity < layout.size());
    return out;
}

} // namespace

TEST_CASE("a looping clip's last sample moves like the rest of it", "[motionfeatures][phaseC]") {
    // A walk at a steady 1.2 m/s that loops. Every sample, the last one included, must read
    // 1.2 m/s, and every trajectory point must be 1.2 m/s times its horizon. The builder used to
    // clamp every look ahead at the clip's end, so the last sample read 0 and the final 0.4 s of
    // trajectory shrank toward the body: a stop the clip does not contain.
    constexpr float speed = 1.2f;
    const scene::MotionDatabase db =
        build(packOf(travellingClip("Walking", [](float t) { return speed * t; }), true));
    const Dims dims = dimsOf(db);
    REQUIRE(db.sampleCount() == 31u);
    // The subject exists: the last sample is the one that used to read zero.
    const std::uint32_t last = db.sampleCount() - 1u;
    REQUIRE(db.sampleNext[last] == 0u);

    float worstVelocity = 0.0f;
    float worstTrajectory = 0.0f;
    for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
        worstVelocity = std::max(worstVelocity, std::abs(raw(db, s, dims.rootVelocity + 2u) - speed));
        for (std::size_t h = 0; h < db.config.trajectoryTimes.size(); ++h) {
            const float want = speed * db.config.trajectoryTimes[h];
            const float got = raw(db, s, dims.trajectory + (h * 4u) + 1u); // planar z
            worstTrajectory = std::max(worstTrajectory, std::abs(got - want));
        }
    }
    WARN(fmt::format("last sample root velocity {:.4f} m/s (want {:.2f}); worst velocity error "
                     "{:.2e}, worst trajectory error {:.2e} m",
                     raw(db, last, dims.rootVelocity + 2u), speed, worstVelocity, worstTrajectory));
    CHECK(worstVelocity < 1e-3f);
    CHECK(worstTrajectory < 1e-3f);
}

TEST_CASE("a clip that does not loop continues at the velocity it ended with",
          "[motionfeatures][phaseC]") {
    // The same steady walk with no loop. Nothing follows it, so the least assumption is that it
    // keeps going as it was, not that it stops dead on its last key.
    constexpr float speed = 1.2f;
    const scene::MotionDatabase db =
        build(packOf(travellingClip("Walking", [](float t) { return speed * t; }), false));
    const Dims dims = dimsOf(db);
    const std::uint32_t last = db.sampleCount() - 1u;
    REQUIRE(db.sampleNext[last] == scene::MotionDatabase::kInvalid);
    CHECK(std::abs(raw(db, last, dims.rootVelocity + 2u) - speed) < 1e-3f);
    const float horizon = db.config.trajectoryTimes.back();
    CHECK(std::abs(raw(db, last, dims.trajectory + ((db.config.trajectoryTimes.size() - 1u) * 4u) +
                                     1u) -
                   (speed * horizon)) < 1e-3f);
}

TEST_CASE("a clip that ends at rest still ends at rest", "[motionfeatures][phaseC]") {
    // The other arm, so the fix cannot pass by inventing motion. The root decelerates from
    // 1.2 m/s to 0 over the second (z = v(t - t²/2)). Its last sample must read close to zero,
    // not the clip's average speed. Extrapolating the final velocity gives the backward
    // difference over the last frame, v·dt/2 = 0.02 m/s.
    constexpr float speed = 1.2f;
    const scene::MotionDatabase db = build(packOf(
        travellingClip("Stopping", [](float t) { return speed * (t - (0.5f * t * t)); }), false));
    const Dims dims = dimsOf(db);
    const std::uint32_t last = db.sampleCount() - 1u;
    const float endVelocity = raw(db, last, dims.rootVelocity + 2u);
    WARN(fmt::format("a stop's last sample reads {:.4f} m/s", endVelocity));
    CHECK(std::abs(endVelocity) < 0.05f);
    // And the first sample is moving, so this is a stop and not a clip that never moved.
    CHECK(raw(db, 0u, dims.rootVelocity + 2u) > 1.0f);
}
