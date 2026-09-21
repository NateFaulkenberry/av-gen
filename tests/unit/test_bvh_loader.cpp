// BVH import (ADR-549), and the first content in this repository that TRAVELS.
//
// Phase 0's licensing survey found one shippable corpus large enough to matter -- 100STYLE, CC BY
// 4.0, four million frames -- and it ships as BVH, which AV Gen could not read. This is the reader.
//
// **The reason this file matters more than a parser test.** Every measurement in Phase A so far was
// taken on in-place clips, because every clip in this repository is in place (ADR-540). Contact
// detection has a horizontal arm that only runs when a clip travels (ADR-546), the retarget has a
// root-scale path that only matters when there is travel to scale (ADR-548), and neither branch has
// ever executed against real data. A hand-built travelling BVH is how those branches get exercised
// before a four-million-frame corpus arrives -- so that a bug there looks like a bug rather than
// like a data problem.
//
// The arms:
//
//   parse         a minimal file: joints, offsets, frame count, frame time, end sites
//   order         channel order is per joint and is APPLIED in the declared order; two files with
//                 identical numbers and different orders must produce different poses
//   scale         centimetre input arrives at metre scale
//   malformed     six ways to be wrong, each refused rather than half-read
//   travelling    a walk that actually moves: the root advances and a foot stays put
//   contacts      ADR-546's horizontal arm fires on it, and finds the planted foot
//   retarget      the travelling walk lands on the real Glowmere alien

#include "assets/bvh_loader.hpp"

#include <fmt/format.h>
#include "assets/gltf_loader.hpp"
#include "scene/motion_analysis.hpp"
#include "scene/retarget.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <string>

using namespace avgen;
using Catch::Approx;

namespace {

namespace fs = std::filesystem;

// The smallest complete BVH: one root with six channels, one child, one end site, two frames.
const char* kMinimal = R"(HIERARCHY
ROOT Hips
{
  OFFSET 0.00 0.00 0.00
  CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation
  JOINT LeftUpLeg
  {
    OFFSET 10.0 -5.0 0.0
    CHANNELS 3 Zrotation Xrotation Yrotation
    End Site
    {
      OFFSET 0.0 -40.0 0.0
    }
  }
}
MOTION
Frames: 2
Frame Time: 0.0333333
0.0 100.0 0.0 0.0 0.0 0.0  0.0 0.0 0.0
0.0 100.0 5.0 0.0 0.0 0.0  0.0 30.0 0.0
)";

// A genuinely travelling walk, built rather than typed: the hips advance along +Z at a constant
// speed while the left foot alternates between planted -- held at a fixed WORLD position by
// cancelling the hips' advance in its own local channel -- and swinging forward.
//
// This is what the alien pack does not have and what 100STYLE is full of.
std::string travellingWalk(int cycles = 3, float speed = 1.2f, float cycleSeconds = 1.0f) {
    const float dt = 1.0f / 30.0f;
    const int perCycle = static_cast<int>(cycleSeconds / dt);
    const int frames = cycles * perCycle;
    std::string out = R"(HIERARCHY
ROOT Hips
{
  OFFSET 0.0 0.0 0.0
  CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation
  JOINT LeftFoot
  {
    OFFSET 0.0 -90.0 0.0
    CHANNELS 3 Xposition Yposition Zposition
    End Site
    {
      OFFSET 0.0 -10.0 0.0
    }
  }
}
MOTION
)";
    out += "Frames: " + std::to_string(frames) + "\n";
    out += "Frame Time: 0.0333333\n";
    for (int f = 0; f < frames; ++f) {
        const float t = static_cast<float>(f) * dt;
        const float hipsZ = speed * t * 100.0f; // centimetres
        const int inCycle = f % perCycle;
        const float phase = static_cast<float>(inCycle) / static_cast<float>(perCycle);
        float footWorldZ = 0.0f;
        float footY = -90.0f;
        if (phase < 0.5f) {
            // STANCE: the foot stays where it was planted at the start of this cycle.
            footWorldZ = speed * (static_cast<float>(f - inCycle) * dt) * 100.0f;
        } else {
            // SWING: it lifts and moves to where it will plant next cycle.
            const float u = (phase - 0.5f) * 2.0f;
            const float from = speed * (static_cast<float>(f - inCycle) * dt) * 100.0f;
            const float to = from + speed * cycleSeconds * 100.0f;
            footWorldZ = from + (to - from) * u;
            footY = -90.0f + 25.0f * std::sin(u * 3.14159265f);
        }
        // The channel is LOCAL, so subtract the hips' own advance.
        const float footLocalZ = footWorldZ - hipsZ;
        out += fmt::format("0.0 100.0 {:.4f} 0.0 0.0 0.0  0.0 {:.4f} {:.4f}\n", hipsZ, footY, footLocalZ);
    }
    return out;
}

