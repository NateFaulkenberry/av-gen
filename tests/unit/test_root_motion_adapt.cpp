// Procedural root motion (Phase B §37).
//
// §37 is explicit that this is not blind X/Z scaling, and each of the four constraints it names
// gets an assertion: orientation (forward and lateral scale separately), turning (yaw untouched),
// foot contacts (the scale is clamped and the clamp is reported), terrain (Y is never scaled).
//
// **The free control.** Root displacement per stride against distance actually travelled is
// `Gait::footSlip` from the other end -- one measures how far the body goes for a stride's worth
// of animation, the other how much animation is played per metre travelled. They are reciprocals,
// so their product must be 1, and a pair of measurements that must agree costs nothing to check.

#include "entity/gait.hpp"
#include "entity/root_motion_adapt.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace avgen;
using Catch::Approx;

namespace {
constexpr float kDt = 1.0f / 60.0f;
}

TEST_CASE("a clip's displacement is scaled to the pace that was asked for", "[rootmotion][adapt]") {
    entity::RootMotionAdaptSettings s;
    // The clip covers 1.6 m/s worth of ground this step; the controller wants 0.8 m/s.
    const glm::vec3 authored(0.0f, 0.0f, 1.6f * kDt);
    const glm::vec3 desired(0.0f, 0.0f, 0.8f);
    const entity::RootMotionAdaptResult r = entity::adaptRootMotion(s, authored, desired, kDt);
    INFO("forward scale " << r.forwardScale << ", displacement " << r.displacement.z);
    CHECK(r.forwardScale == Approx(0.5f).margin(1e-3));
    CHECK(r.displacement.z == Approx(0.8f * kDt).margin(1e-5));
    CHECK_FALSE(r.clamped);
}

TEST_CASE("forward and lateral scale separately, because a strafe is not a slow walk",
          "[rootmotion][adapt]") {
    // §37's orientation constraint. A body asked to move forward at half pace and sideways at full
    // pace is strafing; one scale for both would be a different motion entirely.
    entity::RootMotionAdaptSettings s;
    const glm::vec3 authored(1.0f * kDt, 0.0f, 1.0f * kDt);
    const glm::vec3 desired(1.0f, 0.0f, 0.5f);
    const entity::RootMotionAdaptResult r = entity::adaptRootMotion(s, authored, desired, kDt);
    INFO("forward " << r.forwardScale << " lateral " << r.lateralScale);
    CHECK(r.forwardScale == Approx(0.5f).margin(1e-3));
    CHECK(r.lateralScale == Approx(1.0f).margin(1e-3));

    // The control: with the separation off, one scale covers both and neither component is right.
    entity::RootMotionAdaptSettings joined = s;
    joined.separateLateral = false;
    const entity::RootMotionAdaptResult j = entity::adaptRootMotion(joined, authored, desired, kDt);
    CHECK(j.forwardScale == j.lateralScale);
    CHECK(j.forwardScale != Approx(0.5f).margin(1e-3));
}

TEST_CASE("vertical displacement is never scaled", "[rootmotion][adapt]") {
    // §37's terrain constraint. A clip that steps down a kerb steps down the same kerb at any
    // pace; scaling Y would sink the body into a slope or float it above one.
    entity::RootMotionAdaptSettings s;
    const glm::vec3 authored(0.0f, -0.12f, 1.6f * kDt);
    const glm::vec3 desired(0.0f, 0.0f, 0.4f);
    const entity::RootMotionAdaptResult r = entity::adaptRootMotion(s, authored, desired, kDt);
    INFO("forward scaled to " << r.forwardScale << ", y " << r.displacement.y);
    CHECK(r.forwardScale < 0.5f);          // the horizontal really was scaled hard
    CHECK(r.displacement.y == Approx(-0.12f).margin(1e-6));  // ...and the vertical was not
}

