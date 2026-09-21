// The procedural motion pipeline (Phase B §5).
//
// §5's chain is `base locomotion -> speed/direction adaptation -> stride adjustment -> turn
// adaptation -> foot placement -> body adaptation`. Every link existed before this stage; what did
// not exist was anything making them run **in that order**. The order was whatever a scene file
// happened to list, which is a convention an author can break silently.
//
// **The way it breaks is specific and it is why this is a contract rather than a note**: a foot is
// solved onto the ground, a stride layer then scales the foot's excursion about the body, and the
// carefully placed contact slides off the surface. Nothing reports it. The character just has bad
// feet.

#include "scene/animation.hpp"
#include "scene/pose_layers.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace avgen;
using Catch::Approx;

namespace {

scene::Skeleton walkerRig() {
    scene::Skeleton sk;
    sk.name = "walker";
    sk.joints.push_back(scene::Joint{"rig", -1, scene::Transform{}});
    scene::Transform body;
    body.position = glm::vec3(0.0f, 1.0f, 0.0f);
    sk.joints.push_back(scene::Joint{"body", 0, body});
    scene::Transform knee;
    knee.position = glm::vec3(0.0f, 0.55f, 0.30f);
    sk.joints.push_back(scene::Joint{"knee", 0, knee});
    sk.joints.push_back(scene::Joint{"foot.l", 0, scene::Transform{}});
    sk.palette = {0, 1, 2, 3};
    sk.inverseBind.assign(4, glm::mat4(1.0f));
    return sk;
}

std::vector<scene::AnimationClip> travellingClip() {
    scene::AnimationClip clip;
    clip.name = "Walk";
    scene::AnimationChannel ch;
    ch.joint = 1;
    ch.path = scene::AnimationPath::Translation;
    ch.interpolation = scene::Interpolation::Linear;
    ch.times = {0.0f, 1.0f};
    ch.values.emplace_back(0.0f, 1.0f, 0.0f, 0.0f);
    ch.values.emplace_back(0.0f, 1.0f, 1.0f, 0.0f);
    clip.start = 0.0f;
    clip.duration = 1.0f;
    clip.channels.push_back(std::move(ch));
    return {clip};
}

scene::PoseLayer strideLayer() {
    scene::PoseLayer l;
    l.name = "stride";
    l.kind = scene::PoseLayerKind::Stride;
    l.drive = scene::PoseLayerDrive::Manual;
    l.strideJoint = "foot.l";
    l.strideOrigin = "body";
    l.strideRatio = 0.4f;
    l.strideLift = 0.0f;
    l.weight = 1.0f;
    return l;
}

scene::PoseLayer footLayer() {
    scene::PoseLayer l;
    l.name = "foot";
    l.kind = scene::PoseLayerKind::Foot;
    l.drive = scene::PoseLayerDrive::Manual;
    l.chainRoot = "body";
    l.chainMid = "knee";
    l.chainTip = "foot.l";
    l.hasGround = true;
    l.groundPoint = glm::vec3(0.0f, 0.0f, 0.0f);
    l.groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    l.weight = 1.0f;
    return l;
}

} // namespace

TEST_CASE("the pipeline order is a property of the kind, not of the file", "[pipeline][phaseB]") {
    const scene::Skeleton sk = walkerRig();
    const std::vector<scene::AnimationClip> clips = travellingClip();

    // Authored deliberately backwards: foot first, stride second -- the order that breaks it.
    scene::PoseLayerStack backwards;
    REQUIRE(backwards.bind({footLayer(), strideLayer()}, sk, clips).empty());
    // ...and the right way round.
    scene::PoseLayerStack forwards;
    REQUIRE(forwards.bind({strideLayer(), footLayer()}, sk, clips).empty());

    // **Both run in the same order**, because the order comes from the stage and not the file.
    REQUIRE(backwards.order().size() == 2);
    const auto kindAt = [](const scene::PoseLayerStack& s, std::size_t slot) {
        return s.layers()[s.order()[slot]].kind;
    };
    INFO("authored foot-first runs: " << scene::poseLayerKindName(kindAt(backwards, 0)) << " then "
                                      << scene::poseLayerKindName(kindAt(backwards, 1)));
    CHECK(kindAt(backwards, 0) == scene::PoseLayerKind::Stride);
    CHECK(kindAt(backwards, 1) == scene::PoseLayerKind::Foot);
    CHECK(kindAt(forwards, 0) == scene::PoseLayerKind::Stride);
    CHECK(kindAt(forwards, 1) == scene::PoseLayerKind::Foot);
}

