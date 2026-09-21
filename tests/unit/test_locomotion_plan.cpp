// The locomotion plan (Phase B §8-§11): start, stop, turn-in-place and strafe.
//
// The assertions that matter are the ones about **not flickering** and about the **braking test**,
// because a phase machine that passes "it reached Moving" is easy and a phase machine that does not
// oscillate on a threshold is the whole engineering problem. `GaitSettings` paid for that lesson
// one tier up; these tests are what stop it being paid again.

#include "entity/locomotion_plan.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace avgen;
using Catch::Approx;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// A body driven for `seconds`, reporting the phase it ends in and how many times it changed.
struct Drive {
    entity::LocomotionPlanState state;
    entity::LocomotionPlan plan;
    double time = 0.0;
    int changes = 0;

    void step(const entity::LocomotionPlanSettings& s, float speed, float desired,
              float turnRate = 0.0f, float strafe = 0.0f,
              entity::Activity gait = entity::Activity::Walk) {
        entity::LocomotionPlanState next;
        plan = entity::planLocomotion(s, state, gait, speed, desired, turnRate, strafe, time, next);
        if (plan.changed && state.started) {
            ++changes;
        }
        state = next;
        time += static_cast<double>(kDt);
    }
    void run(const entity::LocomotionPlanSettings& s, double seconds, float speed, float desired,
             float turnRate = 0.0f, float strafe = 0.0f,
             entity::Activity gait = entity::Activity::Walk) {
        const int steps = static_cast<int>((seconds / kDt) + 0.5);
        for (int i = 0; i < steps; ++i) {
            step(s, speed, desired, turnRate, strafe, gait);
        }
    }
};

} // namespace

TEST_CASE("a standing body asked to walk starts before it moves", "[locomotion][plan][start]") {
    // §8: no idle -> instant full-speed locomotion. The `Starting` phase has to exist and has to
    // be passed through, not skipped.
    entity::LocomotionPlanSettings s;
    Drive d;
    d.run(s, 0.2, 0.0f, 0.0f, 0.0f, 0.0f, entity::Activity::Idle); // settle as idle
    REQUIRE(d.plan.phase == entity::LocomotionPhase::Idle);

    d.step(s, 0.05f, 1.6f); // asked to walk, barely moving yet
    CHECK(d.plan.phase == entity::LocomotionPhase::Starting);
    // The stride ramps in from a short first step rather than opening at the authored length.
    INFO("stride at the first frame of the start: " << d.plan.strideScale);
    CHECK(d.plan.strideScale < 0.45f);

    // Reaching the asked-for pace ends the start.
    d.run(s, 0.3, 1.5f, 1.6f);
    CHECK(d.plan.phase == entity::LocomotionPhase::Moving);
    CHECK(d.plan.strideScale == Approx(1.0f).margin(1e-4));
}

TEST_CASE("a start that never reaches its pace still ends", "[locomotion][plan][start]") {
    // The second exit, and it is the one a body on a slope or in a tractor beam needs: asked for
    // 4 m/s and never getting above 0.3, the phase must still resolve. Without this a character
    // would ramp its stride forever and never look like it was walking.
    entity::LocomotionPlanSettings s;
    Drive d;
    d.run(s, 0.2, 0.0f, 0.0f, 0.0f, 0.0f, entity::Activity::Idle);
    d.run(s, 1.0, 0.3f, 4.0f); // asked for four, giving three tenths
    INFO("phase after a second of failing to accelerate: " << entity::locomotionPhaseName(d.plan.phase));
    CHECK(d.plan.phase == entity::LocomotionPhase::Moving);
}

TEST_CASE("a stop is a braking test, not a slow test", "[locomotion][plan][stop]") {
    // **The discriminating case for this repository's content.** 97 of 100 of the shipping cast
    // travel below a quarter of their authored stride (B.A). A "is it slow" stop test would put
    // every one of them into a permanent `Stopping`. The test is whether the body is being asked
    // to SHED speed, and these two arms differ only in that.
    entity::LocomotionPlanSettings s;

    Drive slow;
    slow.run(s, 0.2, 0.0f, 0.0f, 0.0f, 0.0f, entity::Activity::Idle);
    slow.run(s, 1.0, 0.25f, 0.25f); // always crept, and is still being asked to creep
    INFO("a permanently slow body is " << entity::locomotionPhaseName(slow.plan.phase));
    CHECK(slow.plan.phase == entity::LocomotionPhase::Moving);

    Drive braking;
    braking.run(s, 0.2, 0.0f, 0.0f, 0.0f, 0.0f, entity::Activity::Idle);
    braking.run(s, 0.6, 2.0f, 2.0f);   // travelling
    braking.step(s, 2.0f, 0.1f);       // and suddenly asked to stop
    INFO("a braking body is " << entity::locomotionPhaseName(braking.plan.phase));
    CHECK(braking.plan.phase == entity::LocomotionPhase::Stopping);
}

TEST_CASE("a stop eases the stride out and lands in idle", "[locomotion][plan][stop]") {
    // §9: not a fade into idle. The stride shortens toward a final step and the phase resolves.
    entity::LocomotionPlanSettings s;
    Drive d;
    d.run(s, 0.2, 0.0f, 0.0f, 0.0f, 0.0f, entity::Activity::Idle);
    d.run(s, 0.6, 2.0f, 2.0f);
    d.step(s, 2.0f, 0.0f);
    REQUIRE(d.plan.phase == entity::LocomotionPhase::Stopping);
    const float atStart = d.plan.strideScale;

    d.run(s, 0.3, 1.0f, 0.0f);
    INFO("stride " << atStart << " -> " << d.plan.strideScale);
    CHECK(d.plan.strideScale < atStart);
    // **Never zero.** §9 asks for a final step, and a stride scale of zero is both feet in one
    // place, which is a fade by another name.
    CHECK(d.plan.strideScale > 0.3f);

    d.run(s, 0.6, 0.02f, 0.0f);
    CHECK(d.plan.phase == entity::LocomotionPhase::Idle);
}