glm::vec3 jointAt(const scene::Skeleton& sk, const scene::AnimationClip& clip, const char* name, float t) {
    scene::Pose pose;
    scene::setRestPose(sk, pose);
    scene::sampleClip(clip, t, pose);
    std::vector<glm::mat4> model;
    scene::poseToModel(sk, pose, model);
    return glm::vec3(model[static_cast<std::size_t>(sk.find(name))][3]);
}

} // namespace

TEST_CASE("a minimal BVH parses into a skeleton and a clip", "[bvh]") {
    const auto loaded = assets::parseBvh(kMinimal);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    const assets::BvhClip& bvh = *loaded;

    CHECK(bvh.skeleton.jointCount() == 3); // Hips, LeftUpLeg, and the end site
    CHECK(bvh.endSites == 1);
    CHECK(bvh.frames == 2);
    CHECK(bvh.frameSeconds == Approx(1.0f / 30.0f).margin(1e-4));
    CHECK(bvh.channels == 9);
    CHECK(bvh.skeleton.find("Hips") == 0);
    CHECK(bvh.skeleton.find("LeftUpLeg") == 1);
    // The end site is named after its parent, because a BVH does not name it and a retarget
    // profile has to be able to say which one it means.
    CHECK(bvh.skeleton.find("LeftUpLeg_End") == 2);
    CHECK(bvh.skeleton.joints[1].parent == 0);
    CHECK(bvh.skeleton.joints[2].parent == 1);
    // Offsets are the bones.
    CHECK(bvh.skeleton.joints[1].rest.position.x == Approx(10.0f));
    CHECK(bvh.skeleton.joints[2].rest.position.y == Approx(-40.0f));
    // Two frames, so the clip spans one frame time.
    CHECK(bvh.clip.duration == Approx(1.0f / 30.0f).margin(1e-4));
    CHECK_FALSE(bvh.clip.channels.empty());
    // The root really moved between the two frames -- a parser that read zeros passes every
    // structural assertion above.
    const glm::vec3 a = jointAt(bvh.skeleton, bvh.clip, "Hips", 0.0f);
    const glm::vec3 b = jointAt(bvh.skeleton, bvh.clip, "Hips", 1.0f / 30.0f);
    CHECK(a.y == Approx(100.0f).margin(1e-3));
    CHECK(b.z - a.z == Approx(5.0f).margin(1e-3));
}

TEST_CASE("channel order is per joint and changes the pose", "[bvh]") {
    // The trap BVH sets. The same six numbers under `Zrotation Xrotation Yrotation` and under
    // `Xrotation Yrotation Zrotation` are different rotations, and the difference only shows where
    // two axes are both non-zero -- so a reader that assumes an order is right on half its input
    // and subtly wrong on the rest.
    const char* zxy = R"(HIERARCHY
ROOT Hips
{
  OFFSET 0 0 0
  CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation
  End Site
  {
    OFFSET 0 -50 0
  }
}
MOTION
Frames: 2
Frame Time: 0.0333333
0 0 0  40 50 60
0 0 0  40 50 60
)";
    const char* xyz = R"(HIERARCHY
ROOT Hips
{
  OFFSET 0 0 0
  CHANNELS 6 Xposition Yposition Zposition Xrotation Yrotation Zrotation
  End Site
  {
    OFFSET 0 -50 0
  }
}
MOTION
Frames: 2
Frame Time: 0.0333333
0 0 0  40 50 60
0 0 0  40 50 60
)";
    const auto a = assets::parseBvh(zxy);
    const auto b = assets::parseBvh(xyz);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    const glm::vec3 pa = jointAt(a->skeleton, a->clip, "Hips_End", 0.0f);
    const glm::vec3 pb = jointAt(b->skeleton, b->clip, "Hips_End", 0.0f);
    INFO("zxy " << pa.x << "," << pa.y << "," << pa.z << "  xyz " << pb.x << "," << pb.y << "," << pb.z);
    CHECK(glm::length(pa - pb) > 5.0f);
    // And both are real rotations of a 50-unit bone, not garbage.
    CHECK(glm::length(pa) == Approx(50.0f).margin(1e-2));
    CHECK(glm::length(pb) == Approx(50.0f).margin(1e-2));
}

