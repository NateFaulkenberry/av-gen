// ADR-935: a body that is not travelling does not walk.
//
// `EntityState::speed` is what the gait picks a clip from, and it persists from step to step: only a
// mover writes it. So a step in which nothing moved the body kept the last number anybody wrote, and
// ADR-620's limiter turned every dead stop into half a second of deceleration the body never made.
// On GV3 (gv3/production, traced by navfix): rook played its walk on the spot for 6.5 s after a
// `holdPost` walk home, a decider's "range -> range" with nothing to do; the E5 horse walked on the
// spot for 3.3 s while the set piece held it in the beam; and every errand refused on its first step
// left a walking alien's legs running down for 0.4-0.8 s where it stood. Now a body the step did not
// move publishes no speed, unless a director named one.
//
// Each case with a reading the engine before ADR-935 cannot give, and a control the rule must leave
// alone (ADR-182):
//
//   stood     a decider whose next choice is to stand -- its option has no actions -- leaves the body
//             publishing no speed and showing no walk  |  control: on the walk before it, the speed
//             is the walk's and the gait shows it
//   gave up   a `move` that gives up "stuck" leaves no speed behind it  |  control: the walk into
//             the water was a walk
//   refused   a walk refused on its first step stops the legs with the body  |  control: a walk
//             that arrives brakes on its own ramp, every frame of which moves the body
//   held      a set piece's hold stands the animal  |  control: its carry up the beam names a speed,
//             and the legs keep going over ground the body is not crossing
//   scrubbed  a seek to the standing and to the hold lands where the play did

#include "entity/action.hpp"
#include "entity/character_quality.hpp"
#include "entity/entity.hpp"
#include "entity/navigation.hpp"
#include "support/cast_world.hpp"
#include "world/ecology.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

using namespace avgen;
using Catch::Approx;
using testsupport::behavior;
using testsupport::CastMember;
using testsupport::CastWorld;

namespace {

// GV3's rook: its gait as generated (tools/gv3/cast.py), with the ramp the scene authors.
entity::GaitSettings rookGait() {
    entity::GaitSettings g;
    g.walkSpeed = 3.0685f;
    g.runSpeed = 7.3892f;
    g.moveEnter = 0.9028f;
    g.moveExit = 0.4514f;
    g.runEnter = 5.1321f;
    g.runExit = 3.4608f;
    g.accel = 4.8151f;
    g.decel = 6.6207f;
    g.accelAuthored = true;
    g.turnRadius = 1.5f;
    g.turnRate = 100.0f;
    return g;
}

entity::ActionDesc moveTo(glm::vec3 to, const char* name = "errand") {
    entity::ActionDesc go;
    go.kind = entity::ActionKind::Move;
    go.name = name;
    go.target.kind = entity::TargetKind::Point;
    go.target.point = to;
    return go;
}

// What a body's legs did against what its body did, frame by frame. "Still" is a frame on which the
// body's drawn place did not move across the ground at all.
struct Legs {
    int frames = 0;
    int still = 0;
    int stillWithSpeed = 0;    // still, and publishing a speed
    int stillWalking = 0;      // still, and the gait showing a walk or a run
    float stillSpeed = 0.0f;   // the most it published on a still frame
    int moving = 0;
    int movingWalking = 0;     // moving, and the gait showing a walk or a run
    float movingSpeed = 0.0f;  // the most it published on a moving frame
    glm::vec2 last{0.0f};
    bool hasLast = false;

