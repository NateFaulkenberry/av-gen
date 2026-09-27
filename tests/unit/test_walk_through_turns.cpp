// ADR-908: bodies walk through their turns on a circle, pivot only from rest, turn at the rate
// their gait authors, and a body with nothing but a walk cycle plays it while it turns on the spot.
//
// Every mover in the engine travelled at `max(0, cos(heading error))` of its pace, so any turn over
// 90 degrees began with a dead stop and a pivot on the spot -- the GV3 audit measured 27-44% of the
// animals' turning done standing and median turn radii of 0.7-1.4 m, less than a body length. The
// arms below read the ADR-910 recorder's pivot yaw and turn radius, each against a control that
// must read the opposite (ADR-182):
//
//   wander radius    a wanderer given a 3 m circle never turns tighter than it while moving  |
//                    control: a 0.4 m circle does
//   errand U-turn    a body walking north told to walk south walks a half circle of its radius and
//                    turns nothing on the spot  |  control: the same errand with no radius stops and
//                    pivots half a turn
//   authored rate    `gait.turnRate` is the rate a `move` and a `face` turn at  |  control: the
//                    verbs' old defaults when it is absent
//   pivot clip       a body with no idle clip turning on the spot plays its cycle at the rate its
//                    feet move  |  control: standing still, and an asset with an idle, are unchanged
//   the file         the three gait keys survive a save and a load, and are absent when not authored

#include "entity/action.hpp"
#include "entity/character_quality.hpp"
#include "entity/entity.hpp"
#include "entity/gait.hpp"
#include "entity/navigation.hpp"
#include "spatial/obstacle_field.hpp"
#include "support/cast_world.hpp"
#include "world/camera_clearance.hpp"
#include "world/ecology.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

using namespace avgen;
using Catch::Approx;
using testsupport::behavior;
using testsupport::CastMember;
using testsupport::CastWorld;

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDegrees = 180.0f / kPi;

entity::ActionDesc moveTo(glm::vec2 at) {
    entity::ActionDesc a;
    a.kind = entity::ActionKind::Move;
    a.target.kind = entity::TargetKind::Point;
    a.target.point = glm::vec3(at.x, 0.0f, at.y);
    return a;
}

// Flat ground and a set of rock solids: what the two cases below walk among.
struct SolidBed {
    world::WorldMap map;
    world::Ecology ecology;
    world::ClearanceField field;
    entity::Navigator nav;

    explicit SolidBed(const std::vector<glm::vec3>& rocks) { // x, z, radius
        map.size = glm::vec2(400.0f, 400.0f);
        map.prepare();
        field.map = &map;
        field.ecology = &ecology;
        field.cameraRadius = 0.6f;
        field.groundClearance = 0.0f;
        nav = entity::Navigator(&map, field);
        auto solids = std::make_shared<spatial::ObstacleField>();
        for (const glm::vec3& r : rocks) {
            spatial::NavigationObstacle o;
            o.center = glm::vec2(r.x, r.y);
            o.radius = r.z;
            o.base = 0.0f;
            o.height = 6.0f;
            o.type = spatial::ObstacleType::Rock;
            solids->add(o);
        }
        solids->build();
        nav.setObstacles(solids);
    }
    SolidBed(const SolidBed&) = delete;
    SolidBed& operator=(const SolidBed&) = delete;
};

// The navigator's own routes, steering and ground, with `clear` and `refuge` left at the interface's
// defaults: what a mover could see before a walk-through turn checked its heading and a `move` could
// walk back onto the walkable set. The control arms.
class BlindPath final : public entity::IPathProvider {
public:
    explicit BlindPath(const entity::Navigator* nav) : inner_(nav) {}
    [[nodiscard]] entity::RouteStatus route(glm::vec2 from, glm::vec2 to,
                                           std::vector<glm::vec2>& out) const override {
        return inner_.route(from, to, out);
    }
    [[nodiscard]] glm::vec2 steer(glm::vec2 from, glm::vec2 to, float lookahead) const override {
        return inner_.steer(from, to, lookahead);
    }
    [[nodiscard]] float groundHeight(glm::vec2 p) const override { return inner_.groundHeight(p); }

private:
    entity::NavigatorPath inner_;
};

} // namespace

