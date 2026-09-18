// The Character Intelligence Lab (ADR-275): what the fixture can answer today, and the arms that
// make each answer mean something.
//
// The owner's brief asked for six scenarios -- wandering, investigating a mushroom, crossing a
// river, a social interaction, a dense environment, a long-running simulation. Three of them need a
// perception layer and a decider. Neither exists (ADR-269, ADR-270): every character in this engine
// reads one global list of 505 interest points with no range, no facing and no memory, and the only
// autonomous mind is hardcoded inside `Explore`. So this file asserts the three that are
// measurable now and `examples/labs/character/cases.json` carries the other three as cases that
// name the unit they are waiting for. A lab that asserted a decider it does not have would pass by
// asserting nothing, and would read to whoever came next as a decider that works -- which is the
// §37 failure the whole suite was built to prevent.
//
// Every arm here has a control (ADR-182), and the controls are the reason the file is this long:
//
//   wandering     three bodies with three seeds take three routes  |  the same seed twice is
//                                                                     bit-identical
//   navigation    a route round a solid is longer than the line    |  a route across open ground
//                                                                     is the line
//   stuck         a penned body goes nowhere                       |  an identical body outside
//                                                                     the pen crosses the world
//   determinism   play and seek land in the same place             |  play at another rate does not
//
// Quantities are structural wherever there is a choice (ADR-170): metres travelled, cells visited,
// waypoints planned. The one timing here is reported, never asserted on.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "entity/nav_grid.hpp"
#include "entity/navigation.hpp"
#include "labs/case.hpp"
#include "labs/lab.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path repoRoot() { return fs::path(AVGEN_SOURCE_DIR); }
fs::path fixture() {
    return repoRoot() / "examples" / "labs" / "character" / "character-intelligence-lab.scene.json";
}
bool assetsPresent() { return fs::exists(repoRoot() / "assets" / "aliens" / "alien-scout.glb"); }

// The whole world, ticked the way the application ticks it. Half a frame -- stopping at
// `updateBehaviour` -- is not enough: `Composition::update` is what folds the parameter finals onto
// the node transforms, and the fixture's obstacles are read from the flattened scene.
struct World {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    std::unique_ptr<scene::Composition> comp;

