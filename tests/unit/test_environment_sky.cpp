// HDRI sky, CPU side (ADR-049): finding the sun or moon in an equirectangular map, and the scene
// keys that aim and grade it. The GPU half -- that the sky is drawn at infinity, at its own
// resolution, and at an intensity independent of the lighting -- is tests/rendering/test_hdri_sky_gpu.cpp.
#include "assets/asset_registry.hpp"
#include "assets/image.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/sky.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

constexpr float kPi = 3.14159265358979323846f;

// An equirect with a dim sky and one bright disc of angular radius `radius` centred on `dir`,
// built in the parameterisation shaders/environment.wgsl uses.
scene::TextureData discMap(std::uint32_t width, const glm::vec3& dir, float radius, float peak,
                           float background = 0.02f) {
    const std::uint32_t height = width / 2;
    scene::TextureData tex;
    tex.name = "disc";
    tex.width = width;
    tex.height = height;
    tex.format = scene::TextureFormat::Rgba32Float;
    tex.data.resize(static_cast<std::size_t>(width) * height * 16);
    auto* px = reinterpret_cast<float*>(tex.data.data());
    const glm::vec3 centre = glm::normalize(dir);
    for (std::uint32_t y = 0; y < height; ++y) {
        const float theta = ((static_cast<float>(y) + 0.5f) / static_cast<float>(height)) * kPi;
        for (std::uint32_t x = 0; x < width; ++x) {
            const float phi = (((static_cast<float>(x) + 0.5f) / static_cast<float>(width)) - 0.5f) * 2.0f * kPi;
            const glm::vec3 d(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            const float value = std::acos(std::clamp(glm::dot(d, centre), -1.0f, 1.0f)) <= radius ? peak : background;
            const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
            px[i + 0] = px[i + 1] = px[i + 2] = value;
            px[i + 3] = 1.0f;
        }
    }
    return tex;
}

std::filesystem::path writeDiscHdr(const std::string& name, const glm::vec3& dir) {
    const scene::TextureData tex = discMap(256, dir, 0.05f, 400.0f);
    const auto path = std::filesystem::temp_directory_path() / name;
    const auto floats = assets::floatPixels(tex);
    REQUIRE(assets::writeHdr(path, tex.width, tex.height, floats).has_value());
    return path;
}

std::filesystem::path writeText(const std::string& name, const std::string& text) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path) << text;
    return path;
}

} // namespace

TEST_CASE("environmentDominantDirection finds the disc, not the loudest texel", "[sky][environment]") {
    const glm::vec3 moon = glm::normalize(glm::vec3(0.6f, 0.35f, -0.7f));

    SECTION("a disc is located to within a fraction of its own radius") {
        const glm::vec3 found = scene::environmentDominantDirection(discMap(512, moon, 0.05f, 500.0f));
        CHECK_THAT(static_cast<double>(std::acos(glm::dot(found, moon))), WithinAbs(0.0, 0.01));
    }

    SECTION("the answer barely moves with the map's resolution") {
        // The point of a weighted centroid rather than an argmax: the same sky at two resolutions
        // must aim a light the same way, or swapping 4K for 8K would move the shadows.
        const glm::vec3 small = scene::environmentDominantDirection(discMap(128, moon, 0.06f, 500.0f));
        const glm::vec3 large = scene::environmentDominantDirection(discMap(512, moon, 0.06f, 500.0f));
        CHECK_THAT(static_cast<double>(std::acos(std::clamp(glm::dot(small, large), -1.0f, 1.0f))),
                   WithinAbs(0.0, 0.01));
    }

    SECTION("a disc near the pole is not dragged by the projection's stretching") {
        // Without the sin(theta) solid-angle weight, the texels a near-polar disc is smeared over
        // pull the centroid towards the pole.
        const glm::vec3 high = glm::normalize(glm::vec3(0.15f, 0.98f, 0.1f));
        const glm::vec3 found = scene::environmentDominantDirection(discMap(512, high, 0.05f, 500.0f));
        CHECK_THAT(static_cast<double>(std::acos(std::clamp(glm::dot(found, high), -1.0f, 1.0f))),
                   WithinAbs(0.0, 0.02));
    }

    SECTION("rotation is applied as the inverse of the shader's envRotate") {
        const float angle = 0.9f;
        const glm::vec3 base = scene::environmentDominantDirection(discMap(256, moon, 0.06f, 500.0f), 0.0f);
        const glm::vec3 turned = scene::environmentDominantDirection(discMap(256, moon, 0.06f, 500.0f), angle);
        const glm::vec3 expected(std::cos(angle) * base.x - std::sin(angle) * base.z, base.y,
                                 std::sin(angle) * base.x + std::cos(angle) * base.z);
        CHECK_THAT(static_cast<double>(glm::length(turned - expected)), WithinAbs(0.0, 1e-4));
    }

    SECTION("a map with nothing in it falls back to up rather than to a division by zero") {
        scene::TextureData black = discMap(32, moon, 0.0f, 0.0f, 0.0f);
        CHECK(scene::environmentDominantDirection(black) == glm::vec3(0.0f, 1.0f, 0.0f));
        CHECK(scene::environmentDominantDirection(scene::TextureData{}) == glm::vec3(0.0f, 1.0f, 0.0f));
    }
}

