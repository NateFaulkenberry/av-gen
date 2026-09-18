// Route pricing (ADR-335): the fifth stock considerer, and the two-sided control it exists for.
//
// The Character Intelligence Lab's case 9 -- "does a character choose between fording a river and
// walking round it, and can it say why" -- survived P3 unanswered. ADR-333 delivered the option
// list, the selector and four stock considerers, and none of the four can express a preference
// between two *ways* to the same place: `holdPost` and `investigate` score where a thing is,
// `interest` scores taste times nearness, and nearness is a straight line. A character with a
// river between it and a glow patch scores it exactly as it scores one on the same bank.
//
// `RouteConsiderer` asks `Navigator::requestPath` twice at two `NavPathCost::wadePenalty` values --
// one that finds the ford, one that finds the way round -- and scores both with the character's
// own price on a wet metre. The whole claim is that **the crossover is arithmetic**, so the arms
// here are the two sides of it (ADR-182):
//
//   the taste       the detour wins at a high wade penalty  |  and loses at a low one, on the
//                                                              identical world and the identical
//                                                              two routes
//   the water       a world with a river publishes two ways |  the same considerer in a world with
//                                                              no water publishes one
//   the film        the high-priced body walks the dry way  |  the low-priced body walks through
//                                                              the river, in the same run
//   the fixture     a body added to the lab's own fixture   |  which is why case 9 has a fixture of
//                   moves case 15's golden trace               its own
//
// Bands and not floors, throughout. This project shipped a test asserting `> 0.10` against a real
// value of 0.36 and it stayed green through a regression that doubled the quantity.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/decision.hpp"
#include "entity/entity.hpp"
#include "entity/nav_grid.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace avgen;
using namespace avgen::entity;
namespace fs = std::filesystem;

namespace {

fs::path repoRoot() { return fs::path(AVGEN_SOURCE_DIR); }
fs::path riverFixture() {
    return repoRoot() / "examples" / "labs" / "character" / "river-crossing.scene.json";
}
fs::path labFixture() {
    return repoRoot() / "examples" / "labs" / "character" / "character-intelligence-lab.scene.json";
}
fs::path guardFixture() {
    return repoRoot() / "examples" / "labs" / "character" / "guard-post.scene.json";
}
bool assetsPresent() { return fs::exists(repoRoot() / "assets" / "aliens" / "alien-diver.glb"); }

// The whole world, ticked the way the application ticks it -- the same harness
// `test_entity_decision.cpp` uses, for the same reason: half a frame is not a frame.
struct World {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    std::unique_ptr<scene::Composition> comp;

