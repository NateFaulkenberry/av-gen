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

#include <algorithm>
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

// The art presets and the showcase (tools/make_space_presets.py writes them). A route or a preset value
// whose path does not exist is a silent no-op, so a renamed node would quietly take a rule out of the
// music's reach; and a state whose preset puts the eye inside the architecture renders a black frame.
TEST_CASE("the procedural space art presets load, every rule the music drives exists, and every state leaves the "
          "eye in open space",
          "[space][sdf][example]") {
    if (!audioPresent()) {
        SKIP("assets/audio/night-shift.wav is not generated");
    }
    const std::vector<std::string> projects = {"infinite-hall",       "recursive-cathedral", "folding-space",
                                               "radial-architecture", "geometry-explosion",  "showcase",
                                               "experiment-melt-void"};
    for (const std::string& name : projects) {
        INFO(name);
        const std::filesystem::path path =
            std::filesystem::path(AVGEN_SOURCE_DIR) / "examples/space" / (name + ".json");
        app::Engine engine(app::EngineMode::Offline);
        auto loaded = engine.loadProject(path);
        if (!loaded) {
            FAIL(loaded.error().message);
        }
        testsupport::stepFrames(engine, 2);
        REQUIRE(engine.scene().sdfs.size() == 1);
        CHECK(engine.scene().sdfs[0].validate());

        const nlohmann::json doc = testsupport::readJson(path);
        for (const auto& route : doc["routes"]) {
            const std::string target = route["target"].get<std::string>();
            INFO(target);
            CHECK(engine.params().find(target) != nullptr);
        }
        std::vector<std::string> presetNames;
        for (const auto& preset : doc["presets"]) {
            presetNames.push_back(preset["name"].get<std::string>());
            for (const auto& [p, value] : preset["values"].items()) {
                INFO(presetNames.back() << ": " << p);
                CHECK(engine.params().find(p) != nullptr);
            }
        }
        if (doc.contains("states")) {
            for (const auto& state : doc["states"]["states"]) {
                const std::string preset = state["preset"].get<std::string>();
                INFO("state " << state["name"].get<std::string>() << " -> " << preset);
                CHECK(std::find(presetNames.begin(), presetNames.end(), preset) != presetNames.end());
            }
        }

        // The eye is in open space at the start and in every preset (the showcase's presets move it).
        auto eyeClear = [&](const std::string& when) {
            INFO(when);
            const params::IParameter* eye = engine.params().find("camera/position");
            REQUIRE(eye != nullptr);
            const glm::vec3 p(eye->baseComponent(0), eye->baseComponent(1), eye->baseComponent(2));
            CHECK(engine.scene().sdfs[0].tree.evaluate(p, 0.0) > 0.3f);
        };
        eyeClear("as loaded");
        for (const std::string& preset : presetNames) {
            REQUIRE(engine.recallPreset(preset));
            testsupport::stepFrames(engine, 2);
            eyeClear(preset);
        }
    }
}