TEST_CASE("a scene file aims and grades its HDRI sky", "[scene][composition][sky]") {
    // `rotation` had a parameter but no scene-file key before ADR-049, so this whole section
    // asserts values that could not previously come from a file at all.
    assets::AssetRegistry registry;
    registry.setBaseDirectory(std::filesystem::temp_directory_path());
    const glm::vec3 moon = glm::normalize(glm::vec3(0.8f, 0.3f, 0.5f));
    const auto map = writeDiscHdr("avgen_sky_disc.hdr", moon);
    const auto scenePath = writeText("avgen_sky_scene.json", R"({"format": "avgen-scene", "version": 1,
        "name": "sky", "environment": {"map": ")" + map.filename().string() + R"(",
            "intensity": 0.4, "rotation": 0.5, "skyIntensity": 0.08, "skyBloom": 0.3,
            "lightFromEnvironment": true},
        "nodes": [{"name": "orb", "kind": "orb"}]})");

    auto loaded = scene::Composition::loadFile(scenePath, registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    comp.update(FrameTime{});
    const scene::Scene& sc = comp.scene();
    const scene::Environment& env = sc.environment;

    CHECK(env.environmentMap != scene::kInvalidTexture);
    CHECK(env.environmentIntensity == 0.4f);
    CHECK(env.environmentRotation == 0.5f);
    CHECK(env.skyIntensity == 0.08f);
    CHECK(env.skyBloom == 0.3f);
    CHECK(env.lightFromEnvironment);

    SECTION("the key light points away from the moon the map actually contains") {
        const scene::PunctualLight* key = scene::skyKeyLight(sc.lights);
        REQUIRE(key != nullptr);
        const float a = env.environmentRotation;
        const glm::vec3 expected(std::cos(a) * moon.x - std::sin(a) * moon.z, moon.y,
                                 std::sin(a) * moon.x + std::cos(a) * moon.z);
        CHECK_THAT(static_cast<double>(glm::length(glm::normalize(-key->direction) - expected)),
                   WithinAbs(0.0, 0.02));
    }

    SECTION("turning the sky turns the light with it") {
        // The one property that makes a visible moon and its moonlight impossible to separate.
        params::ParameterSet params;
        params::Modulator modulator;
        comp.attach(params, modulator);
        const scene::PunctualLight* before = scene::skyKeyLight(sc.lights);
        REQUIRE(before != nullptr);
        const glm::vec3 first = glm::normalize(before->direction);
        params.findAs<float>("env/rotation")->setBase(2.1f);
        params.resetFinals();
        comp.update(FrameTime{});
        const scene::PunctualLight* after = scene::skyKeyLight(sc.lights);
        REQUIRE(after != nullptr);
        CHECK(env.environmentRotation == 2.1f);
        CHECK(glm::length(glm::normalize(after->direction) - first) > 0.1f);
    }
}

TEST_CASE("environment settings survive a scene-file round trip", "[scene][composition][sky]") {
    assets::AssetRegistry registry;
    registry.setBaseDirectory(std::filesystem::temp_directory_path());
    const auto scenePath = writeText("avgen_sky_roundtrip.json", R"({"format": "avgen-scene", "version": 1,
        "name": "sky", "environment": {"rotation": -1.25, "skyIntensity": 0.06, "skyBloom": 0.4,
            "lightFromEnvironment": true},
        "nodes": [{"name": "orb", "kind": "orb"}]})");
    auto loaded = scene::Composition::loadFile(scenePath, registry);
    REQUIRE(loaded.has_value());
    const nlohmann::json j = (*loaded)->toJson();
    const nlohmann::json& e = j.at("environment");
    CHECK(e.at("rotation").get<float>() == -1.25f);
    CHECK(e.at("skyIntensity").get<float>() == 0.06f);
    CHECK(e.at("skyBloom").get<float>() == 0.4f);
    CHECK(e.at("lightFromEnvironment").get<bool>());

    auto again = scene::Composition::fromJson(j, registry);
    REQUIRE(again.has_value());
    (*again)->update(FrameTime{});
    CHECK((*again)->scene().environment.environmentRotation == -1.25f);
    CHECK((*again)->scene().environment.skyIntensity == 0.06f);
}