    explicit World(const fs::path& file) : registry(file.parent_path()) {
        auto loaded = scene::Composition::loadFile(file, registry);
        INFO((loaded.has_value() ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->setViewport(1280, 720);
        // ADR-186's offline setting. With the distance cull on, "it took the dry way" would be a
        // measurement of which level-of-detail band the camera put the body in.
        comp->scene().detailLimits.entityDistanceCull = false;
    }

    void play(double seconds, double hz = 60.0, const std::function<void(int)>& each = {}) {
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
            if (each) {
                each(i);
            }
        }
    }

    [[nodiscard]] const Entity& entity(const char* name) const {
        const Entity* e = comp->entityWorld().find(name);
        REQUIRE(e != nullptr);
        return *e;
    }
    [[nodiscard]] const Navigator& nav() const { return comp->entityWorld().navigator(); }
    void setKnob(const std::string& path, float value) {
        auto* p = params.findAs<float>(path);
        INFO("knob: " << path);
        REQUIRE(p != nullptr);
        p->setBase(value);
    }
    // What a deciding behaviour is reporting, through the seam an overlay reads.
    [[nodiscard]] bool decision(const char* name, DecisionDebug& out) const {
        for (const auto& behavior : entity(name).behaviors()) {
            if (behavior->decisionDebug(out)) {
                return true;
            }
        }
        return false;
    }
};

// The destination both bodies in the fixture are sent to, on the far bank.
constexpr glm::vec3 kNorthBank{-66.0f, 0.0f, 46.0f};
constexpr glm::vec3 kSouthStart{-70.0f, 0.0f, -8.0f};

nlohmann::json routeSettings(float wadePenalty, glm::vec3 to = kNorthBank) {
    return nlohmann::json{{"name", "cross"},
                          {"weight", 1.0f},
                          {"wadePenalty", wadePenalty},
                          {"fordPenalty", 0.0f},
                          {"detourPenalty", 40.0f},
                          {"falloff", 40.0f},
                          {"goalTolerance", 2.0f},
                          {"destinations",
                           nlohmann::json::array({nlohmann::json{
                               {"name", "north-bank"},
                               {"point", nlohmann::json::array({to.x, to.y, to.z})}}})}};
}

EntityState standingAt(glm::vec3 where) {
    EntityState state;
    state.anchor = where;
    state.radius = 0.7f;
    return state;
}

DecisionContext contextIn(const World& world, const EntityState& state) {
    DecisionContext ctx;
    ctx.time = 0.0;
    ctx.state = &state;
    ctx.nav = &world.nav();
    ctx.world = &world.comp->entityWorld();
    ctx.seed = 0;
    return ctx;
}

// How deep the water is under a point, in this world's terms. `NavSample::waterDepth` is what is
// carried for exactly this (ADR-093), and reading the analytic world rather than the grid is
// deliberate here: the engine measured the route off the four-metre grid to be affordable, and a
// test that checked it against the same grid would be checking the grid against itself.
float depthUnder(const Navigator& nav, glm::vec3 at) {
    return nav.sample(glm::vec2(at.x, at.z)).waterDepth;
}

const RouteConsiderer::Priced* find(const std::vector<RouteConsiderer::Priced>& all, bool detour) {
    for (const RouteConsiderer::Priced& p : all) {
        if (p.detour == detour) {
            return &p;
        }
    }
    return nullptr;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// The fixture constraint, verified rather than believed
// ------------------------------------------------------------------------------------------------

TEST_CASE("a body added to the lab fixture moves the golden trace", "[entity][route][adr335]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present");
        return;
    }
    // `docs/character-ai-plan.md` §P11 says case 9 needs a fixture of its own because "adding a
    // body to `character-intelligence-lab.scene.json` changes what the other five perceive and
    // score, and case 15's golden position trace is taken from it". That is a claim, and a claim
    // taken on trust is how a second fixture gets written for no reason. So it is measured: the
    // same sixty seconds of the same five bodies, with and without one extra hero stone, compared
    // the way `test_decision_extraction.cpp` compares -- on the raw bits.
    //
    // **And the claim is only true of a body the bodies can reach.** Two arms, and the pair is the
    // finding: a stone 90 m away across the river moves *nothing*, because `goalWeight` rejects a
    // point outside the taste's `maxRange` (26 to 30 m here) before it is ever weighed, and the
    // five explorers keep to a `homeRadius` of 28 to 34. A stone dropped among them moves the
    // trace. So the fixture is not fragile to *any* edit; it is fragile to the edit case 9 would
    // have had to make -- a river and a body beside it, in the ground the explorers walk over.
    const auto trace = [](std::optional<glm::vec3> stoneAt) {
        assets::AssetRegistry registry(labFixture().parent_path());
        params::ParameterSet params;
        params::Modulator modulator;
        signals::SignalBus bus;
        std::ifstream in(labFixture());
        REQUIRE(in.good());
        nlohmann::json doc = nlohmann::json::parse(in);
        if (stoneAt.has_value()) {
            nlohmann::json stone = doc["heroes"][0];
            stone["name"] = "probe-stone";
            stone["position"] =
                nlohmann::json::array({stoneAt->x, stoneAt->y, stoneAt->z});
            stone["radius"] = 1.0;
            stone["height"] = 3.0;
            doc["heroes"].push_back(stone);
        }
        auto loaded = scene::Composition::fromJson(doc, registry);
        INFO((loaded.has_value() ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        std::unique_ptr<scene::Composition> comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->setViewport(1280, 720);
        comp->scene().detailLimits.entityDistanceCull = false;

        std::vector<glm::vec4> samples;
        constexpr int kFrames = 3600;
        constexpr double kStep = 1.0 / 60.0;
        FrameTime time;
        for (int i = 0; i < kFrames; ++i) {
            time.renderTime = static_cast<double>(i) * kStep;
            time.deltaTime = i == 0 ? 0.0 : kStep;
            time.frameIndex = static_cast<std::uint64_t>(i);
            params.resetFinals();
            comp->updateFields(time, bus, modulator);
            modulator.applyRoutes(bus, params, time.deltaTime);
            comp->updateBehaviour(time, bus);
            comp->update(time);
            if (i % 5 != 0) {
                continue;
            }
            for (const auto& e : comp->entityWorld().entities()) {
                if (e == nullptr || e->behaviors().empty()) {
                    continue;
                }
                const glm::vec3 p = e->state().position();
                samples.emplace_back(p.x, p.y, p.z, e->state().yaw);
            }
        }
        return samples;
    };

    const std::vector<glm::vec4> plain = trace(std::nullopt);
    REQUIRE(plain.size() == 3600); // the golden's own count, five bodies every fifth frame

    const auto compare = [&](const std::vector<glm::vec4>& other) {
        std::size_t differing = 0;
        float worst = 0.0f;
        REQUIRE(other.size() == plain.size());
        for (std::size_t i = 0; i < plain.size(); ++i) {
            if (std::memcmp(&plain[i], &other[i], sizeof(glm::vec4)) != 0) {
                ++differing;
                worst = std::max(worst, glm::length(glm::vec3(other[i]) - glm::vec3(plain[i])));
            }
        }
        return std::pair<std::size_t, float>{differing, worst};
    };

    // Across the river at (-40, 92): outside every body's range and outside every homeRadius.
    const auto far = compare(trace(glm::vec3(-40.0f, 0.0f, 92.0f)));
    // Among them at (6, 10): six metres from `scout`, inside all five ranges.
    const auto near = compare(trace(glm::vec3(6.0f, 0.0f, 10.0f)));
    WARN(fmt::format("one hero stone added to the lab fixture, of 3,600 samples:\n"
                     "  90 m away, across the river: {} differ, worst {:.3f} m\n"
                     "  6 m from `scout`:            {} differ, worst {:.3f} m",
                     far.first, far.second, near.first, near.second));

    // **The negative half, stated as an assertion rather than as a footnote.** A stone the bodies
    // cannot reach is invisible to the trace, to the bit, so "the fixture is frozen" is not true
    // of any edit at all.
    CHECK(far.first == 0);
    CHECK(far.second == 0.0f);
    // **The positive half, which is the one case 9 would have had to make.** A band and not a
    // floor: "some samples moved" would pass against one sample moving by a micrometre, which
    // would be a fixture worth mutating after all.
    CHECK(near.first > 400);
    CHECK(near.second > 2.0f);
}

// ------------------------------------------------------------------------------------------------
// The considerer, scored directly
// ------------------------------------------------------------------------------------------------

TEST_CASE("two ways to one place, and the taste decides", "[entity][route][adr335]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present");
        return;
    }
    World world(riverFixture());
    const EntityState state = standingAt(kSouthStart);
    const DecisionContext ctx = contextIn(world, state);

