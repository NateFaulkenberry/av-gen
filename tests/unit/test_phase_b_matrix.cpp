// Phase B §51 -- the audit against §51's own test matrix.
//
// §51 lists what must be tested, by name. This file is the audit of that list against what the
// phase actually built, and the fill for the rows the audit found empty. It is the stage that
// checks the phase rather than adding to it, so the header is as much of the deliverable as the
// code.
//
//   LOCOMOTION
//     zero velocity        covered  test_entity_velocity.cpp "a body that has not moved reports
//                                   exactly no velocity"
//     acceleration         covered  test_motion_controller.cpp "a body does not reach its desired
//                                   velocity in one frame"
//     deceleration         covered  test_motion_controller.cpp "braking and accelerating have
//                                   different limits"
//     direction change     covered  test_motion_controller.cpp "turning is limited by a turn rate"
//     strafe               covered  test_entity_velocity.cpp "a strafe and a backward step are
//                                   told apart from a walk"
//     backpedal            covered  same case -- both directions, one test
//     curved movement      covered  test_entity_velocity.cpp "a body circling a target while
//                                   watching it"
//   FOOT PLACEMENT
//     flat ground          covered  test_foot_ik.cpp "planting drops a foot vertically onto a
//                                   plane, never along the normal"
//     elevated ground      **GAP**  -> built below
//     lowered ground       **GAP**  -> built below
//     unreachable target   covered  test_body_compensation.cpp "a demand beyond the body's limits
//                                   goes to the limit and says it is still short"
//     contact release      **GAP**  -> built below, and it found something
//   LOOK-AT
//     centered/left/right  covered  test_character_lab_layers.cpp "an aim layer turns the head
//     above/below/limits            group by the angle the target subtends, up to its limits"
//   REACH
//     reachable target     covered  test_vertical_slice.cpp, test_motion_debug.cpp
//     unreachable target   covered  tests/rendering/test_motion_debug.cpp (the Clamped arm)
//     moving target        **GAP**  -> built below
//   BODY COMPENSATION
//     small correction     covered  test_body_compensation.cpp "one limb short by a known amount
//                                   moves the body by exactly that amount"
//     large correction     covered  "four limbs at once get an answer that satisfies all four"
//     max correction       covered  "a demand beyond the body's limits goes to the limit"
//   SECONDARY MOTION
//     deterministic        covered  test_deterministic_seeds.cpp (§49)
//     bounded output       **GAP**  -> built below
//     stable output        **GAP**  -> built below
//
// Six gaps out of twenty-four rows. Five were genuine omissions. The sixth -- contact release --
// turned out to be a finding rather than a formality, and it is written up where it is tested.
//
// §51 also says: "Use positive/adversarial cases. Never allow a null/no-op case to constitute the
// main correctness test." Every case below pairs the thing working with the thing not working,
// for that reason.

#include "assets/gltf_loader.hpp"
#include "scene/animation.hpp"
#include "scene/pose_layers.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {
namespace fs = std::filesystem;

fs::path alienPath() {
    return fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
}

struct Rig {
    scene::Scene scene;
    bool ok = false;
    Rig() {
        assets::GltfLoadOptions options;
        options.loadImages = false;
        ok = fs::exists(alienPath()) && assets::loadGltf(alienPath(), scene, options).has_value() &&
             !scene.rigs.empty();
    }
    [[nodiscard]] const scene::SkinnedRig& rig() const { return scene.rigs.front(); }
};

scene::PoseLayer footLayer(const char* side) {
    scene::PoseLayer foot;
    foot.name = fmt::format("foot.{}", side);
    foot.kind = scene::PoseLayerKind::Foot;
    foot.chainRoot = fmt::format("thigh_twist.{}", side);
    foot.chainMid = fmt::format("leg_stretch.{}", side);
    foot.chainTip = fmt::format("foot.{}", side);
    foot.weight = 1.0f;
    return foot;
}