TEST_CASE("a body on a threshold does not flicker", "[locomotion][plan][hysteresis]") {
    // The engineering problem. A body whose strafe angle sits exactly on the entry threshold, or
    // whose speed sits on the stop trigger, must not change phase every frame. Two mechanisms are
    // needed and the test uses both: an enter/exit band and a minimum dwell.
    entity::LocomotionPlanSettings s;
    Drive d;
    d.run(s, 0.2, 0.0f, 0.0f, 0.0f, 0.0f, entity::Activity::Idle);
    d.run(s, 0.8, 1.6f, 1.6f);
    REQUIRE(d.plan.phase == entity::LocomotionPhase::Moving);
    const int before = d.changes;

    // Three seconds sitting exactly on the strafe threshold, wobbling by a degree either side.
    for (int i = 0; i < 180; ++i) {
        const float wobble = (i % 2 == 0) ? 1.0f : -1.0f;
        d.step(s, 1.6f, 1.6f, 0.0f, glm::radians(s.strafeEnterDegrees + wobble));
    }
    INFO("phase changes while sitting on the threshold: " << (d.changes - before));
    // A single-threshold machine would change 180 times. The band plus the dwell allow the one
    // legitimate crossing and nothing after it.
    CHECK(d.changes - before <= 2);
}

TEST_CASE("a strafe is a strafe and a walk is a walk", "[locomotion][plan][strafe]") {
    // §11: do not assume velocity == facing. The pair is the assertion -- a body at 10 degrees is
    // walking with a lean, and one at 80 is moving sideways.
    entity::LocomotionPlanSettings s;

    Drive gentle;
    gentle.run(s, 0.2, 0.0f, 0.0f, 0.0f, 0.0f, entity::Activity::Idle);
    gentle.run(s, 1.0, 1.6f, 1.6f, 0.0f, glm::radians(10.0f));
    CHECK(gentle.plan.phase == entity::LocomotionPhase::Moving);

    Drive sideways;
    sideways.run(s, 0.2, 0.0f, 0.0f, 0.0f, 0.0f, entity::Activity::Idle);
    sideways.run(s, 1.0, 1.6f, 1.6f, 0.0f, glm::radians(80.0f));
    INFO("at 80 degrees the body is " << entity::locomotionPhaseName(sideways.plan.phase));
    CHECK(sideways.plan.phase == entity::LocomotionPhase::Strafing);
    // And the angle is carried rather than re-derived downstream.
    CHECK(sideways.plan.strafeAngle == Approx(glm::radians(80.0f)).margin(1e-4));
}

TEST_CASE("a standing body that turns is turning, not starting", "[locomotion][plan][turn]") {
    // §10: turning in place is a distinct state from travelling. A machine that called it
    // `Starting` would ramp a stride for a body going nowhere.
    entity::LocomotionPlanSettings s;
    Drive d;
    d.run(s, 0.2, 0.0f, 0.0f, 0.0f, 0.0f, entity::Activity::Idle);
    d.run(s, 0.5, 0.02f, 0.0f, 1.2f, 0.0f, entity::Activity::Turn);
    INFO("phase while turning on the spot: " << entity::locomotionPhaseName(d.plan.phase));
    CHECK(d.plan.phase == entity::LocomotionPhase::Turning);
    CHECK(d.plan.strideScale == Approx(1.0f).margin(1e-4));

    // And it comes out of the turn when the turn stops, rather than latching.
    d.run(s, 0.5, 0.02f, 0.0f, 0.05f, 0.0f, entity::Activity::Idle);
    CHECK(d.plan.phase == entity::LocomotionPhase::Idle);
}

TEST_CASE("an airborne body holds the phase it left", "[locomotion][plan]") {
    // The rule `Gait::select` already follows for the gait underneath a jump (ADR-194): a body
    // that jumps mid-stride comes back to the stride it left rather than to an idle.
    entity::LocomotionPlanSettings s;
    Drive d;
    d.run(s, 0.2, 0.0f, 0.0f, 0.0f, 0.0f, entity::Activity::Idle);
    d.run(s, 0.8, 2.0f, 2.0f);
    REQUIRE(d.plan.phase == entity::LocomotionPhase::Moving);

    d.run(s, 0.8, 2.0f, 0.0f, 0.0f, 0.0f, entity::Activity::Jump);
    INFO("phase while airborne: " << entity::locomotionPhaseName(d.plan.phase));
    CHECK(d.plan.phase == entity::LocomotionPhase::Moving); // held, not reset
}

TEST_CASE("the plan keeps nothing, so a replay reproduces a play", "[locomotion][plan][determinism]") {
    // ADR-554's contract. `entered` is a time rather than an accumulator, which is what makes a
    // fixed-step replay land on the same instant the play did.
    entity::LocomotionPlanSettings s;
    const auto run = [&](int steps) {
        Drive d;
        for (int i = 0; i < steps; ++i) {
            const float speed = i < 30 ? 0.0f : 1.8f;
            d.step(s, speed, i < 30 ? 0.0f : 1.8f);
        }
        return d;
    };
    const Drive a = run(90);
    const Drive b = run(90);
    CHECK(a.plan.phase == b.plan.phase);
    CHECK(a.state.entered == Approx(b.state.entered));
    CHECK(a.plan.strideScale == Approx(b.plan.strideScale).margin(1e-7));
    CHECK(a.changes == b.changes);
    // And it really moved through phases, so this is not two identical idles.
    CHECK(a.changes > 0);
}
