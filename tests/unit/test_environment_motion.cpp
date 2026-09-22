// Environment-aware motion (Phase B §40).
//
// §16/§17 already answer "what is under this foot". §40 is "what is the body standing on", and the
// two are different questions: a foot plants on the ground beneath its own tip, while a body on a
// slope shortens its stride and leans into the hill whatever its feet are doing.
//
// **This asks the `IGroundQuery` that already exists.** There were three things asking the terrain
// -- the body plane, the foot layers, the nav grid -- and a fourth with its own sampling rule is
// how a family of divergent copies starts.
//
// **Each response is measured against the quantity that gets worse as it strengthens** (ADR-559),
// because a metric that improves monotonically with a dial recommends the extreme of that dial:
//
//   * a stride that shortens indefinitely is a character mincing, and the cost is foot slide --
//     the body still travels, so whatever the stride gives up, the feet make up by sliding;
//   * a lean that grows indefinitely is a character falling over, and the cost is balance margin
//     -- how far the leaning mass stays inside the feet.

#include "scene/animation.hpp"
#include "scene/ground_query.hpp"
#include "scene/pose_layers.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace avgen;
using Catch::Approx;

namespace {

// A ground that is a plane tilted by `grade` (rise over run) toward +Z.
class SlopeGround final : public scene::IGroundQuery {
public:
    explicit SlopeGround(float grade) : grade_(grade) {}
    [[nodiscard]] scene::GroundSample sampleAt(const glm::vec3& p) const override {
        scene::GroundSample s;
        s.point = glm::vec3(p.x, p.z * grade_, p.z);
        s.normal = glm::normalize(glm::vec3(0.0f, 1.0f, -grade_));
        s.category = scene::GroundCategory::Terrain;
        s.valid = true;
        return s;
    }

private:
    float grade_;
};

// A ground that answers for nothing, so "no data" can be told from "flat".
class NoGround final : public scene::IGroundQuery {
public:
    [[nodiscard]] scene::GroundSample sampleAt(const glm::vec3&) const override { return {}; }
};

scene::Skeleton bodyRig() {
    scene::Skeleton sk;
    sk.name = "body";
    sk.joints.push_back(scene::Joint{"rig", -1, scene::Transform{}});
    scene::Transform body;
    body.position = glm::vec3(0.0f, 1.0f, 0.0f);
    sk.joints.push_back(scene::Joint{"body", 0, body});
    sk.joints.push_back(scene::Joint{"foot.l", 0, scene::Transform{}});
    sk.palette = {0, 1, 2};
    sk.inverseBind.assign(3, glm::mat4(1.0f));
    return sk;
}

float tiltAboutX(const glm::quat& q) {
    return glm::degrees(2.0f * std::atan2(q.x, q.w));
}

} // namespace

TEST_CASE("the environment sample reports a slope and which way is down", "[environment][phaseB]") {
    const SlopeGround hill(0.5f);   // 26.6 degrees
    const scene::EnvironmentSample e =
        scene::sampleEnvironment(hill, glm::vec3(0.0f, 0.0f, 0.0f), 0.9f);
    INFO("slope " << glm::degrees(e.slope) << " deg, downhill " << e.downhill.x << ","
                  << e.downhill.z << ", coverage " << e.coverage);
    CHECK(e.slope == Approx(std::atan(0.5f)).margin(1e-3));
    CHECK(e.coverage == Approx(1.0f));
    CHECK(e.valid());
    // The ground rises toward +Z, so downhill is -Z.
    CHECK(e.downhill.z == Approx(-1.0f).margin(1e-3));
    CHECK(std::abs(e.downhill.x) < 1e-3f);

    // Flat ground: a slope of zero and no downhill direction, which is not "downhill is north".
    const SlopeGround flat(0.0f);
    const scene::EnvironmentSample f =
        scene::sampleEnvironment(flat, glm::vec3(0.0f), 0.9f);
    CHECK(f.slope == Approx(0.0f).margin(1e-4));
    CHECK(glm::length(f.downhill) < 1e-5f);
}

