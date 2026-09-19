// ADR-360: the wind was a system the application ran and could not switch on.
//
// `wind::WindParams` has seventeen authored fields. Two were parameters. The one that gates every
// other -- `enabled` -- was not, and the scene writer emitted the whole block only `if
// (windSetting_.enabled)`, so a scene that did not already say the wind was on could never start
// saying it. The owner's shipped project carries `scene/windSpeed = 1.319`, a value they dragged a
// slider to, and it had never moved a single vertex.
//
// ADR-350 prescribes exactly two assertions for this class of defect, and neither needs a GPU:
// every path is registered, with a negative control that an unregistered path is NOT found; and a
// non-default value survives save -> load -> save. Both are here. The second one is the half that
// would have caught this particular defect, because the reader was fine and the writer was not.

#include "assets/asset_registry.hpp"
#include "core/wind.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using Catch::Approx;
using json = nlohmann::json;
using namespace avgen;

namespace {

bool registered(const params::ParameterSet& set, std::string_view path) {
    return set.find(path) != nullptr;
}

std::filesystem::path scratch() {
    auto dir = std::filesystem::temp_directory_path() / "avgen_wind_params";
    std::filesystem::create_directories(dir);
    return dir;
}

// A scene with the wind on and a tree-shaped group that declares a wind body. Every number here is
// deliberately unlike its default, so a value that survives the round trip cannot be a default that
// happened to match.
std::filesystem::path writeScene(const std::string& name) {
    json doc;
    doc["format"] = "avgen-scene";
    doc["version"] = 1;
    doc["name"] = "windy";
    doc["wind"] = {{"enabled", true},      {"speed", 1.319},        {"direction", 0.77},
                   {"gustAmount", 1.44},   {"gustScale", 41.5},     {"gustSpeed", 13.25},
                   {"gustSharpness", 5.5}, {"turbulence", 0.63},    {"turbulenceScale", 22.5},
                   {"turbulenceSpeed", 2.75}, {"regionScale", 88.0}, {"regionAmount", 0.71},
                   {"regionDrift", 0.13},  {"flutterScale", 3.9}};
    doc["nodes"] = json::array({json{{"name", "tree"},
                                     {"kind", "group"},
                                     {"wind",
                                      {{"strength", 1.75},
                                       {"trunk", 0.42},
                                       {"branch", 1.85},
                                       {"foliage", 2.35},
                                       {"flutter", 1.65},
                                       {"lag", 0.55}}}}});
    const auto path = scratch() / name;
    std::ofstream(path) << doc.dump(1);
    return path;
}

const char* const kWindLeaves[] = {"gustAmount",      "gustScale",      "gustSpeed",  "gustSharpness",
                                   "turbulence",      "turbulenceScale", "turbulenceSpeed",
                                   "regionScale",     "regionAmount",   "regionDrift", "flutterScale"};

} // namespace

TEST_CASE("Every wind setting is a real parameter", "[wind][parameters][environment]") {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "composition");
    comp.attach(params, modulator);

    // The gate first. This is the one whose absence made the other sixteen decorative.
    CHECK(registered(params, "scene/wind/enabled"));
    // The two that already existed keep their spelling: renaming them to `scene/wind/*` would
    // orphan the value in every project that has one, which is ADR-264's 94-orphan failure.
    CHECK(registered(params, "scene/windSpeed"));
    CHECK(registered(params, "scene/windDirection"));
    for (const char* leaf : kWindLeaves) {
        const std::string path = std::string("scene/wind/") + leaf;
        INFO(path);
        CHECK(registered(params, path));
    }

    // THE CONTROL. Without it every CHECK above would pass against a set that answered yes to
    // anything, and the loop would prove nothing -- ADR-182 in a unit test.
    CHECK_FALSE(registered(params, "scene/wind/nonesuch"));
    CHECK_FALSE(registered(params, "scene/windNonesuch"));
}