TEST_CASE("a foot ends on the ground whichever order the scene lists the layers",
          "[pipeline][phaseB]") {
    // The outcome the contract exists for. A foot solved and then displaced by a stride warp is
    // planted nowhere; the assertion is that the foot is on the ground in **both** authorings.
    const scene::Skeleton sk = walkerRig();
    const std::vector<scene::AnimationClip> clips = travellingClip();

    const auto footHeight = [&](std::vector<scene::PoseLayer> layers) {
        scene::PoseLayerStack stack;
        REQUIRE(stack.bind(std::move(layers), sk, clips).empty());
        scene::Pose pose;
        scene::setRestPose(sk, pose);
        // **A foot posed close to its rest offset, and that constraint is the finding.** On a
        // DETACHED chain (ADR-543) the three joints are posed independently, so `solveTwoBone`
        // reads its bone lengths from the *current* pose -- posing the tip without the mid
        // therefore changes the limb the solver thinks it has. The first version put the foot at
        // (0, 0.15, 0.55), which shortened the second bone from 0.626 to 0.430, dropped total
        // reach below the target distance, and clamped. The fixture was asking for a leg the
        // fixture had just shortened.
        pose.local[3].position = glm::vec3(0.0f, 0.05f, 0.10f);
        stack.apply(sk, clips, 0.0, pose);
        for (std::size_t k = 0; k < stack.layers().size(); ++k) {
            INFO("layer " << stack.layers()[k].name << " -> "
                          << static_cast<int>(stack.results()[k]) << " ik "
                          << scene::ikStatusName(stack.ikStatuses()[k]));
        }
        std::vector<glm::mat4> model;
        scene::poseToModel(sk, pose, model);
        INFO("foot model y " << model[3][3].y << " z " << model[3][3].z);
        return model[3][3].y;
    };

    const float authoredForwards = footHeight({strideLayer(), footLayer()});
    const float authoredBackwards = footHeight({footLayer(), strideLayer()});
    // **The control: the foot layer alone, with nothing having moved the tip first.**
    const float footOnly = footHeight({footLayer()});
    INFO("foot height -- stride-first " << authoredForwards << ", foot-first " << authoredBackwards
                                        << ", foot alone " << footOnly);

    // The contract, stated as an outcome: the authored order does not change the result.
    CHECK(authoredBackwards == Approx(authoredForwards).margin(1e-4));

    // The control lands the foot on the plane, so the solve works and the target is right.
    CHECK(footOnly == Approx(0.0f).margin(0.02f));

    // With the stride layer in front of it, the foot still reaches the plane -- which is what the
    // ordering contract is for. Stride adjusts the step, the solve then places the foot, and the
    // placement is not undone.
    CHECK(authoredForwards == Approx(0.0f).margin(0.02f));
}

TEST_CASE("the IK solves run last, after everything that moves the body", "[pipeline][phaseB]") {
    // The rule the stage numbers encode: an IK layer puts an end effector at a place, so anything
    // that moved the body afterwards would move the effector off it.
    CHECK(scene::poseLayerStage(scene::PoseLayerKind::Stride) <
          scene::poseLayerStage(scene::PoseLayerKind::Foot));
    CHECK(scene::poseLayerStage(scene::PoseLayerKind::Lean) <
          scene::poseLayerStage(scene::PoseLayerKind::Foot));
    CHECK(scene::poseLayerStage(scene::PoseLayerKind::Secondary) <
          scene::poseLayerStage(scene::PoseLayerKind::Foot));
    CHECK(scene::poseLayerStage(scene::PoseLayerKind::Additive) <
          scene::poseLayerStage(scene::PoseLayerKind::Foot));
    CHECK(scene::poseLayerStage(scene::PoseLayerKind::Foot) <
          scene::poseLayerStage(scene::PoseLayerKind::Reach));
}

TEST_CASE("layers of one kind keep the order the scene gave them", "[pipeline][phaseB]") {
    // Stable within a stage, which is what makes two foot layers stay left then right. A sort that
    // reordered them would swap a character's legs, and the symptom would be a walk that looks
    // almost right.
    const scene::Skeleton sk = walkerRig();
    const std::vector<scene::AnimationClip> clips = travellingClip();
    scene::PoseLayer left = strideLayer();
    left.name = "left";
    scene::PoseLayer right = strideLayer();
    right.name = "right";
    scene::PoseLayerStack stack;
    REQUIRE(stack.bind({left, right}, sk, clips).empty());
    REQUIRE(stack.order().size() == 2);
    CHECK(stack.layers()[stack.order()[0]].name == "left");
    CHECK(stack.layers()[stack.order()[1]].name == "right");
}
