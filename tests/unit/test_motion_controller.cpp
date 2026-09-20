// The motion controller (Phase B §33-§35).
//
// The interesting assertions here are the ones that separate **turning** from **accelerating**,
// because collapsing the two is the obvious implementation and it is wrong in a way that only
// shows up at the extremes: a fast body turns slowly and a slow body turns instantly, which is
// backwards from how bodies work.

#include "entity/motion_controller.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace avgen;
using Catch::Approx;

namespace {

constexpr float kPi = 3.14159265358979323846f;

entity::MotionRequest toward(const glm::vec3& velocity) {
    entity::MotionRequest r;
    r.desiredVelocity = velocity;
    r.desiredFacing = glm::length(velocity) > 1e-5f ? glm::normalize(velocity) : glm::vec3(0, 0, 1);
    return r;
}

float headingOf(const glm::vec3& v) { return std::atan2(v.x, v.z); }

// Run `seconds` of steps at 60 Hz and return the final state.
entity::MotionState run(const entity::MotionRequest& req, entity::MotionState state,
                        const entity::MotionLimits& limits, float seconds) {
    const float dt = 1.0f / 60.0f;
    const int steps = static_cast<int>((seconds / dt) + 0.5f);
    for (int i = 0; i < steps; ++i) {
        entity::MotionState next;
        (void)entity::stepMotion(req, state, limits, dt, next);
        state = next;
    }
    return state;
}

} // namespace

TEST_CASE("a body does not reach its desired velocity in one frame", "[motion][controller]") {
    // §34's rule, and the reason it exists: instant velocity is what makes a character read as a
    // sprite rather than a body.
    entity::MotionLimits limits;
    entity::MotionState state;
    entity::MotionState next;
    const entity::MotionRequest req = toward(glm::vec3(0.0f, 0.0f, 5.0f));

    const entity::MotionSolution s = entity::stepMotion(req, state, limits, 1.0f / 60.0f, next);
    // 6 m/s^2 for 1/60 s is 0.1 m/s, not 5.
    CHECK(s.speed == Approx(6.0f / 60.0f).margin(1e-4));
    CHECK(s.accelerationLimited);
    // And it says so, rather than clamping silently.
    CHECK(next.velocity.z == Approx(0.1f).margin(1e-4));
}

TEST_CASE("braking and accelerating have different limits", "[motion][controller]") {
    // Stopping is not the reverse of starting. With accel 6 and decel 8, reaching 4 m/s takes
    // longer than losing it.
    entity::MotionLimits limits;
    limits.maxAcceleration = 6.0f;
    limits.maxDeceleration = 8.0f;

    entity::MotionState rest;
    const entity::MotionState sped = run(toward(glm::vec3(0.0f, 0.0f, 4.0f)), rest, limits, 4.0f / 6.0f);
    INFO("after accelerating: " << sped.velocity.z);
    CHECK(sped.velocity.z == Approx(4.0f).margin(0.1f));

    // From 4 m/s, braking to a stop should take 4/8 = 0.5 s. Check at 0.4 s it is NOT yet stopped
    // and at 0.6 s it is -- the pair, because only one of them distinguishes 8 from 6.
    const entity::MotionState partway = run(toward(glm::vec3(0.0f)), sped, limits, 0.4f);
    const entity::MotionState stopped = run(toward(glm::vec3(0.0f)), sped, limits, 0.6f);
    INFO("at 0.4 s " << partway.velocity.z << ", at 0.6 s " << stopped.velocity.z);
    CHECK(partway.velocity.z > 0.3f);
    CHECK(stopped.velocity.z == Approx(0.0f).margin(1e-3));

    // **The discriminating assertion.** If deceleration used the acceleration limit, 4 m/s would
    // take 4/6 = 0.667 s to shed and there would still be ~0.4 m/s left at 0.6 s.
    CHECK(stopped.velocity.z < 0.05f);
}

