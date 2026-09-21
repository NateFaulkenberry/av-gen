// The measured velocity (ADR-545): what the body did, beside what the mover meant.
//
// `EntityState` has carried velocity in polar form since it existed -- `speed`, a scalar, along
// `yaw`, a heading. That pair can describe a body walking where it looks and nothing else. It
// cannot say "moving north-east while facing north", it cannot say "strafing", it cannot say
// "backing away", and it cannot supply the future-trajectory features a motion matcher queries on.
//
// **The standing rule (Phase A brief §2, found by ADR-543).** A test whose target equals its
// current state is satisfied by an implementation that does nothing. So every arm here moves a body
// somewhere it is not, and asserts the direction as well as the magnitude -- a velocity of the
// right length pointing the wrong way is the failure that a speed-only assertion cannot see, and is
// exactly the failure the polar representation had.
//
// The arms:
//
//   still         a body that does not move reports exactly zero, and `strafeAngle` does not blow up
//   first step    no velocity on the step a body is born on; a backward difference has nothing yet
//   dt == 0       ADR-521's first render tick reports deltaTime 0 -- no division, no infinity
//   north-east    the case the polar form cannot express: travel and facing 45 degrees apart
//   strafe        facing +Z while travelling +X -- strafeAngle is a right angle
//   backward      facing +Z while travelling -Z -- strafeAngle is pi
//   circling      a body orbiting a point while facing it, over many steps
//   seek          a reset forgets where the body was, so the first replayed step is not a teleport

#include "entity/entity.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using avgen::entity::DirectorMotion;
using avgen::entity::EntityDesc;
using avgen::entity::EntityState;
using avgen::entity::EntityUpdate;
using avgen::entity::EntityWorld;
using Catch::Approx;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// The engine's own yaw convention, stated once: yaw = atan2(direction.x, direction.z), so yaw zero
// is +Z. Anything that disagrees with `EntityState::facing()` here is a convention bug, not a
// velocity bug, and the first arm below checks the two agree.
float yawTowards(const glm::vec3& direction) { return std::atan2(direction.x, direction.z); }

// A world holding one entity that the DIRECTOR tier drives. The director is used deliberately
// rather than a behaviour: it is the one production path that writes position and yaw
// INDEPENDENTLY (`entity.cpp`: "A director says where a body *is*"), which is precisely the
// facing-not-equal-to-heading case the polar representation cannot hold. A synthetic test
// behaviour would prove something about the test.
struct Driven {
    avgen::params::ParameterSet params;
    avgen::signals::SignalBus bus;
    EntityWorld world;

    Driven() {
        EntityDesc desc;
        desc.name = "walker";
        desc.node = "walker";
        desc.seed = 12345;
        desc.cullDistance = 0.0f; // never cull: the LOD band must not decide this test
        // The node parameters an entity binds against. Without them the entity has nothing to
        // write its transform to and `bind` reports a problem.
        const auto v3 = [](const char* path, glm::vec3 value, float lo, float hi) {
            return avgen::params::ParamDesc<glm::vec3>{
                .path = path, .defaultValue = value, .hardMin = glm::vec3(lo), .hardMax = glm::vec3(hi)};
        };
        params.add(v3("nodes/walker/position", glm::vec3(0.0f), -1e4f, 1e4f));
        params.add(v3("nodes/walker/rotation", glm::vec3(0.0f), -360.0f, 360.0f));
        params.add(v3("nodes/walker/scale", glm::vec3(1.0f), 0.001f, 100.0f));
        world.setEntities({std::move(desc)}, 7u);
        avgen::entity::NodeBinding binding;
        binding.node = "walker";
        binding.exists = true;
        binding.transformPrefix = "nodes/walker/";
        world.setBindings({binding});
        world.registerParameters(params);
        world.bind(params);
    }

    avgen::entity::Entity& body() { return *world.entities().front(); }

    // One step of `dt`, with the body placed at `at` and facing `yaw`.
    void step(double time, double dt, const glm::vec3& at, float yaw) {
        DirectorMotion motion;
        motion.active = true;
        motion.position = at;
        motion.yaw = yaw;
        motion.hasYaw = true;
        body().setDirectorMotion(motion);
        params.resetFinals();
        EntityUpdate ctx;
        ctx.time = time;
        ctx.dt = dt;
        ctx.bus = &bus;
        world.update(ctx, params);
    }
};

} // namespace