    void watch(const entity::Entity& e) {
        const glm::vec3 p = e.locomotion().position;
        const glm::vec2 here(p.x, p.z);
        if (hasLast) {
            ++frames;
            const bool walking = e.locomotion().activity == entity::Activity::Walk ||
                                 e.locomotion().activity == entity::Activity::Run;
            if (here == last) {
                ++still;
                stillWithSpeed += e.state().speed > 0.0f ? 1 : 0;
                stillWalking += walking ? 1 : 0;
                stillSpeed = std::max(stillSpeed, e.state().speed);
            } else {
                ++moving;
                movingWalking += walking ? 1 : 0;
                movingSpeed = std::max(movingSpeed, e.state().speed);
            }
        }
        last = here;
        hasLast = true;
    }
};

std::string describe(const Legs& l) {
    return fmt::format("{} frames: {} still ({} with a speed, up to {:.3f} m/s; {} showing a walk), {} moving "
                       "(up to {:.3f} m/s, {} showing a walk)",
                       l.frames, l.still, l.stillWithSpeed, l.stillSpeed, l.stillWalking, l.moving, l.movingSpeed,
                       l.movingWalking);
}

// ---- stood: a decider's choice to stand -------------------------------------------------------------

// rook's shape: a decider whose post pulls it once it is past 30 m, and nothing else to do but stand.
// Its post is a marker 60 m east, so its first choice is the walk to it, and when that walk ends the
// post is chosen again with nothing left to do -- "range -> range", GV3's rook at 195.42 s.
std::vector<CastMember> postKeeper() {
    CastMember m;
    m.name = "rook";
    m.seed = 11235813;
    m.gait = rookGait();
    m.behaviors.push_back(behavior(
        "decide", {{"hertz", 4.0},
                   {"dwellTicks", 1.0},
                   {"margin", 0.05},
                   {"considerers",
                    {{{"kind", "holdPost"}, {"name", "range"}, {"weight", 0.12}, {"post", "marker"},
                      {"tolerance", 30.0}, {"pull", 0.5}},
                     {{"kind", "idle"}, {"name", "idle"}, {"weight", 0.05}}}}}));
    m.behaviors.push_back(behavior("ground", {{"bodyRadius", 0.9}}));
    CastMember marker;
    marker.name = "marker";
    marker.seed = 7;
    marker.at = glm::vec3(60.0f, 0.0f, 0.0f);
    return {m, marker};
}

struct Stood {
    Legs walks;          // while an errand is under way
    Legs standing;       // after the decider's last walk has ended
    double homeAt = -1.0;
    std::string log;
    entity::CharacterQuality quality;
};

// The walk to the post, to half its tolerance; then, if `interrupt`, that walk is abandoned half way
// for standing (the idle option made worth more than the post, the way an audio-driven weight moves
// one on GV3), which is the latch at a walking pace.
Stood stand(bool interrupt) {
    CastWorld w(postKeeper());
    entity::Entity& rook = w.body("rook");
    Stood out;
    bool home = false;
    w.world.setActionListener([&](const entity::ActionEvent& e) {
        out.log += fmt::format("{:.2f} {} {}{}\n", e.time, e.action, entity::actionResultName(e.result),
                               e.reason.empty() ? "" : " (" + e.reason + ")");
        if (e.action == "range" && e.result == entity::ActionResult::Completed) {
            home = true;
            out.homeAt = e.time;
        }
    });
    auto* idle = w.params.findAs<float>("entity/rook/decide/idle/weight");
    REQUIRE(idle != nullptr);
    bool interrupted = false;
    w.play(60.0, [&] {
        if (interrupt && !interrupted && rook.state().position().x > 20.0f) {
            // Half way there: standing is now worth more than the post.
            idle->setBase(5.0f);
            interrupted = true;
            home = true;
            out.homeAt = w.time();
        }
        (home ? out.standing : out.walks).watch(rook);
    });
    out.quality = w.quality("rook");
    entity::DecisionDebug debug;
    for (const auto& b : rook.behaviors()) {
        if (b->decisionDebug(debug)) {
            break;
        }
    }
    for (const entity::DecisionTraceEntry& e : debug.history) {
        out.log += fmt::format("{:.2f} {} -> {} ({})\n", e.time, e.previous, e.option, e.previousOutcome);
    }
    return out;
}

} // namespace

TEST_CASE("a decider that chooses to stand leaves the body publishing no speed", "[entity][decide][adr935]") {
    // GV3's rook exactly: its walk home ends, the next choice is the post again with nothing to do
    // ("range -> range"), and the body stands.
    const Stood arrived = stand(false);
    INFO("arrived: walks " << describe(arrived.walks) << "\nstanding " << describe(arrived.standing) << "\n"
                           << arrived.log);
    REQUIRE(arrived.homeAt > 0.0);
    REQUIRE(arrived.standing.still > 60 * 20);
    CHECK(arrived.standing.stillWithSpeed == 0);
    CHECK(arrived.standing.stillWalking == 0);

    // The same choice made half way home, from a walking pace: the latch at its worst. Before ADR-935
    // the body stopped dead and published its walking pace for as long as it stood.
    const Stood abandoned = stand(true);
    INFO("abandoned: walks " << describe(abandoned.walks) << "\nstanding " << describe(abandoned.standing) << "\n"
                             << abandoned.log);
    REQUIRE(abandoned.homeAt > 0.0);
    REQUIRE(abandoned.standing.still > 60 * 20);
    CHECK(abandoned.standing.stillWithSpeed == 0);
    CHECK(abandoned.standing.stillWalking == 0);
    // ADR-910's stuck time -- intending over 0.3 m/s, covering under 0.05 m/s -- over the whole run.
    CHECK(abandoned.quality.behaviour.stuckSeconds == 0.0);
    CHECK(arrived.quality.behaviour.stuckSeconds == 0.0);

    // Control: on the walks the speed is the walk's and the gait shows it, so the reading above is of
    // a body that was walking and then stood, not of one that never moved.
    CHECK(abandoned.walks.movingSpeed == Approx(rookGait().walkSpeed).margin(0.05));
    CHECK(abandoned.walks.movingWalking > 60 * 5);
    CHECK(arrived.walks.movingWalking > 60 * 12); // 45 m to half the post's tolerance, at 3.07 m/s
    WARN("ADR-935, a decider's choice to stand: arrived " << describe(arrived.standing) << "; abandoned "
                                                          << describe(abandoned.standing) << "; stuck "
                                                          << arrived.quality.behaviour.stuckSeconds << " s and "
                                                          << abandoned.quality.behaviour.stuckSeconds << " s");
}