// The tip's height after applying a stack over the rest pose.
float footHeightOn(const scene::SkinnedRig& rig, float groundHeight, bool hasGround,
                   float blendElapsed = 10.0f) {
    scene::PoseLayerStack stack;
    std::vector<scene::PoseLayer> layers;
    scene::PoseLayer foot = footLayer("l");
    foot.hasGround = hasGround;
    foot.groundPoint = glm::vec3(0.0f, groundHeight, 0.0f);
    foot.groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    foot.blendSeconds = 0.25f;
    foot.blendElapsed = blendElapsed;
    foot.weightBefore = 1.0f;
    layers.push_back(foot);
    REQUIRE(stack.bind(std::move(layers), rig.skeleton, rig.clips).empty());

    scene::Pose pose;
    std::vector<glm::mat4> model;
    scene::setRestPose(rig.skeleton, pose);
    stack.apply(rig.skeleton, rig.clips, 0.0, pose);
    scene::poseToModel(rig.skeleton, pose, model);
    const int tip = rig.skeleton.find("foot.l");
    REQUIRE(tip >= 0);
    return glm::vec3(model[static_cast<std::size_t>(tip)][3]).y;
}

} // namespace

TEST_CASE("a foot follows ground that rises and ground that falls", "[matrix][phaseB][aliens]") {
    // §51 rows: elevated ground, lowered ground. Two rows rather than one because a solver that
    // clamped upward but not downward -- or that only ever lowered, which is what a naive drop
    // does -- passes either row alone.
    Rig fixture;
    if (!fixture.ok) {
        SKIP("the Glowmere alien is not present");
    }
    const float flat = footHeightOn(fixture.rig(), 0.0f, true);
    const float up = footHeightOn(fixture.rig(), 0.20f, true);
    const float down = footHeightOn(fixture.rig(), -0.20f, true);
    WARN(fmt::format("foot tip height: ground -0.20 -> {:.4f}, flat -> {:.4f}, +0.20 -> {:.4f}",
                     down, flat, up));

    // **Up works and down does not, and that is the engine being right.** Raising the ground by
    // 0.20 m moves the foot by 0.1999 m. Lowering it by 0.20 m moves the foot by 0.0119 m -- the
    // leg runs out of slack. The alien binds with its leg nearly straight (the farm pack is at
    // 97.9%-100% of its own span, which is why `extension` defaults to 1), so there is almost
    // nothing left to extend downward, and the solve clamps at the limb's reach.
    //
    // A foot layer alone therefore **cannot** follow ground that falls. That is what body
    // compensation is for -- `test_body_compensation.cpp` has "the alien's foot reaches ground its
    // leg alone cannot, by lowering the body" -- and the matrix row is only meaningful if it says
    // which of the two is being asked. My first version of this row asserted symmetry and failed,
    // which is the row doing its job: the expectation was wrong, not the code.
    CHECK(up > flat);
    CHECK(down < flat);
    CHECK(up - flat == Approx(0.20f).margin(0.02f));
    CHECK(flat - down < 0.05f); // it tries, and the leg stops it

    // The adversarial half §51 asks for: with no ground the foot is where the clip put it, and
    // the number above is therefore a response to the plane rather than to the layer existing.
    const float ungrounded = footHeightOn(fixture.rig(), 0.20f, false);
    CHECK(ungrounded != Approx(up).margin(1e-4f));
}