TEST_CASE("a wanderer given a turn radius never turns tighter than it while moving",
          "[wander][adr908]") {
    const auto run = [](float radius) {
        CastMember m;
        m.name = "grazer";
        m.seed = 90210;
        m.behaviors.push_back(behavior("wander", {{"speed", 1.5}, {"turnRate", 120.0}, {"minRange", 8.0},
                                                  {"maxRange", 16.0}, {"pauseMin", 0.5}, {"pauseMax", 1.5},
                                                  {"homeRadius", 30.0}, {"turnRadius", radius}}));
        CastWorld w({m});
        w.play(400.0);
        return w.quality("grazer");
    };
    const entity::CharacterQuality wide = run(3.0f);
    WARN(fmt::format("turnRadius 3: {} turn samples, radius p10 {:.2f} m, median {:.2f} m; {:.0f} of {:.0f} "
                     "degrees turned below 0.3 m/s",
                     wide.motion.turnSamples, wide.motion.turnRadiusP10, wide.motion.turnRadiusMedian,
                     wide.motion.pivotYawDegrees, wide.motion.yawDegrees));
    REQUIRE(wide.motion.turnSamples > 500);
    // One turn in ten no tighter than the radius asked for, less the sliver a 60 Hz chord loses.
    CHECK(wide.motion.turnRadiusP10 >= 3.0 * 0.95);
    // Turning on the spot is what a walk-through turn is not. The forward cone and its lean keep
    // every destination within 90 degrees of the way the body faces, which a body at rest walks out
    // to on a curve from its first step, so almost nothing is left to turn standing: measured, 4 of
    // 1,641 degrees. The GV3 animals turned 27-44% of theirs below 0.3 m/s.
    CHECK(wide.motion.pivotYawFraction < 0.02);

    // The control: a circle smaller than a stride. The same body turns as tight as it is told to,
    // which is what shows the radius -- and not the forward cone -- set the first body's turns.
    const entity::CharacterQuality tight = run(0.4f);
    WARN(fmt::format("turnRadius 0.4: radius p10 {:.2f} m, median {:.2f} m", tight.motion.turnRadiusP10,
                     tight.motion.turnRadiusMedian));
    CHECK(tight.motion.turnRadiusP10 < 1.0);
}

TEST_CASE("an errand turned back mid-walk walks a half circle rather than stopping to pivot",
          "[action][adr908]") {
    // A body walking north at 1.5 m/s is told, five seconds in, to go to a point behind it. The old
    // mover started every walk from rest and travelled at the cosine of its heading error, so this
    // was a stop, a 180-degree pivot and a walk: the pattern the owner named.
    const auto run = [](float radius) {
        CastMember m;
        m.name = "walker";
        m.gait.walkSpeed = 1.5f;
        m.gait.accel = 3.0f;
        m.gait.decel = 4.0f;
        m.gait.turnRadius = radius;
        CastWorld w({m});
        entity::Entity& e = w.body("walker");
        e.actions().push(moveTo({0.0f, 40.0f}), entity::Authority::Routine);
        w.play(5.0);
        REQUIRE(e.state().speed > 1.4f); // at pace, heading north
        e.actions().override({moveTo({0.0f, -10.0f})}, entity::Authority::Routine, w.time());
        float widest = 0.0f;
        const float laneX = e.state().position().x;
        w.play(30.0, [&] { widest = std::max(widest, std::abs(e.state().position().x - laneX)); });
        const glm::vec3 end = e.state().position();
        return std::tuple(w.quality("walker"), widest, glm::length(glm::vec2(end.x, end.z + 10.0f)));
    };
    constexpr float kRadius = 2.5f;
    const auto [curved, swing, missBy] = run(kRadius);
    WARN(fmt::format("turnRadius {:.1f}: swung {:.2f} m out of its lane, {:.0f} deg turned below 0.3 m/s, "
                     "turn radius p10 {:.2f} m, arrived {:.2f} m from the goal, {} stops",
                     kRadius, swing, curved.motion.pivotYawDegrees, curved.motion.turnRadiusP10, missBy,
                     curved.behaviour.stops));
    // A half circle of the radius: it swung out of its lane by the circle's diameter, give or take
    // the easing, and never tighter than the radius while it moved.
    CHECK(swing > 2.0f * kRadius * 0.8f);
    CHECK(curved.motion.turnRadiusP10 >= kRadius * 0.95);
    // Nothing on the spot, and no stop between the two walks: the one stop is the arrival.
    CHECK(curved.motion.pivotYawDegrees < 5.0);
    CHECK(curved.behaviour.stops == 1);
    CHECK(missBy < 1.0f); // and it got there: a walk-through turn is still an errand

    // The control: no radius, the old mover. It stops, pivots standing, and walks back down the
    // lane it came up. Not the whole half turn below 0.3 m/s: it turns at its full rate while its
    // pace is still braking from walking speed, so about 50 degrees go by before it is slow enough
    // to count -- measured 103 of the 180. What matters is that it is most of the turn, against
    // none for the walk-through mover, and that the body never left its lane.
    const auto [pivoted, noSwing, missed] = run(0.0f);
    WARN(fmt::format("turnRadius 0: swung {:.2f} m, {:.0f} deg turned below 0.3 m/s, {} stops",
                     noSwing, pivoted.motion.pivotYawDegrees, pivoted.behaviour.stops));
    CHECK(pivoted.motion.pivotYawDegrees > 90.0);
    CHECK(pivoted.behaviour.stops == 2); // the stop to pivot, and the arrival
    CHECK(noSwing < 0.5f);
    CHECK(missed < 1.0f);
}

