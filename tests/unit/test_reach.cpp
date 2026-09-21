// Reach / hand IK (Phase B §23) and reachability (§24).
//
// **The structural claim under test is ADR-543's.** That decision -- a limb is three named joints
// and a write-back rule, not an ancestor chain -- was made for the alien's legs, whose three joints
// sit on separate branches. Its arms are the same shape and worse: `shoulder.l` hangs off
// `spine_05.x` while `forearm_stretch.l` and `hand.l` both hang off `rig`, so **one limb spans
// three different parents**. This is the second consumer of that decision on the same rig, and it
// needed no change to it.
//
// §23 is explicit that this is not a grasping system: target -> reach pose -> IK. §24 is the half
// that makes it usable -- a reach that silently produces a broken arm is worse than one that
// reports it cannot.

#include "assets/asset_registry.hpp"
#include "assets/gltf_loader.hpp"
#include "scene/animation.hpp"
#include "scene/pose_layers.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace avgen;
using Catch::Approx;

namespace {

namespace fs = std::filesystem;

fs::path alienPath() {
#ifdef AVGEN_SOURCE_DIR
    return fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
#else
    return {};
#endif
}

scene::PoseLayer armLayer(const char* side) {
    scene::PoseLayer layer;
    layer.name = std::string("reach.") + side;
    layer.kind = scene::PoseLayerKind::Reach;
    layer.drive = scene::PoseLayerDrive::Manual;
    layer.chainRoot = std::string("shoulder.") + side;
    layer.chainMid = std::string("forearm_stretch.") + side;
    layer.chainTip = std::string("hand.") + side;
    layer.poleDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    layer.weight = 1.0f;
    return layer;
}

} // namespace

TEST_CASE("the alien's arm is not an ancestor chain either, and reach solves it anyway",
          "[reach][ik][aliens]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(alienPath())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions options;
    options.loadImages = false;
    auto loaded = assets::loadGltf(alienPath(), sc, options);
    REQUIRE(loaded.has_value());
    REQUIRE_FALSE(sc.rigs.empty());
    const scene::SkinnedRig& rig = sc.rigs.front();
    const scene::Skeleton& sk = rig.skeleton;

    // **The topology, asserted rather than described.** A re-export that made the arm an ordinary
    // chain would pass every other test in this file and quietly invalidate the claim ADR-543
    // rests on, so the shape is checked here.
    const int shoulder = sk.find("shoulder.l");
    const int forearm = sk.find("forearm_stretch.l");
    const int hand = sk.find("hand.l");
    REQUIRE(shoulder >= 0);
    REQUIRE(forearm >= 0);
    REQUIRE(hand >= 0);
    INFO("shoulder parent " << sk.joints[static_cast<std::size_t>(shoulder)].parent
                            << ", forearm parent " << sk.joints[static_cast<std::size_t>(forearm)].parent
                            << ", hand parent " << sk.joints[static_cast<std::size_t>(hand)].parent);
    // Not a chain: the hand is not a descendant of the forearm, nor the forearm of the shoulder.
    CHECK(sk.joints[static_cast<std::size_t>(hand)].parent !=
          static_cast<int>(forearm));
    CHECK(sk.joints[static_cast<std::size_t>(forearm)].parent !=
          static_cast<int>(shoulder));

    scene::PoseLayerStack stack;
    const std::vector<std::string> problems = stack.bind({armLayer("l")}, sk, rig.clips);
    INFO((problems.empty() ? std::string("none") : problems.front()));
    REQUIRE(problems.empty());

    // Where the hand rests, so the target can be somewhere it is not.
    scene::Pose pose;
    scene::setRestPose(sk, pose);
    std::vector<glm::mat4> model;
    scene::poseToModel(sk, pose, model);
    const glm::vec3 restHand(model[static_cast<std::size_t>(hand)][3]);
    const glm::vec3 restShoulder(model[static_cast<std::size_t>(shoulder)][3]);
    const float armLength = glm::length(restHand - restShoulder);
    INFO("arm spans " << armLength << " from shoulder to hand at rest");
    REQUIRE(armLength > 0.1f);

    // A target comfortably inside reach, off to the side so the solve has work to do.
    scene::PoseLayer layer = armLayer("l");
    layer.target = restShoulder + glm::vec3(armLength * 0.5f, -armLength * 0.4f, armLength * 0.4f);
    layer.hasTarget = true;
    stack.bind({layer}, sk, rig.clips);
    scene::Pose posed;
    scene::setRestPose(sk, posed);
    const scene::PoseLayerStats stats = stack.apply(sk, rig.clips, 0.0, posed);
    CHECK(stats.applied == 1);
    CHECK(stack.results().front() == scene::LayerResolution::Applied);
    CHECK(stack.ikStatuses().front() == scene::IkStatus::Solved);

    std::vector<glm::mat4> after;
    scene::poseToModel(sk, posed, after);
    const glm::vec3 solvedHand(after[static_cast<std::size_t>(hand)][3]);
    INFO("hand landed " << glm::length(solvedHand - layer.target) << " from the target");
    CHECK(glm::length(solvedHand - layer.target) < 0.02f);
    // And it actually moved, so "it is on the target" is not a statement about where it already was.
    CHECK(glm::length(solvedHand - restHand) > 0.1f);
#endif
}