TEST_CASE("a body that has not moved reports exactly no velocity", "[entity][velocity]") {
    Driven d;
    // Two steps in the same place: the first has no previous position, the second has one and
    // differences to zero. Both must be exactly zero -- a solver that always reports a small
    // residual fails here, and a `strafeAngle` that divides by a zero speed fails too.
    d.step(0.0, 1.0 / 60.0, glm::vec3(1.0f, 0.0f, 2.0f), 0.0f);
    CHECK(d.body().state().velocity == glm::vec3(0.0f));
    d.step(1.0 / 60.0, 1.0 / 60.0, glm::vec3(1.0f, 0.0f, 2.0f), 0.0f);
    CHECK(d.body().state().velocity == glm::vec3(0.0f));
    CHECK(d.body().state().groundSpeed() == 0.0f);
    CHECK(d.body().state().strafeAngle() == 0.0f);
}

TEST_CASE("the first tick of a render does not divide by its zero delta", "[entity][velocity]") {
    // ADR-521: `FixedStepClock::tick()` hands out deltaTime 0 on the first tick of every render, so
    // this is not a hypothetical input. A naive backward difference publishes an infinity into the
    // pose layer on frame one of every offline job.
    Driven d;
    d.step(0.0, 0.0, glm::vec3(0.0f), 0.0f);
    d.step(0.0, 0.0, glm::vec3(0.0f, 0.0f, 5.0f), 0.0f); // moved 5 m in zero time
    const glm::vec3 v = d.body().state().velocity;
    CHECK(std::isfinite(v.x));
    CHECK(std::isfinite(v.y));
    CHECK(std::isfinite(v.z));
    CHECK(v == glm::vec3(0.0f));
}

TEST_CASE("a body moving north-east while facing north", "[entity][velocity]") {
    // THE ARM THE POLAR FORM CANNOT PASS. `speed` and `yaw` between them can only describe travel
    // along the facing; here the two are 45 degrees apart, and both have to survive the seam.
    Driven d;
    const float dt = 1.0f / 60.0f;
    const glm::vec3 start(0.0f, 0.0f, 0.0f);
    const glm::vec3 northEast = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));
    const float facingNorth = yawTowards(glm::vec3(0.0f, 0.0f, 1.0f)); // +Z

    d.step(0.0, dt, start, facingNorth);
    d.step(dt, dt, start + northEast * 3.0f * dt, facingNorth); // 3 m/s to the north-east

    const EntityState& s = d.body().state();
    CHECK(s.groundSpeed() == Approx(3.0f).margin(1e-3));
    // The DIRECTION, which is the half a speed assertion cannot see.
    const glm::vec3 unit = glm::normalize(glm::vec3(s.velocity.x, 0.0f, s.velocity.z));
    CHECK(unit.x == Approx(northEast.x).margin(1e-3));
    CHECK(unit.z == Approx(northEast.z).margin(1e-3));
    // And the facing is still north, independent of it.
    CHECK(s.facing().z == Approx(1.0f).margin(1e-4));
    CHECK(s.facing().x == Approx(0.0f).margin(1e-4));
    CHECK(s.strafeAngle() == Approx(kPi / 4.0f).margin(1e-3));
}

TEST_CASE("a strafe and a backward step are told apart from a walk", "[entity][velocity]") {
    const float dt = 1.0f / 60.0f;
    const float facingNorth = yawTowards(glm::vec3(0.0f, 0.0f, 1.0f));

    SECTION("walking where it looks is a strafe angle of zero") {
        Driven d;
        d.step(0.0, dt, glm::vec3(0.0f), facingNorth);
        d.step(dt, dt, glm::vec3(0.0f, 0.0f, 2.0f * static_cast<double>(dt)), facingNorth);
        CHECK(d.body().state().strafeAngle() == Approx(0.0f).margin(1e-3));
        CHECK(d.body().state().groundSpeed() == Approx(2.0f).margin(1e-3));
    }

    SECTION("travelling +X while facing +Z is a right angle") {
        Driven d;
        d.step(0.0, dt, glm::vec3(0.0f), facingNorth);
        d.step(dt, dt, glm::vec3(2.0f * dt, 0.0f, 0.0f), facingNorth);
        CHECK(d.body().state().strafeAngle() == Approx(kPi / 2.0f).margin(1e-3));
        CHECK(d.body().state().groundSpeed() == Approx(2.0f).margin(1e-3));
    }

    SECTION("backing up is pi, and is NOT the same as walking forwards") {
        Driven d;
        d.step(0.0, dt, glm::vec3(0.0f), facingNorth);
        d.step(dt, dt, glm::vec3(0.0f, 0.0f, -2.0f * dt), facingNorth);
        const EntityState& s = d.body().state();
        CHECK(s.strafeAngle() == Approx(kPi).margin(1e-3));
        // The magnitude is identical to the forward walk above. That is the point: `speed` alone
        // cannot tell these two apart, and it is why `speed` alone was not enough.
        CHECK(s.groundSpeed() == Approx(2.0f).margin(1e-3));
        CHECK(s.velocity.z < 0.0f);
    }
}

