// Stride warping (Phase B §7, `PoseLayerKind::Stride`).
//
// **Why this is the load-bearing half of speed adaptation here, rather than clip blending.**
// `Gait::footSlip` across the shipping cast: 100 warnings, 51 entities, **97 of them the body
// moving slower than its own stride**, median ratio 0.250, Glowmere aliens at 0.016-0.042x with
// their playback-rate clamp already 7.5x below default and still saturated. Below the slowest
// authored clip there is nothing to blend toward and no rate left to give. The step has to get
// shorter in space.

#include "scene/animation.hpp"
#include "scene/pose_layers.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace avgen;
using Catch::Approx;

namespace {

// A body and a foot, both children of a `rig` wrapper that nothing animates -- which is the shape
// `alien-scout.glb` actually has, and the reason the origin joint is resolved from the clip's
// translation channels rather than taken as joint 0 (ADR-337).
scene::Skeleton walkerRig() {
    scene::Skeleton sk;
    sk.name = "walker";
    sk.joints.push_back(scene::Joint{"rig", -1, scene::Transform{}});
    sk.joints.push_back(scene::Joint{"body", 0, scene::Transform{}});
    sk.joints.push_back(scene::Joint{"foot.l", 0, scene::Transform{}});
    sk.palette = {0, 1, 2};
    sk.inverseBind = {glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
    return sk;
}

// A clip that translates `body` (joint 1) so ADR-337's rule has something to find.
std::vector<scene::AnimationClip> travellingClip() {
    scene::AnimationClip clip;
    clip.name = "Walk";
    scene::AnimationChannel ch;
    ch.joint = 1;
    ch.path = scene::AnimationPath::Translation;
    ch.interpolation = scene::Interpolation::Linear;
    ch.times = {0.0f, 1.0f};
    ch.values.emplace_back(0.0f, 0.0f, 0.0f, 0.0f);
    ch.values.emplace_back(0.0f, 0.0f, 1.0f, 0.0f);
    clip.start = 0.0f;
    clip.duration = 1.0f;
    clip.channels.push_back(std::move(ch));
    return {clip};
}

scene::PoseLayer strideLayer(float ratio) {
    scene::PoseLayer layer;
    layer.name = "stride.l";
    layer.kind = scene::PoseLayerKind::Stride;
    layer.drive = scene::PoseLayerDrive::Manual;
    layer.strideJoint = "foot.l";
    layer.strideOrigin = "body";
    layer.strideRatio = ratio;
    layer.strideLift = 0.0f; // height left alone, so the horizontal claim is measured alone
    layer.weight = 1.0f;
    return layer;
}

// The foot a stride ahead of the body and half a metre up, which is a step at its apex.
scene::Pose steppingPose(const scene::Skeleton& sk) {
    scene::Pose pose;
    scene::setRestPose(sk, pose);
    pose.local[1].position = glm::vec3(0.0f, 1.0f, 2.0f);   // body
    pose.local[2].position = glm::vec3(0.0f, 1.2f, 2.8f);   // foot: 0.8 ahead, 0.2 up
    return pose;
}

struct Result {
    glm::vec3 foot{0.0f};
    scene::LayerResolution resolution = scene::LayerResolution::Inactive;
};

Result run(float ratio, float lo = 0.35f, float hi = 1.6f, float lift = 0.0f, float weight = 1.0f) {
    const scene::Skeleton sk = walkerRig();
    const std::vector<scene::AnimationClip> clips = travellingClip();
    scene::PoseLayer layer = strideLayer(ratio);
    layer.strideMin = lo;
    layer.strideMax = hi;
    layer.strideLift = lift;
    layer.weight = weight;
    scene::PoseLayerStack stack;
    const std::vector<std::string> problems = stack.bind({layer}, sk, clips);
    REQUIRE(problems.empty());
    scene::Pose pose = steppingPose(sk);
    stack.apply(sk, clips, 0.0, pose);
    Result out;
    out.foot = pose.local[2].position;
    out.resolution = stack.results().front();
    return out;
}

} // namespace

TEST_CASE("a ratio of one changes nothing at all", "[stride][layers]") {
    // The control arm, and it is the one that would catch a layer that scaled the wrong thing:
    // an implementation that scaled the joint's LOCAL translation about its parent, or that
    // measured the excursion from joint 0, would still move the foot here.
    const Result r = run(1.0f);
    CHECK(r.foot.x == Approx(0.0f).margin(1e-5));
    CHECK(r.foot.y == Approx(1.2f).margin(1e-5));
    CHECK(r.foot.z == Approx(2.8f).margin(1e-5));
    CHECK(r.resolution == scene::LayerResolution::Applied);
}

TEST_CASE("a body at half its authored speed takes half the step", "[stride][layers]") {
    // The foot stands 0.8 ahead of the body. At ratio 0.5 it should stand 0.4 ahead -- and the
    // body must not have moved, because stride warping shortens the step and never relocates the
    // character (§36: one authoritative movement result, and this is not it).
    const Result r = run(0.5f);
    INFO("foot at " << r.foot.x << "," << r.foot.y << "," << r.foot.z);
    CHECK(r.foot.z == Approx(2.0f + 0.4f).margin(1e-4));
    CHECK(r.resolution == scene::LayerResolution::Applied);
}

TEST_CASE("the excursion is measured from the body, not from the rig's wrapper", "[stride][layers]") {
    // **The assertion that catches ADR-337's mistake.** `rig` is joint 0 and nothing animates it;
    // `body` is what travels. Measuring from joint 0 would treat the body's whole 2.0 of travel as
    // stride and, at ratio 0.5, drag the foot back to 1.4 instead of 2.4.
    const Result r = run(0.5f);
    INFO("foot z " << r.foot.z << " (from-body expects 2.4, from-wrapper would give 1.4)");
    CHECK(r.foot.z == Approx(2.4f).margin(1e-4));
    CHECK(r.foot.z > 2.0f);
}

TEST_CASE("the clamp is reported rather than applied silently", "[stride][layers]") {
    // The Glowmere aliens sit at 0.016-0.042x. A twentieth of a stride is a character mincing, not
    // walking, so the layer clamps -- and says so, because a clamp nobody can see is exactly how
    // `Gait::playbackRate` sat saturated across the whole shipping cast with nothing reporting it.
    const Result low = run(0.02f, 0.35f, 1.6f);
    INFO("clamped foot z " << low.foot.z);
    CHECK(low.resolution == scene::LayerResolution::Clamped);
    // Clamped to 0.35, so the 0.8 excursion becomes 0.28, not 0.016.
    CHECK(low.foot.z == Approx(2.0f + (0.8f * 0.35f)).margin(1e-4));

    const Result high = run(9.0f, 0.35f, 1.6f);
    CHECK(high.resolution == scene::LayerResolution::Clamped);
    CHECK(high.foot.z == Approx(2.0f + (0.8f * 1.6f)).margin(1e-4));
}

TEST_CASE("the lift scales with the step, on a dial", "[stride][layers]") {
    // A short step does not lift the foot as high; shortening the reach while leaving the lift
    // alone is what makes a shortened walk read as a march. The foot is 0.2 above the body.
    const Result none = run(0.5f, 0.35f, 1.6f, 0.0f);
    CHECK(none.foot.y == Approx(1.2f).margin(1e-4));           // height untouched

    const Result full = run(0.5f, 0.35f, 1.6f, 1.0f);
    CHECK(full.foot.y == Approx(1.0f + 0.1f).margin(1e-4));    // halved with the stride

    const Result partial = run(0.5f, 0.35f, 1.6f, 0.7f);
    INFO("partial lift y " << partial.foot.y);
    CHECK(partial.foot.y > full.foot.y);
    CHECK(partial.foot.y < none.foot.y);
}

TEST_CASE("weight fades the correction rather than switching it", "[stride][layers]") {
    // §63: every procedural layer must be disableable, and a weight is how. Half weight is half
    // the correction, which is what lets a scene ease it in.
    const Result off = run(0.5f, 0.35f, 1.6f, 0.0f, 0.0f);
    CHECK(off.resolution == scene::LayerResolution::Inactive);
    CHECK(off.foot.z == Approx(2.8f).margin(1e-5)); // untouched

    const Result half = run(0.5f, 0.35f, 1.6f, 0.0f, 0.5f);
    // Full correction lands at 2.4 from 2.8, so half of it is 2.6.
    CHECK(half.foot.z == Approx(2.6f).margin(1e-4));
}

TEST_CASE("a stride layer that names a joint this rig lacks says so", "[stride][layers]") {
    // §64: do not hide failures. A typo in a joint name must not be a silent no-op, which is the
    // failure the whole layer stack was built to stop repeating.
    const scene::Skeleton sk = walkerRig();
    const std::vector<scene::AnimationClip> clips = travellingClip();
    scene::PoseLayer layer = strideLayer(0.5f);
    layer.strideJoint = "foot.typo";
    scene::PoseLayerStack stack;
    const std::vector<std::string> problems = stack.bind({layer}, sk, clips);
    INFO((problems.empty() ? std::string("no problems reported") : problems.front()));
    CHECK_FALSE(problems.empty());

    // And a layer whose joint and origin are the same joint scales an excursion that is always
    // zero -- a no-op that looks like a working layer, so it is refused too.
    scene::PoseLayer same = strideLayer(0.5f);
    same.strideOrigin = same.strideJoint;
    scene::PoseLayerStack stack2;
    CHECK_FALSE(stack2.bind({same}, sk, clips).empty());
}