// Brief §19-21, §27 and §36 ("modulation mapping"): the music moves the rules it is routed to, only the
// music moves them, and the same audio moves them the same way twice. Infinite Hall's routes are all
// ungated (no macro depth), so every routed rule must move within 20 s of the project's own score.
TEST_CASE("the Infinite Hall's music moves its rules, only the music does, and it is repeatable",
          "[space][sdf][example]") {
    if (!audioPresent()) {
        SKIP("assets/audio/night-shift.wav is not generated");
    }
    const std::filesystem::path path = std::filesystem::path(AVGEN_SOURCE_DIR) / "examples/space/infinite-hall.json";
    nlohmann::json doc = testsupport::readJson(path);
    std::vector<std::string> targets;
    for (const auto& route : doc["routes"]) {
        const std::string target = route["target"].get<std::string>();
        if (std::find(targets.begin(), targets.end(), target) == targets.end()) {
            targets.push_back(target);
        }
    }
    REQUIRE(targets.size() == 10);

    struct Run {
        std::vector<std::size_t> components;           // per target: its component count
        std::vector<std::vector<float>> samples;      // per sample: every target's final components
        std::vector<std::vector<spatial::SdfNodeGpu>> tables; // per sample: the compiled parameter table
    };
    constexpr int kFrames = 1200; // 20 s at the helper's 60 fps
    constexpr int kEvery = 30;
    const auto run = [&](const std::filesystem::path& project) {
        app::Engine engine(app::EngineMode::Offline);
        auto loaded = engine.loadProject(project);
        if (!loaded) {
            FAIL(loaded.error().message);
        }
        Run out;
        constexpr double kDt = 1.0 / 60.0;
        for (int i = 0; i < kFrames; ++i) {
            FrameTime time;
            time.renderTime = static_cast<double>(i) * kDt;
            time.deltaTime = i == 0 ? 0.0 : kDt;
            time.frameIndex = static_cast<std::uint64_t>(i);
            engine.update(time);
            if (i % kEvery != 0) {
                continue;
            }
            std::vector<float> values;
            out.components.clear();
            for (const std::string& target : targets) {
                const params::IParameter* p = engine.params().find(target);
                REQUIRE(p != nullptr);
                out.components.push_back(p->componentCount());
                for (std::size_t c = 0; c < p->componentCount(); ++c) {
                    values.push_back(p->finalComponent(c));
                }
            }
            out.samples.push_back(std::move(values));
            REQUIRE(engine.scene().sdfs.size() == 1);
            std::vector<spatial::SdfNodeGpu> table;
            spatial::sdfCompileTable(engine.scene().sdfs[0].tree, table);
            out.tables.push_back(std::move(table));
        }
        return out;
    };
    // The targets whose final value moved over the window.
    const auto moved = [&](const Run& r) {
        std::vector<std::string> names;
        std::size_t offset = 0;
        for (std::size_t t = 0; t < targets.size(); ++t) {
            bool any = false;
            for (std::size_t c = 0; c < r.components[t]; ++c) {
                float lo = r.samples.front()[offset + c];
                float hi = lo;
                for (const auto& s : r.samples) {
                    lo = std::min(lo, s[offset + c]);
                    hi = std::max(hi, s[offset + c]);
                }
                any = any || (hi - lo) > 1e-3f;
            }
            if (any) {
                names.push_back(targets[t]);
            }
            offset += r.components[t];
        }
        return names;
    };

    // With the score: every routed rule moves.
    const Run a = run(path);
    const std::vector<std::string> movedWithMusic = moved(a);
    for (const std::string& target : targets) {
        INFO(target);
        CHECK(std::find(movedWithMusic.begin(), movedWithMusic.end(), target) != movedWithMusic.end());
    }
    // The same audio, a fresh engine: the same rules at every sample, to the bit (§27).
    const Run b = run(path);
    REQUIRE(a.samples.size() == b.samples.size());
    for (std::size_t i = 0; i < a.samples.size(); ++i) {
        INFO("sample " << i);
        CHECK(a.samples[i] == b.samples[i]);
        REQUIRE(a.tables[i].size() == b.tables[i].size());
        CHECK(std::memcmp(a.tables[i].data(), b.tables[i].data(), a.tables[i].size() * sizeof(spatial::SdfNodeGpu)) == 0);
    }
    // And the music is what moves them: the same project with no audio holds every rule still (the bar
    // clock, and so the seeded per-bar re-spacing, needs a track as well).
    doc["assets"].erase("audio");
    doc["assets"]["scene"]["path"] = (path.parent_path() / "infinite-hall.scene.json").string();
    const std::filesystem::path silent = std::filesystem::temp_directory_path() / "avgen-space-hall-no-audio.json";
    {
        std::ofstream out(silent);
        out << doc.dump(1);
    }
    const Run still = run(silent);
    std::filesystem::remove(silent);
    CHECK(moved(still).empty());
}
