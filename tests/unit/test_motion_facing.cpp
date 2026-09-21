// Phase C §7: features are in the body's own frame, and so is the query.
//
// §7 has always said "in the body's own frame", and the header said it too. The builder
// subtracted the body's position and never removed its heading. That is harmless for content
// authored facing +Z, which is every Glowmere clip, and wrong for anything that turns: 100STYLE, and
// any character in a scene that is not facing +Z. The query side had the same gap. A world-space
// request was compared with clip-space features, so a body facing east that asked to walk forward
// was scored against sideways motion.
//
// Both cases are built so that the wrong frame gives a *different* answer, not a slightly worse
// one. Every earlier test built its body facing +Z, where the two frames coincide, and could not
// tell them apart.

#include "entity/match_motion_provider.hpp"
#include "scene/motion_database.hpp"
#include "scene/motion_pack.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr float kRate = 30.0f;
constexpr float kSpeed = 1.2f;

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

// A one-second looping walk. The body faces `yaw` (about +Y) throughout, and it travels along
// `travel`, which is given in the body's own frame: +Z is forward and +X is sideways. The feet
// swing along the travel direction, relative to the root, so a forward walk and a sidestep have
// different shapes as well as different velocities.
scene::AnimationClip walk(std::string name, float yaw, glm::vec3 travel) {
    scene::AnimationClip clip;
    clip.name = std::move(name);
    const glm::quat facing = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    scene::AnimationChannel root;
    root.joint = 0;
    root.path = scene::AnimationPath::Translation;
    root.interpolation = scene::Interpolation::Linear;
    scene::AnimationChannel turn = root;
    turn.path = scene::AnimationPath::Rotation;
    scene::AnimationChannel left = root;
    left.joint = 1;
    scene::AnimationChannel right = root;
    right.joint = 2;
    for (int i = 0; i < 31; ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float swing = 0.3f * std::sin(6.283185307179586f * t);
        const glm::vec3 world = facing * (travel * (kSpeed * t));
        root.times.push_back(t);
        root.values.emplace_back(world, 0.0f);
        turn.times.push_back(t);
        turn.values.emplace_back(facing.x, facing.y, facing.z, facing.w);
        left.times.push_back(t);
        left.values.emplace_back(glm::vec3(0.1f, 0.0f, 0.0f) + (travel * swing), 0.0f);
        right.times.push_back(t);
        right.values.emplace_back(glm::vec3(-0.1f, 0.0f, 0.0f) - (travel * swing), 0.0f);
    }
    clip.start = 0.0f;
    clip.duration = 1.0f;
    clip.channels = {std::move(root), std::move(turn), std::move(left), std::move(right)};
    return clip;
}

scene::MotionPack packOf(std::vector<scene::AnimationClip> clips) {
    scene::MotionPack pack;
    pack.name = "facing";
    pack.skeleton = bodyRig();
    pack.skeletonDigest = "facing-digest";
    for (const scene::AnimationClip& clip : clips) {
        scene::PackClip meta;
        meta.name = clip.name;
        meta.loop = true;
        meta.sampleRate = kRate;
        meta.frames = 31;
        pack.clips.push_back(std::move(meta));
    }
    pack.animation = std::move(clips);
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

float raw(const scene::MotionDatabase& db, std::uint32_t s, std::size_t d) {
    const float stored = db.featuresFor(s)[d];
    return db.scale[d] != 0.0f ? (stored / db.scale[d]) + db.mean[d] : stored;
}

} // namespace

