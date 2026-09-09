// Fields through the engine (ADR-025): field nodes register parameters, effectors and particle
// forces reference them by name, nested scenes rename references, audio routes drive field
// parameters, and projects round-trip.

#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "spatial/field.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
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
const char* kWorld = R"({"format":"avgen-scene","version":1,"name":"w","nodes":[
  {"name":"pulse","kind":"field","position":[0,2,0],"field":{
    "kind":"wave","waveGeometry":"radial","amplitude":1.0,"wavelength":4,"waveSpeed":3,"waveWidth":5,
    "strength":1.5,"falloff":{"kind":"smoothstep","inner":0,"outer":40}}},
  {"name":"swirl","kind":"field","field":{"kind":"vortex","strength":2.0}},
  {"name":"columns","kind":"procedural","procedural":{
    "source":{"kind":"box","size":[0.5,4,0.5]},
    "distribution":{"kind":"radial","count":32,"radius":12},
    "ops":[{"kind":"filterProbability","probability":0.75,"seed":5}],
    "effectors":[{"field":"pulse","op":"scale","strength":0.5},{"field":"swirl","op":"positionOffset","strength":1.0}],
    "emissiveField":"pulse","emissiveFieldAmount":2.0,
    "deformers":[{"kind":"field","field":"swirl","amount":0.2}]}},
  {"name":"dust","kind":"particles","particles":{"capacity":4096,"spawnRate":500,
    "fieldForces":[{"field":"swirl","mode":"force","strength":2.0}]}}]})";
} // namespace

TEST_CASE("Field nodes register parameters and flatten into Scene::fields", "[integration][fields]") {
    const auto dir = fs::temp_directory_path() / "avgen_fields_engine";
    const auto scene = writeScene(dir, "world.json", kWorld);
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(scene).has_value());
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
    const Scene& s = engine.scene();
    REQUIRE(s.fields.fields.size() == 2);
    CHECK(s.fields.fields[0].name == "pulse");
    CHECK(s.fields.fields[0].kind == spatial::FieldKind::Wave);
    CHECK_THAT(s.fields.fields[0].position.y, WithinAbs(2.0, 1e-5)); // node transform folded in
    CHECK(s.fields.find("swirl") != nullptr);
    REQUIRE(engine.params().find("field/pulse/strength") != nullptr);
    REQUIRE(engine.params().find("field/pulse/amplitude") != nullptr);
    REQUIRE(engine.params().find("field/pulse/falloff/outer") != nullptr);
    REQUIRE(engine.params().find("field/swirl/strength") != nullptr);
    // Ops are structural: the probability filter removed a deterministic subset.
    REQUIRE(s.procedurals.size() == 1);
    const auto& pg = s.procedurals[0];
    CHECK(pg.instances.size() < 32);
    CHECK(pg.instances.size() > 16);
    CHECK(pg.effectors.size() == 2);
    CHECK(pg.effectors[0].field == "pulse");
    REQUIRE(engine.params().find("procedural/columns/effector/1/strength") != nullptr);
    REQUIRE(engine.params().find("procedural/columns/effector/2/strength") != nullptr);
    REQUIRE(engine.params().find("procedural/columns/ops/1/amount") != nullptr);
    REQUIRE(engine.params().find("procedural/columns/emissiveFieldAmount") != nullptr);
    REQUIRE(s.particles.size() == 1);
    REQUIRE(s.particles[0].fieldForces.size() == 1);
    CHECK(s.particles[0].fieldForces[0].field == "swirl");
    REQUIRE(engine.params().find("particles/dust/fieldForce/1/strength") != nullptr);

    // Field parameters are live: the node position and the strength reach the scene copy.
    engine.params().find("field/pulse/strength")->setBaseComponent(0, 0.25f);
    engine.params().find("nodes/pulse/position")->setBaseComponent(1, 7.0f);
    engine.update(engine.tick(clock));
    CHECK_THAT(engine.scene().fields.fields[0].strength, WithinAbs(0.25, 1e-5));
    CHECK_THAT(engine.scene().fields.fields[0].position.y, WithinAbs(7.0, 1e-4));
    // Effector strength is per-frame, not structural.
    const auto version = engine.scene().procedurals[0].structureVersion;
    engine.params().find("procedural/columns/effector/1/strength")->setBaseComponent(0, 2.0f);
    engine.update(engine.tick(clock));
    CHECK(engine.scene().procedurals[0].structureVersion == version);
    CHECK_THAT(engine.scene().procedurals[0].effectors[0].strength, WithinAbs(2.0, 1e-5));
    fs::remove_all(dir);
}

