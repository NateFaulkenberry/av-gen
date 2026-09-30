// The Procedural Space POC's example project (examples/space, ADR-1000) loads through the ordinary
// engine path: its SDF node validates, every route reaches a registered parameter, the structural
// state presets move the morph, the camera starts in open space in every state, and two engines
// produce the same packed program for the same frames (determinism, brief §27).
#include "app/engine.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "spatial/sdf.hpp"
#include "support/project_round_trip.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;

namespace {

const std::filesystem::path kProject = std::filesystem::path(AVGEN_SOURCE_DIR) / "examples/space/space.json";

bool audioPresent() {
    // The generated scores are not committed (assets/audio/manifest.json): regenerate to run this.
    return std::filesystem::exists(std::filesystem::path(AVGEN_SOURCE_DIR) / "assets/audio/night-shift.wav");
}

// root = twist > bend > fold > morph "state"
const spatial::SdfNode& morphOf(const scene::SdfObject& o) {
    return o.tree.root.children[0].children[0].children[0];
}

} // namespace

TEST_CASE("the procedural space example loads, and its routes and presets reach the SDF", "[space][sdf][example]") {
    if (!audioPresent()) {
        SKIP("assets/audio/night-shift.wav is not generated");
    }
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(kProject);
    if (!loaded) {
        FAIL(loaded.error().message);
    }
    testsupport::stepFrames(engine, 2);
    const scene::Scene& s = engine.scene();
    REQUIRE(s.sdfs.size() == 1);
    const scene::SdfObject& space = s.sdfs[0];
    CHECK(space.validate());
    CHECK(space.renderMode == scene::SdfRenderMode::Raymarch);
    CHECK(morphOf(space).kind == spatial::SdfNodeKind::Morph);
    CHECK(morphOf(space).name == "state");

    // Every route target is a registered parameter (a route to a missing path is a silent no-op).
    const nlohmann::json doc = testsupport::readJson(kProject);
    REQUIRE(doc.contains("routes"));
    for (const auto& route : doc["routes"]) {
        const std::string target = route["target"].get<std::string>();
        INFO(target);
        CHECK(engine.params().find(target) != nullptr);
    }
    // Every preset value too.
    for (const auto& preset : doc["presets"]) {
        for (const auto& [path, value] : preset["values"].items()) {
            INFO(preset["name"].get<std::string>() << ": " << path);
            CHECK(engine.params().find(path) != nullptr);
        }
    }

    // The structural state presets switch the morph; the camera is in open space in every state.
    const glm::vec3 eye(0.0f, 2.2f, 0.0f);
    const std::vector<std::pair<const char*, float>> states = {
        {"state/hall", 0.0f}, {"state/rotunda", 1.0f}, {"state/cathedral", 2.0f}, {"state/lattice", 3.0f}};
    for (const auto& [preset, amount] : states) {
        INFO(preset);
        REQUIRE(engine.recallPreset(preset));
        testsupport::stepFrames(engine, 2);
        const scene::SdfObject& live = engine.scene().sdfs[0];
        CHECK(morphOf(live).amount == amount);
        CHECK(live.tree.evaluate(eye, 0.0) > 0.5f);
    }
}

TEST_CASE("the procedural space example packs the same program on two fresh engines", "[space][sdf][example]") {
    if (!audioPresent()) {
        SKIP("assets/audio/night-shift.wav is not generated");
    }
    const auto run = [] {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(kProject));
        testsupport::stepFrames(engine, 90);
        REQUIRE(engine.scene().sdfs.size() == 1);
        std::vector<spatial::SdfNodeGpu> packed;
        REQUIRE(spatial::packSdfTree(engine.scene().sdfs[0].tree, packed) > 0);
        return packed;
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(spatial::SdfNodeGpu)) == 0);
}
