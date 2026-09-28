// ADR-933: no pacing on urgent options.
//
// ADR-909's habits -- no walk straight back to where the body set out from, no turning round
// mid-walk -- never touched an option with urgency: a reaction, a flinch out of someone's way. So when
// a reaction's walk could not get there, the reaction was as free to send the body back as it was the
// first time. GV3's ember heard E5's beam across the river, walked in to its wade limit and stalled;
// its post, pulling harder the further it strayed, walked it home; the reaction won again and turned
// it round: walk, stop, turn round, walk back (gv3-cast, iteration 1; this stream's trace of it).
//
// Now an urgent option keeps its exemption for its first attempt at a subject. Once that walk has
// failed, or been given up stalled, the same subject cannot send the body back to that place while the
// failure is remembered (the mind's `failSeconds`), and anywhere else only under the habits. Each case
// with a control that must read the opposite (ADR-182):
//
//   rule      the selector, option by option: a retry to the failed place is not on offer; a retry
//             elsewhere is held to the turn-back rule; a first attempt at another subject -- a flinch
//             -- is not  |  with nothing remembered, every one of them keeps its exemption
//   stall     a reaction that stalls in the water on ground the river does not divide (ADR-932 does
//             not apply; the straight line ADR-936 replaced makes the stall): the body turns back
//             once and does not pace  |  `failSeconds` 0, the memory
//             off: it paces, walk, stop, turn round, walk back, again and again
//   across    GV3's case, a reaction to a beam across a river that runs edge to edge: the body walks
//             to the bank and watches; no reversal, no pacing  |  the old straight-line route with
//             the memory off: the loop gv3-cast measured
//   flinch    a body walked at by another steps out of its way  |  the same body with no reason to
//             flinch is barged, not flinching

#include "entity/action.hpp"
#include "entity/character_quality.hpp"
#include "entity/decision.hpp"
#include "entity/entity.hpp"
#include "entity/mind.hpp"
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
using testsupport::behavior;
using testsupport::CastMember;
using testsupport::CastWorld;

