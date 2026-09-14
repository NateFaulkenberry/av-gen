// ADR-194: a body off the ground.
//
// Everything else in the entity layer assumes a walker is standing on something -- `GroundFollower`
// guarantees it, and the gait machine picks between standing, walking and running from a horizontal
// speed. There was no way to say "not standing on anything", so a character could not cross a gap it
// could obviously clear and four of the alien pack's clips were unreachable.
//
// These test the arc itself. The gap-crossing that uses it is an integration case, because it needs
// a world with a gap in it.

#include "entity/airborne.hpp"
#include "entity/navigation.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

// A navigator with no world answers ground zero everywhere, which is exactly the flat table an arc
// should be measured on.
entity::Navigator flatGround() { return entity::Navigator{}; }

struct Flight {
    std::vector<glm::vec3> path;
    std::vector<entity::Activity> phases;
    double airborneSeconds = 0.0;
    float apex = 0.0f;
};

Flight fly(entity::Airborne& air, const entity::JumpSettings& settings, double step = 1.0 / 60.0,
           int maxFrames = 600) {
    const entity::Navigator nav = flatGround();
    Flight out;
    glm::vec3 at{0.0f};
    for (int i = 0; i < maxFrames; ++i) {
        if (!air.update(nav, step, at, settings)) {
            break;
        }
        out.path.push_back(at);
        out.phases.push_back(air.activity());
        out.apex = std::max(out.apex, at.y);
        if (air.airborne()) {
            out.airborneSeconds += step;
        }
    }
    return out;
}

} // namespace

TEST_CASE("a hop rises to its apex, lands, and recovers", "[entity][airborne]") {
    entity::JumpSettings settings;
    settings.apex = 1.5f;
    settings.gravity = 18.0f;
    settings.maxDistance = 6.0f;
    settings.landSeconds = 0.2f;

    entity::Airborne air;
    REQUIRE(air.launch(glm::vec3(0.0f), glm::vec3(3.0f, 0.0f, 0.0f), settings));
    CHECK(air.active());
    CHECK(air.airborne());

    const Flight flight = fly(air, settings);
    REQUIRE(flight.path.size() > 10);

    // It reaches roughly the apex it was asked for. Not exactly: the arc is integrated on the
    // frame step rather than solved, so the peak lands between two frames.
    INFO("apex " << flight.apex);
    CHECK(flight.apex > settings.apex * 0.9f);
    CHECK(flight.apex < settings.apex * 1.1f);

    // It ends on the ground, having travelled roughly the distance asked for. "Roughly" is the
    // honest word: the horizontal speed is set from the rise doubled, so a hop landing level comes
    // back very close and one landing lower goes long.
    const glm::vec3 end = flight.path.back();
    CHECK(end.y == Approx(0.0f).margin(1e-3));
    INFO("travelled " << end.x);
    CHECK(end.x > 2.6f);
    CHECK(end.x < 3.4f);

    // And it is over: the component hands the body back rather than holding it for ever.
    CHECK_FALSE(air.active());

    // The phases run in order and each one actually happens. A jump that never reports `Fall` would
    // play a rising clip all the way down.
    bool sawJump = false, sawFall = false, sawLand = false;
    for (std::size_t i = 0; i < flight.phases.size(); ++i) {
        sawJump = sawJump || flight.phases[i] == entity::Activity::Jump;
        sawFall = sawFall || flight.phases[i] == entity::Activity::Fall;
        sawLand = sawLand || flight.phases[i] == entity::Activity::Land;
        // Never backwards: once falling it does not rise again, once landing it does not fly.
        if (flight.phases[i] == entity::Activity::Fall) {
            CHECK(flight.phases[i] != entity::Activity::Jump);
        }
    }
    CHECK(sawJump);
    CHECK(sawFall);
    CHECK(sawLand);

    // `Land` is active but not airborne -- the distinction grounding depends on, because a landing
    // body is standing again and must be back under the ground follower.
    entity::Airborne second;
    REQUIRE(second.launch(glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f), settings));
    const entity::Navigator nav = flatGround();
    glm::vec3 at{0.0f};
    bool checked = false;
    for (int i = 0; i < 600 && second.update(nav, 1.0 / 60.0, at, settings); ++i) {
        if (second.activity() == entity::Activity::Land) {
            CHECK(second.active());
            CHECK_FALSE(second.airborne());
            checked = true;
        }
    }
    CHECK(checked);
}

TEST_CASE("a hop refuses what it cannot clear", "[entity][airborne]") {
    entity::JumpSettings settings;
    settings.maxDistance = 4.0f;
    entity::Airborne air;

    // Too far. Refusing is the whole point: a body that attempts anything launches itself into a
    // lake, and there is no mid-air correction to save it.
    CHECK_FALSE(air.launch(glm::vec3(0.0f), glm::vec3(9.0f, 0.0f, 0.0f), settings));
    CHECK_FALSE(air.active());

    // Too near to be worth leaving the ground for -- a quarter-metre hop reads as a stumble.
    CHECK_FALSE(air.launch(glm::vec3(0.0f), glm::vec3(0.1f, 0.0f, 0.0f), settings));
    CHECK_FALSE(air.active());

    // And a hop in progress is committed: a second request does not retarget it.
    REQUIRE(air.launch(glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f), settings));
    CHECK_FALSE(air.launch(glm::vec3(0.0f), glm::vec3(3.0f, 0.0f, 0.0f), settings));
}

TEST_CASE("an arc is a pure function of its launch and its steps", "[entity][airborne][determinism]") {
    entity::JumpSettings settings;
    settings.maxDistance = 6.0f;

    // The same launch, twice, in two separate components: identical to the bit. This is what an
    // offline render of the same second depends on.
    entity::Airborne a;
    entity::Airborne b;
    REQUIRE(a.launch(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(4.0f, 2.0f, 3.0f), settings));
    REQUIRE(b.launch(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(4.0f, 2.0f, 3.0f), settings));
    const Flight first = fly(a, settings);
    const Flight second = fly(b, settings);
    REQUIRE(first.path.size() == second.path.size());
    for (std::size_t i = 0; i < first.path.size(); ++i) {
        CHECK(first.path[i].x == second.path[i].x);
        CHECK(first.path[i].y == second.path[i].y);
        CHECK(first.path[i].z == second.path[i].z);
    }

    // And `reset` really clears: a component reused after a seek must not carry the old arc.
    a.reset();
    CHECK_FALSE(a.active());
    REQUIRE(a.launch(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(4.0f, 2.0f, 3.0f), settings));
    const Flight third = fly(a, settings);
    REQUIRE(third.path.size() == first.path.size());
    CHECK(third.path.back().x == first.path.back().x);
}

TEST_CASE("an arc that never finds ground still ends", "[entity][airborne]") {
    // The guard, not a control. A launch off the edge of the world -- or into a hole the terrain
    // does not close -- must not leave a character falling for the rest of the piece.
    entity::JumpSettings settings;
    settings.maxSeconds = 0.5f;
    settings.apex = 40.0f;      // far more air time than the guard allows
    settings.gravity = 1.0f;
    settings.maxDistance = 6.0f;

    entity::Airborne air;
    REQUIRE(air.launch(glm::vec3(0.0f, 500.0f, 0.0f), glm::vec3(3.0f, 500.0f, 0.0f), settings));
    const Flight flight = fly(air, settings, 1.0 / 60.0, 2000);
    INFO("airborne for " << flight.airborneSeconds << " s");
    CHECK(flight.airborneSeconds <= 0.6);
    CHECK_FALSE(air.active());
}