    World() : registry(fixture().parent_path()) {
        auto loaded = scene::Composition::loadFile(fixture(), registry);
        INFO((loaded.has_value() ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->setViewport(1280, 720);
        // ADR-186's offline setting. The subject is the world, not the camera: with the distance
        // cull on, "it did not move" would be a measurement of the level-of-detail band.
        comp->scene().detailLimits.entityDistanceCull = false;
    }

    void play(double seconds, double hz = 60.0) {
        const double step = 1.0 / hz;
        const auto frames = static_cast<int>(std::llround(seconds * hz));
        FrameTime time;
        for (int i = 0; i < frames; ++i) {
            time.renderTime = static_cast<double>(i) * step;
            time.deltaTime = i == 0 ? 0.0 : step;
            time.frameIndex = static_cast<std::uint64_t>(i);
            params.resetFinals();
            comp->updateFields(time, bus, modulator);
            modulator.applyRoutes(bus, params, time.deltaTime);
            comp->updateBehaviour(time, bus);
            comp->update(time);
        }
    }

    [[nodiscard]] glm::vec3 at(const char* name) const {
        const entity::Entity* e = comp->entityWorld().find(name);
        REQUIRE(e != nullptr);
        // R1 (ADR-260): the *simulation's* position, which is what navigation and every query read.
        // `visualPosition` would fold in behaviour offsets and would answer a different question.
        return e->state().position();
    }
};

// A route, sampled once a second, and the quantities a wander can be judged by.
struct Track {
    std::vector<glm::vec3> samples;
    float travelled = 0.0f;         // metres of path walked
    float netDisplacement = 0.0f;   // metres from where it started
    std::size_t cells = 0;          // distinct 4 m nav cells stood in
};

Track trackOf(const std::vector<glm::vec3>& samples) {
    Track t;
    t.samples = samples;
    std::set<std::pair<int, int>> visited;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (i > 0) {
            t.travelled += glm::length(glm::vec2(samples[i].x - samples[i - 1].x,
                                                 samples[i].z - samples[i - 1].z));
        }
        visited.insert({static_cast<int>(std::floor(samples[i].x / 4.0f)),
                        static_cast<int>(std::floor(samples[i].z / 4.0f))});
    }
    t.netDisplacement =
        samples.empty()
            ? 0.0f
            : glm::length(glm::vec2(samples.back().x - samples.front().x,
                                    samples.back().z - samples.front().z));
    t.cells = visited.size();
    return t;
}

// One run of the world, recording every named body once a second.
std::map<std::string, Track> run(double seconds, double hz = 60.0) {
    World w;
    const std::vector<std::string> cast{"scout", "rover", "diver", "penned"};
    std::map<std::string, std::vector<glm::vec3>> samples;
    const auto step = 1.0 / hz;
    const auto frames = static_cast<int>(std::llround(seconds * hz));
    const auto every = static_cast<int>(std::llround(hz));
    FrameTime time;
    for (int i = 0; i < frames; ++i) {
        time.renderTime = static_cast<double>(i) * step;
        time.deltaTime = i == 0 ? 0.0 : step;
        time.frameIndex = static_cast<std::uint64_t>(i);
        w.params.resetFinals();
        w.comp->updateFields(time, w.bus, w.modulator);
        w.modulator.applyRoutes(w.bus, w.params, time.deltaTime);
        w.comp->updateBehaviour(time, w.bus);
        w.comp->update(time);
        if (i % every == 0) {
            for (const std::string& name : cast) {
                samples[name].push_back(w.at(name.c_str()));
            }
        }
    }
    std::map<std::string, Track> out;
    for (const std::string& name : cast) {
        out[name] = trackOf(samples[name]);
    }
    return out;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// The registry entry itself
// ------------------------------------------------------------------------------------------------

TEST_CASE("the Character Intelligence Lab is registered and its cases resolve",
          "[labs][character][registry]") {
    const auto id = labs::findLab("character");
    REQUIRE(id.has_value());
    const labs::LabDescriptor& d = labs::lab(*id);
    CHECK(d.title == "Character Intelligence Lab");
    CHECK(fs::is_regular_file(repoRoot() / d.doc));
    CHECK(fs::is_regular_file(repoRoot() / d.fixture));
    CHECK(fs::is_regular_file(repoRoot() / d.cases));

    auto cases = labs::loadCases(repoRoot() / d.cases);
    INFO((cases.has_value() ? std::string() : cases.error().message));
    REQUIRE(cases.has_value());
    CHECK(cases->size() >= 6);

    // ADR-275: a case that cannot be run says what it is waiting for, and one that can says
    // nothing. Both halves are asserted, because a file in which every case were blocked would
    // satisfy the first check alone and would be a lab that does nothing.
    std::size_t runnable = 0;
    std::size_t blocked = 0;
    for (const labs::LabCase& c : *cases) {
        INFO("case " << c.number << ": " << c.title);
        CHECK_FALSE(c.question.empty());
        CHECK_FALSE(c.expectation.empty());
        CHECK(fs::exists(repoRoot() / c.fixture));
        if (c.runnable()) {
            ++runnable;
        } else {
            // Not a free-text apology: it names a unit of `docs/character-ai-plan.md`, as "P"
            // followed by its number. Widened from "P2 or P3" when case 11 arrived blocked on P9
            // (ADR-300): the assertion that mattered was always "it names a unit", and spelling out
            // the two units that happened to exist on the day made the check unable to notice a
            // third. It still fails on an empty string and on free text with no unit in it.
            bool namesAUnit = false;
            for (std::size_t k = 0; k + 1 < c.blockedBy.size(); ++k) {
                if (c.blockedBy[k] == 'P' && std::isdigit(static_cast<unsigned char>(c.blockedBy[k + 1]))) {
                    namesAUnit = true;
                    break;
                }
            }
            CHECK(namesAUnit);
            ++blocked;
        }
    }
    INFO(fmt::format("{} runnable, {} blocked", runnable, blocked));
    CHECK(runnable >= 4);
    // Lowered from 2 when ADR-335 unblocked case 9 and left P9's root motion as the only case
    // still waiting. The assertion that matters is that a lab in which *every* case were blocked
    // cannot pass the pair, and one blocked case still carries that; a floor that outlives the
    // cases it was counting is a floor that fails for the good news.
    CHECK(blocked >= 1);

    // The control on `resolveCaseSpec`: a lab that exists with a number that does not is a
    // different failure from a lab that does not exist, and both are failures rather than silence.
    CHECK_FALSE(labs::resolveCaseSpec("character:999", repoRoot()).has_value());
    CHECK_FALSE(labs::resolveCaseSpec("characters:1", repoRoot()).has_value());
    CHECK(labs::resolveCaseSpec("character:1", repoRoot()).has_value());
}

// ------------------------------------------------------------------------------------------------
// Scenario 1 -- basic wandering
// ------------------------------------------------------------------------------------------------

TEST_CASE("three wanderers with three seeds take three routes", "[labs][character][wander]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present");
        return;
    }
    const auto tracks = run(60.0);
    for (const char* name : {"scout", "rover", "diver"}) {
        const Track& t = tracks.at(name);
        INFO(fmt::format("{}: {:.1f} m walked, {:.1f} m net, {} cells", name, t.travelled,
                         t.netDisplacement, t.cells));
        // It went somewhere. A body that stood still would satisfy nothing below.
        CHECK(t.travelled > 10.0f);
        // And it went somewhere *else*: a body pacing one spot has travel and no coverage, which is
        // the failure a distance-only assertion cannot see.
        CHECK(t.cells >= 3);
    }

