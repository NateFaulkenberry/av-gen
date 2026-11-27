// Volumetric lighting (ADR-2210..2214), the CPU side: the tier grids, the medium on the CPU (ADR-2211), why a
// scene is declined, the scene block's round trip, and the lab's generated fixtures.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "rendering/render_quality.hpp"
#include "rendering/volumetric_light_medium.hpp"
#include "scene/composition.hpp"
#include "scene/scene.hpp"
#include "signals/signal_bus.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace fs = std::filesystem;
using namespace avgen;
using Catch::Approx;
using nlohmann::json;

namespace {

fs::path scratch() {
    const fs::path dir = fs::temp_directory_path() / "avgen-volumetric-light";
    fs::create_directories(dir);
    return dir;
}

fs::path writeScene(const std::string& name, const json& body) {
    json doc = body;
    doc["format"] = "avgen-scene";
    doc["version"] = 1;
    doc["name"] = name;
    const fs::path out = scratch() / (name + ".scene.json");
    std::ofstream os(out);
    os << doc.dump(1);
    return out;
}

struct Built {
    std::unique_ptr<scene::Composition> comp;
    params::ParameterSet parameters;
    params::Modulator modulator;
    signals::SignalBus bus;
};

std::unique_ptr<Built> build(const fs::path& file) {
    auto b = std::make_unique<Built>();
    assets::AssetRegistry registry(file.parent_path());
    auto loaded = scene::Composition::loadFile(file, registry);
    REQUIRE(loaded.has_value());
    b->comp = std::move(*loaded);
    b->comp->attach(b->parameters, b->modulator);
    b->comp->setViewport(640, 360);
    FrameTime time;
    time.deltaTime = 1.0 / 60.0;
    b->parameters.resetFinals();
    b->comp->updateFields(time, b->bus, b->modulator);
    b->modulator.applyRoutes(b->bus, b->parameters, time.deltaTime);
    b->comp->updateBehaviour(time, b->bus);
    b->comp->update(time);
    return b;
}

json fogEnvironment(json volumetric) {
    json env = {{"volumeDensity", 0.02}, {"volumeMaxDistance", 120.0}};
    if (!volumetric.is_null()) {
        env["volumetricLight"] = std::move(volumetric);
    }
    return env;
}

} // namespace

TEST_CASE("Each tier's froxel grid at 1080p is the one ADR-2213 names", "[volumetric][light]") {
    using rendering::QualitySettings;
    using rendering::QualityTier;
    const auto grid = [](QualityTier t) {
        return rendering::volumetricLightGrid(1920, 1080, QualitySettings::forTier(t));
    };
    CHECK(grid(QualityTier::Preview) == glm::uvec3(80, 45, 32));
    CHECK(grid(QualityTier::Realtime) == glm::uvec3(160, 90, 64));
    CHECK(grid(QualityTier::High) == glm::uvec3(240, 135, 128));
    CHECK(grid(QualityTier::Offline) == glm::uvec3(240, 135, 128));
    // Only sampling moves between tiers (ADR-035): the offline tier samples each froxel four times with a
    // 2x2 shadow tap; nothing about what lights the air changes.
    CHECK(QualitySettings::forTier(QualityTier::Offline).volumetricLightSubsamples == 4);
    CHECK(QualitySettings::forTier(QualityTier::Realtime).volumetricLightSubsamples == 1);
    CHECK(QualitySettings::forTier(QualityTier::High).volumetricLightShadowTaps == 4);
    // The grid follows the frame: 4K at the Realtime tile is twice 1080p's in each axis, and a tiny frame
    // still has a froxel.
    CHECK(rendering::volumetricLightGrid(3840, 2160, QualitySettings::forTier(QualityTier::Realtime)) ==
          glm::uvec3(320, 180, 64));
    CHECK(rendering::volumetricLightGrid(1, 1, QualitySettings{}) == glm::uvec3(1, 1, 64));
}

TEST_CASE("The medium on the CPU is the height-fog profile the shaders share (ADR-2211)", "[volumetric][light]") {
    scene::Environment env;
    env.volumeDensity = 0.05f;
    env.fogHeight = 10.0f;
    env.fogHeightFalloff = 0.2f;
    // Inside the layer: the full density, whatever the profile's shape controls are.
    CHECK(rendering::airDensityAt(env, {0.0f, 5.0f, 0.0f}, 0.0f) == Approx(0.05f));
    // Above it, the exponential: exp(-b * d).
    CHECK(rendering::airDensityAt(env, {0.0f, 15.0f, 0.0f}, 0.0f) == Approx(0.05f * std::exp(-1.0f)));
    // `curve` 1 is the compact quadratic that ends at 2/b = 10 m above the top.
    env.fogHeightCurve = 1.0f;
    CHECK(rendering::airDensityAt(env, {0.0f, 20.0f, 0.0f}, 0.0f) == Approx(0.0f).margin(1e-7));
    CHECK(rendering::airDensityAt(env, {0.0f, 15.0f, 0.0f}, 0.0f) == Approx(0.05f * 0.25f));
    // `upper` is the floor of the profile at any height.
    env.fogUpperDensity = 0.4f;
    CHECK(rendering::airDensityAt(env, {0.0f, 500.0f, 0.0f}, 0.0f) == Approx(0.05f * 0.4f));
    // ADR-705's horizon growth: by distance from the eye, 1 + h * t / 1000.
    env.fogUpperDensity = 0.0f;
    env.fogHeightCurve = 0.0f;
    env.horizonDensity = 2.0f;
    CHECK(rendering::airDensityAt(env, {0.0f, 0.0f, 0.0f}, 500.0f) == Approx(0.05f * 2.0f));
    // No air, no density.
    env.volumeDensity = 0.0f;
    CHECK(rendering::airDensityAt(env, {0.0f, 0.0f, 0.0f}, 0.0f) == 0.0f);
}