namespace {

// ---- the rule, one option at a time ---------------------------------------------------------------

// Options a test lists by hand: a name, a subject, where the walk goes, and how urgent it is.
struct Listed final : public entity::IConsiderer {
    struct Entry {
        std::string name;
        std::uint64_t subject = 0;
        glm::vec3 to{0.0f};
        float urgency = 0.0f;
    };
    std::vector<Entry> entries;
    void consider(const entity::DecisionContext& ctx, std::vector<entity::Option>& out) const override {
        (void)ctx;
        actions_.clear();
        actions_.reserve(entries.size());
        for (const Entry& e : entries) {
            entity::ActionDesc walk;
            walk.kind = entity::ActionKind::Move;
            walk.target.kind = entity::TargetKind::Point;
            walk.target.point = e.to;
            walk.tolerance = 1.0f;
            actions_.push_back(walk);
        }
        for (std::size_t i = 0; i < entries.size(); ++i) {
            out.push_back(entity::Option{entries[i].name, 1.0f, std::span<const entity::ActionDesc>(&actions_[i], 1),
                                         entity::Authority::Routine});
            out.back().subject = entries[i].subject;
            out.back().urgency = entries[i].urgency;
        }
    }

private:
    mutable std::vector<entity::ActionDesc> actions_;
};

// ---- the river fixtures --------------------------------------------------------------------------

// A flat world with a deep channel at x = +30, from beyond the south edge to `head` (beyond the
// north edge: it divides the world; inside it: the banks join round its head). Its walkers wade to
// 0.8 m, as GV3's do to 0.85, so the channel's margins are walkable and its middle is not.
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

// The route `NavigatorPath` gave before ADR-932: the straight line, `Ready`, for any standable goal.
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

// ember's shape, scaled to the fixture: an aware decider with a post it is pulled home to harder the
// further it strays, a reaction to a beam, and nothing else to do. Placed on the west bank. Two
// settings keep the fixture to its question: the beam does not grow stale to it (a long
// habituation), so nothing but the rule under test can stop it being sent again; and its `flee` is
// no distance at all -- stand, face it and watch -- so a reaction it cannot walk is still one it can
// have where it stands.
CastMember watcher(double failSeconds) {
    CastMember m;
    m.name = "watcher";
    m.seed = 4242;
    m.at = glm::vec3(0.0f, 0.0f, 0.0f);
    m.gait.walkSpeed = 2.0f;
    m.behaviors.push_back(behavior(
        "decide", {{"hertz", 2.0},
                   {"dwellTicks", 1.0},
                   {"margin", 0.05},
                   {"mind",
                    {{"memory",
                      {{"eventSeconds", 90.0}, {"failSeconds", failSeconds}, {"habituationSeconds", 600.0}}}}},
                   {"considerers",
                    {{{"kind", "react"}, {"name", "beam"}, {"events", {"beam"}}, {"weight", 1.5}, {"approach", 6.0},
                      {"flee", 0.0}, {"dwell", 2.0}, {"fadeSeconds", 80.0}},
                     {{"kind", "holdPost"}, {"name", "home"}, {"weight", 0.5}, {"tolerance", 6.0}, {"pull", 0.12},
                      {"activity", "observe"}},
                     {{"kind", "idle"}, {"name", "idle"}, {"weight", 0.05}}}}}));
    return m;
}

struct Watched {
    entity::CharacterQuality quality;
    float deepest = 0.0f;     // the deepest water it stood in
    float nearestBeam = 1e9f; // the nearest it came to the beam
    int waterTrips = 0;       // separate walks into water over 0.3 m deep
    int shoreTrips = 0;       // separate walks down to the river's edge (x past 12 m, back past 8 m)
    glm::vec2 end{0.0f};
    std::string decisions;    // its choices, for a failing case's message
};

// A beam across the river 40 m east of the watcher, raised at 1 s, and `seconds` of the film.
Watched watchBeam(const entity::Navigator& nav, double failSeconds, bool straightLine, double seconds) {
    CastWorld w({watcher(failSeconds)}, &nav);
    StraightLine old(&w.world.navigator());
    if (straightLine) {
        w.world.setPathProvider(&old);
    }
    const glm::vec3 beam(46.0f, 0.0f, 0.0f);
    const std::uint32_t type = w.world.eventType("beam");
    bool raised = false;
    bool wet = false;
    bool ashore = false;
    Watched out;
    w.play(seconds, [&] {
        if (!raised && w.time() >= 1.0) {
            entity::WorldEvent e;
            e.type = type;
            e.position = beam;
            e.radius = 200.0f;
            e.magnitude = 1.0f;
            e.time = w.time();
            w.world.emitEvent(e);
            raised = true;
        }
        const glm::vec3 p = w.body("watcher").state().position();
        const float depth = w.world.navigator().terrain().waterDepthAt(glm::vec2(p.x, p.z));
        out.deepest = std::max(out.deepest, depth);
        out.nearestBeam = std::min(out.nearestBeam, glm::length(glm::vec2(p.x - beam.x, p.z - beam.z)));
        if (!wet && depth > 0.3f) {
            ++out.waterTrips;
        }
        wet = depth > 0.3f ? true : (depth <= 0.0f ? false : wet);
        if (!ashore && p.x > 12.0f) {
            ++out.shoreTrips;
            ashore = true;
        } else if (ashore && p.x < 8.0f) {
            ashore = false;
        }
    });
    out.quality = w.quality("watcher");
    const glm::vec3 p = w.body("watcher").state().position();
    out.end = glm::vec2(p.x, p.z);
    entity::DecisionDebug debug;
    for (const auto& b : w.body("watcher").behaviors()) {
        if (b->decisionDebug(debug)) {
            break;
        }
    }
    for (const entity::DecisionTraceEntry& e : debug.history) {
        out.decisions += fmt::format("{:.2f} {} -> {} ({})\n", e.time, e.previous, e.option, e.previousOutcome);
    }
    return out;
}

std::string summary(const Watched& w) {
    const entity::CharacterBehaviourMetrics& b = w.quality.behaviour;
    return fmt::format("stops {}, reversals {}, turned back {}, longest pacing {} ({:.1f} s from {:.1f} s), deepest "
                       "water {:.2f} m, {} walk(s) into the water, {} down to the river, nearest the beam {:.1f} m, "
                       "ended at ({:.1f}, {:.1f})\n{}",
                       b.stops, b.reversals, b.turnsOver90, b.longestPacing, b.longestPacingSeconds,
                       b.longestPacingFrom, w.deepest, w.waterTrips, w.shoreTrips, w.nearestBeam, w.end.x, w.end.y,
                       w.decisions);
}

} // namespace