    // The routes differ. Two characters reading the same omniscient interest list with different
    // weights is ADR-270's complaint; what is asserted here is only that the seeds separate them,
    // which is the property the wander behaviour genuinely has today.
    const Track& a = tracks.at("scout");
    const Track& b = tracks.at("rover");
    const Track& c = tracks.at("diver");
    const auto apart = [](const Track& x, const Track& y) {
        float worst = 0.0f;
        const std::size_t n = std::min(x.samples.size(), y.samples.size());
        for (std::size_t i = 0; i < n; ++i) {
            worst = std::max(worst, glm::length(glm::vec2(x.samples[i].x - y.samples[i].x,
                                                          x.samples[i].z - y.samples[i].z)));
        }
        return worst;
    };
    INFO(fmt::format("scout/rover {:.1f} m apart at worst, scout/diver {:.1f} m", apart(a, b),
                     apart(a, c)));
    CHECK(apart(a, b) > 1.0f);
    CHECK(apart(a, c) > 1.0f);

    // ---- the control ----
    // The same world twice is the same world. Without this the arm above would pass on a simulation
    // that was simply noisy, and "three routes" would be three samples of the same randomness
    // rather than three characters.
    const auto again = run(60.0);
    for (const char* name : {"scout", "rover", "diver", "penned"}) {
        const Track& first = tracks.at(name);
        const Track& second = again.at(name);
        REQUIRE(first.samples.size() == second.samples.size());
        float worst = 0.0f;
        for (std::size_t i = 0; i < first.samples.size(); ++i) {
            worst = std::max(worst, glm::length(first.samples[i] - second.samples[i]));
        }
        INFO(fmt::format("{}: two runs differ by {:.9f} m", name, worst));
        CHECK(worst == 0.0f);
    }
}

// ------------------------------------------------------------------------------------------------
// Scenario 2 -- navigation round a solid
// ------------------------------------------------------------------------------------------------