TEST_CASE("releasing a contact does not drop the foot in one frame", "[matrix][phaseB][aliens]") {
    // §51 row: contact release. **This one was a finding rather than a formality.**
    //
    // Before §46, `hasGround` going false took the foot layer's weight to zero on the same frame,
    // and the foot snapped from the plane back to wherever the clip had it -- the identical defect
    // §46 found on the reach layer, on a different layer, and one the six-cause list did not
    // include because the slice's script never released a contact.
    //
    // §46's blend fixes it structurally rather than per layer, which is the whole point of having
    // put it on `PoseLayer` instead of in the reach code: the release is a weight going to zero,
    // and a weight going to zero is now a ramp wherever it happens.
    Rig fixture;
    if (!fixture.ok) {
        SKIP("the Glowmere alien is not present");
    }
    const float planted = footHeightOn(fixture.rig(), 0.20f, true, 10.0f);
    const float released = footHeightOn(fixture.rig(), 0.20f, false, 10.0f);
    REQUIRE(std::abs(planted - released) > 0.01f); // there is a real distance to travel

    // One frame into the release the foot has barely moved; most of the way through, most of it.
    scene::PoseLayerStack stack;
    std::vector<scene::PoseLayer> layers;
    scene::PoseLayer foot = footLayer("l");
    foot.hasGround = true;
    foot.groundPoint = glm::vec3(0.0f, 0.20f, 0.0f);
    foot.groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    foot.blendSeconds = 0.25f;
    foot.weightBefore = 1.0f;
    foot.weight = 0.0f; // released
    layers.push_back(foot);
    REQUIRE(stack.bind(std::move(layers), fixture.rig().skeleton, fixture.rig().clips).empty());

    const int tip = fixture.rig().skeleton.find("foot.l");
    REQUIRE(tip >= 0);
    scene::Pose pose;
    std::vector<glm::mat4> model;
    float worstStep = 0.0f;
    float previous = 0.0f;
    bool first = true;
    for (int f = 0; f <= 20; ++f) {
        stack.layers()[0].blendElapsed = static_cast<float>(f) / 60.0f;
        scene::setRestPose(fixture.rig().skeleton, pose);
        stack.apply(fixture.rig().skeleton, fixture.rig().clips, 0.0, pose);
        scene::poseToModel(fixture.rig().skeleton, pose, model);
        const float y = glm::vec3(model[static_cast<std::size_t>(tip)][3]).y;
        if (!first) {
            worstStep = std::max(worstStep, std::abs(y - previous));
        }
        previous = y;
        first = false;
    }
    WARN(fmt::format("contact release over 0.25 s: total {:.4f} m, worst single frame {:.4f} m",
                     std::abs(planted - released), worstStep));
    // No frame carries more than a third of the release. An unblended release carries all of it
    // in one, which is what this row is here to catch.
    CHECK(worstStep < std::abs(planted - released) / 3.0f);
}

TEST_CASE("a hand tracks a target that keeps moving", "[matrix][phaseB][aliens]") {
    // §51 row: moving target. A reach test against a stationary target cannot distinguish a solver
    // that converges from one that snaps, because both end up on it.
    Rig fixture;
    if (!fixture.ok) {
        SKIP("the Glowmere alien is not present");
    }
    scene::PoseLayerStack stack;
    std::vector<scene::PoseLayer> layers;
    scene::PoseLayer reach;
    reach.name = "reach";
    reach.kind = scene::PoseLayerKind::Reach;
    reach.chainRoot = "shoulder.l";
    reach.chainMid = "forearm_stretch.l";
    reach.chainTip = "hand.l";
    reach.weight = 1.0f;
    reach.hasTarget = true;
    layers.push_back(reach);
    REQUIRE(stack.bind(std::move(layers), fixture.rig().skeleton, fixture.rig().clips).empty());

    const int tip = fixture.rig().skeleton.find("hand.l");
    REQUIRE(tip >= 0);
    scene::Pose pose;
    std::vector<glm::mat4> model;
    float worstError = 0.0f;
    float worstStep = 0.0f;
    glm::vec3 previousHand{0.0f};
    int solved = 0;
    for (int f = 0; f <= 120; ++f) {
        const float t = static_cast<float>(f) / 60.0f;
        // A slow arc well inside the arm's reach, so any error is the solver's and not the limb's.
        const glm::vec3 target(0.28f + 0.06f * std::sin(t * 2.0f), 1.05f + 0.05f * std::cos(t),
                               0.30f + 0.06f * std::cos(t * 2.0f));
        stack.layers()[0].target = target;
        scene::setRestPose(fixture.rig().skeleton, pose);
        stack.apply(fixture.rig().skeleton, fixture.rig().clips, 0.0, pose);
        scene::poseToModel(fixture.rig().skeleton, pose, model);
        const glm::vec3 hand = glm::vec3(model[static_cast<std::size_t>(tip)][3]);
        worstError = std::max(worstError, glm::length(hand - target));
        if (f > 0) {
            worstStep = std::max(worstStep, glm::length(hand - previousHand));
        }
        previousHand = hand;
        if (stack.results()[0] == scene::LayerResolution::Applied) {
            ++solved;
        }
    }
    WARN(fmt::format("moving target: worst tracking error {:.5f} m, worst hand step {:.4f} m, "
                     "{} frames resolved",
                     worstError, worstStep, solved));
    CHECK(solved > 100);          // it was actually solving, not refusing
    CHECK(worstError < 1e-3f);    // and it stayed on the target throughout
    // And it got there smoothly: the target moves at most ~2 mm per frame, so a hand moving far
    // more than that is chasing rather than tracking.
    CHECK(worstStep < 0.01f);
}