namespace {

// ---- gave up and refused: the move's own ends -----------------------------------------------------

// A flat world with a deep channel at x = +30 whose head is inside the world (one region), walkers
// wading to 0.8 m: the fixture ADR-933's stall uses.
world::WorldMap channelWorld() {
    world::WorldMap map;
    map.name = "a channel";
    map.size = glm::vec2(240.0f, 240.0f);
    map.baseHeight = 0.0f;
    world::Feature river;
    river.name = "river";
    river.kind = world::FeatureKind::River;
    river.path = {{30.0f, 0.0f, -130.0f}, {30.0f, 0.0f, 40.0f}};
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

// The straight line whatever lies between, as `NavigatorPath` answered before ADR-936, so a walk
// into the channel still stalls there and gives up.
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

CastMember walker(const entity::GaitSettings& gait) {
    CastMember m;
    m.name = "walker";
    m.seed = 4242;
    m.gait = gait;
    m.behaviors.push_back(behavior("ground", {{"bodyRadius", 0.5}}));
    return m;
}

} // namespace

TEST_CASE("a move that gives up leaves no speed behind it", "[entity][action][adr935]") {
    const world::WorldMap map = channelWorld();
    world::Ecology ecology;
    const entity::Navigator nav = wader(map, ecology);
    entity::GaitSettings gait;
    gait.walkSpeed = 2.0f; // no ramp authored: the latch needs no limiter
    CastWorld w({walker(gait)}, &nav);
    StraightLine straight(&w.world.navigator());
    w.world.setPathProvider(&straight);
    entity::Entity& body = w.body("walker");
    body.actions().push(std::vector<entity::ActionDesc>{moveTo(glm::vec3(40.0f, 0.0f, 0.0f))}, entity::Authority::Action);
    double gaveUp = -1.0;
    std::string reason;
    w.world.setActionListener([&](const entity::ActionEvent& e) {
        if (e.result == entity::ActionResult::Failed && gaveUp < 0.0) {
            gaveUp = e.time;
            reason = e.reason;
        }
    });
    Legs before;
    Legs after;
    w.play(40.0, [&] { (gaveUp < 0.0 ? before : after).watch(body); });
    INFO("gave up at " << gaveUp << " (" << reason << "); before " << describe(before) << "; after " << describe(after));
    REQUIRE(gaveUp > 0.0);
    REQUIRE(reason == "stuck");
    REQUIRE(after.still > 60 * 10);
    // Standing in the water it gave up in, and saying so. Before ADR-935 it went on publishing the
    // pace of its last stride (1.46 m/s, measured) for the rest of the run.
    CHECK(after.stillWithSpeed == 0);
    CHECK(after.stillWalking == 0);
    // Control: the walk into the water was a walk.
    CHECK(before.movingSpeed == Approx(gait.walkSpeed).margin(0.01));
    CHECK(before.movingWalking > 60 * 5);
}

TEST_CASE("a walk refused on its first step stops the legs with the body", "[entity][action][adr935]") {
    const world::WorldMap map = channelWorld();
    world::Ecology ecology;
    const entity::Navigator nav = wader(map, ecology);
    const auto run = [&](bool refuse) {
        CastWorld w({walker(rookGait())}, &nav);
        entity::Entity& body = w.body("walker");
        entity::EntityWorld& world = w.world;
        // South along the bank at full pace; at 6 s, an order to a point in the channel's deep middle,
        // which no walker can stand on, is refused on its first step (`route`: unreachable). The
        // control's order is to a point 6 m ahead, which it walks to and arrives at.
        body.actions().push(std::vector<entity::ActionDesc>{moveTo(glm::vec3(0.0f, 0.0f, -80.0f))},
                            entity::Authority::Action);
        bool ordered = false;
        w.director = [&](double now) {
            if (!ordered && now >= 6.0) {
                ordered = true;
                const glm::vec3 p = world.find("walker")->state().position();
                const glm::vec3 to = refuse ? glm::vec3(30.0f, 0.0f, p.z) : glm::vec3(p.x, 0.0f, p.z - 6.0f);
                world.find("walker")->actions().override(std::vector<entity::ActionDesc>{moveTo(to, "order")},
                                                         entity::Authority::Action, now);
            }
        };
        std::string ended;
        world.setActionListener([&](const entity::ActionEvent& e) {
            if (e.action == "order") {
                ended = fmt::format("{} at {:.2f}{}", static_cast<int>(e.result), e.time,
                                    e.reason.empty() ? "" : " (" + e.reason + ")");
            }
        });
        Legs before;
        Legs after;
        float pace = 0.0f;
        w.play(10.0, [&] {
            if (w.time() < 6.0) {
                before.watch(body);
                pace = body.state().speed;
            } else {
                after.watch(body);
            }
        });
        return std::tuple{before, after, pace, ended};
    };
    const auto [before, after, pace, ended] = run(true);
    INFO("refused (" << ended << "): at " << pace << " m/s before; after " << describe(after));
    REQUIRE(pace == Approx(rookGait().walkSpeed).margin(0.01));
    REQUIRE(after.still > 60 * 3);
    // Stopped dead, legs and body alike. Before ADR-935 the limiter ran the legs down from the walk
    // over the next 0.46 s, on the spot: rook's gait keeps the walk above 0.45 m/s for 0.4 s of it.
    CHECK(after.stillWithSpeed == 0);
    CHECK(after.stillWalking == 0);

    // Control: an order the body can walk to. It brakes into it on its own ramp, and every frame of
    // the ramp moves the body, so the rule leaves every one of them alone.
    const auto [cBefore, cAfter, cPace, cEnded] = run(false);
    INFO("arrived (" << cEnded << "): after " << describe(cAfter));
    CHECK(cAfter.moving > 10);
    CHECK(cAfter.movingSpeed > 1.0f);
    CHECK(cAfter.stillWithSpeed == 0);
    WARN("ADR-935, a walk refused on its first step: " << describe(after) << "; control, an arrival: "
                                                       << describe(cAfter));
}

namespace {

// ---- held: a set piece's two ways of owning a body ------------------------------------------------

constexpr double kHoldAt = 3.0;
constexpr double kCarryAt = 6.0;
constexpr double kReleaseAt = 8.0;

// A grazing cow; a director (as staging is one) that holds it where it stands from 3 s, as a `follow`
// with `hold` and no rate does, and carries it up from 6 s at a named gait speed, as the `lift` does.
std::vector<CastMember> grazer() {
    CastMember cow;
    cow.name = "cow";
    cow.seed = 1107;
    cow.gait.walkSpeed = 1.0f;
    cow.behaviors.push_back(behavior("wander", {{"speed", 1.0}, {"minRange", 6.0}, {"maxRange", 10.0},
                                                {"pauseMin", 0.2}, {"pauseMax", 0.4}, {"homeRadius", 12.0}}));
    cow.behaviors.push_back(behavior("ground", {{"bodyRadius", 1.2}}));
    return {cow};
}

std::function<void(double)> beam(entity::EntityWorld& world) {
    auto held = std::make_shared<glm::vec3>(0.0f);
    auto caught = std::make_shared<bool>(false);
    return [&world, held, caught](double now) {
        entity::Entity* cow = world.find("cow");
        if (now < kHoldAt || now >= kReleaseAt) {
            cow->setDirectorMotion(entity::DirectorMotion{});
            return;
        }
        if (!*caught) {
            *held = cow->state().position();
            *caught = true;
        }
        entity::DirectorMotion motion;
        motion.active = true;
        motion.position = *held;
        if (now >= kCarryAt) {
            motion.position.y += static_cast<float>(now - kCarryAt); // up the beam at 1 m/s
            motion.speed = 0.7f;
            motion.hasSpeed = true;
        }
        cow->setDirectorMotion(motion);
    };
}

} // namespace

TEST_CASE("a set piece's hold stands the animal, and its carry keeps the legs going", "[entity][staging][adr935]") {
    CastWorld w(grazer());
    w.director = beam(w.world);
    entity::Entity& cow = w.body("cow");
    Legs grazing;
    Legs held;
    Legs carried;
    int carriedAtRate = 0;
    float speedAtHold = 0.0f;
    w.play(kReleaseAt, [&] {
        const double t = w.time() - CastWorld::kStep; // the step just taken
        if (t < kHoldAt) {
            grazing.watch(cow);
            speedAtHold = cow.state().speed;
        } else if (t < kCarryAt) {
            held.watch(cow);
        } else {
            carried.watch(cow);
            carriedAtRate += cow.state().speed == Approx(0.7f).margin(1e-4) ? 1 : 0;
        }
    });
    INFO("grazing " << describe(grazing) << "\nheld " << describe(held) << "\ncarried " << describe(carried)
                    << "\nwalking at " << speedAtHold << " m/s when caught");
    REQUIRE(grazing.movingWalking > 30);
    REQUIRE(speedAtHold > 0.5f); // caught mid-stride, which is what the latch needs
    REQUIRE(held.still > 60 * 2);
    // Held in the light, standing. Before ADR-935 the hold named no speed, `wander` yielded to it
    // without writing one, and the cow walked on the spot at its last stride for the whole hold (GV3's
    // E5 horse, 3.3 s at 1.61 m/s).
    CHECK(held.stillWithSpeed == 0);
    CHECK(held.stillWalking == 0);
    // Control: the carry names its speed, and a director that names one keeps it -- legs going up the
    // beam over ground the body is not crossing, exactly as the set piece asked.
    CHECK(carriedAtRate >= static_cast<int>((kReleaseAt - kCarryAt) * 60.0) - 2);
    CHECK(carried.stillWalking > 60);
}

TEST_CASE("a scrub to a standing body and to a held one lands where the play did", "[entity][adr935][seek]") {
    // The hold, sought mid-hold and mid-carry, against the play.
    for (const double at : {kHoldAt + 1.5, kCarryAt + 1.0}) {
        CastWorld played(grazer());
        played.director = beam(played.world);
        played.play(at);
        CastWorld sought(grazer());
        const auto director = beam(sought.world);
        sought.director = director;
        sought.step();
        entity::EntityWorld::SeekHooks hooks;
        hooks.before = [&director](double now, double) { director(now); };
        sought.world.seek(played.time() - CastWorld::kStep, &sought.params, &sought.bus, CastWorld::kStep, {}, &hooks);
        const entity::Entity& a = played.body("cow");
        const entity::Entity& b = sought.body("cow");
        INFO("at " << at << " s: played speed " << a.state().speed << " sought " << b.state().speed);
        CHECK(glm::length(a.state().position() - b.state().position()) < 1e-3f);
        CHECK(a.state().speed == Approx(b.state().speed).margin(1e-5));
        CHECK(a.locomotion().activity == b.locomotion().activity);
    }

    // And the refused walk, sought after it: stood, with no speed, where the play stood.
    const world::WorldMap map = channelWorld();
    world::Ecology ecology;
    const entity::Navigator nav = wader(map, ecology);
    const auto order = [](entity::EntityWorld& world) {
        return [&world](double now) {
            entity::Entity* body = world.find("walker");
            if (now < 1e-6) {
                body->actions().override(std::vector<entity::ActionDesc>{moveTo(glm::vec3(0.0f, 0.0f, -80.0f))},
                                         entity::Authority::Action, now);
            } else if (std::abs(now - 6.0) < 1e-6) {
                const glm::vec3 p = body->state().position();
                body->actions().override(
                    std::vector<entity::ActionDesc>{moveTo(glm::vec3(30.0f, 0.0f, p.z), "order")},
                    entity::Authority::Action, now);
            }
        };
    };
    CastWorld played({walker(rookGait())}, &nav);
    played.director = order(played.world);
    played.play(8.0);
    CastWorld sought({walker(rookGait())}, &nav);
    const auto director = order(sought.world);
    sought.director = director;
    sought.step();
    entity::EntityWorld::SeekHooks hooks;
    hooks.before = [&director](double now, double) { director(now); };
    sought.world.seek(played.time() - CastWorld::kStep, &sought.params, &sought.bus, CastWorld::kStep, {}, &hooks);
    const entity::Entity& a = played.body("walker");
    const entity::Entity& b = sought.body("walker");
    CHECK(a.state().speed == 0.0f);
    CHECK(b.state().speed == 0.0f);
    CHECK(glm::length(a.state().position() - b.state().position()) < 1e-3f);
    CHECK(a.locomotion().activity == b.locomotion().activity);
}