TEST_CASE("the scale is clamped and the clamp is reported", "[rootmotion][adapt]") {
    // §37's foot-contact constraint. A root advancing at a tenth of the feet is a body
    // moonwalking, so the scale has a floor -- and past it the honest answer is a different clip.
    //
    // **Reported, not hidden**, because a limiter that saturates silently is exactly how
    // `Gait::playbackRate` sat at its floor across the whole shipping cast until B.A measured it.
    entity::RootMotionAdaptSettings s;
    const glm::vec3 authored(0.0f, 0.0f, 3.07f * kDt);   // the alien's authored walk
    const glm::vec3 desired(0.0f, 0.0f, 0.1f);           // ...and what it actually travels at
    const entity::RootMotionAdaptResult r = entity::adaptRootMotion(s, authored, desired, kDt);
    INFO("scale " << r.forwardScale << " (raw would be " << (0.1f / 3.07f) << ")");
    CHECK(r.clamped);
    CHECK(r.forwardScale == Approx(s.minScale).margin(1e-4));

    // The unclamped control, so `clamped` is a measurement rather than the only answer (ADR-182).
    const entity::RootMotionAdaptResult ok =
        entity::adaptRootMotion(s, authored, glm::vec3(0.0f, 0.0f, 2.0f), kDt);
    CHECK_FALSE(ok.clamped);
}

TEST_CASE("a clip with no displacement cannot be scaled into having some", "[rootmotion][adapt]") {
    // ADR-540: every locomotion clip in this repository is authored in place, so this is not a
    // corner case -- it is the common one. Multiplying nothing gives nothing however large the
    // factor, and a naive `want / have` produces an infinity here.
    entity::RootMotionAdaptSettings s;
    const glm::vec3 authored(0.0f, 0.0f, 0.0f);
    const entity::RootMotionAdaptResult r =
        entity::adaptRootMotion(s, authored, glm::vec3(0.0f, 0.0f, 1.5f), kDt);
    CHECK(std::isfinite(r.forwardScale));
    CHECK(r.displacement.z == Approx(0.0f));
    CHECK(r.clamped);   // it says it could not do what was asked
}

TEST_CASE("a stopping body is left to the stop's own ramp", "[rootmotion][adapt]") {
    // §9 owns the stop. Driving the scale toward its floor here as well would fight that ramp and
    // the two would compose into something neither intended.
    entity::RootMotionAdaptSettings s;
    const glm::vec3 authored(0.0f, 0.0f, 1.6f * kDt);
    const entity::RootMotionAdaptResult r =
        entity::adaptRootMotion(s, authored, glm::vec3(0.0f, 0.0f, 0.02f), kDt);
    CHECK(r.forwardScale == Approx(1.0f));
    CHECK(r.displacement.z == Approx(authored.z));
}

TEST_CASE("adaptation and footSlip are the same quantity from opposite ends",
          "[rootmotion][adapt][control]") {
    // **The free control.** `Gait::footSlip` reports how far the body travels per stride's worth
    // of animation; this reports how much animation is played per metre of travel. They are
    // reciprocals, so their product is 1 -- and if they ever disagree, one of them is wrong.
    //
    // Checked across the range rather than at a point, because two functions can agree at one
    // value by coincidence.
    entity::GaitSettings gait;
    gait.walkSpeed = 1.6f;
    gait.matchRate = false;   // rate matching off, so footSlip is the raw stride mismatch
    entity::RootMotionAdaptSettings adapt;
    adapt.minScale = 0.0f;    // unclamped, so the identity is tested and not the limiter
    adapt.maxScale = 100.0f;

    for (const float speed : {0.5f, 0.8f, 1.2f, 1.6f, 2.4f, 3.2f}) {
        const float slip = entity::Gait::footSlip(gait, entity::Activity::Walk, speed);
        const glm::vec3 authored(0.0f, 0.0f, gait.walkSpeed * kDt);
        const entity::RootMotionAdaptResult r =
            entity::adaptRootMotion(adapt, authored, glm::vec3(0.0f, 0.0f, speed), kDt);
        INFO("speed " << speed << ": footSlip " << slip << ", root scale " << r.forwardScale
                      << ", product " << (slip * (1.0f / r.forwardScale)));
        // footSlip is speed/authored; the root scale is the same ratio. One is how far the body
        // outruns the stride, the other how much the stride is shortened to match it.
        CHECK(r.forwardScale == Approx(slip).margin(1e-3));
        CHECK(slip * (1.0f / r.forwardScale) == Approx(1.0f).margin(1e-3));
    }
}