    // The knob is registered rather than read once from the JSON (ADR-225), which is what lets one
    // considerer be driven from one side of the crossover to the other. Two considerers with two
    // authored numbers would also flip, and would prove nothing about the parameter.
    params::ParameterSet knobs;
    const nlohmann::json settings = routeSettings(1.2f);
    RouteConsiderer considerer(&settings);
    considerer.registerParameters(knobs, "test/route/");
    auto* penalty = knobs.findAs<float>("test/route/wadePenalty");
    REQUIRE(penalty != nullptr);
    CHECK(penalty->base() == 1.2f); // the authored value reached the parameter

    std::vector<RouteConsiderer::Priced> cheap;
    penalty->setBase(0.4f);
    knobs.resetFinals(); // a base is not a value until a modulation pass has run
    REQUIRE(considerer.price(ctx, cheap) == 2);
    const RouteConsiderer::Priced* cheapFord = find(cheap, false);
    const RouteConsiderer::Priced* cheapRound = find(cheap, true);
    REQUIRE(cheapFord != nullptr);
    REQUIRE(cheapRound != nullptr);

    std::vector<RouteConsiderer::Priced> dear;
    penalty->setBase(12.0f);
    knobs.resetFinals();
    REQUIRE(considerer.price(ctx, dear) == 2);
    const RouteConsiderer::Priced* dearFord = find(dear, false);
    const RouteConsiderer::Priced* dearRound = find(dear, true);
    REQUIRE(dearFord != nullptr);
    REQUIRE(dearRound != nullptr);