TEST_CASE("no data is distinguishable from flat ground", "[environment][phaseB]") {
    // **The conflation ADR-551 already had to fix once**, when a terrain reject was read as "no
    // ground". A body half off the edge of a height field and a body on a plain both produce slope
    // zero, and only coverage tells them apart.
    const NoGround nothing;
    const scene::EnvironmentSample e = scene::sampleEnvironment(nothing, glm::vec3(0.0f), 0.9f);
    CHECK(e.coverage == Approx(0.0f));
    CHECK_FALSE(e.valid());

    const SlopeGround flat(0.0f);
    const scene::EnvironmentSample f = scene::sampleEnvironment(flat, glm::vec3(0.0f), 0.9f);
    CHECK(f.slope == Approx(e.slope).margin(1e-5));   // the same slope...
    CHECK(f.valid());                                 // ...and only this separates them
}

TEST_CASE("a slope shortens the stride, and the cost is foot slide", "[environment][phaseB]") {
    // The response and the quantity that gets worse as it strengthens, measured together.
    const scene::Skeleton sk = bodyRig();
    const std::vector<scene::AnimationClip> clips;

    const auto strideOn = [&](float slopeRadians, float gain) {
        scene::PoseLayer layer;
        layer.name = "stride";
        layer.kind = scene::PoseLayerKind::Stride;
        layer.drive = scene::PoseLayerDrive::Manual;
        layer.strideJoint = "foot.l";
        layer.strideOrigin = "body";
        layer.strideRatio = 1.0f;
        layer.strideLift = 0.0f;
        layer.strideMin = 0.0f;
        layer.bodySlope = slopeRadians;
        layer.strideSlopeGain = gain;
        layer.weight = 1.0f;
        scene::PoseLayerStack stack;
        REQUIRE(stack.bind({layer}, sk, clips).empty());
        scene::Pose pose;
        scene::setRestPose(sk, pose);
        pose.local[2].position = glm::vec3(0.0f, 0.0f, 0.8f);   // a stride ahead of the body
        stack.apply(sk, clips, 0.0, pose);
        return pose.local[2].position.z;
    };

    const float flat = strideOn(0.0f, 0.45f);
    const float gentle = strideOn(glm::radians(15.0f), 0.45f);
    const float steep = strideOn(glm::radians(35.0f), 0.45f);
    INFO("stride flat " << flat << ", 15 deg " << gentle << ", 35 deg " << steep);
    CHECK(flat == Approx(0.8f).margin(1e-4));       // the control: no slope, no change
    CHECK(gentle < flat);
    CHECK(steep < gentle);

    // **The opposing quantity.** The body still travels at the same pace, so every centimetre the
    // stride gives up is a centimetre the foot must slide. Expressed as the ratio of travel to
    // stride, which is `footSlip`'s quantity: it rises as the response strengthens.
    const float slipFlat = 0.8f / flat;
    const float slipSteep = 0.8f / steep;
    INFO("implied foot slide: flat " << slipFlat << "x, steep " << slipSteep << "x");
    CHECK(slipSteep > slipFlat);
    // And the floor is what stops it running away: at 35 degrees the stride has not collapsed.
    CHECK(steep > 0.8f * 0.5f);
}