TEST_CASE("an urgent option keeps its exemption for its first attempt, and not for a second",
          "[decide][adr933]") {
    // The body at (10, 0), facing east and walking an errand there; it set out from home at (0, 0)
    // five seconds ago. A reaction to subject 101 walked it toward (-20, 5), behind it, and failed.
    Listed listed;
    listed.entries = {{"ahead", 0, glm::vec3(30.0f, 0.0f, 0.0f), 0.0f},
                      {"retry-there", 101, glm::vec3(-20.0f, 0.0f, 5.0f), 0.8f},   // back where it failed
                      {"retry-behind", 101, glm::vec3(-20.0f, 0.0f, -30.0f), 0.8f}, // elsewhere, but behind
                      {"retry-aside", 101, glm::vec3(10.0f, 0.0f, 25.0f), 0.8f},    // elsewhere, to the side
                      {"flinch", 202, glm::vec3(-20.0f, 0.0f, 5.0f), 0.8f}};        // another subject
    const entity::IConsiderer* views[] = {&listed};
    entity::EntityState state;
    state.travel = glm::vec3(10.0f, 0.0f, 0.0f);
    state.yaw = 1.5707963f;
    const entity::Departure left{glm::vec2(0.0f), 0.0};
    const entity::Attempt failed{101, glm::vec2(-20.0f, 5.0f), 1.0f, 3.0};
    const auto scores = [&](float speed, bool remembered) {
        state.speed = speed;
        entity::Selector selector;
        entity::SelectorSettings settings;
        settings.hertz = 0.0f;
        selector.setSettings(settings);
        entity::DecisionContext ctx;
        ctx.time = 5.0;
        ctx.state = &state;
        ctx.departures = std::span<const entity::Departure>(&left, 1);
        ctx.loopSeconds = 20.0;
        if (remembered) {
            ctx.attempts = std::span<const entity::Attempt>(&failed, 1);
        }
        // Walking an errand east, so there is one in hand for the turn-back rule.
        entity::DecisionContext first = ctx;
        first.time = 4.5;
        first.departures = {};
        first.attempts = {};
        first.loopSeconds = 0.0;
        Listed only;
        only.entries = {{"ahead", 0, glm::vec3(30.0f, 0.0f, 0.0f), 0.0f}};
        const entity::IConsiderer* one[] = {&only};
        (void)selector.select(first, one);
        (void)selector.select(ctx, views);
        std::map<std::string, float> out;
        for (const entity::Option& o : selector.options()) {
            out[std::string(o.name)] = o.score;
        }
        return out;
    };
    // Walking, with the failure remembered.
    auto walking = scores(2.0f, true);
    CHECK(walking["retry-there"] == 0.0f);          // not back where it failed, ever
    CHECK(walking["retry-behind"] == 0.0f);         // a second attempt is a habit: no turning round for it
    CHECK(walking["retry-aside"] == Approx(1.0f));  // ...and a habit that breaks no rule is on offer
    CHECK(walking["flinch"] == Approx(1.0f));       // a first attempt at another subject keeps its exemption
    CHECK(walking["ahead"] == Approx(1.0f));
    // Standing: nothing is a turn back, and the place it failed is still refused.
    auto standing = scores(0.0f, true);
    CHECK(standing["retry-there"] == 0.0f);
    CHECK(standing["retry-behind"] == Approx(1.0f));
    CHECK(standing["flinch"] == Approx(1.0f));
    // Control: nothing remembered, and every urgent option keeps its exemption -- including the walk
    // back to where the failure was, turning the body round mid-walk to do it. The loop.
    auto forgot = scores(2.0f, false);
    CHECK(forgot["retry-there"] == Approx(1.0f));
    CHECK(forgot["retry-behind"] == Approx(1.0f));
    CHECK(forgot["flinch"] == Approx(1.0f));
}

