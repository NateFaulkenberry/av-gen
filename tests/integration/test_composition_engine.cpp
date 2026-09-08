// Milestone 0.7: the engine opens scene files, drives compositions with audio, edits nodes and
// saves scene files that reopen.

#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "scene/composition.hpp"
#include "support/gltf_fixture.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace avgen;

namespace {

std::filesystem::path writeSceneFile(const std::filesystem::path& dir, const std::string& name, const std::string& body) {
    const auto path = dir / name;
    std::ofstream out(path);
    out << body;
    return path;
}

} // namespace

TEST_CASE("Engine opens scene files by format, drives them with audio and saves them back",
          "[integration][composition]") {
    const auto dir = std::filesystem::temp_directory_path() / "avgen_composition_engine";
    std::filesystem::create_directories(dir);
    const auto glb = testsupport::writeTriangleGlb("composition_engine");
    std::filesystem::copy_file(glb, dir / "tri.glb", std::filesystem::copy_options::overwrite_existing);
    const auto sceneFile = writeSceneFile(dir, "stage.json", R"({
        "format": "avgen-scene", "version": 1, "name": "stage",
        "nodes": [
            { "name": "a", "kind": "gltf", "asset": "tri.glb" },
            { "name": "b", "kind": "gltf", "asset": "tri.glb", "position": [3, 0, 0] },
            { "name": "orb", "kind": "orb" },
            { "name": "dust", "kind": "particles", "particles": { "maxParticles": 100, "spawnRate": 10 } }
        ]})");

    constexpr std::uint32_t rate = 48000;
    auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(testsupport::sine(60.0f, rate, rate, 0.9f), 2), 2, rate);
    const auto wav = dir / "bass.wav";
    REQUIRE(file.writeWav(wav).has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());

    // .json is routed by its "format" field: a scene file becomes a composition.
    REQUIRE(engine.loadFile(sceneFile).has_value());
    REQUIRE(engine.composition() != nullptr);
    CHECK(engine.orbScene() == nullptr);
    CHECK(engine.composition()->nodeCount() == 4);
    CHECK(engine.compositionPath() == sceneFile);
    REQUIRE(engine.params().find("nodes/a/position") != nullptr);
    REQUIRE(engine.params().find("nodes/b/visible") != nullptr);
    REQUIRE(engine.params().find("particles/dust/spawnRate") != nullptr);
    REQUIRE(engine.params().find("root/scale") != nullptr);
    CHECK(engine.modulator().bound());

    FixedStepClock clock(60.0);
    float maxScale = 0.0f;
    for (int i = 0; i < 60; ++i) {
        engine.update(engine.tick(clock));
        maxScale = std::max(maxScale, engine.params().find("root/scale")->finalComponent(0));
    }
    CHECK(maxScale > 1.1f); // bass drives root/scale through the default route
    // Two glTF instances share one mesh.
    const auto& sc = engine.controller().scene();
    CHECK(sc.meshes.size() == 2); // triangle + orb sphere
    CHECK(sc.particles.size() == 1);

    // Node editing through the engine keeps the parameter surface bound.
    scene::CompositionNode grid;
    grid.name = "floor";
    grid.kind = scene::NodeKind::Grid;
    REQUIRE(engine.addNode(std::move(grid)).has_value());
    CHECK(engine.composition()->nodeCount() == 5);
    CHECK(engine.params().find("nodes/floor/visible") != nullptr);
    CHECK(engine.modulator().bound());
    engine.removeNode("b");
    CHECK(engine.composition()->nodeCount() == 4);
    CHECK(engine.params().find("nodes/b/visible") == nullptr);
    engine.update(engine.tick(clock));

    // Saving writes asset paths relative to the scene file; the saved file reopens.
    const auto saved = dir / "saved.json";
    REQUIRE(engine.saveComposition(saved).has_value());
    {
        std::ifstream in(saved);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(text.find("avgen-scene") != std::string::npos);
        CHECK(text.find("\"floor\"") != std::string::npos);
        CHECK(text.find("\"b\"") == std::string::npos);
        CHECK(text.find("\"asset\": \"tri.glb\"") != std::string::npos); // relative, not absolute
    }
    REQUIRE(engine.loadFile(saved).has_value());
    REQUIRE(engine.composition() != nullptr);
    CHECK(engine.composition()->nodeCount() == 4);
    CHECK(engine.params().find("nodes/floor/visible") != nullptr);
    for (int i = 0; i < 5; ++i) {
        engine.update(engine.tick(clock));
    }

    // Broken scene files leave the current scene untouched; the orb scene is still reachable.
    const auto broken = writeSceneFile(dir, "broken.json", R"({ "format": "avgen-scene", "version": 99 })");
    CHECK_FALSE(engine.loadFile(broken).has_value());
    CHECK(engine.composition() != nullptr);
    engine.loadOrbScene();
    CHECK(engine.orbScene() != nullptr);
    CHECK(engine.params().find("nodes/a/position") == nullptr);

    // A fresh composition from nothing: adding a node converts the orb scene.
    engine.newComposition();
    REQUIRE(engine.composition() != nullptr);
    CHECK(engine.composition()->nodeCount() == 0);
    engine.loadOrbScene();
    scene::CompositionNode orb;
    orb.name = "orb";
    orb.kind = scene::NodeKind::Orb;
    REQUIRE(engine.addNode(std::move(orb)).has_value());
    REQUIRE(engine.composition() != nullptr);
    CHECK(engine.params().find("nodes/orb/scale") != nullptr);
    CHECK(engine.saveComposition(dir / "fresh.json").has_value());
    std::filesystem::remove_all(dir);
    std::filesystem::remove(glb);
}