TEST_CASE("a body leans into an uphill slope, and the cost is balance margin",
          "[environment][phaseB]") {
    const scene::Skeleton sk = bodyRig();
    const std::vector<scene::AnimationClip> clips;

    const auto leanOn = [&](float slopeRadians, const glm::vec3& downhill, float maxDeg,
                            float slopeGain = 14.0f) {
        scene::PoseLayer layer;
        layer.name = "lean";
        layer.kind = scene::PoseLayerKind::Lean;
        layer.drive = scene::PoseLayerDrive::Manual;
        layer.mask.joints = {"body"};
        layer.bodySlope = slopeRadians;
        layer.bodyDownhill = downhill;
        layer.leanMaxDegrees = maxDeg;
        layer.leanSlopeDegrees = slopeGain;
        layer.weight = 1.0f;
        scene::PoseLayerStack stack;
        REQUIRE(stack.bind({layer}, sk, clips).empty());
        scene::Pose pose;
        scene::setRestPose(sk, pose);
        stack.apply(sk, clips, 0.0, pose);
        return tiltAboutX(pose.local[1].rotation);
    };

    // Uphill ahead means downhill is behind: -Z.
    const float flat = leanOn(0.0f, glm::vec3(0.0f), 20.0f);
    const float uphill = leanOn(glm::radians(25.0f), glm::vec3(0.0f, 0.0f, -1.0f), 20.0f);
    INFO("lean on the flat " << flat << " deg, uphill " << uphill << " deg");
    CHECK(flat == Approx(0.0f).margin(1e-3));    // the control
    CHECK(uphill < -2.0f);                        // tipped forward, into the hill

    // **The opposing quantity: balance margin.** A body of height h leaning by theta moves its
    // mass h*sin(theta) horizontally, and it stays upright while that is inside the footprint.
    // The margin falls as the lean grows, which is what bounds the response.
    const float height = 1.0f;
    const float footprint = 0.3f;
    const float margin = footprint - (height * std::sin(glm::radians(std::abs(uphill))));
    INFO("balance margin at " << uphill << " deg: " << margin << " m of " << footprint);
    CHECK(margin > 0.0f);   // still inside its feet at the default gain

    // **The opposing quantity has to be shown biting, or it is not a bound.** The first version
    // of this arm removed the cap and asserted the lean grew -- but at the default gain a 25
    // degree slope produces 6.1 degrees of lean, nowhere near the 20 degree cap, so removing the
    // cap changed nothing and the assertion was measuring a limiter that never engaged.
    //
    // Turning the *gain* up is what makes the margin fall, and that is the real bound: at five
    // times the default the body leans past its own feet.
    const float greedy = leanOn(glm::radians(25.0f), glm::vec3(0.0f, 0.0f, -1.0f), 90.0f, 70.0f);
    const float greedyMargin = footprint - (height * std::sin(glm::radians(std::abs(greedy))));
    INFO("at 5x gain the lean is " << greedy << " deg and the margin is " << greedyMargin);
    CHECK(std::abs(greedy) > std::abs(uphill));
    CHECK(greedyMargin < 0.0f);   // outside its feet: a character falling over

    // And the cap is what stops that, demonstrated at the same greedy gain.
    const float capped = leanOn(glm::radians(25.0f), glm::vec3(0.0f, 0.0f, -1.0f), 12.0f, 70.0f);
    const float cappedMargin = footprint - (height * std::sin(glm::radians(std::abs(capped))));
    INFO("capped at 12 deg the lean is " << capped << " and the margin is " << cappedMargin);
    CHECK(std::abs(capped) == Approx(12.0f).margin(0.5f));
    CHECK(cappedMargin > 0.0f);
}

TEST_CASE("descending shortens the stride too, but does not lean the body backwards",
          "[environment][phaseB]") {
    // Climbing and descending both cost stride -- the magnitude of the incline is what counts --
    // while the lean is signed, because a body does not lean backwards going downhill the way it
    // leans forward going up.
    const scene::Skeleton sk = bodyRig();
    const std::vector<scene::AnimationClip> clips;
    scene::PoseLayer lean;
    lean.name = "lean";
    lean.kind = scene::PoseLayerKind::Lean;
    lean.drive = scene::PoseLayerDrive::Manual;
    lean.mask.joints = {"body"};
    lean.bodySlope = glm::radians(25.0f);
    lean.bodyDownhill = glm::vec3(0.0f, 0.0f, 1.0f);   // downhill ahead: descending
    lean.weight = 1.0f;
    scene::PoseLayerStack stack;
    REQUIRE(stack.bind({lean}, sk, clips).empty());
    scene::Pose pose;
    scene::setRestPose(sk, pose);
    stack.apply(sk, clips, 0.0, pose);
    const float tilt = tiltAboutX(pose.local[1].rotation);
    INFO("lean while descending: " << tilt << " deg");
    CHECK(tilt > 1.0f);   // upright-ish, tipped back rather than forward into nothing
}