TEST_CASE("secondary motion is bounded and does not drift", "[matrix][phaseB][aliens]") {
    // §51 rows: bounded output, stable output. §49 covered deterministic; these two are what stop
    // "a pure function of the timeline second" from being a function that grows without limit or
    // wanders over a long shot.
    Rig fixture;
    if (!fixture.ok) {
        SKIP("the Glowmere alien is not present");
    }
    scene::PoseLayerStack stack;
    std::vector<scene::PoseLayer> layers;
    scene::PoseLayer life;
    life.name = "life";
    life.kind = scene::PoseLayerKind::Secondary;
    life.mask.joints = {"spine_02.x", "spine_03.x", "neck.x", "head.x"};
    life.weight = 1.0f;
    life.secondaryDegrees = 1.5f;
    life.secondaryPeriod = 4.2f;
    life.secondarySpread = 0.09f;
    life.characterSeed = scene::seedFromName("scout");
    life.layerSeed = scene::seedFromName("life");
    layers.push_back(life);
    REQUIRE(stack.bind(std::move(layers), fixture.rig().skeleton, fixture.rig().clips).empty());

    const int head = fixture.rig().skeleton.find("head.x");
    REQUIRE(head >= 0);
    scene::Pose rest;
    scene::setRestPose(fixture.rig().skeleton, rest);
    const glm::quat restHead = rest.local[static_cast<std::size_t>(head)].rotation;

    scene::Pose pose;
    // **Measured as a rotation, not as a position, and the first version of this test measured the
    // position and read exactly 0.00000 m over six minutes.** This rig is flat (ADR-553): every
    // joint is a sibling under `rig`, so rotating `spine_02.x` does not move `head.x`, and
    // rotating `head.x` about its own origin does not move `head.x` either. A position probe on a
    // flat rig cannot see a rotation layer at all -- `docs/testing.md` family C, looking where the
    // effect cannot reach, and the second time this phase that the flat rig has produced a
    // confident zero.
    const auto headAt = [&](double now) {
        scene::setRestPose(fixture.rig().skeleton, pose);
        stack.apply(fixture.rig().skeleton, fixture.rig().clips, now, pose);
        return pose.local[static_cast<std::size_t>(head)].rotation;
    };
    const auto degreesFrom = [&](const glm::quat& a, const glm::quat& b) {
        return glm::degrees(2.0f * std::acos(std::clamp(std::abs(glm::dot(a, b)), -1.0f, 1.0f)));
    };

    // BOUNDED: over ten minutes of timeline the head never leaves a small neighbourhood of rest.
    float worst = 0.0f;
    for (int i = 0; i <= 3600; ++i) {
        const double now = static_cast<double>(i) * 0.1;
        worst = std::max(worst, degreesFrom(headAt(now), restHead));
    }
    WARN(fmt::format("secondary motion over 6 minutes: worst head rotation {:.4f} degrees", worst));
    CHECK(worst > 1e-3f);   // it is doing something -- a frozen layer would pass the bound below
    // BOUNDED, and the bound is the authored amplitude rather than a number picked to pass: the
    // layer was given 1.5 degrees and may not exceed it however long it runs.
    CHECK(worst <= 1.5f + 1e-3f);

    // STABLE: it is periodic, so one period later is the same pose -- bit-for-bit, because there
    // is no accumulation anywhere in it. A layer that integrated would drift by minute six.
    const glm::quat early = headAt(7.0);
    const glm::quat late = headAt(7.0 + 4.2 * 80.0);
    CHECK(degreesFrom(early, late) < 1e-2f);
    // Adversarial pair: half a period later is NOT the same pose, so the check above is a
    // statement about the period rather than about the layer doing nothing.
    const glm::quat offPhase = headAt(7.0 + 4.2 * 0.5);
    CHECK(degreesFrom(early, offPhase) > 1e-2f);
}