TEST_CASE("A scene that asks for volumetric lighting and cannot have it is told why", "[volumetric][light]") {
    using rendering::VolumetricLightDecline;
    scene::Scene s;
    CHECK(rendering::volumetricLightDecline(s) == VolumetricLightDecline::Off);
    s.environment.volumetricLight.enabled = true;
    CHECK(rendering::volumetricLightDecline(s) == VolumetricLightDecline::NoAir);
    s.environment.volumeDensity = 0.01f;
    CHECK(rendering::volumetricLightDecline(s) == VolumetricLightDecline::None);
    s.environment.volumeDensityField = "mist";
    CHECK(rendering::volumetricLightDecline(s) == VolumetricLightDecline::DensityField);
    s.environment.volumeDensityField.clear();
    s.environment.volumeColorField = "tint";
    s.environment.volumeEmission = 1.0f;
    CHECK(rendering::volumetricLightDecline(s) == VolumetricLightDecline::ColorField);
    // Every reason has a name a log line can carry.
    CHECK(std::string(rendering::volumetricLightDeclineName(VolumetricLightDecline::DensityField)).find("density") !=
          std::string::npos);
}

TEST_CASE("The volumetricLight block and volumetricShadow round-trip, and absent stays absent", "[volumetric][light]") {
    const json light = {{"name", "sun"},       {"type", "directional"},      {"direction", {0.0, -1.0, 0.0}},
                        {"castsShadow", true}, {"volumetricShadow", false}};
    auto withBlock = build(writeScene("vl-roundtrip",
                                      {{"environment", fogEnvironment({{"enabled", true}, {"distance", 250.0},
                                                                       {"intensity", 2.5}})},
                                       {"lights", json::array({light})}}));
    const scene::Scene& s = withBlock->comp->scene();
    CHECK(s.environment.volumetricLight.enabled);
    CHECK(s.environment.volumetricLight.distance == 250.0f);
    CHECK(s.environment.volumetricLight.intensity == 2.5f);
    bool found = false;
    for (const scene::PunctualLight& l : s.lights) {
        if (l.name == "sun") {
            found = true;
            CHECK_FALSE(l.volumetricShadow);
        }
    }
    CHECK(found);
    // Every control is an ordinary parameter, so routes, the timeline, OSC and MIDI reach it.
    CHECK(withBlock->parameters.findAs<bool>("scene/volumetric/enabled") != nullptr);
    CHECK(withBlock->parameters.findAs<float>("scene/volumetric/distance") != nullptr);
    CHECK(withBlock->parameters.findAs<float>("scene/volumetric/intensity") != nullptr);
    const json saved = withBlock->comp->toJson();
    REQUIRE(saved.at("environment").contains("volumetricLight"));
    CHECK(saved.at("environment").at("volumetricLight") ==
          json({{"enabled", true}, {"distance", 250.0}, {"intensity", 2.5}}));
    CHECK(saved.at("lights").at(0).at("volumetricShadow") == false);

    // A scene that never wrote the block saves without it (every older scene round-trips to its bytes).
    auto bare = build(writeScene("vl-bare", {{"environment", fogEnvironment(nullptr)},
                                             {"lights", json::array({{{"name", "sun"}, {"type", "directional"}}})}}));
    CHECK_FALSE(bare->comp->scene().environment.volumetricLight.enabled);
    const json bareSaved = bare->comp->toJson();
    CHECK_FALSE(bareSaved.at("environment").contains("volumetricLight"));
    CHECK_FALSE(bareSaved.at("lights").at(0).contains("volumetricShadow"));
}

TEST_CASE("The volumetric lab's environments are exactly what build.py writes", "[volumetric][light][lab]") {
    const fs::path script = fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "volumetric" / "build.py";
    REQUIRE(fs::exists(script));
    const std::string cmd = "python3 \"" + script.string() + "\" --check >/dev/null 2>&1";
    CHECK(std::system(cmd.c_str()) == 0);
    // And every environment loads as a project's scene, with volumetric lighting on.
    for (const char* stem : {"e01-light-shafts", "e02-dense-atmosphere", "e06-coloured-scattering"}) {
        INFO(stem);
        auto b = build(fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "volumetric" /
                       (std::string(stem) + ".scene.json"));
        CHECK(rendering::volumetricLightActive(b->comp->scene()));
    }
}