TEST_CASE("turning is limited by a turn rate, not by an acceleration", "[motion][controller]") {
    // **The assertion that catches the obvious wrong implementation.** Two bodies, one at 1 m/s and
    // one at 8 m/s, both asked to reverse direction. A controller that lerped the velocity vector
    // at `maxAcceleration` would turn the slow one almost instantly and the fast one barely at all.
    // A turn rate turns them at the same angular speed.
    entity::MotionLimits limits;
    limits.maxTurnRate = 2.0f; // rad/s
    const float dt = 1.0f / 60.0f;

    const auto turnedAfterOneStep = [&](float speed) {
        entity::MotionState state;
        state.velocity = glm::vec3(0.0f, 0.0f, speed);
        state.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        state.started = true;
        entity::MotionState next;
        // Ask for the same speed, ninety degrees to the left.
        (void)entity::stepMotion(toward(glm::vec3(speed, 0.0f, 0.0f)), state, limits, dt, next);
        return std::abs(headingOf(next.velocity) - headingOf(state.velocity));
    };

    const float slow = turnedAfterOneStep(1.0f);
    const float fast = turnedAfterOneStep(8.0f);
    INFO("slow body turned " << slow << " rad, fast body turned " << fast << " rad");
    // Both turned by the rate limit, and by the same amount.
    CHECK(slow == Approx(limits.maxTurnRate * dt).margin(1e-4));
    CHECK(fast == Approx(limits.maxTurnRate * dt).margin(1e-4));
    CHECK(slow == Approx(fast).margin(1e-5));
}

TEST_CASE("a standing body may set off in any direction without turning first",
          "[motion][controller]") {
    // The `headingFloor` case. A body at rest has no heading, and making it rotate a velocity
    // vector it does not have would give a visible, wrong quarter-second of drift before it set off.
    entity::MotionLimits limits;
    entity::MotionState rest; // velocity zero, facing +Z
    entity::MotionState next;
    // Ask for motion directly backwards, which is the worst case for a turn limit.
    const entity::MotionSolution s =
        entity::stepMotion(toward(glm::vec3(0.0f, 0.0f, -3.0f)), rest, limits, 1.0f / 60.0f, next);

    INFO("velocity " << next.velocity.x << "," << next.velocity.z);
    CHECK_FALSE(s.turnLimited);
    CHECK(next.velocity.z < 0.0f);       // it went backwards immediately
    CHECK(std::abs(next.velocity.x) < 1e-4f);
    // Its speed is still acceleration-limited, which is the half that does apply from rest.
    CHECK(s.speed == Approx(limits.maxAcceleration / 60.0f).margin(1e-4));
}

TEST_CASE("facing and heading are separate, so a body can circle what it watches",
          "[motion][controller]") {
    // ADR-545's distinction, in the controller. The body travels +X and faces +Z; neither answer
    // may be derived from the other.
    entity::MotionLimits limits;
    entity::MotionState state;
    state.velocity = glm::vec3(2.0f, 0.0f, 0.0f);
    state.facing = glm::vec3(0.0f, 0.0f, 1.0f);
    state.started = true;

    entity::MotionRequest req;
    req.desiredVelocity = glm::vec3(2.0f, 0.0f, 0.0f); // keep going +X
    req.desiredFacing = glm::vec3(0.0f, 0.0f, 1.0f);   // keep looking +Z
    const entity::MotionState after = run(req, state, limits, 0.5f);

    INFO("velocity " << after.velocity.x << "," << after.velocity.z << " facing " << after.facing.x
                     << "," << after.facing.z);
    CHECK(after.velocity.x == Approx(2.0f).margin(1e-3));
    CHECK(std::abs(after.velocity.z) < 1e-3f);
    CHECK(after.facing.z == Approx(1.0f).margin(1e-3));
    CHECK(std::abs(after.facing.x) < 1e-3f);
    // A controller that made facing follow heading would have turned the body to +X by now, so
    // this assertion is the one that can fail.
    CHECK(std::abs(headingOf(after.velocity) - headingOf(after.facing)) > 1.0f);
}

