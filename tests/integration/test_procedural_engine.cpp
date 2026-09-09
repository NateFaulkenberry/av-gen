// Procedural geometry through the engine (ADR-023): scene files with procedural nodes register
// their parameters, audio routes drive them, projects round-trip, and the example worlds load.

#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>

using namespace avgen;
using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;

namespace {
fs::path writeScene(const fs::path& dir, const std::string& name, const std::string& body) {
    fs::create_directories(dir);
    const auto p = dir / name;
    std::ofstream(p) << body;
    return p;
}
const char* kColumns = R"({"format":"avgen-scene","version":1,"name":"t","nodes":[
  {"name":"columns","kind":"procedural","procedural":{
    "source":{"kind":"cylinder","radius":0.4,"height":6,"radialSegments":12,"heightSegments":4},
    "distribution":{"kind":"radial","count":24,"radius":10,"orientation":"outward"},
    "variation":{"seed":3,"randomScale":[0,0.2,0]},
    "deformers":[{"kind":"twist","amount":0.1},{"kind":"noise","amount":0.0,"scale":0.5,"speed":0.3,"seed":2,"space":"world"}],
    "material":{"baseColor":[0.2,0.2,0.25],"emissiveColor":[1,0.5,1],"emissiveIntensity":0.5}}}]})";
} // namespace

TEST_CASE("A procedural node registers parameters and builds instances", "[integration][procedural]") {
    const auto dir = fs::temp_directory_path() / "avgen_proc_engine";
    const auto scene = writeScene(dir, "columns.json", kColumns);
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(scene).has_value());
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
    REQUIRE(engine.scene().procedurals.size() == 1);
    const auto& pg = engine.scene().procedurals[0];
    CHECK(pg.instances.size() == 24);
    CHECK(pg.structureVersion > 0);
    REQUIRE(engine.params().find("procedural/columns/distribution/count") != nullptr);
    REQUIRE(engine.params().find("procedural/columns/distribution/radius") != nullptr);
    REQUIRE(engine.params().find("procedural/columns/deform/1/amount") != nullptr);
    REQUIRE(engine.params().find("procedural/columns/deform/2/amount") != nullptr);
    REQUIRE(engine.params().find("procedural/columns/material/emissive") != nullptr);
    // Structural parameters regenerate instances; the count changes the record count.
    engine.params().find("procedural/columns/distribution/count")->setBaseComponent(0, 40.0f);
    engine.update(engine.tick(clock));
    CHECK(engine.scene().procedurals[0].instances.size() == 40);
    // The node transform folds into the distribution transform every frame.
    engine.params().find("nodes/columns/position")->setBaseComponent(1, 5.0f);
    engine.update(engine.tick(clock));
    CHECK_THAT(engine.scene().procedurals[0].distributionTransform.position.y, WithinAbs(5.0, 1e-4));
    fs::remove_all(dir);
}

TEST_CASE("Audio drives procedural parameters through ordinary routes", "[integration][procedural]") {
    const auto dir = fs::temp_directory_path() / "avgen_proc_audio";
    const auto scene = writeScene(dir, "columns.json", kColumns);
    constexpr std::uint32_t rate = 48000;
    auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(testsupport::sine(60.0f, rate, rate * 2, 0.9f), 2), 2, rate);
    const auto wav = dir / "bass.wav";
    REQUIRE(file.writeWav(wav).has_value());
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    REQUIRE(engine.loadComposition(scene).has_value());
    params::ModRoute r{.source = "audio.bass", .target = "procedural/columns/distribution/radius", .amount = 5.0f};
    r.chain.attackMs = 10.0f;
    r.chain.decayMs = 200.0f;
    engine.modulator().addRoute(r);
    params::ModRoute t{.source = "audio.bass", .target = "procedural/columns/deform/1/amount", .amount = 1.0f};
    engine.modulator().addRoute(t);
    engine.rebind();
    CHECK(engine.modulator().bound());
    FixedStepClock clock(60.0);
    float maxRadius = 0.0f;
    float maxTwist = 0.0f;
    for (int i = 0; i < 90; ++i) {
        engine.update(engine.tick(clock));
        maxRadius = std::max(maxRadius, engine.params().find("procedural/columns/distribution/radius")->finalComponent(0));
        maxTwist = std::max(maxTwist, engine.params().find("procedural/columns/deform/1/amount")->finalComponent(0));
    }
    CHECK(maxRadius > 11.0f); // rest 10 + bass
    CHECK(maxTwist > 0.3f);
    // The live struct followed the final values: instance origins moved outwards.
    const auto& pg = engine.scene().procedurals[0];
    float farthest = 0.0f;
    for (const auto& inst : pg.instances) {
        farthest = std::max(farthest, glm::length(glm::vec3(inst.position)));
    }
    CHECK(farthest > 10.5f);
    CHECK(pg.deformers[0].amount > 0.3f);
    fs::remove_all(dir);
}

TEST_CASE("Projects round-trip procedural scenes and the example worlds load", "[integration][procedural][json]") {
    const auto dir = fs::temp_directory_path() / "avgen_proc_project";
    const auto scene = writeScene(dir, "columns.json", kColumns);
    const auto project = dir / "p.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadComposition(scene).has_value());
        engine.params().find("procedural/columns/deform/2/amount")->setBaseComponent(0, 0.35f);
        engine.params().find("procedural/columns/distribution/count")->setBaseComponent(0, 30.0f);
        REQUIRE(engine.saveProject(project).has_value());
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
    CHECK_THAT(engine.params().find("procedural/columns/deform/2/amount")->baseComponent(0), WithinAbs(0.35, 1e-5));
    CHECK(engine.scene().procedurals[0].instances.size() == 30);
    fs::remove_all(dir);

#ifdef AVGEN_SOURCE_DIR
    const fs::path examples = fs::path(AVGEN_SOURCE_DIR) / "examples";
    for (const char* name : {"temple/temple.json", "lab/lab.json", "cathedral/cathedral.json", "helix/helix.json",
                             "chamber/chamber.json", "hyperspace/hyperspace.json", "worlds/worlds.json",
                             "benchmark/benchmark.json"}) {
        const auto file = examples / name;
        if (!fs::exists(file)) {
            continue; // authored later in the phase
        }
        INFO(file.string());
        app::Engine ex(app::EngineMode::Offline);
        REQUIRE(ex.loadProject(file).has_value());
        CHECK(ex.projectWarnings().empty());
        ex.update(ex.tick(clock));
        CHECK_FALSE(ex.scene().procedurals.empty());
    }
#endif
}