TEST_CASE("a target out of reach is reported, not faked", "[reach][ik][aliens]") {
    // §24. A reach that silently produces a broken arm is worse than one that says it cannot: the
    // limb aims at the target and stops at its own limit, and `IkStatus::Clamped` is the answer.
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(alienPath())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions options;
    options.loadImages = false;
    REQUIRE(assets::loadGltf(alienPath(), sc, options).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();
    const scene::Skeleton& sk = rig.skeleton;
    const int shoulder = sk.find("shoulder.l");
    const int hand = sk.find("hand.l");
    REQUIRE(shoulder >= 0);

    scene::Pose rest;
    scene::setRestPose(sk, rest);
    std::vector<glm::mat4> model;
    scene::poseToModel(sk, rest, model);
    const glm::vec3 restShoulder(model[static_cast<std::size_t>(shoulder)][3]);

    scene::PoseLayer layer = armLayer("l");
    layer.target = restShoulder + glm::vec3(50.0f, 0.0f, 0.0f);   // across the valley
    layer.hasTarget = true;
    scene::PoseLayerStack stack;
    REQUIRE(stack.bind({layer}, sk, rig.clips).empty());
    scene::Pose posed;
    scene::setRestPose(sk, posed);
    (void)stack.apply(sk, rig.clips, 0.0, posed);

    INFO("status " << scene::ikStatusName(stack.ikStatuses().front()));
    CHECK(stack.ikStatuses().front() == scene::IkStatus::Clamped);
    CHECK(stack.results().front() == scene::LayerResolution::Clamped);

    // The arm is straight toward the target rather than stretched to it: the bones keep their
    // lengths, which is what "not faked" means.
    std::vector<glm::mat4> after;
    scene::poseToModel(sk, posed, after);
    const glm::vec3 solvedHand(after[static_cast<std::size_t>(hand)][3]);
    const float reached = glm::length(solvedHand - restShoulder);
    INFO("hand ended " << reached << " from the shoulder, target was 50 away");
    CHECK(reached < 2.0f);
    // ...and it is on the ray toward the target, not left where it was.
    const glm::vec3 toTarget = glm::normalize(layer.target - restShoulder);
    const glm::vec3 toHand = glm::normalize(solvedHand - restShoulder);
    CHECK(glm::dot(toTarget, toHand) > 0.9f);
#endif
}

TEST_CASE("a reach with no target does nothing and says so", "[reach][ik][aliens]") {
    // A hand that has been given no work keeps whatever the animation had it doing. Crucially a
    // reach never falls back to a ground plane the way a foot does -- a hand planted on the floor
    // under the shoulder is not a reach, it is a bug that looks like one.
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(alienPath())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions options;
    options.loadImages = false;
    REQUIRE(assets::loadGltf(alienPath(), sc, options).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();
    const scene::Skeleton& sk = rig.skeleton;

    scene::PoseLayer layer = armLayer("l");
    layer.hasTarget = false;
    layer.hasGround = true;                       // offered a plane, which a foot would plant on
    layer.groundPoint = glm::vec3(0.0f);
    layer.groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    scene::PoseLayerStack stack;
    REQUIRE(stack.bind({layer}, sk, rig.clips).empty());
    scene::Pose posed;
    scene::setRestPose(sk, posed);
    scene::Pose before = posed;
    (void)stack.apply(sk, rig.clips, 0.0, posed);

    CHECK(stack.results().front() == scene::LayerResolution::NoTarget);
    const int hand = sk.find("hand.l");
    CHECK(posed.local[static_cast<std::size_t>(hand)].position ==
          before.local[static_cast<std::size_t>(hand)].position);
#endif
}