    WARN(fmt::format(
        "ford {:.2f} m ({:.2f} m wet) vs detour {:.2f} m ({:.2f} m wet)\n"
        "  at wadePenalty 0.4:  ford cost {:.2f} score {:.4f} | detour cost {:.2f} score {:.4f}\n"
        "  at wadePenalty 12.0: ford cost {:.2f} score {:.4f} | detour cost {:.2f} score {:.4f}",
        cheapFord->length, cheapFord->wadeMetres, cheapRound->length, cheapRound->wadeMetres,
        cheapFord->cost, cheapFord->score, cheapRound->cost, cheapRound->score, dearFord->cost,
        dearFord->score, dearRound->cost, dearRound->score));

    // The two ways are the two ways: the short one is wet and the long one is dry. Without this
    // the flip below could be a flip between two routes that both cross the water.
    CHECK(cheapFord->length > 50.0f);
    CHECK(cheapFord->length < 62.0f);
    CHECK(cheapFord->wadeMetres > 8.0f);
    CHECK(cheapFord->wadeMetres < 20.0f);
    CHECK(cheapRound->length > 74.0f);
    CHECK(cheapRound->length < 100.0f);
    // **The detour is not bone dry and it should not be asserted to be.** It clips the shallow tip
    // of the river where the channel runs out, in 0.27 m of water, because 1.6 weighted wet metres
    // at `detourPenalty` 40 is cheaper than the sixty-odd dry metres of walking further round --
    // which is the planner being right rather than the fixture being wrong. What the arm claims is
    // the ratio: the ford is seven times as wet.
    CHECK(cheapRound->wadeMetres < 3.0f);
    CHECK(cheapFord->wadeMetres > 5.0f * cheapRound->wadeMetres);

    // **The two sides.** Neither alone proves anything.
    CHECK(cheapFord->score > cheapRound->score);
    CHECK(dearRound->score > dearFord->score);
    // Bands on the scores themselves, which is what the overlay prints. A floor would have stayed
    // green through a change that halved both.
    CHECK(cheapFord->score > 0.37f);
    CHECK(cheapFord->score < 0.45f);
    CHECK(cheapRound->score > 0.29f);
    CHECK(cheapRound->score < 0.35f);
    CHECK(dearFord->score > 0.15f);
    CHECK(dearFord->score < 0.20f);
    CHECK(dearRound->score > 0.25f);
    CHECK(dearRound->score < 0.31f);
    // And by how much it wins, as a band. A margin of a thousandth would flip on a rounding change.
    CHECK((cheapFord->score - cheapRound->score) > 0.04f);
    CHECK((cheapFord->score - cheapRound->score) < 0.16f);
    CHECK((dearRound->score - dearFord->score) > 0.05f);
    CHECK((dearRound->score - dearFord->score) < 0.20f);

    // The route is the same route at both tastes -- only the price moved. If the lengths differed
    // the flip would be a measurement of the planner rather than of the considerer.
    CHECK(cheapFord->length == dearFord->length);
    CHECK(cheapRound->length == dearRound->length);
    CHECK(cheapFord->wadeMetres == dearFord->wadeMetres);
}

TEST_CASE("a world with no water has one way, not two", "[entity][route][adr335]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present");
        return;
    }
    // The control for the arm above. `guard-post.scene.json` is the same flat 240 m world with the
    // river taken out, so the two requests at two wade penalties must come back as one way -- and
    // a considerer that published two identical options would put the selector's margin between a
    // route and itself and would print the same line twice in the overlay.
    World world(guardFixture());
    const EntityState state = standingAt(glm::vec3(-70.0f, 0.0f, -8.0f));
    const DecisionContext ctx = contextIn(world, state);
    const nlohmann::json settings = routeSettings(1.2f, glm::vec3(-66.0f, 0.0f, 46.0f));
    RouteConsiderer considerer(&settings);
    std::vector<RouteConsiderer::Priced> priced;
    const std::size_t ways = considerer.price(ctx, priced);
    WARN(fmt::format("dry world: {} way(s), {:.2f} m, {:.2f} m wet", ways,
                     priced.empty() ? 0.0f : priced[0].length,
                     priced.empty() ? 0.0f : priced[0].wadeMetres));
    REQUIRE(ways == 1);
    CHECK_FALSE(priced[0].detour);
    CHECK(priced[0].wadeMetres == 0.0f);
    CHECK(priced[0].length > 50.0f);
    CHECK(priced[0].length < 62.0f);
}