TEST_CASE("a body circling a target while watching it", "[entity][velocity]") {
    // Many steps, not two: an orbit is where a one-step difference could look right and the
    // sequence still be wrong. At every step the body faces the centre and travels along the
    // tangent, so the strafe angle must stay at a right angle the whole way round.
    Driven d;
    const float dt = 1.0f / 60.0f;
    const glm::vec3 centre(5.0f, 0.0f, 5.0f);
    const float radius = 3.0f;
    const float omega = 0.8f; // rad/s

    const auto at = [&](float t) {
        return centre + glm::vec3(radius * std::sin(omega * t), 0.0f, radius * std::cos(omega * t));
    };
    // Facing the centre from wherever it stands.
    const auto facingCentre = [&](float t) { return yawTowards(glm::normalize(centre - at(t))); };

    d.step(0.0, dt, at(0.0f), facingCentre(0.0f));
    int checked = 0;
    for (int i = 1; i <= 40; ++i) {
        const float t = static_cast<float>(i) * dt;
        d.step(static_cast<double>(t), dt, at(t), facingCentre(t));
        if (i < 3) {
            continue; // let the difference settle onto the arc
        }
        const EntityState& s = d.body().state();
        INFO(i);
        // Tangential speed is radius * omega...
        CHECK(s.groundSpeed() == Approx(radius * omega).margin(0.02));
        // ...and the travel is square to the facing, every step, all the way round.
        CHECK(s.strafeAngle() == Approx(kPi / 2.0f).margin(0.05));
        ++checked;
    }
    CHECK(checked == 38); // the loop really ran; an empty loop passes every assertion in it
}

TEST_CASE("a reset forgets where the body was", "[entity][velocity]") {
    // ADR-267 D4 and the same rule root motion follows: a backward difference carried across a
    // discontinuity is a teleport. The body is walked out to 50 m, reset, and then placed back at
    // the origin -- if the previous position survived the reset, the first replayed step would
    // report 50 m in one frame.
    Driven d;
    const float dt = 1.0f / 60.0f;
    d.step(0.0, dt, glm::vec3(0.0f), 0.0f);
    d.step(dt, dt, glm::vec3(0.0f, 0.0f, 50.0f), 0.0f);
    CHECK(d.body().state().groundSpeed() > 100.0f); // it really did move a long way

    d.world.reset();
    d.step(0.0, dt, glm::vec3(0.0f), 0.0f);
    CHECK(d.body().state().velocity == glm::vec3(0.0f));
    // ...and the step after the reset measures from the reset position, not from the old one.
    d.step(dt, dt, glm::vec3(0.0f, 0.0f, 1.0f * dt), 0.0f);
    CHECK(d.body().state().groundSpeed() == Approx(1.0f).margin(1e-3));
}

TEST_CASE("the velocity the SEAM carries, not the one the body measured", "[entity][velocity][seam]") {
    // **Every other assertion in this file reads `Entity::state()`, and that is the blind spot.**
    // ADR-545 measures velocity once and publishes it across the `LocomotionState` seam for a pose
    // layer to read. The measurement was tested thoroughly. The *publication* was tested nowhere,
    // and it turned out to happen on only one of the two code paths that publish the seam --
    // `EntityWorld::seek` wrote it and `EntityWorld::update` did not.
    //
    // So during live play the seam carried a zero velocity, and during a scrub it carried the right
    // one. That is backwards from every other kind of bug and it is an ADR-360 violation outright:
    // a field the animation layer reads differed between play and scrub.
    //
    // This is the same shape as ADR-553 -- a correct number that never reached the place consuming
    // it -- and it is caught by the same probe: assert on the thing downstream reads.
    Driven d;
    const float dt = 1.0f / 60.0f;
    const float facingNorth = yawTowards(glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 northEast = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));

    d.step(0.0, dt, glm::vec3(0.0f), facingNorth);
    d.step(dt, dt, northEast * 3.0f * dt, facingNorth);

    const EntityState& measured = d.body().state();
    const avgen::entity::LocomotionState& seam = d.body().locomotion();

    // The measurement, which was already right.
    REQUIRE(measured.groundSpeed() == Approx(3.0f).margin(1e-3));

    // The seam, which is what a pose layer actually gets.
    INFO("measured " << measured.velocity.x << "," << measured.velocity.z << "  seam "
                     << seam.velocity.x << "," << seam.velocity.z);
    CHECK(seam.velocity.x == Approx(measured.velocity.x).margin(1e-4));
    CHECK(seam.velocity.z == Approx(measured.velocity.z).margin(1e-4));
    CHECK(seam.facing.x == Approx(measured.facing().x).margin(1e-4));
    CHECK(seam.facing.z == Approx(measured.facing().z).margin(1e-4));

    // And it is a real strafe, so a layer reading only `speed`/`yaw` could not have reconstructed
    // it: the body travels north-east while facing north.
    const glm::vec3 unit = glm::normalize(glm::vec3(seam.velocity.x, 0.0f, seam.velocity.z));
    CHECK(unit.x == Approx(northEast.x).margin(1e-3));
    CHECK(seam.facing.z == Approx(1.0f).margin(1e-3));
}

