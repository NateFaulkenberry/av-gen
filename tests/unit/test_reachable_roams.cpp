// ADR-936: roam targets are reachable, and a walk goes round water it cannot wade.
//
// ADR-932 made a walk to a goal across a divide end at the bank, and left two things open that GV3
// showed (navfix's trace of gv3-cast's iteration 2, with every decision):
//
//  * `interest` -- GV3's roam, graze and watch -- still offered places a walk cannot end at. Across
//    the river each errand now ended at the bank, failing "unreachable: no way across to it" (ember,
//    three at 22-52 s); and where the stand-off was not ground a body can stand on -- bloom hero parts,
//    glow patches beside them -- the move failed on its first step ("unreachable"; ember, four
//    running at 24-33 s). Each is a walk that goes nowhere and a stop that reads as a mechanism.
//  * A goal in the walker's own region whose straight line crosses water deeper than it wades --
//    a channel whose banks join round its head, a pond -- was walked straight at: the body waded in
//    to its limit and dithered, and the stuck clock counted each creep deeper as progress.
//
// Each case with a control that must read the opposite (ADR-182):
//
//   offered   `interest` offers a glow on its own bank and not one across a river that runs edge to
//             edge  |  the same glow, where the river ends inside the world and the banks are one
//             region, is offered
//   stood on  a glow whose stand-off is in the channel is offered with its walk ending on standable
//             ground within the walk's own tolerance of it  |  the raw stand-off is a route
//             `Unreachable`, the old option's failure on its first step; and with no stand-off (the
//             move's 0.75 m) nothing near enough is standable, and it is not offered
//   round     a move to a goal across a channel it shares a region with walks round the channel's
//             head and arrives, never deeper than it wades, with its stuck clock quiet while the walk
//             heads away from the goal  |  the straight line: into the channel to its wade limit,
//             dithering there, and "stuck"

#include "entity/action.hpp"
#include "entity/decision.hpp"
#include "entity/entity.hpp"
#include "entity/nav_grid.hpp"
#include "entity/navigation.hpp"
#include "support/cast_world.hpp"
#include "world/ecology.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;
using testsupport::CastMember;
using testsupport::CastWorld;