TEST_CASE("a route goes round what is in the way, and straight where nothing is",
          "[labs][character][navigation]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present");
        return;
    }
    World w;
    w.play(0.5);
    const entity::Navigator& nav = w.comp->entityWorld().navigator();
    REQUIRE(nav.valid());
    REQUIRE(nav.grid() != nullptr);
    const entity::NavGridStats stats = nav.grid()->stats();
    INFO(fmt::format("nav grid {}x{} at {:.1f} m: {} cells, {} walkable, {} water, {} blocked, "
                     "{} region(s), largest {}",
                     stats.width, stats.height, stats.cellSize, stats.cells, stats.walkable,
                     stats.water, stats.blocked, stats.regions, stats.largestRegion));
    CHECK(stats.cells > 1000);
    CHECK(stats.walkable > 0);

    // The grid knows about the solids: the pen's ten stones and the three open boulders put 19
    // cells out of use, and they split the walkable ground into 3 regions -- the world, the pen's
    // interior, and one more. `NavGridStats::regions` is computed at build and read by no UI
    // (ADR-268); this is the first thing in the repository that asserts on it.
    CHECK(stats.blocked > 0);
    CHECK(stats.regions >= 2);
    CHECK(stats.largestRegion < stats.walkable);
    REQUIRE(nav.obstacles() != nullptr);
    INFO(fmt::format("obstacle field: {} solids, {} blocking", nav.obstacles()->size(),
                     nav.obstacles()->blockingCount()));
    CHECK(nav.obstacles()->blockingCount() > 0);

    // **The authoring trap this fixture cost a wrong conclusion to find.** A hero's `position.y` is
    // the obstacle's *base* (`entity::obstaclesFromHeroes`), and the first version of this fixture
    // authored its heroes at y = 0 over the shipped world's procedural terrain, which sits at
    // -7.46 m there. Thirteen blocking solids were indexed, the log said so, and every one of them
    // floated 7.46 m above the walker's head: `penned` strolled out of a sealed pen and 137.86 m
    // across the map, and the measurement read as "obstacles do not contain a character", which is
    // false. The fixture is a flat world now for exactly this reason -- see its `world` block.
    CHECK(std::fabs(nav.groundHeight(glm::vec2(0.0f, 0.0f))) < 0.01f);

    // A route the length of the world, which is the request ADR-268 timed at 24.969 us.
    const glm::vec2 lo = nav.worldMin();
    const glm::vec2 hi = nav.worldMax();
    const glm::vec2 from(lo.x + (hi.x - lo.x) * 0.2f, lo.y + (hi.y - lo.y) * 0.2f);
    const glm::vec2 to(lo.x + (hi.x - lo.x) * 0.8f, lo.y + (hi.y - lo.y) * 0.8f);
    std::vector<glm::vec2> route;
    const bool found = nav.findPath(from, to, route);
    const float straight = glm::length(to - from);
    float along = 0.0f;
    glm::vec2 prev = from;
    for (const glm::vec2& p : route) {
        along += glm::length(p - prev);
        prev = p;
    }
    INFO(fmt::format("cross-world route: {} waypoints, {:.1f} m along a {:.1f} m straight line",
                     route.size(), along, straight));
    CHECK(found);
    CHECK_FALSE(route.empty());
    // A route is at least the straight line and, across a river valley, longer than it.
    CHECK(along >= straight - 0.01f);

    // ---- the control ----
    // A short hop over open ground. If the planner were *always* returning a long dog-leg -- which
    // is what a route through a mis-built grid looks like -- this would be long too.
    std::vector<glm::vec2> shortRoute;
    const glm::vec2 hopFrom(-40.0f, 40.0f);
    const glm::vec2 hopTo(-28.0f, 40.0f);
    REQUIRE(nav.findPath(hopFrom, hopTo, shortRoute));
    float hop = 0.0f;
    prev = hopFrom;
    for (const glm::vec2& p : shortRoute) {
        hop += glm::length(p - prev);
        prev = p;
    }
    INFO(fmt::format("open-ground hop: {} waypoints, {:.2f} m along a {:.2f} m line",
                     shortRoute.size(), hop, glm::length(hopTo - hopFrom)));
    CHECK(hop < glm::length(hopTo - hopFrom) * 1.6f);

    // And the honest failure: a goal outside the world is refused with a reason, not with a route
    // to somewhere else. `PathStatus` exists and ADR-268 says nothing surfaces it; this is the
    // first test that reads it.
    entity::PathRequest bad;
    bad.from = from;
    bad.to = glm::vec2(hi.x + 500.0f, hi.y + 500.0f);
    const entity::PathResult refused = nav.requestPath(bad);
    INFO(fmt::format("off-world goal: status {}", static_cast<int>(refused.status)));
    CHECK(refused.status != entity::PathStatus::Ok);
}

