// Deliberate water tears (ADR-916): the settings' CPU half. The GPU half -- that each setting
// reaches the picture, that the seams are what they claim to be -- is test_water_tears_gpu.cpp.
//
// What is checked here is that an authored tear survives the round trip a scene file takes, that
// nonsense is refused with a message naming the key, that every setting is a control under one
// "tears" heading, labelled for what it does and reaching the surface the renderer reads, that a
// route can drive exactly three of them, and that the liveness registry knows when a tear setting is
// unread and when keying one is a phase-rate hazard.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/liveness.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "params/timeline.hpp"
#include "scene/composition.hpp"
#include "scene/route_liveness.hpp"
#include "scene/water_surface.hpp"
#include "signals/signal_bus.hpp"
#include "ui/ui_logic.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <functional>
#include <limits>
#include <string>
#include <vector>

using namespace avgen;
using json = nlohmann::json;

namespace {

json terrainWith(const json& water) {
    json doc = json::parse(R"({
      "format": "avgen-scene", "version": 1, "name": "tears",
      "camera": { "mode": 1, "position": [0, 30, 60], "target": [0, 0, -60], "fov": 50.0 },
      "nodes": [
        { "name": "sea", "kind": "terrain",
          "world": { "name": "small", "size": [160, 160] },
          "terrain": { "chunkSize": 40.0, "resolution": 8, "lodLevels": 2,
                       "lodDistance": 50.0, "viewDistance": 400.0 } }
      ]
    })");
    doc["nodes"][0]["terrain"]["water"] = water;
    return doc;
}

const scene::WaterSettings& waterOf(const scene::Composition& comp) {
    const scene::CompositionNode* node = comp.findNode("sea");
    REQUIRE(node != nullptr);
    return node->terrain.water;
}

} // namespace

TEST_CASE("tear settings round-trip through a scene file", "[unit][water][tears]") {
    // Off by default, and valid as shipped.
    const scene::WaterSettings defaults;
    CHECK(defaults.tears == 0.0f);
    CHECK(defaults.tearFollowsWind);
    CHECK(defaults.validate());

    assets::AssetRegistry registry;
    const json authored = {{"enabled", true},      {"tears", 0.45},        {"tearShear", 2.5},
                           {"tearCoverage", 0.6},  {"tearCell", 0.9},      {"tearSpacing", 22.0},
                           {"tearStretch", 2.2},   {"tearDirection", 1.1}, {"tearDrift", -0.2},
                           {"tearWind", 0.3}};
    auto loaded = scene::Composition::fromJson(terrainWith(authored), registry);
    REQUIRE(loaded.has_value());
    const scene::WaterSettings& w = waterOf(**loaded);
    CHECK(w.tears == 0.45f);
    CHECK(w.tearShear == 2.5f);
    CHECK(w.tearCoverage == 0.6f);
    CHECK(w.tearCell == 0.9f);
    CHECK(w.tearSpacing == 22.0f);
    CHECK(w.tearStretch == 2.2f);
    CHECK_FALSE(w.tearFollowsWind); // a number is an angle
    CHECK(w.tearAngle == 1.1f);
    CHECK(w.tearDrift == -0.2f);
    CHECK(w.tearWind == 0.3f);

    // Written back out, and read again: the same settings. A key the writer drops or the reader
    // ignores is the "a setting the application does not keep is not a setting" defect (ADR-225).
    const json written = (*loaded)->toJson();
    const json& water = written.at("nodes").at(0).at("terrain").at("water");
    CHECK(water.at("tearDirection").is_number());
    auto again = scene::Composition::fromJson(written, registry);
    REQUIRE(again.has_value());
    const scene::WaterSettings& back = waterOf(**again);
    CHECK(back.tears == w.tears);
    CHECK(back.tearShear == w.tearShear);
    CHECK(back.tearCoverage == w.tearCoverage);
    CHECK(back.tearCell == w.tearCell);
    CHECK(back.tearSpacing == w.tearSpacing);
    CHECK(back.tearStretch == w.tearStretch);
    CHECK(back.tearFollowsWind == w.tearFollowsWind);
    CHECK(back.tearAngle == w.tearAngle);
    CHECK(back.tearDrift == w.tearDrift);
    CHECK(back.tearWind == w.tearWind);

    // "wind" is the other spelling, and it survives the round trip as itself.
    auto windy = scene::Composition::fromJson(terrainWith({{"tears", 0.3}, {"tearDirection", "wind"}}), registry);
    REQUIRE(windy.has_value());
    CHECK(waterOf(**windy).tearFollowsWind);
    const json windyOut = (*windy)->toJson();
    CHECK(windyOut.at("nodes").at(0).at("terrain").at("water").at("tearDirection") == "wind");
}