TEST_CASE("a reaction that stalls in the water does not send the body back to it", "[decide][adr933]") {
    // The river ends inside this world, so its banks are one region and ADR-932 leaves them alone.
    // The stall this case is about -- a reaction walks the body into the water to its wade limit,
    // where it stalls -- is the straight line's, which every same-region walk took until ADR-936 sent
    // it round the channel's head. So both arms walk the straight line: what is under test is what the
    // memory does after a stall, and the stall is the fixture.
    const world::WorldMap map = riverWorld(40.0f);
    world::Ecology ecology;
    const entity::Navigator nav = wader(map, ecology);
    REQUIRE(nav.grid() != nullptr);
    REQUIRE(nav.grid()->connected(glm::vec2(0.0f), glm::vec2(46.0f, 0.0f)));

    const Watched now = watchBeam(nav, 30.0, true, 90.0);
    INFO("now: " << summary(now));
    const Watched before = watchBeam(nav, 0.0, true, 90.0);
    INFO("control (failSeconds 0): " << summary(before));
    // The fixture does what the defect needs: the reaction took the body into the water.
    CHECK(now.deepest > 0.3f);
    // Tried once and gave up: one walk into the water, and it was not sent back. What pacing is left
    // is the one walk's own: stalled at its wade limit, the `move` dithers for the four seconds its
    // stuck clock allows before it gives up (two turns in 2.4 s, measured) -- which ADR-933 does not
    // change, and which the same stall shows in the control's first trip.
    CHECK(now.waterTrips == 1);
    CHECK(now.shoreTrips == 1);
    CHECK(now.quality.behaviour.longestPacing <= 2);
    CHECK(now.quality.behaviour.reversals <= 2);
    // Control: with the failure forgotten at once, the same beam sends it back into the same water:
    // home, water, home, water -- the loop, a run of turn-backs across it.
    CHECK(before.waterTrips >= 2);
    CHECK(before.quality.behaviour.longestPacing >= 3);
    CHECK(before.quality.behaviour.reversals > now.quality.behaviour.reversals);
    WARN("ADR-933, a stalled reaction: now " << summary(now) << "control " << summary(before));
}

TEST_CASE("a reaction to a beam across a river that runs edge to edge: to the bank, and watch",
          "[decide][adr932][adr933]") {
    // GV3's E5 as gv3-cast measured it: the beam is across a river that divides the world.
    const world::WorldMap map = riverWorld(130.0f);
    world::Ecology ecology;
    const entity::Navigator nav = wader(map, ecology);
    REQUIRE_FALSE(nav.grid()->connected(glm::vec2(0.0f), glm::vec2(46.0f, 0.0f)));

    const Watched now = watchBeam(nav, 30.0, false, 90.0);
    INFO("now: " << summary(now));
    // To the bank, dry, once, and watched from there: the bank is 28 m from the beam, and a
    // reaction's walk counts as there within half its approach. No walk, stop, turn round, walk back
    // to the water, however long the beam is remembered: the one turn it may make is its walk home
    // to its post when it has watched.
    CHECK(now.deepest == 0.0f);
    CHECK(now.nearestBeam < 32.0f);
    CHECK(now.shoreTrips == 1);
    CHECK(now.quality.behaviour.reversals <= 1);
    CHECK(now.quality.behaviour.longestPacing <= 1);

    // Control: the engine before this stream -- the straight line into the water, and a failure
    // remembered by nothing. The loop gv3-cast measured on ember: in, out, in again.
    const Watched before = watchBeam(nav, 0.0, true, 90.0);
    INFO("control: " << summary(before));
    CHECK(before.deepest > 0.3f);
    CHECK(before.waterTrips >= 2);
    CHECK(before.quality.behaviour.longestPacing >= 3);
    CHECK(before.quality.behaviour.reversals >= 3);
    WARN("ADR-932/933, a beam across the river: now " << summary(now) << "control " << summary(before));
}