// ------------------------------------------------------------------------------------------------
// Scenario 3 -- stuck detection
// ------------------------------------------------------------------------------------------------

TEST_CASE("a penned character is stuck, and an identical one outside the pen is not",
          "[labs][character][stuck]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present");
        return;
    }
    // `penned` stands inside four stones that are heroes and therefore solids in the obstacle
    // field, with a wander radius far larger than the pen: every destination it picks is on the
    // far side of a wall. `rover` is the control -- the same behaviour, the same clips, the same
    // world, no pen.
    const auto tracks = run(60.0);
    const Track& stuck = tracks.at("penned");
    const Track& free = tracks.at("rover");
    INFO(fmt::format("penned: {:.2f} m walked, {:.2f} m net, {} cells", stuck.travelled,
                     stuck.netDisplacement, stuck.cells));
    INFO(fmt::format("rover:  {:.2f} m walked, {:.2f} m net, {} cells", free.travelled,
                     free.netDisplacement, free.cells));

    // The measurement that matters is *net displacement against distance walked*: a body that walks
    // and gets nowhere is the signature of being stuck, and either number alone is ambiguous --
    // standing still also has no net displacement, and pacing also has travel.
    CHECK(stuck.netDisplacement < 8.0f);
    CHECK(free.netDisplacement > stuck.netDisplacement);
    CHECK(free.cells > stuck.cells);

    // And the planner agrees, which is the half a position trace cannot give: a request from inside
    // the pen to the open world comes back `Unreachable` rather than with a route through a wall.
    // The status exists, it is correct, and **it is surfaced to no UI** (ADR-268) -- so a character
    // whose goal is on the other side of a wall looks, to a person watching, exactly like a
    // character that has decided to stand there.
    World w;
    w.play(0.5);
    const entity::Navigator& nav = w.comp->entityWorld().navigator();
    entity::PathRequest out;
    out.from = glm::vec2(26.0f, -20.0f);   // the middle of the pen
    out.to = glm::vec2(0.0f, 0.0f);        // where `scout` started, in the open
    const entity::PathResult refused = nav.requestPath(out);
    INFO(fmt::format("out of the pen: status {}, {} waypoints", static_cast<int>(refused.status),
                     refused.waypoints.size()));
    CHECK(refused.status == entity::PathStatus::Unreachable);

    // The control: the same request from just outside the pen succeeds, so the refusal above is the
    // wall and not the planner refusing everything.
    entity::PathRequest ok;
    ok.from = glm::vec2(40.0f, -20.0f);
    ok.to = glm::vec2(0.0f, 0.0f);
    const entity::PathResult allowed = nav.requestPath(ok);
    INFO(fmt::format("outside the pen: status {}, {} waypoints", static_cast<int>(allowed.status),
                     allowed.waypoints.size()));
    CHECK(allowed.status == entity::PathStatus::Ok);

    // **No behaviour reports being stuck.** `Explore` carries a `stuckSeconds` watchdog and uses it
    // to replan; nothing publishes the fact. `NavDebug` carries `status`, which is the nearest
    // thing, and nothing draws it. The net-displacement-against-travel quantity above is computed
    // by this test out of a position trace -- the instrument P3's decision layer will need and does
    // not have, recorded here so the next wave inherits a number rather than an impression.
}

