// ADR-932: the action tier's routes respect connected regions.
//
// `NavigatorPath::route` -- the path every `move` runs through: a decider's errands and reactions, a
// schedule, an order, a set piece's walk -- checked only that the goal was somewhere a walker could
// stand, and answered with the straight line. On GV3 the river runs edge to edge and divides the
// valley's walkable ground into pieces, so a goal on the far bank was `Ready`: the body waded in to
// its limit, stalled, gave up, and was sent again (ember, eight times in 40 s). The graph already knew:
// `NavGrid` labels connected regions and its own search refuses a goal in another one.
//
// Each case reads the answer off the provider or off the body, with a control that must read the
// opposite (ADR-182):
//
//   across    a goal across a river that runs edge to edge is `Nearest`, one waypoint on the dry
//             near bank, across from the goal  |  the planner's own verdict on the same pair is
//             `Unreachable`, and the straight line crosses water past the wade limit
//   round     the same river ending inside the world: the banks join round its head, one region,
//             and the answer is the straight line exactly as before  |  (ADR-933 is what stops a
//             body pacing there)
//   move      a `move` to the far bank walks to the bank dry, fails "unreachable", and the face after
//             it turns the body to the goal  |  the old straight line, through a provider that
//             answers as `NavigatorPath` did: the body wades in to its limit and gives up "stuck"
//   validator the Director's goal check (`SceneFacts::walkable`) hears "no route" across the river

#include "entity/action.hpp"
#include "entity/entity.hpp"
#include "entity/nav_grid.hpp"
#include "entity/navigation.hpp"
#include "support/cast_world.hpp"
#include "world/ecology.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace avgen;
using testsupport::CastMember;
using testsupport::CastWorld;