TEST_CASE("a body's authored turn rate is the rate its move and face verbs turn at",
          "[action][adr908][gait]") {
    // Standing, facing north, told to face east: a quarter turn at whatever rate the verb uses.
    const auto faceEast = [](float turnRateDegrees) {
        CastMember m;
        m.name = "turner";
        m.gait.turnRate = turnRateDegrees;
        CastWorld w({m});
        entity::Entity& e = w.body("turner");
        entity::ActionDesc face;
        face.kind = entity::ActionKind::Face;
        face.target.kind = entity::TargetKind::Point;
        face.target.point = glm::vec3(10.0f, 0.0f, 0.0f);
        e.actions().push(face, entity::Authority::Routine);
        double done = -1.0;
        w.play(5.0, [&] {
            if (done < 0.0 && std::abs(e.state().yaw - kPi * 0.5f) < 0.06f) {
                done = w.time();
            }
        });
        return done;
    };
    const double authored = faceEast(45.0f); // 90 degrees at 45 deg/s: two seconds
    const double inherited = faceEast(0.0f); // the verb's own 2.5 rad/s: 0.63 s
    WARN(fmt::format("a quarter turn: {:.3f} s at an authored 45 deg/s, {:.3f} s at the verb's default",
                     authored, inherited));
    CHECK(authored == Approx(2.0).margin(0.1));
    CHECK(inherited == Approx(kPi * 0.5 / 2.5).margin(0.05));

    // And a `move` whose goal is behind it turns at the authored rate too (no radius: the pivot).
    const auto pivotRate = [](float turnRateDegrees) {
        CastMember m;
        m.name = "walker";
        m.gait.turnRate = turnRateDegrees;
        CastWorld w({m});
        entity::Entity& e = w.body("walker");
        e.actions().push(moveTo({0.0f, -20.0f}), entity::Authority::Routine);
        float fastest = 0.0f;
        w.play(3.0, [&] { fastest = std::max(fastest, std::abs(e.state().turnRate)); });
        return fastest * kDegrees;
    };
    CHECK(pivotRate(60.0f) == Approx(60.0f).margin(0.5f));
    CHECK(pivotRate(0.0f) == Approx(2.45f * kDegrees).margin(0.5f)); // the control: the old 140 deg/s
}