TEST_CASE("units scale on the way in", "[bvh]") {
    assets::BvhLoadOptions options;
    options.scale = 0.01f; // centimetres, which is what 100STYLE and CMU use
    const auto loaded = assets::parseBvh(kMinimal, options);
    REQUIRE(loaded.has_value());
    CHECK(loaded->skeleton.joints[1].rest.position.x == Approx(0.10f));
    CHECK(loaded->skeleton.joints[2].rest.position.y == Approx(-0.40f));
    const glm::vec3 hips = jointAt(loaded->skeleton, loaded->clip, "Hips", 0.0f);
    CHECK(hips.y == Approx(1.0f).margin(1e-3)); // 100 cm became 1 m
}

TEST_CASE("a malformed BVH is refused rather than half-read", "[bvh]") {
    // Each of these is a real way a file goes wrong, and each must fail rather than produce a
    // skeleton that looks plausible.
    CHECK_FALSE(assets::parseBvh("").has_value());
    CHECK_FALSE(assets::parseBvh("HIERARCHY\nROOT Hips\n{\nOFFSET 0 0 0\n").has_value());
    CHECK_FALSE(assets::parseBvh("MOTION\nFrames: 1\n").has_value());
    // A hierarchy with no channels has nothing to read.
    CHECK_FALSE(assets::parseBvh("HIERARCHY\nROOT H\n{\nOFFSET 0 0 0\n}\nMOTION\nFrames: 1\nFrame "
                                 "Time: 0.03\n")
                    .has_value());
    // Frame data that runs out part way through.
    const char* truncated = R"(HIERARCHY
ROOT Hips
{
  OFFSET 0 0 0
  CHANNELS 3 Xposition Yposition Zposition
  End Site
  {
    OFFSET 0 -1 0
  }
}
MOTION
Frames: 3
Frame Time: 0.0333333
0 0 0
0 0 1
)";
    const auto result = assets::parseBvh(truncated);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("frame 3") != std::string::npos);
    // A frame time of zero is not a frame rate.
    CHECK_FALSE(assets::parseBvh("HIERARCHY\nROOT H\n{\nOFFSET 0 0 0\nCHANNELS 3 Xposition Yposition "
                                 "Zposition\n}\nMOTION\nFrames: 1\nFrame Time: 0\n0 0 0\n")
                    .has_value());
}

TEST_CASE("a travelling walk actually travels, and its foot stays put", "[bvh][travel]") {
    // THE ARM THIS FILE EXISTS FOR. Every other clip in this repository is in place.
    assets::BvhLoadOptions options;
    options.scale = 0.01f;
    options.clipName = "walk";
    const auto loaded = assets::parseBvh(travellingWalk(), options);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    const scene::Skeleton& sk = loaded->skeleton;
    const scene::AnimationClip& clip = loaded->clip;

    // The body covers ground: 1.2 m/s for 3 seconds.
    const glm::vec3 first = jointAt(sk, clip, "Hips", 0.0f);
    const glm::vec3 last = jointAt(sk, clip, "Hips", clip.duration);
    CHECK(last.z - first.z == Approx(1.2f * clip.duration).margin(0.05));
    CHECK(clip.duration > 2.5f);

    // And the foot is stationary in the WORLD during stance, which is the property an in-place clip
    // cannot have and the one the horizontal contact arm looks for.
    const float held = jointAt(sk, clip, "LeftFoot", 0.10f).z;
    for (const float t : {0.10f, 0.20f, 0.30f, 0.40f}) {
        INFO(t);
        CHECK(jointAt(sk, clip, "LeftFoot", t).z == Approx(held).margin(0.02));
    }
    // ...and it is NOT stationary during swing, or the fixture is a standing pose.
    CHECK(jointAt(sk, clip, "LeftFoot", 0.90f).z > held + 0.5f);
}

