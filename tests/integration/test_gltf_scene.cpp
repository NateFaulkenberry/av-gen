// GltfScene controller: import, parameter surface, root transform, and the engine's scene swap.
#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "scene/gltf_scene.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

// Minimal GLB: one node at (2, 0, 0) with a unit right triangle, one material, one point light.
std::filesystem::path writeTriangleGlb(const char* name) {
    std::vector<float> positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    std::vector<float> normals = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    std::vector<std::uint16_t> indices = {0, 1, 2, 0};
    std::vector<std::uint8_t> bin;
    auto append = [&](const void* data, std::size_t bytes) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        bin.insert(bin.end(), p, p + bytes);
        while (bin.size() % 4 != 0) bin.push_back(0);
    };
    append(positions.data(), positions.size() * 4);
    append(normals.data(), normals.size() * 4);
    append(indices.data(), indices.size() * 2);

    std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1]}],
"nodes":[{"mesh":0,"translation":[2,0,0],"name":"tri"},{"name":"lamp","translation":[0,3,0],"extensions":{"KHR_lights_punctual":{"light":0}}}],
"extensionsUsed":["KHR_lights_punctual"],"extensions":{"KHR_lights_punctual":{"lights":[{"type":"point","intensity":7.0,"color":[1,0.5,0.25]}]}},
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2,"material":0}]}],
"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.1,0.2,0.3,1.0],"metallicFactor":0.0,"roughnessFactor":0.6},"emissiveFactor":[1,1,1]}],
"buffers":[{"byteLength":)" + std::to_string(bin.size()) + R"(}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":6}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    while (json.size() % 4 != 0) json.push_back(' ');

    std::vector<std::uint8_t> glb;
    auto u32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) glb.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    };
    u32(0x46546C67u); // "glTF"
    u32(2);
    u32(static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
    u32(static_cast<std::uint32_t>(json.size()));
    u32(0x4E4F534Au); // JSON
    glb.insert(glb.end(), json.begin(), json.end());
    u32(static_cast<std::uint32_t>(bin.size()));
    u32(0x004E4942u); // BIN
    glb.insert(glb.end(), bin.begin(), bin.end());

    const auto path = std::filesystem::temp_directory_path() / (std::string("avgen_") + name + ".glb");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
    return path;
}

} // namespace

TEST_CASE("GltfScene imports, exposes parameters and applies the root transform", "[scene][gltf]") {
    const auto path = writeTriangleGlb("controller");
    auto ctrl = scene::GltfScene::load(path);
    REQUIRE(ctrl.has_value());
    auto& gs = **ctrl;
    CHECK(gs.name() == "avgen_controller");
    REQUIRE(gs.scene().entities.size() == 2); // triangle + grid
    CHECK(gs.scene().entities[0].name == "tri");
    CHECK(gs.scene().entities[0].material.emissiveIntensity == 1.0f);
    REQUIRE(gs.scene().lights.size() == 1);
    CHECK(gs.scene().lights[0].type == scene::PunctualLight::Type::Point);
    CHECK_THAT(static_cast<double>(gs.scene().lights[0].position.y), WithinAbs(3.0, 1e-5));
    CHECK_THAT(static_cast<double>(gs.boundsCenter().x), WithinAbs(2.5, 1e-4));

    params::ParameterSet params;
    params::Modulator modulator;
    gs.attach(params, modulator);
    for (const char* p : {"root/scale", "root/rotationSpeed", "root/impulse", "material/emissiveBoost", "camera/distance",
                          "env/intensity", "scene/brightness", "lights/lamp/intensity"}) {
        INFO(p);
        CHECK(params.find(p) != nullptr);
    }
    CHECK(modulator.routes().size() == 5);
    CHECK(params.findAs<float>("lights/lamp/intensity")->value() == 7.0f);

    FixedStepClock clock(60.0);
    gs.update(clock.tick());
    const glm::vec3 rest = gs.scene().entities[0].transform.position;
    CHECK_THAT(static_cast<double>(rest.x), WithinAbs(2.0, 1e-4));
    params.findAs<float>("root/scale")->setBase(2.0f);
    params.findAs<float>("material/emissiveBoost")->setBase(3.0f);
    params.findAs<float>("lights/lamp/intensity")->setBase(1.0f);
    params.resetFinals();
    gs.update(clock.tick());
    const auto& e = gs.scene().entities[0];
    // Scaling about the bounds centre (x = 2.5): the node at x = 2 moves to 2.5 + (2 - 2.5) * 2 = 1.5.
    CHECK_THAT(static_cast<double>(e.transform.position.x), WithinAbs(1.5, 1e-4));
    CHECK_THAT(static_cast<double>(e.transform.scale.x), WithinAbs(2.0, 1e-5));
    CHECK(e.material.emissiveIntensity == 3.0f);
    CHECK(gs.scene().lights[0].intensity == 1.0f);
    CHECK(gs.scene().camera.target.x == gs.boundsCenter().x);
    std::filesystem::remove(path);
}

TEST_CASE("GltfScene load fails cleanly on bad files", "[scene][gltf]") {
    CHECK_FALSE(scene::GltfScene::load("/nope/missing.glb").has_value());
    const auto bad = std::filesystem::temp_directory_path() / "avgen_bad.glb";
    {
        std::ofstream out(bad, std::ios::binary);
        out << "not a glb";
    }
    CHECK_FALSE(scene::GltfScene::load(bad).has_value());
    std::filesystem::remove(bad);
}

TEST_CASE("Engine swaps scenes and keeps audio driving the new parameter surface", "[integration][gltf]") {
    const auto glb = writeTriangleGlb("engine");
    constexpr std::uint32_t rate = 48000;
    auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(testsupport::sine(60.0f, rate, rate, 0.9f), 2), 2, rate);
    const auto wav = std::filesystem::temp_directory_path() / "avgen_engine_scene.wav";
    REQUIRE(file.writeWav(wav).has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    CHECK(engine.orbScene() != nullptr);
    // A failed scene load leaves the orb scene and its parameters in place.
    CHECK_FALSE(engine.loadScene("/nope/missing.glb").has_value());
    CHECK(engine.orbScene() != nullptr);
    CHECK(engine.params().find("orb/scale") != nullptr);

    REQUIRE(engine.loadScene(glb).has_value());
    CHECK(engine.orbScene() == nullptr);
    REQUIRE(engine.gltfScene() != nullptr);
    CHECK(engine.params().find("orb/scale") == nullptr);
    REQUIRE(engine.params().find("root/scale") != nullptr);
    CHECK(engine.modulator().bound());

    FixedStepClock clock(60.0);
    float maxScale = 0.0f;
    for (int i = 0; i < 60; ++i) {
        engine.update(engine.tick(clock));
        maxScale = std::max(maxScale, engine.params().find("root/scale")->finalComponent(0));
    }
    CHECK(maxScale > 1.1f); // bass sine drives root/scale through the default route

    engine.loadOrbScene();
    CHECK(engine.orbScene() != nullptr);
    CHECK(engine.params().find("orb/scale") != nullptr);
    std::filesystem::remove(glb);
    std::filesystem::remove(wav);
}