TEST_CASE("a body with no idle clip plays its cycle while it turns on the spot",
          "[gait][adr908]") {
    // The farm pack's gait: one clip, rate matching on, and `idleRate` 0 because a standing body
    // would otherwise walk on the spot (ADR-213, ADR-622's labelled compensation).
    entity::GaitSettings farm;
    farm.matchRate = true;
    farm.walkSpeed = 3.0f;
    farm.rateMin = 0.02f;
    farm.rateMax = 1.0f;
    farm.idleRate = 0.0f;
    farm.pivotRadius = 1.2f;
    using entity::Activity;
    using entity::Gait;
    // Turning at 1.5 rad/s: its feet move at 1.8 m/s round the pivot, so the 3 m/s cycle plays at 0.6.
    CHECK(Gait::playbackRate(farm, Activity::Turn, 0.0f, 1.5f) == Approx(0.6f));
    CHECK(Gait::playbackRate(farm, Activity::Turn, 0.0f, -1.5f) == Approx(0.6f)); // either way round
    // A slow turn is floored where a cycle becomes visible, a wild one capped by the gait's ceiling.
    CHECK(Gait::playbackRate(farm, Activity::Turn, 0.0f, 0.001f) == Approx(entity::kVisibleClipRate));
    CHECK(Gait::playbackRate(farm, Activity::Turn, 0.0f, 50.0f) == Approx(farm.rateMax));

    // The controls. Standing still is still frozen -- the compensation is for not turning -- and so
    // is a turn with no rate to go on.
    CHECK(Gait::playbackRate(farm, Activity::Idle, 0.0f, 0.0f) == 0.0f);
    CHECK(Gait::playbackRate(farm, Activity::Turn, 0.0f, 0.0f) == 0.0f);
    // An asset with an idle has a turn clip of its own, played at its own rate as before.
    entity::GaitSettings alien = farm;
    alien.idleRate = 1.0f;
    CHECK(Gait::playbackRate(alien, Activity::Turn, 0.0f, 1.5f) == 1.0f);

    // And through a body: a farm-gaited body told to face behind it turns on the spot, and every
    // frame the gait calls a turn plays the cycle rather than rotating a frozen stride.
    CastMember m;
    m.name = "cow";
    m.gait = farm;
    CastWorld w({m});
    entity::Entity& e = w.body("cow");
    entity::ActionDesc face;
    face.kind = entity::ActionKind::Face;
    face.target.kind = entity::TargetKind::Point;
    face.target.point = glm::vec3(0.0f, 0.0f, -10.0f);
    e.actions().push(face, entity::Authority::Routine);
    int turning = 0;
    int frozen = 0;
    w.play(3.0, [&] {
        if (e.locomotion().activity == Activity::Turn) {
            ++turning;
            frozen += e.locomotion().playbackRate < entity::kVisibleClipRate ? 1 : 0;
        }
    });
    INFO("turning frames " << turning << ", frozen " << frozen);
    REQUIRE(turning > 30);
    CHECK(frozen == 0);
}

TEST_CASE("the gait's turn keys survive a save and are absent when nobody wrote them",
          "[gait][adr908][serialisation]") {
    const nlohmann::json authored = {{"walkSpeed", 3.0}, {"turnRate", 75.0}, {"turnRadius", 2.25},
                                     {"pivotRadius", 1.4}};
    const auto read = entity::gaitFromJson(authored);
    REQUIRE(read.has_value());
    CHECK(read->turnRate == 75.0f);
    CHECK(read->turnRadius == 2.25f);
    CHECK(read->pivotRadius == Approx(1.4f));
    const auto again = entity::gaitFromJson(entity::gaitToJson(*read));
    REQUIRE(again.has_value());
    CHECK(*again == *read); // bit for bit: degrees in memory as in the file

    const auto plain = entity::gaitFromJson(nlohmann::json{{"walkSpeed", 3.0}});
    REQUIRE(plain.has_value());
    const nlohmann::json written = entity::gaitToJson(*plain);
    CHECK_FALSE(written.contains("turnRate"));
    CHECK_FALSE(written.contains("turnRadius"));
    CHECK_FALSE(written.contains("pivotRadius"));

    CHECK_FALSE(entity::gaitFromJson(nlohmann::json{{"turnRadius", -1.0}}).has_value());
}