namespace {

// A flat, noiseless world with one deep channel across it at x = +30, running from beyond one edge to
// beyond the other (or, with `head`, ending inside the world at z = `head`). Flat on purpose: what is
// under test is reachability, and terrain noise would put a slope rejection in front of it. A `River`
// feature's path level is its water surface and `amplitude` how far its bed sits below it.
world::WorldMap riverWorld(float head = 130.0f) {
    world::WorldMap map;
    map.name = "a river edge to edge";
    map.size = glm::vec2(240.0f, 240.0f);
    map.baseHeight = 0.0f;
    world::Feature river;
    river.name = "river";
    river.kind = world::FeatureKind::River;
    river.path = {{30.0f, 0.0f, -130.0f}, {30.0f, 0.0f, head}};
    river.width = 12.0f;
    river.amplitude = 2.5f;
    river.water = true;
    map.features.push_back(river);
    map.prepare();
    return map;
}

// GV3's walkers wade (its `navWadeDepth` is 0.85 m): the channel's margins are walkable, its middle
// is not, and a body walking straight at the far bank goes in up to its knees before anything stops it.
entity::Navigator wader(const world::WorldMap& map, world::Ecology& ecology) {
    world::ClearanceField clearance;
    clearance.map = &map;
    clearance.ecology = &ecology;
    clearance.cameraRadius = 0.6f;
    clearance.groundClearance = 0.0f;
    entity::NavSettings settings;
    settings.wadeDepth = 0.8f;
    entity::Navigator nav(&map, clearance, settings);
    nav.buildGrid(4.0f);
    return nav;
}

const glm::vec2 kWest(0.0f, 0.0f);   // where the body stands
const glm::vec2 kEast(50.0f, 0.0f);  // the far bank

// The route `NavigatorPath` gave before ADR-932, for every goal a walker could stand on: the straight
// line, `Ready`. Everything else is the real provider's, so the control differs in the route alone.
class StraightLine final : public entity::IPathProvider {
public:
    explicit StraightLine(const entity::Navigator* nav) : real_(nav) {}
    [[nodiscard]] entity::RouteStatus route(glm::vec2, glm::vec2 to, std::vector<glm::vec2>& out) const override {
        out.assign(1, to);
        return entity::RouteStatus::Ready;
    }
    [[nodiscard]] glm::vec2 steer(glm::vec2 from, glm::vec2 to, float lookahead) const override {
        return real_.steer(from, to, lookahead);
    }
    [[nodiscard]] float groundHeight(glm::vec2 p) const override { return real_.groundHeight(p); }
    [[nodiscard]] bool clear(glm::vec2 from, glm::vec2 to) const override { return real_.clear(from, to); }
    [[nodiscard]] bool walkable(glm::vec2 p) const override { return real_.walkable(p); }
    [[nodiscard]] bool refuge(glm::vec2 from, glm::vec2& out) const override { return real_.refuge(from, out); }

private:
    entity::NavigatorPath real_;
};

// A `move` to the far bank and a `face` toward it, run for `seconds` on the Action tier.
struct Errand {
    std::vector<entity::ActionEvent> events;
    float deepest = 0.0f;   // the deepest water the body stood in
    glm::vec2 end{0.0f};    // where it was when the run ended
    float endYaw = 0.0f;
};

Errand sendAcross(const entity::Navigator& nav, bool straightLine, double seconds = 20.0) {
    CastMember walker;
    walker.name = "walker";
    walker.at = glm::vec3(kWest.x, 0.0f, kWest.y);
    walker.gait.walkSpeed = 2.0f;
    CastWorld w({walker}, &nav);
    StraightLine old(&w.world.navigator());
    if (straightLine) {
        w.world.setPathProvider(&old);
    }
    entity::ActionDesc move;
    move.kind = entity::ActionKind::Move;
    move.name = "across";
    move.target.kind = entity::TargetKind::Point;
    move.target.point = glm::vec3(kEast.x, 0.0f, kEast.y);
    entity::ActionDesc face;
    face.kind = entity::ActionKind::Face;
    face.name = "look";
    face.target = move.target;
    w.body("walker").actions().push(std::vector<entity::ActionDesc>{move, face}, entity::Authority::Action);
    Errand out;
    w.play(seconds, [&] {
        for (const entity::ActionEvent& e : w.world.actionEvents()) {
            out.events.push_back(e);
        }
        const glm::vec3 p = w.body("walker").state().position();
        out.deepest = std::max(out.deepest, w.world.navigator().terrain().waterDepthAt(glm::vec2(p.x, p.z)));
    });
    const glm::vec3 p = w.body("walker").state().position();
    out.end = glm::vec2(p.x, p.z);
    out.endYaw = w.body("walker").state().yaw;
    return out;
}

const entity::ActionEvent* eventFor(const Errand& e, const std::string& action) {
    for (const entity::ActionEvent& ev : e.events) {
        if (ev.action == action) {
            return &ev;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("a goal across a river that runs edge to edge routes to the near bank, not into the water",
          "[entity][route][adr932]") {
    const world::WorldMap map = riverWorld();
    world::Ecology ecology;
    const entity::Navigator nav = wader(map, ecology);
    const entity::NavGrid* grid = nav.grid();
    REQUIRE(grid != nullptr);
    // The fixture is what it says: both banks are ground a body stands on, the channel divides them,
    // and the straight line between them crosses water no walker here may enter.
    REQUIRE(nav.navigable(kWest));
    REQUIRE(nav.navigable(kEast));
    REQUIRE(grid->stats().regions >= 2);
    REQUIRE_FALSE(grid->connected(kWest, kEast));
    REQUIRE_FALSE(nav.pathClear(kWest, kEast));
    // Control: the planner's own verdict on the same pair. ADR-932 makes the action tier agree with it.
    entity::PathRequest request;
    request.from = kWest;
    request.to = kEast;
    CHECK(nav.requestPath(request).status == entity::PathStatus::Unreachable);

    const entity::NavigatorPath path(&nav);
    std::vector<glm::vec2> route;
    const entity::RouteStatus status = path.route(kWest, kEast, route);
    INFO("status " << entity::routeStatusName(status));
    // Before ADR-932: `Ready`, one waypoint, the far bank itself.
    CHECK(status == entity::RouteStatus::Nearest);
    REQUIRE(route.size() == 1);
    const glm::vec2 end = route.back();
    INFO("ends at " << end.x << ", " << end.y);
    // On this side, on ground a body stands on, dry, and reached by a clear straight walk.
    CHECK(end.x < 30.0f - 6.0f);
    CHECK(nav.navigable(end));
    CHECK(nav.terrain().waterDepthAt(end) == 0.0f);
    CHECK(nav.pathClear(kWest, end));
    // As near as it gets: across from the goal, at the water's edge -- half a metre further toward the
    // goal is wet -- and not in the middle of the last dry cell.
    CHECK(std::abs(end.y - kEast.y) < 4.0f);
    CHECK(nav.terrain().waterDepthAt(end + glm::normalize(kEast - end) * 0.5f) > 0.0f);
    CHECK(end.x > kWest.x + 10.0f);

    // Within one region the answer is the straight line, exactly as before: a goal on the same bank.
    const glm::vec2 sameBank(-40.0f, 35.0f);
    REQUIRE(grid->connected(kWest, sameBank));
    std::vector<glm::vec2> near;
    CHECK(path.route(kWest, sameBank, near) == entity::RouteStatus::Ready);
    REQUIRE(near.size() == 1);
    CHECK(near.back() == sameBank);
    // And so is every answer in a world with no graph: nothing there can say two points are apart.
    world::Ecology bareEcology;
    world::ClearanceField clearance;
    clearance.map = &map;
    clearance.ecology = &bareEcology;
    entity::NavSettings settings;
    settings.wadeDepth = 0.8f;
    const entity::Navigator gridless(&map, clearance, settings);
    const entity::NavigatorPath blind(&gridless);
    std::vector<glm::vec2> straight;
    CHECK(blind.route(kWest, kEast, straight) == entity::RouteStatus::Ready);
    CHECK(straight == std::vector<glm::vec2>{kEast});
}

TEST_CASE("a river that ends inside the world divides nothing, and the route stays the straight line",
          "[entity][route][adr932]") {
    // The same channel, ending at z = 40: the banks join round its head, as GV3's did in gv3-world's
    // W2 until its river was run edge to edge again. One region, so this ADR does not touch it; a body
    // sent across walks into the water as it always did, and ADR-933 is what stops it pacing there.
    const world::WorldMap map = riverWorld(40.0f);
    world::Ecology ecology;
    const entity::Navigator nav = wader(map, ecology);
    REQUIRE(nav.grid() != nullptr);
    REQUIRE(nav.grid()->connected(kWest, kEast));
    REQUIRE_FALSE(nav.pathClear(kWest, kEast));
    const entity::NavigatorPath path(&nav);
    std::vector<glm::vec2> route;
    CHECK(path.route(kWest, kEast, route) == entity::RouteStatus::Ready);
    CHECK(route == std::vector<glm::vec2>{kEast});
}

TEST_CASE("a move to the far bank walks to the bank, says it could not get there, and looks across",
          "[entity][action][adr932]") {
    const world::WorldMap map = riverWorld();
    world::Ecology ecology;
    const entity::Navigator nav = wader(map, ecology);

    const Errand now = sendAcross(nav, false);
    const entity::ActionEvent* move = eventFor(now, "across");
    REQUIRE(move != nullptr);
    INFO("move " << entity::actionResultName(move->result) << " at " << move->time << " s: " << move->reason);
    CHECK(move->result == entity::ActionResult::Failed);
    CHECK(move->reason.rfind("unreachable", 0) == 0);
    // It walked to the bank and no further: dry the whole way, and ended at the water's edge.
    CHECK(now.deepest == 0.0f);
    CHECK(now.end.x > kWest.x + 10.0f);
    CHECK(now.end.x < 30.0f - 6.0f);
    // At a walk: some 20 m at 2 m/s, and not four seconds of standing at a wall before giving up.
    CHECK(move->time < 16.0);
    // The rest of the errand ran: the face after the move turned the body to the far bank.
    const entity::ActionEvent* face = eventFor(now, "look");
    REQUIRE(face != nullptr);
    CHECK(face->result == entity::ActionResult::Completed);
    const float east = std::atan2(1.0f, 0.0f); // yaw of +x
    CHECK(std::abs(std::remainder(now.endYaw - east, 6.2831853f)) < 0.1f);

    // Control: the same errand along the straight line `NavigatorPath` gave before this ADR. The body
    // wades in toward the far bank until the channel stops it, and gives up standing in the river.
    const Errand before = sendAcross(nav, true);
    const entity::ActionEvent* stuck = eventFor(before, "across");
    REQUIRE(stuck != nullptr);
    INFO("before: " << entity::actionResultName(stuck->result) << " at " << stuck->time << " s: " << stuck->reason
                    << "; deepest " << before.deepest << " m");
    CHECK(stuck->result == entity::ActionResult::Failed);
    CHECK(stuck->reason.rfind("unreachable", 0) != 0);
    CHECK(before.deepest > 0.4f);
    WARN(fmt::format("ADR-932, a move to the far bank: now \"{}\" at {:.2f} s, standing at ({:.2f}, {:.2f}), deepest "
                     "water {:.2f} m; the old straight line \"{}\" at {:.2f} s at ({:.2f}, {:.2f}), deepest {:.2f} m",
                     move->reason, move->time, now.end.x, now.end.y, now.deepest, stuck->reason, stuck->time,
                     before.end.x, before.end.y, before.deepest));
}