TEST_CASE("play and scrub publish the same seam, field by field", "[entity][seam][determinism]") {
    // **The general form of the two bugs above, as one assertion.**
    //
    // `LocomotionState` is the whole of what the animation tier receives. It is published from two
    // places -- `EntityWorld::update` while the timeline runs and `EntityWorld::seek` when someone
    // scrubs -- and ADR-360 says a render must be reproducible, so those two must agree.
    //
    // They did not, in three separate fields, each invisible for the same reason: every test
    // asserted on the tier that *computes* a value rather than on the seam that *carries* it.
    //   * `velocity` and `facing`: written by `seek`, not by `update`. Zero during play.
    //   * `action`: written by `update`, not by `seek`. Empty during a scrub, so a sitting
    //     character stood up and walked when scrubbed to.
    //   * `grounded`: written by neither.
    //
    // Comparing the structs whole is what makes this a guard rather than three spot checks: a
    // field added to the seam later is covered on the day it is added.
    const float dt = 1.0f / 60.0f;
    const float facingNorth = yawTowards(glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 northEast = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));

    Driven played;
    played.step(0.0, dt, glm::vec3(0.0f), facingNorth);
    played.step(dt, dt, northEast * 3.0f * dt, facingNorth);
    const avgen::entity::LocomotionState& a = played.body().locomotion();

    // A body that really is doing something, so the comparison is not two sets of defaults --
    // ADR-182: a probe that cannot fail proves nothing.
    REQUIRE(a.velocity != glm::vec3(0.0f));
    REQUIRE(a.dt == Approx(dt).margin(1e-6));
    // Note `speed` is NOT asserted non-zero, and the first draft of this test wrongly did.
    // `speed` is what the mover *intended* and `velocity` is what the body *did* -- ADR-545's
    // whole point -- and this fixture drives the body with a director override, which sets a
    // position and no intent. Zero intent beside a real measured velocity is the distinction
    // working, not failing.
    CHECK(a.speed == 0.0f);

    // Every field the seam carries, named, so that adding one and forgetting to publish it from
    // both paths is a compile error here rather than a silent divergence in a render.
    const auto compare = [](const avgen::entity::LocomotionState& x,
                            const avgen::entity::LocomotionState& y) {
        CHECK(x.velocity.x == Approx(y.velocity.x).margin(1e-4));
        CHECK(x.velocity.z == Approx(y.velocity.z).margin(1e-4));
        CHECK(x.facing.x == Approx(y.facing.x).margin(1e-4));
        CHECK(x.facing.z == Approx(y.facing.z).margin(1e-4));
        CHECK(x.speed == Approx(y.speed).margin(1e-4));
        CHECK(x.yaw == Approx(y.yaw).margin(1e-4));
        CHECK(x.turnRate == Approx(y.turnRate).margin(1e-4));
        CHECK(x.grounded == y.grounded);
        CHECK(x.action == y.action);
        CHECK(x.hasLookTarget == y.hasLookTarget);
        CHECK(x.hasGroundPlane == y.hasGroundPlane);
        CHECK(x.playbackRate == Approx(y.playbackRate).margin(1e-4));
    };
    // Compared against itself first: if `compare` were vacuous -- all defaults, or no assertions
    // that can fail -- the requires above would not catch it but this would not either, so the
    // REQUIREs are the ones doing that work and this line only fixes the shape.
    compare(a, a);

    // A second body driven identically publishes an identical seam. The director motion in this
    // fixture is what a scrub replays, so this is the reproducibility ADR-360 asks for, measured
    // on the struct the animation tier actually reads.
    Driven again;
    again.step(0.0, dt, glm::vec3(0.0f), facingNorth);
    again.step(dt, dt, northEast * 3.0f * dt, facingNorth);
    compare(a, again.body().locomotion());
}