TEST_CASE("tear settings reject nonsense and say which key", "[unit][water][tears]") {
    assets::AssetRegistry registry;
    struct Bad {
        json water;
        const char* mentions;
    };
    const Bad bad[] = {
        {{{"tearDirection", "north"}}, "tearDirection"},
        {{{"tearDirection", true}}, "tearDirection"},
        {{{"tearDirection", 7.0}}, "tearDirection"}, // more than a turn: the control could not show it
        {{{"tearShear", 9.0}}, "tearShear"},
        {{{"tearCoverage", 1.5}}, "tearCoverage"},
        {{{"tearCell", 0.0}}, "tearCell"},
        {{{"tearCell", 2.0}, {"tearSpacing", 3.0}}, "tearSpacing"}, // closer than two cells
        {{{"tearStretch", 0.1}}, "tearStretch"},
        {{{"tearDrift", 25.0}}, "tearDrift"},
        {{{"tearWind", -0.1}}, "tearWind"},
        {{{"tears", -1.0}}, "tears"},
        {{{"tearShear", "lots"}}, "tearShear"},
    };
    for (const Bad& b : bad) {
        INFO(b.water.dump());
        auto loaded = scene::Composition::fromJson(terrainWith(b.water), registry);
        REQUIRE_FALSE(loaded.has_value());
        INFO(loaded.error().message);
        CHECK(loaded.error().message.find(b.mentions) != std::string::npos);
    }
    // The control: the same document with sane tear settings loads, so the refusals above are about
    // the values and not about the harness.
    CHECK(scene::Composition::fromJson(terrainWith({{"tears", 0.5}, {"tearShear", 8.0}}), registry).has_value());

    // And a NaN, which no comparison-shaped check refuses unless it is written to.
    scene::WaterSettings w;
    w.tearShear = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(w.validate());
}

namespace {

// A composition with the tears on and every setting off its default, attached to a parameter set.
struct TornSea {
    assets::AssetRegistry registry;
    std::unique_ptr<scene::Composition> comp;
    params::ParameterSet params;
    params::Modulator modulator;

    explicit TornSea(json water = {{"tears", 0.25},       {"tearShear", 2.0},    {"tearCoverage", 0.5},
                                   {"tearCell", 0.9},     {"tearSpacing", 22.0}, {"tearStretch", 2.2},
                                   {"tearDirection", 1.1}, {"tearDrift", -0.2},   {"tearWind", 0.3}}) {
        auto loaded = scene::Composition::fromJson(terrainWith(water), registry);
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->update(FrameTime{});
        REQUIRE(comp->scene().waters.size() == 1);
    }
    const scene::WaterSettings& drawn() {
        params.resetFinals();
        comp->update(FrameTime{});
        return comp->scene().waters[0].settings;
    }
};

// The ten controls: path leaf, the label a person reads, whether a route may drive it, the authored
// value TornSea gives it, and an edit with the setting it must reach.
struct TearControl {
    const char* leaf;
    const char* label;
    bool routable;
    float authored; // bool: 0/1
    float edited;
    std::function<float(const scene::WaterSettings&)> read;
};

const std::vector<TearControl>& tearControls() {
    static const std::vector<TearControl> rows{
        {"amount", "amount", true, 0.25f, 0.8f, [](const scene::WaterSettings& w) { return w.tears; }},
        {"shear", "shear (m)", true, 2.0f, 5.0f, [](const scene::WaterSettings& w) { return w.tearShear; }},
        {"coverage", "coverage", true, 0.5f, 0.9f, [](const scene::WaterSettings& w) { return w.tearCoverage; }},
        {"cell", "step size (m)", false, 0.9f, 1.6f, [](const scene::WaterSettings& w) { return w.tearCell; }},
        {"spacing", "spacing (m)", false, 22.0f, 35.0f, [](const scene::WaterSettings& w) { return w.tearSpacing; }},
        {"stretch", "stretch", false, 2.2f, 4.0f, [](const scene::WaterSettings& w) { return w.tearStretch; }},
        {"followWind", "follow the wind", false, 0.0f, 1.0f,
         [](const scene::WaterSettings& w) { return w.tearFollowsWind ? 1.0f : 0.0f; }},
        {"direction", "direction (radians, if not following the wind)", false, 1.1f, -0.7f,
         [](const scene::WaterSettings& w) { return w.tearAngle; }},
        {"drift", "drift (m per second)", false, -0.2f, 0.6f, [](const scene::WaterSettings& w) { return w.tearDrift; }},
        {"wind", "wind (gusts tighten the seams)", false, 0.3f, 0.9f, [](const scene::WaterSettings& w) { return w.tearWind; }},
    };
    return rows;
}

bool hasRule(const std::vector<params::liveness::Finding>& findings, std::string_view rule) {
    return std::any_of(findings.begin(), findings.end(), [&](const auto& f) { return f.rule == rule; });
}

} // namespace