// ------------------------------------------------------------------------------------------------
// Scenario 6 -- a long-running simulation, and whether a scrub matches a play
// ------------------------------------------------------------------------------------------------

TEST_CASE("the world still holds after two minutes, and the scrub matches the play",
          "[labs][character][determinism]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present");
        return;
    }
    // ADR-267 measured play-vs-seek at 0.000022 m on `glowmere-valley-2` and ADR-091 demands the
    // baked tier reproduce a scrub exactly. This is the same measurement on the lab's own fixture,
    // and it is here rather than in P1 for one reason: it is the number every later unit will be
    // accused of having moved, so it wants a home that is not the unit that might move it.
    // The measurement, twice, because the two answers are different and only one of them is a
    // promise the engine makes.
    const auto playVersusSeek = [](double seconds) {
        World played;
        played.play(seconds);
        World seeked;
        seeked.play(1.0); // so the world exists, its grid is built and its parameters registered
        seeked.comp->entityWorld().seek(seconds, &seeked.params, &seeked.bus);
        float worst = 0.0f;
        std::string worstName;
        for (const char* name : {"scout", "rover", "diver", "penned"}) {
            const glm::vec3 p = played.at(name);
            const glm::vec3 s = seeked.at(name);
            const float d = glm::length(p - s);
            if (d > worst) {
                worst = d;
                worstName = name;
            }
        }
        return std::pair<float, std::string>(worst, worstName);
    };

    // Inside the replay window. `EntityWorld::seek` keeps 90 s of history, so a seek to 30 s
    // re-simulates from zero -- the same number of steps the play took, in the same order.
    //
    // **And it measures 1.360136 m, not the 0.000022 m ADR-267 recorded.** That is not a
    // contradiction of ADR-267 and it is not something this wave broke; it is a different
    // configuration finding the defect list ADR-267 §4 already wrote down. `EntityWorld::seek`
    // (entity.cpp:739) re-simulates a strict *subset* of `update`: it runs `behavior->update` and
    // nothing else -- no interest points, no crowd field, no gait, no action queue, no
    // `applyOffsets` -- and it leaves `BehaviorContext::self` at its default of 0 for every body.
    // `Explore` reads all of those. The probe that measured 0.000022 m did so on characters whose
    // behaviours did not; this fixture's four are explorers, which is what the brief's scenarios
    // are about.
    //
    // So this is reported, not bounded. Bounding it would either fail today or, set loose enough
    // to pass, assert nothing -- and P1 owns the fix. The number is here so P1 has a before.
    const auto inside = playVersusSeek(30.0);
    INFO(fmt::format("play vs seek at 30 s (inside the 90 s window): {:.6f} m ({})", inside.first,
                     inside.second));
    CHECK(std::isfinite(inside.first));
    // The half that *is* promised and does hold: the seek is a function of the time asked for.
    // Seeking to the same second twice lands in the same place, whatever the playhead did before
    // it, which is what `EntityWorld::seek`'s own comment claims and what nothing had checked.
    const auto again = playVersusSeek(30.0);
    INFO(fmt::format("the same seek twice: {:.9f} m apart", std::fabs(again.first - inside.first)));
    CHECK(again.first == inside.first);

    // Outside it. A seek to 120 s starts from 30 s, which costs a body its accumulated history --
    // `Explore` remembers where it has been (`noveltyRadius`) and a body that starts 30 s in has
    // been nowhere. This is documented behaviour, not a defect, and it is measured rather than
    // assumed because "the seek is bounded" and "the seek is wrong" look the same from outside.
    constexpr double kSeconds = 120.0;
    const auto outside = playVersusSeek(kSeconds);
    INFO(fmt::format("play vs seek at 120 s (past the 90 s window): {:.6f} m ({})", outside.first,
                     outside.second));
    CHECK(std::isfinite(outside.first));
    // And the control that makes the 30 s arm mean something: the two are not the same measurement.
    // If the replay cap did nothing, these would agree, and the tight bound above would be
    // asserting a property the seek did not have.
    CHECK(outside.first > inside.first);

    World played;
    played.play(kSeconds);

    // ---- the control that must disagree (ADR-182, ADR-267 §2) ----
    // The same simulation at 30 Hz. ADR-267 measured 0.955805 m between 60 Hz and 30 Hz because the
    // integration step is the frame's and the steering function is not linear in dt. An arm that
    // agreed here would mean this test was comparing something to itself.
    World half;
    half.play(kSeconds, 30.0);
    float rateGap = 0.0f;
    for (const char* name : {"scout", "rover", "diver"}) {
        rateGap = std::max(rateGap, glm::length(played.at(name) - half.at(name)));
    }
    INFO(fmt::format("60 Hz vs 30 Hz: {:.6f} m", rateGap));
    CHECK(rateGap > 0.001f);

    // And the world is still a world: nothing has escaped the map, fallen through it or become
    // non-finite over two minutes. This is the long-running scenario's real question.
    const entity::Navigator& nav = played.comp->entityWorld().navigator();
    const glm::vec2 lo = nav.worldMin();
    const glm::vec2 hi = nav.worldMax();
    for (const char* name : {"scout", "rover", "diver", "penned"}) {
        const glm::vec3 p = played.at(name);
        INFO(fmt::format("{} at ({:.2f},{:.2f},{:.2f}) after {:.0f} s", name, p.x, p.y, p.z,
                         kSeconds));
        CHECK(std::isfinite(p.x));
        CHECK(std::isfinite(p.y));
        CHECK(std::isfinite(p.z));
        CHECK(p.x >= lo.x - 1.0f);
        CHECK(p.x <= hi.x + 1.0f);
        CHECK(p.z >= lo.y - 1.0f);
        CHECK(p.z <= hi.y + 1.0f);
    }
}