TEST_CASE("a flinch out of someone's way still works", "[decide][adr933][social]") {
    // A body standing where another is about to walk. The walker goes straight through its spot; a
    // body that means to get out of the way steps aside before it arrives (`social`'s avoid, urgent
    // by how close the other is), and did before this ADR as it does now.
    const auto run = [](bool flinches) {
        CastMember stander;
        stander.name = "stander";
        stander.seed = 77;
        stander.at = glm::vec3(0.0f, 0.0f, 0.0f);
        stander.perceives = true;
        stander.perception.range = 30.0f;
        stander.perception.fieldOfView = 360.0f;
        stander.gait.walkSpeed = 2.0f;
        stander.behaviors.push_back(behavior("ground", {{"bodyRadius", 0.5}}));
        nlohmann::json considerers = nlohmann::json::array();
        if (flinches) {
            considerers.push_back({{"kind", "social"}, {"name", "company"}, {"tags", {"walker"}}, {"weight", 1.0},
                                   {"distance", 3.0}, {"personalSpace", 3.0}, {"keepAway", 5.0}, {"dwell", 0.5},
                                   {"sociabilityPull", 0.0}});
        }
        considerers.push_back({{"kind", "idle"}, {"name", "idle"}, {"weight", 0.05}});
        stander.behaviors.push_back(
            behavior("decide", {{"hertz", 4.0}, {"dwellTicks", 1.0}, {"mind", nlohmann::json::object()},
                                {"considerers", considerers}}));
        CastMember walker;
        walker.name = "walker";
        walker.seed = 78;
        walker.tags = {"walker"};
        walker.at = glm::vec3(-12.0f, 0.0f, 0.3f);
        walker.gait.walkSpeed = 1.5f;
        walker.behaviors.push_back(behavior("ground", {{"bodyRadius", 0.5}}));
        CastWorld w({stander, walker});
        entity::ActionDesc through;
        through.kind = entity::ActionKind::Move;
        through.target.kind = entity::TargetKind::Point;
        through.target.point = glm::vec3(12.0f, 0.0f, 0.3f);
        w.body("walker").actions().push(std::vector<entity::ActionDesc>{through}, entity::Authority::Action);
        float stepped = 0.0f;
        bool avoided = false;
        w.play(12.0, [&] {
            const glm::vec3 s = w.body("stander").state().position();
            stepped = std::max(stepped, glm::length(glm::vec2(s.x, s.z)));
            entity::DecisionDebug debug;
            for (const auto& b : w.body("stander").behaviors()) {
                if (b->decisionDebug(debug)) {
                    break;
                }
            }
            avoided = avoided || debug.chosen == "company/avoid";
        });
        return std::pair<float, bool>{stepped, avoided};
    };
    const auto [stepped, avoided] = run(true);
    INFO("flinching: stepped " << stepped << " m, chose to avoid: " << avoided);
    CHECK(avoided);        // it chose to get out of the way
    CHECK(stepped > 1.5f); // and did: over a metre and a half, on its own feet
    const auto [pushed, never] = run(false);
    INFO("control: moved " << pushed << " m");
    CHECK_FALSE(never);
    CHECK(pushed < 1.0f);  // only the crowd's push moved it
    WARN(fmt::format("ADR-933, a flinch: stepped {:.2f} m (chose to avoid: {}); control moved {:.2f} m", stepped,
                     avoided, pushed));
}