TEST_CASE("a walk-through turn too wide for its corridor brakes and pivots rather than walking into the wall",
          "[action][adr908][navigation]") {
    // A corridor of rocks either side of x = 0, 3.1 m of walkable width between the keep-out bands,
    // and a body turning on a 2.5 m circle -- a 5 m swing -- told to turn back. The steering fan
    // checks the way it offers (south, down the corridor, clear); the body walks its turn along its
    // heading, which swings through the wall. GV3's rook did exactly this on its 1.5 m circle and
    // stood off the walkable set for the rest of the film, every errand "blocked".
    std::vector<glm::vec3> rocks;
    for (float z = -30.0f; z <= 60.0f; z += 1.0f) {
        rocks.emplace_back(-2.6f, z, 0.6f);
        rocks.emplace_back(2.6f, z, 0.6f);
    }
    SolidBed bed(rocks);
    REQUIRE(bed.nav.navigable({0.0f, 0.0f}));
    REQUIRE_FALSE(bed.nav.navigable({1.8f, 0.0f})); // the keep-out band: rock plus body radius

    const auto run = [&](bool blind) {
        CastMember m;
        m.name = "walker";
        m.gait.walkSpeed = 1.5f;
        m.gait.accel = 3.0f;
        m.gait.decel = 4.0f;
        m.gait.turnRadius = 2.5f;
        CastWorld w({m}, &bed.nav);
        BlindPath blindPath(&w.world.navigator());
        if (blind) {
            w.world.setPathProvider(&blindPath);
        }
        entity::Entity& e = w.body("walker");
        e.actions().push(moveTo({0.0f, 40.0f}), entity::Authority::Routine);
        w.play(5.0);
        REQUIRE(e.state().speed > 1.4f);
        e.actions().override({moveTo({0.0f, -10.0f})}, entity::Authority::Routine, w.time());
        int offSet = 0;
        float widest = 0.0f;
        w.play(30.0, [&] {
            const glm::vec3 p = e.state().position();
            offSet += bed.nav.navigable({p.x, p.z}) ? 0 : 1;
            widest = std::max(widest, std::abs(p.x));
        });
        const glm::vec3 end = e.state().position();
        const entity::ActionQueue::Drained& drained = e.actions().drained(entity::Authority::Routine);
        return std::tuple(offSet, widest, glm::length(glm::vec2(end.x, end.z + 10.0f)), drained.failed,
                          drained.reason);
    };
    const auto [offSet, widest, missBy, failed, reason] = run(false);
    WARN(fmt::format("corridor, heading checked: {} frames off the walkable set, widest {:.2f} m from the "
                     "middle, arrived {:.2f} m from the goal{}",
                     offSet, widest, missBy, failed ? ", failed: " + reason : ""));
    CHECK(offSet == 0);
    CHECK_FALSE(failed);
    CHECK(missBy < 1.0f);

    // The control: the same errand with a provider that cannot see the heading's way (the mover
    // before this check). It swings into the wall.
    const auto [blindOff, blindWidest, blindMiss, blindFailed, blindReason] = run(true);
    WARN(fmt::format("corridor, heading unchecked: {} frames off the walkable set, widest {:.2f} m{}",
                     blindOff, blindWidest, blindFailed ? ", failed: " + blindReason : ""));
    CHECK(blindOff > 0);
}

TEST_CASE("a body standing off the walkable set walks back onto it and on to its errand",
          "[action][adr908][navigation]") {
    // Standing inside a rock's keep-out: from here the steering fan finds no clear way in any
    // direction, because it samples from the body outward (ADR-162). `wander` has walked back
    // onto the set since ADR-240; a `move` failed "blocked" -- and every errand after it, from the
    // same spot, for ever.
    SolidBed bed({glm::vec3(0.0f, 0.0f, 2.0f)});
    const glm::vec2 start(0.5f, 0.0f);
    REQUIRE_FALSE(bed.nav.navigable(start));

    const auto run = [&](bool blind) {
        CastMember m;
        m.name = "walker";
        m.at = glm::vec3(start.x, 0.0f, start.y);
        m.gait.walkSpeed = 1.5f;
        CastWorld w({m}, &bed.nav);
        BlindPath blindPath(&w.world.navigator());
        if (blind) {
            w.world.setPathProvider(&blindPath);
        }
        entity::Entity& e = w.body("walker");
        e.actions().push(moveTo({0.0f, 20.0f}), entity::Authority::Routine);
        w.play(25.0);
        const glm::vec3 end = e.state().position();
        const entity::ActionQueue::Drained& drained = e.actions().drained(entity::Authority::Routine);
        return std::tuple(glm::length(glm::vec2(end.x, end.z - 20.0f)), drained.failed, drained.reason);
    };
    const auto [missBy, failed, reason] = run(false);
    WARN(fmt::format("from inside a rock: arrived {:.2f} m from the goal{}", missBy,
                     failed ? ", failed: " + reason : ""));
    CHECK_FALSE(failed);
    CHECK(missBy < 1.0f);

    // The control: no refuge on offer (the provider before this change). Blocked, where it stands.
    const auto [blindMiss, blindFailed, blindReason] = run(true);
    WARN(fmt::format("no refuge on offer: {:.2f} m from the goal, {}", blindMiss,
                     blindFailed ? "failed: " + blindReason : "not failed"));
    CHECK(blindFailed);
    CHECK(blindReason == "blocked");
    CHECK(blindMiss > 19.0f);
}