TEST_CASE("A node that declares a wind body gets its controls, and one that does not stays clean",
          "[wind][parameters][nodes]") {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "composition");
    comp.attach(params, modulator);

    scene::CompositionNode body;
    body.name = "tree";
    body.kind = scene::NodeKind::Group;
    body.windAuthored = true;
    body.wind.strength = 1.0f;
    REQUIRE(comp.addNode(std::move(body)).has_value());

    scene::CompositionNode plain;
    plain.name = "rock";
    plain.kind = scene::NodeKind::Group;
    REQUIRE(comp.addNode(std::move(plain)).has_value());

    for (const char* leaf : {"strength", "trunk", "branch", "foliage", "flutter", "lag"}) {
        const std::string path = std::string("nodes/tree/wind/") + leaf;
        INFO(path);
        CHECK(registered(params, path));
    }
    // The other half of the decision, asserted so it is recorded rather than assumed: a node that
    // declares no wind body grows no sliders. A scene full of rocks would otherwise gain six inert
    // controls each, and a panel nobody can find anything in is its own kind of unreachable.
    CHECK_FALSE(registered(params, "nodes/rock/wind/strength"));
    CHECK_FALSE(registered(params, "nodes/tree/wind/nonesuch"));
}

TEST_CASE("A saved scene keeps its wind", "[wind][parameters][environment]") {
    const auto source = writeScene("in.scene.json");

    assets::AssetRegistry registry;
    registry.setBaseDirectory(scratch());
    params::ParameterSet params;
    params::Modulator modulator;

    auto loaded = scene::Composition::loadFile(source, registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    comp.attach(params, modulator);

    const auto out = scratch() / "out.scene.json";
    REQUIRE(comp.saveFile(out).has_value());

    json written;
    std::ifstream(out) >> written;
    REQUIRE(written.contains("wind"));
    const json& w = written.at("wind");
    CHECK(w.at("enabled").get<bool>());
    CHECK(w.at("speed").get<float>() == Approx(1.319f));
    CHECK(w.at("direction").get<float>() == Approx(0.77f));
    CHECK(w.at("gustAmount").get<float>() == Approx(1.44f));
    CHECK(w.at("gustSharpness").get<float>() == Approx(5.5f));
    CHECK(w.at("turbulenceScale").get<float>() == Approx(22.5f));
    CHECK(w.at("regionAmount").get<float>() == Approx(0.71f));
    CHECK(w.at("flutterScale").get<float>() == Approx(3.9f));

    REQUIRE(written.contains("nodes"));
    REQUIRE(written.at("nodes").size() == 1);
    const json& node = written.at("nodes").at(0);
    REQUIRE(node.contains("wind"));
    CHECK(node.at("wind").at("strength").get<float>() == Approx(1.75f));
    CHECK(node.at("wind").at("foliage").get<float>() == Approx(2.35f));
    CHECK(node.at("wind").at("lag").get<float>() == Approx(0.55f));

    // ...and again, because save -> load -> save is the trip a real session makes and the one the
    // first save can still pass while the second drops everything.
    auto reloaded = scene::Composition::loadFile(out, registry);
    REQUIRE(reloaded.has_value());
    params::ParameterSet params2;
    params::Modulator modulator2;
    (*reloaded)->attach(params2, modulator2);
    const auto out2 = scratch() / "out2.scene.json";
    REQUIRE((*reloaded)->saveFile(out2).has_value());
    json written2;
    std::ifstream(out2) >> written2;
    CHECK(written2.at("wind") == written.at("wind"));
    CHECK(written2.at("nodes").at(0).at("wind") == node.at("wind"));
}

TEST_CASE("A scene that never mentions the wind grows no wind block", "[wind][parameters][environment]") {
    // The other half of ADR-350's round-trip rule, and the reason the writer's condition is
    // "differs from the defaults" rather than "always": a file written before this key existed must
    // come back byte-identical, or every scene in the repository gains a diff on its next save.
    json doc;
    doc["format"] = "avgen-scene";
    doc["version"] = 1;
    doc["name"] = "calm";
    doc["nodes"] = json::array();
    const auto source = scratch() / "calm.scene.json";
    std::ofstream(source) << doc.dump(1);

    assets::AssetRegistry registry;
    registry.setBaseDirectory(scratch());
    params::ParameterSet params;
    params::Modulator modulator;
    auto loaded = scene::Composition::loadFile(source, registry);
    REQUIRE(loaded.has_value());
    (*loaded)->attach(params, modulator);

    const auto out = scratch() / "calm-out.scene.json";
    REQUIRE((*loaded)->saveFile(out).has_value());
    json written;
    std::ifstream(out) >> written;
    CHECK_FALSE(written.contains("wind"));
}