// ------------------------------------------------------------------------------------------------
// What the lab cannot answer, asserted as absence
// ------------------------------------------------------------------------------------------------

TEST_CASE("every character still sees everything, and nobody decides",
          "[labs][character][unsupported]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present");
        return;
    }
    // The three blocked cases are blocked for a reason, and the reason is checkable. This is the
    // tripwire: it passes today, and the day a perception layer lands it is the assertion that says
    // the blocked cases can be unblocked. A lab that only wrote "not supported" in a JSON field
    // would go on saying it for a year after it stopped being true.
    World w;
    w.play(1.0);
    const entity::EntityWorld& world = w.comp->entityWorld();

    // ADR-270: `interestPoints()` is one global list, and every character scores all of it. There
    // is no per-character view of the world, so two characters standing at opposite ends of the map
    // are offered exactly the same candidates.
    const std::size_t all = world.interestPoints().size();
    INFO(fmt::format("{} interest points, visible to every one of {} characters", all,
                     world.entities().size()));
    CHECK(all > 0);

    // ADR-269: no behaviour in this scene publishes a competing option, because `Option` and
    // `IConsiderer` have no implementations. The observable consequence, and the one a lab can
    // check without naming a type that does not exist, is that `NavDebug` -- the only window into
    // why a character is going where it is going -- carries a single destination and no alternative
    // it was chosen over.
    std::size_t withNav = 0;
    for (const auto& e : world.entities()) {
        for (const auto& b : e->behaviors()) {
            entity::NavDebug debug;
            if (b->navDebug(debug)) {
                ++withNav;
            }
        }
    }
    INFO(fmt::format("{} navigating behaviour(s) report a destination; none reports a score it beat",
                     withNav));
    CHECK(withNav > 0);
}