// UI reach (the owner's standing rule): anything visible must be findable and adjustable under a name
// that says what it is. The tears are one heading -- the Parameters panel's "nodes" group shows the
// sub-group "sea/water/tears", the World panel's Inspector shows "tears/<label>" rows under "water" --
// computed here with the panels' own path functions (ui::parameterSubGroup, ui::inspectorRowLabel),
// because a wrong path neither fails to compile nor throws: the section just renders somewhere else.
TEST_CASE("every tear setting is a control under one 'tears' heading, labelled, and reaches the surface",
          "[unit][water][tears][parameters][ui]") {
    TornSea sea;
    const std::string inspectorCut = "nodes/sea/water/";
    for (const TearControl& row : tearControls()) {
        const std::string path = std::string("nodes/sea/water/tears/") + row.leaf;
        INFO(path);
        params::IParameter* p = sea.params.find(path);
        REQUIRE(p != nullptr);
        CHECK(p->label() == row.label);
        CHECK(p->flags().exposed);
        CHECK(p->flags().serialized);
        CHECK(p->flags().modulatable == row.routable);
        CHECK(ui::parameterSubGroup(path, "nodes") == "sea/water/tears");
        CHECK(ui::inspectorRowLabel(path, inspectorCut.size(), p->label()) == std::string("tears/") + row.label);
        // Registered with the authored value, so a project that never touches it keeps the scene's.
        CHECK(p->baseComponent(0) == row.authored);
        CHECK(row.read(sea.drawn()) == row.authored);
        // Moved as an edit in either panel moves it, it reaches the settings the renderer reads.
        p->setBaseComponent(0, row.edited);
        CHECK(row.read(sea.drawn()) == row.edited);
    }
    // The spellings the work-in-progress used are gone, so nothing half-migrated can bind to them.
    for (const char* stale : {"nodes/sea/water/tears", "nodes/sea/water/tearShear", "nodes/sea/water/tearCoverage",
                              "nodes/sea/water/tearCell", "nodes/sea/water/tearDirection"}) {
        INFO(stale);
        CHECK(sea.params.find(stale) == nullptr);
    }
}

// Three of the ten may be routed; the seven that rescale or turn the lattice about the world origin,
// multiply the clock, or couple the wind refuse a route at bind -- the modulator's own refusal, which
// the liveness registry reports as `not-modulatable`. The control is the three that bind.
TEST_CASE("a route binds to the tears' amount, shear and coverage and is refused by the other seven",
          "[unit][water][tears][parameters]") {
    TornSea sea;
    signals::SignalBus bus;
    bus.declare("test.level");
    const std::size_t first = sea.modulator.routes().size(); // after the composition's own routes
    for (const TearControl& row : tearControls()) {
        params::ModRoute route;
        route.source = "test.level";
        route.target = std::string("nodes/sea/water/tears/") + row.leaf;
        route.amount = 0.1f;
        sea.modulator.addRoute(route);
    }
    static_cast<void>(sea.modulator.bind(bus, sea.params)); // an error listing the seven refusals
    const params::liveness::BusFacts facts(bus, sea.params);
    std::size_t bound = 0;
    for (std::size_t i = 0; i < tearControls().size(); ++i) {
        const TearControl& row = tearControls()[i];
        const params::ModRoute& route = sea.modulator.routes()[first + i];
        INFO(route.target);
        CHECK((route.targetParam != nullptr) == row.routable);
        CHECK(hasRule(params::liveness::Registry::standard().checkRoute(route, facts), "not-modulatable") == !row.routable);
        bound += route.targetParam != nullptr ? 1 : 0;
    }
    CHECK(bound == 3);
}

