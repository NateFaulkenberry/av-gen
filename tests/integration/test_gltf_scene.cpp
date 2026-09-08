// GltfScene controller: import, parameter surface, root transform, and the engine's scene swap.
#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "scene/gltf_scene.hpp"
#include "support/gltf_fixture.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>

using namespace avgen;
using Catch::Matchers::WithinAbs;

TEST_CASE("GltfScene imports, exposes parameters and applies the root transform", "[scene][gltf]") {
    const auto path = testsupport::writeTriangleGlb("controller");
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
    CHECK(modulator.routes().size() == 7); // five scene routes + two dust routes
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
    const auto glb = testsupport::writeTriangleGlb("engine");
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