// ------------------------------------------------------------------------------------------------
// The film: two bodies, one destination, two ways
// ------------------------------------------------------------------------------------------------

TEST_CASE("the cheap body fords and the dear body goes round", "[entity][route][adr335]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present");
        return;
    }
    // The arm that makes the scores mean something on screen. An option called "go round" whose
    // actions were a single `Move` to the far bank would be a body that waded anyway --
    // `NavigatorPath::route` answers a move with the straight line and leaves the rest to local
    // steering, which cannot get a body round a lake.
    World world(riverFixture());
    float waderDeepest = 0.0f;
    float dryDeepest = 0.0f;
    float waderEast = -1000.0f;
    float dryEast = -1000.0f;
    // The overlay at the moment the choice is live, not at the end of the run. Once a body is
    // standing at its destination `requestPath` answers `AlreadyThere`, the considerer has nothing
    // to say and `idle` is all that is left -- which is correct, and is not the frame case 9 is
    // about.
    std::vector<std::pair<std::string, float>> waderScores;
    std::vector<std::pair<std::string, float>> dryScores;
    const auto snapshot = [&](const char* who, std::vector<std::pair<std::string, float>>& into) {
        DecisionDebug debug;
        if (!world.decision(who, debug) || !into.empty()) {
            return;
        }
        for (const ScoredOption& option : debug.options) {
            into.emplace_back(std::string(option.name), option.score);
        }
    };
    world.play(150.0, 60.0, [&](int frame) {
        if (frame == 120) { // two seconds in: both bodies are walking and neither has arrived
            snapshot("wader", waderScores);
            snapshot("drylander", dryScores);
        }
        if (frame % 10 != 0) {
            return;
        }
        const glm::vec3 wader = world.entity("wader").state().position();
        const glm::vec3 dry = world.entity("drylander").state().position();
        waderDeepest = std::max(waderDeepest, depthUnder(world.nav(), wader));
        dryDeepest = std::max(dryDeepest, depthUnder(world.nav(), dry));
        waderEast = std::max(waderEast, wader.x);
        dryEast = std::max(dryEast, dry.x);
    });
    const glm::vec3 wader = world.entity("wader").state().position();
    const glm::vec3 dry = world.entity("drylander").state().position();
    WARN(fmt::format("wader ended ({:.1f}, {:.1f}) deepest {:.2f} m, furthest east {:.1f}\n"
                     "drylander ended ({:.1f}, {:.1f}) deepest {:.2f} m, furthest east {:.1f}",
                     wader.x, wader.z, waderDeepest, waderEast, dry.x, dry.z, dryDeepest, dryEast));

    // Both got to the far bank. Without this the arm below would pass for a body that never left.
    CHECK(wader.z > 40.0f);
    CHECK(dry.z > 40.0f);
    // The wader crossed the channel and the drylander only clipped its shallow tip. Bands, both
    // ends: the channel is 1.40 m at the centre and the tip the dry route touches is 0.27 m, so
    // "more than nothing" and "less than everything" would both pass for the wrong body.
    CHECK(waderDeepest > 1.2f);
    CHECK(dryDeepest < 0.6f);
    CHECK(waderDeepest > 3.0f * dryDeepest);
    // And the drylander got round by going east, past the end of the channel at x = -50; the wader
    // never left the line between its start and the far bank.
    CHECK(dryEast > -45.0f);
    CHECK(waderEast < -60.0f);

    // **Both scores in the overlay**, which is what case 9 asks for, through the seam an overlay
    // reads (`IBehavior::decisionDebug`) rather than round the back of it.
    const auto report = [](const std::vector<std::pair<std::string, float>>& scores) {
        std::string line;
        for (const auto& [name, score] : scores) {
            line += fmt::format("{} = {:.4f}  ", name, score);
        }
        return line;
    };
    WARN(fmt::format("at 2 s, the overlay reads\n  wader:     {}\n  drylander: {}",
                     report(waderScores), report(dryScores)));
    const auto scoreOf = [](const std::vector<std::pair<std::string, float>>& scores,
                            std::string_view name) {
        for (const auto& [n, s] : scores) {
            if (n == name) {
                return s;
            }
        }
        return -1.0f;
    };
    // Three options each: the ford, the detour and the idle floor.
    REQUIRE(waderScores.size() == 3);
    REQUIRE(dryScores.size() == 3);
    const float waderFord = scoreOf(waderScores, "north-bank");
    const float waderRound = scoreOf(waderScores, "north-bank round");
    const float dryFord = scoreOf(dryScores, "north-bank");
    const float dryRound = scoreOf(dryScores, "north-bank round");
    REQUIRE(waderFord > 0.0f);
    REQUIRE(waderRound > 0.0f);
    REQUIRE(dryFord > 0.0f);
    REQUIRE(dryRound > 0.0f);
    // The same two ways, weighed by two tastes, in one run of one world.
    CHECK(waderFord > waderRound);
    CHECK(dryRound > dryFord);
}