TEST_CASE("a walk facing east has the features of the same walk facing north",
          "[motionfeatures][facing][phaseC]") {
    // The same motion, turned a quarter. Motion matching compares shapes, and a walk is the same
    // walk whichever way the room happens to be laid out, so every raw feature must agree.
    const float quarter = 1.5707963267948966f;
    const scene::MotionDatabase north =
        build(packOf({walk("Walking", 0.0f, glm::vec3(0.0f, 0.0f, 1.0f))}));
    const scene::MotionDatabase east =
        build(packOf({walk("Walking", quarter, glm::vec3(0.0f, 0.0f, 1.0f))}));
    REQUIRE(north.sampleCount() == east.sampleCount());
    REQUIRE(north.dimension == east.dimension);

    // The subject exists: the two clips really do differ in world space, by a quarter turn.
    const scene::MotionPack eastPack = packOf({walk("Walking", quarter, glm::vec3(0.0f, 0.0f, 1.0f))});
    REQUIRE(std::abs(eastPack.animation[0].channels[0].values.back().x - kSpeed) < 1e-4f);

    float worst = 0.0f;
    std::size_t worstDim = 0;
    for (std::uint32_t s = 0; s < north.sampleCount(); ++s) {
        for (std::size_t d = 0; d < north.dimension; ++d) {
            const float gap = std::abs(raw(north, s, d) - raw(east, s, d));
            if (gap > worst) {
                worst = gap;
                worstDim = d;
            }
        }
    }
    WARN(fmt::format("worst raw feature disagreement between the two headings: {:.2e} (dimension "
                     "{})",
                     worst, worstDim));
    CHECK(worst < 1e-4f);
}

TEST_CASE("a body facing east that asks to walk forward is given the forward walk",
          "[matching][facing][phaseC]") {
    // Two clips, both authored facing +Z: one walks forward and one sidesteps along its own +X.
    // The body faces east. Asked to go east, which is forward for this body, it must get the
    // forward walk. Asked to go the way its own +X points, which is world -Z when facing east, it
    // must get the sidestep.
    //
    // A matcher that ignores the facing gives the sidestep for "east", because east is +X in world
    // space. The second arm is there so the fix cannot pass by always answering "Forward": facing
    // east, world -Z is the body's own +X, and that must still be the sidestep.
    const scene::MotionPack pack = packOf({walk("Forward", 0.0f, glm::vec3(0.0f, 0.0f, 1.0f)),
                                           walk("Sidestep", 0.0f, glm::vec3(1.0f, 0.0f, 0.0f))});
    const scene::MotionDatabase db = build(pack);
    entity::MatchMotionProvider provider(&db, &pack.animation, "match");

    const glm::vec3 east(1.0f, 0.0f, 0.0f);
    const auto chosen = [&](glm::vec3 facing, glm::vec3 velocity) {
        entity::MotionRequest request;
        request.bodyFacing = facing;
        request.desiredVelocity = velocity;
        entity::MotionMemory memory;
        entity::MotionMemory next;
        const entity::MotionResult r = provider.advance(request, memory, 0.0, 1.0f / 60.0f, next);
        REQUIRE(r.ok());
        return std::string(r.content);
    };

    // The control: facing +Z the frames coincide, so this passes with or without the fix.
    CHECK(chosen(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, kSpeed)) == "Forward");
    CHECK(chosen(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(kSpeed, 0.0f, 0.0f)) == "Sidestep");

    // Facing east. What the body calls forward is world +X, and what it calls its own +X is
    // world -Z.
    CHECK(chosen(east, east * kSpeed) == "Forward");
    CHECK(chosen(east, glm::vec3(0.0f, 0.0f, -kSpeed)) == "Sidestep");
}

#include "assets/gltf_loader.hpp"
#include "scene/scene.hpp"

#include <filesystem>

