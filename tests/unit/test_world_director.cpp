// The World Director and look presets (ADR-041): artistic words over ordinary macros, and looks
// that change how a world renders without touching its geometry.

#include "app/world_director.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace avgen;
using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;

namespace {
app::WorldDirector makeDirector() {
    app::WorldDirector d;
    d.name = "temple";
    app::DirectorMapping drama;
    drama.knob = app::DirectorKnob::Drama;
    drama.defaultValue = 0.4f;
    drama.targets.push_back({.path = "scene/keyLight", .min = 0.3f, .max = 2.5f});
    drama.targets.push_back({.path = "post/output/vignette", .min = 0.1f, .max = 0.6f});
    app::DirectorMapping warmth;
    warmth.knob = app::DirectorKnob::Warmth;
    warmth.defaultValue = 0.5f;
    warmth.targets.push_back({.path = "post/grade/temperature", .min = -0.4f, .max = 0.6f});
    d.mappings = {drama, warmth};
    return d;
}
} // namespace

TEST_CASE("Every director knob has a name and a description", "[director]") {
    const auto knobs = app::allDirectorKnobs();
    CHECK(knobs.size() == 18);
    for (const app::DirectorKnob knob : knobs) {
        const std::string name = app::directorKnobName(knob);
        INFO(name);
        CHECK_FALSE(name.empty());
        CHECK(app::directorKnobFromName(name) == knob);
        CHECK_FALSE(std::string(app::directorKnobDescription(knob)).empty());
    }
    CHECK_FALSE(app::directorKnobFromName("nonsense").has_value());
}

TEST_CASE("A director expands into ordinary world macros", "[director]") {
    const app::WorldDirector d = makeDirector();
    REQUIRE(d.validate().has_value());
    const auto macros = d.macros();
    REQUIRE(macros.size() == 2);
    CHECK(macros[0].name == "drama");
    CHECK(macros[0].label == "Drama");
    CHECK_THAT(macros[0].defaultValue, WithinAbs(0.4, 1e-6));
    REQUIRE(macros[0].targets.size() == 2);
    // Which means it expands to ordinary routes with remaps, like any macro.
    const auto routes = macros[0].routes();
    REQUIRE(routes.size() == 2);
    CHECK(routes[0].source == "macro.drama");
    CHECK(routes[0].target == "scene/keyLight");
    CHECK(routes[0].chain.remapEnabled);
    CHECK_THAT(routes[0].chain.remapOutMin, WithinAbs(0.3, 1e-6));
    CHECK_THAT(routes[0].chain.remapOutMax, WithinAbs(2.5, 1e-6));

    CHECK(d.find(app::DirectorKnob::Warmth) != nullptr);
    CHECK(d.find(app::DirectorKnob::Alien) == nullptr);
}

TEST_CASE("A director rejects duplicates and bad defaults", "[director]") {
    app::WorldDirector d = makeDirector();
    d.mappings.push_back(d.mappings[0]);
    CHECK_FALSE(d.validate().has_value());

    d = makeDirector();
    d.mappings[0].defaultValue = 1.4f;
    CHECK_FALSE(d.validate().has_value());

    d = makeDirector();
    d.mappings[0].targets[0].path.clear();
    CHECK_FALSE(d.validate().has_value());
}

TEST_CASE("A director round-trips through JSON and a file", "[director]") {
    const app::WorldDirector d = makeDirector();
    auto back = app::WorldDirector::fromJson(d.toJson());
    REQUIRE(back.has_value());
    CHECK(back->name == "temple");
    REQUIRE(back->mappings.size() == 2);
    CHECK(back->mappings[0].knob == app::DirectorKnob::Drama);
    CHECK(back->mappings[0].targets[1].path == "post/output/vignette");

    const auto dir = fs::temp_directory_path() / "avgen_director";
    fs::create_directories(dir);
    const auto file = dir / "temple.json";
    std::ofstream(file) << d.toJson().dump(2);
    auto loaded = app::WorldDirector::loadFile(file);
    REQUIRE(loaded.has_value());
    CHECK(loaded->mappings.size() == 2);
    CHECK_FALSE(app::WorldDirector::loadFile(dir / "missing.json").has_value());

    // An unknown knob name is an error, not a silent drop.
    nlohmann::json bad = d.toJson();
    bad["knobs"][0]["knob"] = "sparkle";
    CHECK_FALSE(app::WorldDirector::fromJson(bad).has_value());
    fs::remove_all(dir);
}

TEST_CASE("A look carries only visual parameters and applies partially", "[director][look]") {
    params::ParameterSet params;
    params.add(params::ParamDesc<float>{.path = "post/bloom/intensity", .defaultValue = 0.4f, .hardMin = 0.0f,
                                        .hardMax = 4.0f});
    params.add(params::ParamDesc<float>{.path = "scene/keyLight", .defaultValue = 1.0f, .hardMin = 0.0f,
                                        .hardMax = 8.0f});
    params.add(params::ParamDesc<float>{.path = "procedural/columns/distribution/radius", .defaultValue = 20.0f,
                                        .hardMin = 0.0f, .hardMax = 200.0f});
    params.find("post/bloom/intensity")->setBaseComponent(0, 0.9f);
    params.find("scene/keyLight")->setBaseComponent(0, 2.0f);
    params.find("procedural/columns/distribution/radius")->setBaseComponent(0, 42.0f);

    const app::LookPreset look = app::captureLook(params, "Monumental");
    CHECK(look.name == "Monumental");
    // Geometry is not part of a look: the world stays the world.
    CHECK(look.preset.values.count("post/bloom/intensity") == 1);
    CHECK(look.preset.values.count("scene/keyLight") == 1);
    CHECK(look.preset.values.count("procedural/columns/distribution/radius") == 0);

    // Applying it elsewhere restores the visual values and reports what is missing.
    params::ParameterSet other;
    other.add(params::ParamDesc<float>{.path = "post/bloom/intensity", .defaultValue = 0.1f, .hardMin = 0.0f,
                                       .hardMax = 4.0f});
    const app::LookApplyResult result = app::applyLook(other, look);
    CHECK(result.applied == 1);
    CHECK(result.missing == 1); // scene/keyLight does not exist here
    CHECK_THAT(other.find("post/bloom/intensity")->baseComponent(0), WithinAbs(0.9, 1e-5));

    // JSON round trip keeps the filter.
    auto back = app::LookPreset::fromJson(look.toJson());
    REQUIRE(back.has_value());
    CHECK(back->name == "Monumental");
    CHECK(back->filtered().values.size() == look.filtered().values.size());
}

TEST_CASE("Looks are scanned from a directory", "[director][look]") {
    const auto dir = fs::temp_directory_path() / "avgen_looks";
    fs::remove_all(dir);
    fs::create_directories(dir);
    for (const char* name : {"Sacred", "Alien"}) {
        nlohmann::json j;
        j["format"] = "avgen-look";
        j["name"] = name;
        j["values"] = nlohmann::json{{"post/bloom/intensity", nlohmann::json::array({0.5})}};
        std::ofstream(dir / (std::string(name) + ".json")) << j.dump(2);
    }
    std::ofstream(dir / "notes.txt") << "ignored";
    const auto looks = app::scanLooks({dir});
    REQUIRE(looks.size() == 2);
    CHECK(looks[0].name == "Alien"); // sorted
    CHECK(looks[1].name == "Sacred");
    CHECK(app::scanLooks({dir / "missing"}).empty());
    fs::remove_all(dir);
}