// ------------------------------------------------------------------------------------------------
// A real character in a real world: nobody named the destinations
// ------------------------------------------------------------------------------------------------

TEST_CASE("the goal model picks the places and the router picks the ways", "[entity][route][adr335]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present");
        return;
    }
    // The authored-destination arm above is the lab's, and a considerer that only worked with a
    // list an author wrote would not serve the Glowmere showcase this unit gates -- a jetpack
    // alien crossing the river while a walking-only one routes around it, neither of them told
    // where to go. With no `destinations`, the goal model chooses the places (`goalWeight`, the
    // same function `Explore` and `interest` weigh with) and this chooses the ways to them.
    World world(riverFixture());
    const EntityState state = standingAt(kSouthStart);
    const DecisionContext ctx = contextIn(world, state);
    const nlohmann::json settings{{"name", "wander"},
                                  {"weight", 1.0f},
                                  {"wadePenalty", 1.2f},
                                  {"source", "omniscient"},
                                  {"maxDestinations", 3},
                                  // Far enough out that the nearest thing worth walking to is on
                                  // the other bank. Below 40 m every candidate is a shore point of
                                  // this side of the river, and the arm would be a probe that
                                  // could not fail.
                                  {"minRange", 45.0f},
                                  {"maxRange", 90.0f}};
    RouteConsiderer considerer(&settings);
    std::vector<RouteConsiderer::Priced> priced;
    const std::size_t ways = considerer.price(ctx, priced);
    std::string report;
    for (const RouteConsiderer::Priced& p : priced) {
        report += fmt::format("\n  {}{}  {:.1f} m, {:.2f} wet, score {:.4f}",
                              p.destination.empty() ? "(unnamed)" : p.destination,
                              p.detour ? " round" : "", p.length, p.wadeMetres, p.score);
    }
    WARN(fmt::format("{} way(s) to {} place(s) nobody named:{}", ways, priced.size(), report));

    // **The cap is a cap.** Three destinations, so at most six ways -- and at least three, because
    // a destination with no way at all would mean the goal model handed over somewhere unreachable.
    REQUIRE(ways >= 3);
    REQUIRE(ways <= 6);
    CHECK(ways == 6); // three places, two ways each: every one of them is across the water
    // At least one of them is across the river and has two ways, which is the whole point: an
    // omniscient list with no water in it would give six identical single options and this arm
    // would be the probe that cannot fail.
    std::size_t withTwo = 0;
    for (const RouteConsiderer::Priced& p : priced) {
        if (p.detour) {
            ++withTwo;
        }
    }
    CHECK(withTwo == 3);
    // And the names came out of the world rather than out of the test: `north-cairn` and
    // `ford-marker` are heroes this fixture places, and an unnamed one is a shore point the grid
    // extracted while it was being built.
    std::size_t named = 0;
    for (const RouteConsiderer::Priced& p : priced) {
        named += p.destination.empty() ? 0 : 1;
    }
    CHECK(named >= 4);
    // And every way is a real route with a real price: a zero-length option would be a body
    // scoring the ground it stands on.
    for (const RouteConsiderer::Priced& p : priced) {
        CHECK(p.length > 5.0f);
        CHECK(p.cost >= p.length);
        CHECK(p.score > 0.0f);
    }
}