TEST_CASE("the contact detector's horizontal arm fires on travelling content", "[bvh][travel][contacts]") {
    // ADR-546 switched to a vertical test because in-place clips have no ground frame, and kept a
    // horizontal arm for clips that travel. That arm has never executed against real data. This is
    // the first time it does.
    assets::BvhLoadOptions options;
    options.scale = 0.01f;
    const auto loaded = assets::parseBvh(travellingWalk(), options);
    REQUIRE(loaded.has_value());
    const std::array<scene::ContactJoint, 1> foot{{{"LeftFoot", scene::ContactKind::Foot}}};
    scene::ContactSettings settings;
    settings.looping = false;
    const scene::ClipAnalysis analysis =
        scene::analyseClip(loaded->skeleton, loaded->clip, foot, 0, settings);

    // The clip travels, which is the switch: ADR-546 takes the horizontal branch above 0.05 m/s.
    CHECK(analysis.groundSpeed > 1.0f);
    // Three cycles, so three plants -- and the detector must find them all without inventing a
    // fourth out of the swing.
    INFO("spans " << analysis.contacts.front().spans.size() << " duty "
                  << analysis.contacts.front().dutyCycle);
    CHECK(analysis.contacts.front().spans.size() == 3);
    // Stance is about half of each cycle by construction.
    CHECK(analysis.contacts.front().dutyCycle > 0.3f);
    CHECK(analysis.contacts.front().dutyCycle < 0.7f);
    // And the phase is cyclic at the cycle the fixture was built with.
    CHECK(analysis.phase.cyclic);
    CHECK(analysis.phase.cycleSeconds == Approx(1.0f).margin(0.1));
}

TEST_CASE("a travelling BVH walk retargets onto the Glowmere alien", "[bvh][travel][retarget][aliens]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const fs::path path = fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
    if (!fs::exists(path)) {
        SKIP("assets are not present");
    }
    scene::Scene scene;
    assets::GltfLoadOptions gltf;
    gltf.loadImages = false;
    REQUIRE(assets::loadGltf(path, scene, gltf));
    const scene::SkinnedRig& rig = scene.rigs.front();

    assets::BvhLoadOptions options;
    options.scale = 0.01f;
    options.clipName = "bvh-walk";
    const auto loaded = assets::parseBvh(travellingWalk(), options);
    REQUIRE(loaded.has_value());

    // A deliberately small profile: this fixture has a hips and a foot and the alien has both, so
    // the mapping is two joints. A real 100STYLE profile is twenty; the machinery is the same.
    scene::RetargetProfile profile;
    profile.name = "bvh-to-alien";
    profile.rootJoint = "Hips";
    profile.joints.push_back({"Hips", "root.x", scene::HumanoidRole::Hips});
    profile.joints.push_back({"LeftFoot", "foot.l", scene::HumanoidRole::LeftFoot});

    const scene::RetargetBinding binding = scene::bindRetarget(loaded->skeleton, rig.skeleton, profile);
    INFO((binding.problems.empty() ? std::string() : binding.problems.front()));
    REQUIRE(binding.problems.empty());
    REQUIRE(binding.usable());
    // The alien is shorter than the 1.9 m BVH figure, so the stride scales DOWN. That this number
    // is neither 1 nor absurd is the arm: a rootScale of 1 means the derivation did nothing.
    INFO("rootScale " << binding.rootScale);
    CHECK(binding.rootScale > 0.2f);
    CHECK(binding.rootScale < 1.0f);

    scene::RetargetStats stats;
    const scene::AnimationClip out =
        scene::retargetClip(loaded->clip, loaded->skeleton, rig.skeleton, binding, &stats);
    CHECK(stats.frames > 80);
    CHECK_FALSE(out.channels.empty());

    // The alien travels, and it travels LESS far than the source, in proportion.
    const glm::vec3 sourceTravel = jointAt(loaded->skeleton, loaded->clip, "Hips", loaded->clip.duration) -
                                   jointAt(loaded->skeleton, loaded->clip, "Hips", 0.0f);
    const glm::vec3 alienTravel =
        jointAt(rig.skeleton, out, "root.x", out.duration) - jointAt(rig.skeleton, out, "root.x", 0.0f);
    INFO("source " << sourceTravel.z << " alien " << alienTravel.z);
    CHECK(alienTravel.z > 0.5f);
    CHECK(alienTravel.z < sourceTravel.z);
    CHECK(alienTravel.z == Approx(sourceTravel.z * binding.rootScale).margin(0.2));

    // And the retargeted clip is analysable by the same pipeline: it travels, so the horizontal
    // contact arm applies to it on the alien too.
    const std::array<scene::ContactJoint, 1> foot{{{"foot.l", scene::ContactKind::Foot}}};
    scene::ContactSettings settings;
    settings.looping = false;
    const scene::ClipAnalysis analysis = scene::analyseClip(rig.skeleton, out, foot, 0, settings);
    CHECK(analysis.groundSpeed > 0.2f);
#endif
}