namespace {

// A flat world with a deep channel at x = +30 from beyond the south edge to `head`: beyond the north
// edge it divides the world in two; inside it, the banks join round its head. Walkers wade to 0.8 m,
// as GV3's do to 0.85, so the channel's margins are walkable and its middle is not.
world::WorldMap riverWorld(float head) {
    world::WorldMap map;
    map.name = "a river";
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

entity::InterestPoint glow(const char* name, glm::vec2 at) {
    entity::InterestPoint p;
    p.name = name;
    p.position = glm::vec3(at.x, 0.0f, at.y);
    p.kind = entity::InterestKind::Glow;
    p.weight = 1.0f;
    return p;
}

// What `interest` offers a body standing at the origin: each option's name, where its walk ends and
// where it looks when it gets there.
struct Offer {
    glm::vec2 walkTo{0.0f};
    glm::vec2 lookAt{0.0f};
};

std::map<std::string, Offer> offers(const entity::Navigator& nav, std::vector<entity::InterestPoint> points,
                                    float approach) {
    CastMember m;
    m.name = "roamer";
    m.seed = 31;
    CastWorld w({m}, &nav);
    w.world.setExtraInterestPoints(std::move(points));
    w.step();
    const nlohmann::json settings = {{"source", "omniscient"}, {"weights", {{"glow", 1.0}, {"water", 0.0}, {"vista", 0.0}, {"landmark", 0.0}, {"character", 0.0}}}, {"maxRange", 150.0},
                                     {"approach", approach}, {"dwell", 2.0}, {"activity", "observe"}};
    entity::InterestConsiderer roam(&settings);
    roam.setName("roam");
    entity::DecisionContext ctx;
    ctx.time = 0.0;
    ctx.state = &w.body("roamer").state();
    ctx.world = &w.world;
    ctx.nav = &w.world.navigator();
    std::vector<entity::Option> out;
    roam.consider(ctx, out);
    std::map<std::string, Offer> result;
    for (const entity::Option& o : out) {
        REQUIRE(o.actions.size() == 2);
        Offer offer;
        offer.walkTo = glm::vec2(o.actions[0].target.point.x, o.actions[0].target.point.z);
        offer.lookAt = glm::vec2(o.actions[1].target.point.x, o.actions[1].target.point.z);
        result[std::string(o.name)] = offer;
    }
    return result;
}

std::string names(const std::map<std::string, Offer>& m) {
    std::string s;
    for (const auto& [name, offer] : m) {
        s += fmt::format("{} (walk to {:.1f}, {:.1f}) ", name, offer.walkTo.x, offer.walkTo.y);
    }
    return s.empty() ? "nothing" : s;
}

} // namespace

TEST_CASE("interest offers no place across a divide", "[decide][route][adr936]") {
    const std::vector<entity::InterestPoint> points = {glow("near", {-25.0f, 15.0f}), glow("far", {50.0f, 5.0f})};

    // The river runs edge to edge: its far bank is another region.
    const world::WorldMap divided = riverWorld(130.0f);
    world::Ecology ecology;
    const entity::Navigator nav = wader(divided, ecology);
    REQUIRE_FALSE(nav.grid()->connected(glm::vec2(0.0f), glm::vec2(50.0f, 5.0f)));
    const std::map<std::string, Offer> across = offers(nav, points, 0.0f);
    INFO("edge to edge: " << names(across));
    CHECK(across.count("near") == 1);
    // Before ADR-936 offered, and when chosen walked to the bank and failed "unreachable: no way
    // across to it" -- the route there is ADR-932's `Nearest`.
    CHECK(across.count("far") == 0);
    std::vector<glm::vec2> route;
    CHECK(entity::NavigatorPath(&nav).route(glm::vec2(0.0f), glm::vec2(50.0f, 5.0f), route) ==
          entity::RouteStatus::Nearest);
    // A place on its own side keeps the walk it always had: to the glow itself, with no stand-off.
    REQUIRE(across.count("near") == 1);
    CHECK(across.at("near").walkTo == glm::vec2(-25.0f, 15.0f));

    // Control: the same glows where the river ends inside the world. The banks are one region, the far
    // glow is reachable (round the head, ADR-936's route), and it is offered.
    const world::WorldMap joined = riverWorld(40.0f);
    world::Ecology ecology2;
    const entity::Navigator nav2 = wader(joined, ecology2);
    REQUIRE(nav2.grid()->connected(glm::vec2(0.0f), glm::vec2(50.0f, 5.0f)));
    const std::map<std::string, Offer> round = offers(nav2, points, 0.0f);
    INFO("joined: " << names(round));
    CHECK(round.count("near") == 1);
    CHECK(round.count("far") == 1);
}

TEST_CASE("interest walks to standable ground beside a place it cannot stand on", "[decide][route][adr936]") {
    // A glow on the channel's near margin, seen from the origin: its stand-off 6 m this side of it is
    // in water deeper than a walker wades -- the shape of GV3's bloom hero parts, whose stand-offs were
    // ground a body cannot stand on.
    const world::WorldMap map = riverWorld(130.0f);
    world::Ecology ecology;
    const entity::Navigator nav = wader(map, ecology);
    const glm::vec2 brink(31.0f, 12.0f);
    const std::vector<entity::InterestPoint> points = {glow("brink", brink)};
    const glm::vec2 dir = glm::normalize(brink);
    const glm::vec2 rawStand = brink - dir * 6.0f;
    // The fixture: the raw stand-off is not standable, so the walk the old option pushed failed on its
    // first step -- `route` refuses a goal no walker can stand on.
    REQUIRE_FALSE(nav.navigable(rawStand));
    std::vector<glm::vec2> route;
    CHECK(entity::NavigatorPath(&nav).route(glm::vec2(0.0f), rawStand, route) == entity::RouteStatus::Unreachable);

    const std::map<std::string, Offer> offered = offers(nav, points, 6.0f);
    INFO("offered: " << names(offered) << "; raw stand-off (" << rawStand.x << ", " << rawStand.y << ")");
    REQUIRE(offered.count("brink") == 1);
    const Offer& o = offered.at("brink");
    // The walk ends where a walker can stand, within the walk's own tolerance (half its approach) of
    // the stand-off -- so arriving there is arriving -- on this side of it, and the look is still at
    // the glow itself.
    CHECK(nav.navigable(o.walkTo));
    CHECK(glm::length(o.walkTo - rawStand) <= 3.0f + 1e-4f);
    CHECK(glm::length(o.walkTo) < glm::length(rawStand));
    CHECK(o.lookAt == brink);
    std::vector<glm::vec2> walk;
    CHECK(entity::NavigatorPath(&nav).route(glm::vec2(0.0f), o.walkTo, walk) == entity::RouteStatus::Ready);

    // Control: with no stand-off the walk is to the glow itself, in the channel, and a move's own
    // 0.75 m holds no standable ground: not offered, where before it was chosen and failed at once.
    const glm::vec2 channel(30.0f, 12.0f);
    REQUIRE_FALSE(nav.navigable(channel));
    const std::map<std::string, Offer> bare = offers(nav, {glow("channel", channel)}, 0.0f);
    INFO("with no stand-off: " << names(bare));
    CHECK(bare.count("channel") == 0);
}

namespace {

// The straight line whatever lies between, as `NavigatorPath` answered a same-region goal before
// ADR-936.
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

struct Crossing {
    std::string result = "none";
    std::string reason;
    double at = -1.0;
    float deepest = 0.0f;       // the deepest water it stood in
    double inWater = 0.0;       // seconds standing in water over 0.5 m
    float walked = 0.0f;        // metres covered
    double longestAway = 0.0;   // the longest it went without getting nearer the goal in a straight line
    glm::vec2 end{0.0f};
};

// A `move` from the origin to (40, 0), across the channel's line, for 90 s.
Crossing cross(const entity::Navigator& nav, bool straightLine) {
    CastMember m;
    m.name = "walker";
    m.seed = 4242;
    m.gait.walkSpeed = 2.0f;
    m.behaviors.push_back(testsupport::behavior("ground", {{"bodyRadius", 0.5}}));
    CastWorld w({m}, &nav);
    StraightLine old(&w.world.navigator());
    if (straightLine) {
        w.world.setPathProvider(&old);
    }
    const glm::vec2 goal(40.0f, 0.0f);
    entity::ActionDesc go;
    go.kind = entity::ActionKind::Move;
    go.name = "across";
    go.target.kind = entity::TargetKind::Point;
    go.target.point = glm::vec3(goal.x, 0.0f, goal.y);
    w.body("walker").actions().push(std::vector<entity::ActionDesc>{go}, entity::Authority::Action);
    Crossing out;
    w.world.setActionListener([&](const entity::ActionEvent& e) {
        if (e.action == "across" && out.at < 0.0) {
            out.result = entity::actionResultName(e.result);
            out.reason = e.reason;
            out.at = e.time;
        }
    });
    glm::vec2 last(0.0f);
    float best = glm::length(goal);
    double away = 0.0;
    w.play(90.0, [&] {
        const glm::vec3 p = w.body("walker").state().position();
        const glm::vec2 here(p.x, p.z);
        const float depth = w.world.navigator().terrain().waterDepthAt(here);
        out.deepest = std::max(out.deepest, depth);
        out.inWater += depth > 0.5f ? CastWorld::kStep : 0.0;
        out.walked += glm::length(here - last);
        last = here;
        const float d = glm::length(goal - here);
        if (d < best - 0.05f) {
            best = d;
            away = 0.0;
        } else if (out.at < 0.0) {
            away += CastWorld::kStep;
            out.longestAway = std::max(out.longestAway, away);
        }
    });
    out.end = last;
    return out;
}

std::string describe(const Crossing& c) {
    return fmt::format("{} ({}) at {:.2f} s, walked {:.1f} m, deepest {:.2f} m, {:.1f} s in water over 0.5 m, "
                       "{:.1f} s at most without nearing the goal in a straight line, ended at ({:.1f}, {:.1f})",
                       c.result, c.reason, c.at, c.walked, c.deepest, c.inWater, c.longestAway, c.end.x, c.end.y);
}

} // namespace

TEST_CASE("a move across a channel it shares a region with walks round the head, and arrives",
          "[entity][action][route][adr936]") {
    const world::WorldMap map = riverWorld(40.0f);
    world::Ecology ecology;
    const entity::Navigator nav = wader(map, ecology);
    REQUIRE(nav.grid()->connected(glm::vec2(0.0f), glm::vec2(40.0f, 0.0f)));

    const Crossing now = cross(nav, false);
    INFO("now: " << describe(now));
    CHECK(now.result == "completed");
    CHECK(glm::length(now.end - glm::vec2(40.0f, 0.0f)) < 1.0f);
    // Never in water deeper than it wades, and never at its limit: the head's shallows are its deepest.
    CHECK(now.deepest < 0.5f);
    CHECK(now.inWater == 0.0);
    // Round the head, not across: much further than the straight 40 m.
    CHECK(now.walked > 80.0f);
    // And it walked away from the goal for longer than the stuck clock's four seconds on the way
    // round -- a clock that measured the straight line would have given up -- and did not give up.
    CHECK(now.longestAway > 4.0);

    // Control: the straight line. Into the channel to its wade limit, dithering there, and "stuck".
    const Crossing before = cross(nav, true);
    INFO("the straight line: " << describe(before));
    CHECK(before.result == "failed");
    CHECK(before.reason == "stuck");
    CHECK(before.deepest > 0.6f);
    CHECK(before.inWater > 2.0);
    WARN("ADR-936, across a channel in one region: now " << describe(now) << "; the straight line "
                                                          << describe(before));
}