TEST_CASE("steering is added to the desired velocity, not blended with it", "[motion][controller]") {
    // §39. Phase D supplies the avoidance correction; Phase B executes it. The sum is what the body
    // aims for, so a steering vector that exactly cancels the request stops the body.
    entity::MotionLimits limits;
    entity::MotionState state;
    state.velocity = glm::vec3(0.0f, 0.0f, 3.0f);
    state.started = true;

    entity::MotionRequest req = toward(glm::vec3(0.0f, 0.0f, 3.0f));
    req.steering = glm::vec3(0.0f, 0.0f, -3.0f);
    const entity::MotionState after = run(req, state, limits, 1.0f);
    INFO("velocity " << after.velocity.z);
    CHECK(after.velocity.z == Approx(0.0f).margin(1e-3));
}

TEST_CASE("the first tick of a render changes nothing and divides by nothing",
          "[motion][controller][determinism]") {
    // ADR-521 again: `FixedStepClock::tick()` hands out dt 0 on the first tick of every render.
    entity::MotionLimits limits;
    entity::MotionState state;
    state.velocity = glm::vec3(1.0f, 0.0f, 2.0f);
    state.started = true;
    entity::MotionState next;
    const entity::MotionSolution s =
        entity::stepMotion(toward(glm::vec3(0.0f, 0.0f, 9.0f)), state, limits, 0.0f, next);

    CHECK(std::isfinite(s.acceleration.x));
    CHECK(std::isfinite(s.acceleration.z));
    CHECK(s.acceleration == glm::vec3(0.0f));
    CHECK(next.velocity == state.velocity);
}

TEST_CASE("the controller keeps nothing: the same state gives the same step",
          "[motion][controller][determinism]") {
    // The property `EntityWorld::seek` depends on, same as the provider's.
    entity::MotionLimits limits;
    entity::MotionState state;
    state.velocity = glm::vec3(0.4f, 0.0f, 1.9f);
    state.facing = glm::normalize(glm::vec3(0.2f, 0.0f, 1.0f));
    state.started = true;
    const entity::MotionRequest req = toward(glm::vec3(2.5f, 0.0f, 0.5f));

    entity::MotionState a;
    entity::MotionState b;
    const entity::MotionSolution sa = entity::stepMotion(req, state, limits, 1.0f / 60.0f, a);
    // Interleave unrelated work with different inputs, to catch anything cached at namespace scope.
    entity::MotionState junk;
    (void)entity::stepMotion(toward(glm::vec3(-9.0f, 0.0f, 0.0f)), a, limits, 0.5f, junk);
    const entity::MotionSolution sb = entity::stepMotion(req, state, limits, 1.0f / 60.0f, b);

    CHECK(sa.speed == Approx(sb.speed).margin(1e-7));
    CHECK(a.velocity.x == Approx(b.velocity.x).margin(1e-7));
    CHECK(a.velocity.z == Approx(b.velocity.z).margin(1e-7));
    CHECK(a.facing.x == Approx(b.facing.x).margin(1e-7));
    // The interleaved call really did produce something different, so this is not two identical
    // answers from a function that ignores its arguments (ADR-182).
    CHECK(junk.velocity.x != Approx(a.velocity.x).margin(1e-5));
}

TEST_CASE("acceleration is measured once and reported", "[motion][controller]") {
    // §35: do not let lean, stride and balance each derive it independently.
    entity::MotionLimits limits;
    entity::MotionState state;
    state.started = true;
    entity::MotionState next;
    const float dt = 1.0f / 60.0f;
    const entity::MotionSolution s =
        entity::stepMotion(toward(glm::vec3(0.0f, 0.0f, 5.0f)), state, limits, dt, next);

    // It accelerated at exactly the limit, so the reported acceleration is that limit.
    CHECK(s.acceleration.z == Approx(limits.maxAcceleration).margin(1e-3));
    CHECK(next.previousVelocity == state.velocity);
    // And the reported value agrees with differencing the state, which is what a subsystem that
    // re-derived it would compute.
    const glm::vec3 derived = (next.velocity - next.previousVelocity) / dt;
    CHECK(s.acceleration.z == Approx(derived.z).margin(1e-4));
}