// ADR-902's registry, taught the tears (ADR-916):
//   * `tear-setting-unread` (dead): a tear setting of a water whose amount is 0 with nothing to lift it
//     is never read -- that water is drawn with the tear code compiled out -- and a fixed direction is
//     never read while the seams follow the wind;
//   * the phase-rate table: the lattice drifts at t * drift, its step size, spacing, stretch and
//     direction scale or turn that drifting coordinate, and with the seams following the wind the
//     scene wind's direction is theirs.
TEST_CASE("the liveness registry knows when a tear setting is unread and when keying one jumps the seams",
          "[unit][water][tears][liveness]") {
    const auto& registry = params::liveness::Registry::standard();
    TornSea sea({{"tears", 0.0}, {"tearDirection", "wind"}, {"tearDrift", 0.15}});
    params::Timeline timeline;
    scene::LivenessInputs in;
    in.params = &sea.params;
    in.composition = sea.comp.get();
    in.modulator = &sea.modulator;
    in.timeline = &timeline;
    const auto target = [&](const std::string& leaf) {
        const scene::SceneLivenessFacts facts(in);
        return registry.checkTarget("nodes/sea/water/tears/" + leaf, -1, facts);
    };
    const auto windDirection = [&] {
        const scene::SceneLivenessFacts facts(in);
        return registry.checkTarget("scene/windDirection", -1, facts);
    };
    REQUIRE(registry.rule("tear-setting-unread") != nullptr);

    // Off, and nothing lifts it: every setting but the amount is unread.
    for (const char* leaf : {"shear", "coverage", "cell", "spacing", "stretch", "followWind", "direction", "drift", "wind"}) {
        INFO(leaf);
        CHECK(hasRule(target(leaf), "tear-setting-unread"));
    }
    CHECK_FALSE(hasRule(target("amount"), "tear-setting-unread"));
    // Unknown -- no routes or tracks to look in -- is not a verdict.
    in.modulator = nullptr;
    CHECK_FALSE(hasRule(target("shear"), "tear-setting-unread"));
    in.modulator = &sea.modulator;
    // Nor is the seams' being off a hazard for the wind's direction.
    CHECK_FALSE(hasRule(windDirection(), "phase-rate"));

    // THE CONTROLS: a track that keys the amount above 0, a route that lifts it, a base above 0.
    params::Track zeros;
    zeros.target = "nodes/sea/water/tears/amount";
    zeros.keys = {params::Key{.time = 0.0, .value = {0.0f}}, params::Key{.time = 8.0, .value = {0.0f}}};
    timeline.addTrack(zeros);
    CHECK(hasRule(target("shear"), "tear-setting-unread")); // keyed, but never above 0
    params::Track rising = zeros;
    rising.keys[1].value[0] = 0.5f;
    timeline.addTrack(rising);
    CHECK_FALSE(hasRule(target("shear"), "tear-setting-unread"));
    timeline.clear();
    CHECK(hasRule(target("shear"), "tear-setting-unread"));
    params::ModRoute lift;
    lift.source = "audio.bass";
    lift.target = "nodes/sea/water/tears/amount";
    lift.amount = 0.4f;
    sea.modulator.addRoute(lift);
    CHECK_FALSE(hasRule(target("shear"), "tear-setting-unread"));
    sea.modulator.clearRoutes();
    sea.params.find("nodes/sea/water/tears/amount")->setBaseComponent(0, 0.4f);
    CHECK_FALSE(hasRule(target("shear"), "tear-setting-unread"));

    // Showing, following the wind: the fixed direction is unread, and read once they stop following.
    CHECK(hasRule(target("direction"), "tear-setting-unread"));
    // Drifting and following the wind, the wind's direction is the lattice's: a phase rate.
    CHECK(hasRule(windDirection(), "phase-rate"));
    sea.params.find("nodes/sea/water/tears/followWind")->setBaseComponent(0, 0.0f);
    CHECK_FALSE(hasRule(target("direction"), "tear-setting-unread"));
    CHECK_FALSE(hasRule(windDirection(), "phase-rate")); // no longer theirs

    // The lattice drifts at t * drift: the drift is a phase rate, and the scales of the drifting
    // coordinate are while it drifts -- and not once it stops (the table's `whenNonZero`).
    CHECK(hasRule(target("drift"), "phase-rate"));
    for (const char* leaf : {"cell", "spacing", "stretch", "direction"}) {
        INFO(leaf);
        CHECK(hasRule(target(leaf), "phase-rate"));
    }
    sea.params.find("nodes/sea/water/tears/drift")->setBaseComponent(0, 0.0f);
    sea.params.resetFinals();
    for (const char* leaf : {"cell", "spacing", "stretch", "direction"}) {
        INFO(leaf);
        CHECK_FALSE(hasRule(target(leaf), "phase-rate"));
    }
    // The routable three are amplitudes and thresholds, not rates.
    for (const char* leaf : {"amount", "shear", "coverage"}) {
        INFO(leaf);
        CHECK_FALSE(hasRule(target(leaf), "phase-rate"));
    }
}