TEST_CASE("Nested scenes rename their fields and every reference to them", "[integration][fields]") {
    const auto dir = fs::temp_directory_path() / "avgen_fields_nested";
    writeScene(dir, "inner.json", kWorld);
    const auto outer = writeScene(dir, "outer.json", R"({"format":"avgen-scene","version":1,"name":"o","nodes":[
      {"name":"left","kind":"scene","asset":"inner.json","position":[-10,0,0]},
      {"name":"right","kind":"scene","asset":"inner.json","position":[10,0,0]}]})");
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(outer).has_value());
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
    const Scene& s = engine.scene();
    REQUIRE(s.fields.fields.size() == 4);
    CHECK(s.fields.find("left_pulse") != nullptr);
    CHECK(s.fields.find("right_swirl") != nullptr);
    CHECK_THAT(s.fields.find("left_pulse")->position.x, WithinAbs(-10.0, 1e-4));
    REQUIRE(s.procedurals.size() == 2);
    CHECK(s.procedurals[0].effectors[0].field == "left_pulse");
    CHECK(s.procedurals[0].deformers[0].field == "left_swirl");
    CHECK(s.procedurals[0].emissiveField == "left_pulse");
    CHECK(s.procedurals[1].effectors[1].field == "right_swirl");
    REQUIRE(s.particles.size() == 2);
    CHECK(s.particles[1].fieldForces[0].field == "right_swirl");
    REQUIRE(engine.params().find("nodes/left/field/pulse/strength") != nullptr);
    // A second frame keeps names stable (prefixing is idempotent).
    engine.update(engine.tick(clock));
    CHECK(engine.scene().procedurals[0].effectors[0].field == "left_pulse");
    fs::remove_all(dir);
}

TEST_CASE("Audio routes drive field parameters and projects round-trip fields", "[integration][fields]") {
    const auto dir = fs::temp_directory_path() / "avgen_fields_project";
    const auto scene = writeScene(dir, "world.json", kWorld);
    constexpr std::uint32_t rate = 48000;
    auto file = audio::AudioFile::fromInterleaved(
        testsupport::interleave(testsupport::sine(60.0f, rate, rate * 2, 0.9f), 2), 2, rate);
    const auto wav = dir / "bass.wav";
    REQUIRE(file.writeWav(wav).has_value());
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    REQUIRE(engine.loadComposition(scene).has_value());
    params::ModRoute r{.source = "audio.bass", .target = "field/pulse/amplitude", .amount = 3.0f};
    engine.modulator().addRoute(r);
    engine.rebind();
    CHECK(engine.modulator().bound());
    FixedStepClock clock(60.0);
    float maxAmplitude = 0.0f;
    for (int i = 0; i < 90; ++i) {
        engine.update(engine.tick(clock));
        maxAmplitude = std::max(maxAmplitude, engine.scene().fields.fields[0].amplitude);
    }
    CHECK(maxAmplitude > 1.3f); // rest 1 + bass

    const auto project = dir / "world.project.json";
    REQUIRE(engine.saveProject(project).has_value());
    app::Engine other(app::EngineMode::Offline);
    REQUIRE(other.loadProject(project).has_value());
    REQUIRE(other.modulator().routes().size() >= 1);
    other.update(other.tick(clock));
    REQUIRE(other.scene().fields.fields.size() == 2);
    // The scene file itself round-trips the field node and the object's ops/effectors.
    REQUIRE(engine.saveComposition(dir / "roundtrip.json").has_value());
    app::Engine third(app::EngineMode::Offline);
    REQUIRE(third.loadComposition(dir / "roundtrip.json").has_value());
    third.update(third.tick(clock));
    CHECK(third.scene().fields.fields.size() == 2);
    REQUIRE(third.scene().procedurals.size() == 1);
    CHECK(third.scene().procedurals[0].effectors.size() == 2);
    CHECK(third.scene().procedurals[0].pointOps.size() == 1);
    CHECK(third.scene().procedurals[0].emissiveField == "pulse");
    CHECK(third.scene().particles[0].fieldForces.size() == 1);
    fs::remove_all(dir);
}