namespace {

namespace fs = std::filesystem;

// The travel joint, by the builder's own rule: the lowest-indexed joint with a translation track.
int travelJoint(const scene::AnimationClip& clip) {
    int root = -1;
    for (const scene::AnimationChannel& c : clip.channels) {
        if (c.path == scene::AnimationPath::Translation &&
            (root < 0 || static_cast<int>(c.joint) < root)) {
            root = static_cast<int>(c.joint);
        }
    }
    return root < 0 ? 0 : root;
}

struct Sway {
    double rmsDegrees = 0.0; // RMS deviation of the facing from the clip's own mean facing
    double peakDegrees = 0.0;
};

Sway swayOf(const std::vector<glm::vec3>& facing) {
    glm::vec3 mean(0.0f);
    for (const glm::vec3& f : facing) {
        mean += f;
    }
    const double meanYaw = std::atan2(static_cast<double>(mean.x), static_cast<double>(mean.z));
    Sway out;
    for (const glm::vec3& f : facing) {
        double d = std::atan2(static_cast<double>(f.x), static_cast<double>(f.z)) - meanYaw;
        while (d > 3.141592653589793) { d -= 6.283185307179586; }
        while (d < -3.141592653589793) { d += 6.283185307179586; }
        const double deg = d * 57.29577951308232;
        out.rmsDegrees += deg * deg;
        out.peakDegrees = std::max(out.peakDegrees, std::abs(deg));
    }
    out.rmsDegrees = facing.empty() ? 0.0 : std::sqrt(out.rmsDegrees / static_cast<double>(facing.size()));
    return out;
}

} // namespace

TEST_CASE("how much a Glowmere pelvis sways, and what the facing window leaves of it",
          "[motionfeatures][facing][phaseC][aliens]") {
    // The measurement `facingWindow`'s default is chosen from. Every Glowmere locomotion clip is
    // in place and faces +Z throughout (ADR-540), so its true heading is constant and **any yaw
    // left in the facing is pelvis sway that would rotate every feature in time with the gait**.
    // Reported for the looping clips, which are the steady cycles, at four windows.
    const fs::path glb = fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
    if (!fs::exists(glb)) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions loadOptions;
    loadOptions.loadImages = false;
    REQUIRE(assets::loadGltf(glb, sc, loadOptions).has_value());
    REQUIRE_FALSE(sc.rigs.empty());
    const scene::SkinnedRig& rig = sc.rigs.front();

    const float windows[] = {0.0f, 0.25f, 0.5f, 1.0f};
    double worstRms[4] = {0, 0, 0, 0};
    double meanRms[4] = {0, 0, 0, 0};
    double worstPeak[4] = {0, 0, 0, 0};
    int clips = 0;
    std::string worstClip;
    for (const scene::AnimationClip& clip : rig.clips) {
        const bool locomotion = clip.name.find("Walk") != std::string::npos ||
                                clip.name.find("Run") != std::string::npos;
        if (!locomotion || clip.length() <= 0.0f) {
            continue;
        }
        ++clips;
        const auto frames =
            static_cast<std::uint32_t>(std::floor(clip.length() * kRate + 0.5f) + 1.0f);
        for (int w = 0; w < 4; ++w) {
            const Sway s = swayOf(scene::clipFacing(rig.skeleton, clip, travelJoint(clip), frames,
                                                    1.0f / kRate, true, windows[w]));
            meanRms[w] += s.rmsDegrees;
            if (s.rmsDegrees > worstRms[w]) {
                worstRms[w] = s.rmsDegrees;
                if (w == 0) {
                    worstClip = clip.name;
                }
            }
            worstPeak[w] = std::max(worstPeak[w], s.peakDegrees);
        }
    }
    REQUIRE(clips > 0);
    std::string report = fmt::format("{} Glowmere locomotion clips; facing yaw about each clip's "
                                     "mean (degrees):\n",
                                     clips);
    for (int w = 0; w < 4; ++w) {
        report += fmt::format("  window {:.2f} s   mean RMS {:6.3f}   worst RMS {:6.3f}   worst peak "
                              "{:6.3f}\n",
                              windows[w], meanRms[w] / clips, worstRms[w], worstPeak[w]);
    }
    report += fmt::format("  worst raw clip: {}\n", worstClip);
    WARN(report);
    // Smoothing must never add sway.
    CHECK(worstRms[2] <= worstRms[0] + 1e-6);
}
