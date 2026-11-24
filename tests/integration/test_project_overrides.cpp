// A project persists its overrides, not a photograph of the run; and what it does not understand it
// keeps (ADR-2132).
//
// Measured before this (docs/decisions/ADR-2132): one headless `project.save` of a two-parameter project
// wrote 564 parameters -- every one the session registered, the scene's camera and node transforms
// among them. A project's parameters are applied over its scene, so after that save an edit to the
// scene file's camera did nothing. And a load dropped every parameter path and every top-level key the
// build did not know, so load-then-save deleted them.
//
// The arms:
//   A  save without edits     the parameters block is the one the file had (named paths + unknowns),
//                             not every registered value
//   B  an edit persists       a moved node and a changed post value are written
//   C  the scene still runs   after A's save, an edit to the scene file's camera takes effect on reload
//   D  a named override stays a path the project named is written even when it equals the scene
//   E  unknowns round-trip    an unregistered parameter and unknown top-level keys come back verbatim
//   F  top-level keys         every key the file had is still there after a save
//   G  a rebuild is not a reset   a composition rebuilt in session (the assistant's rollback path)
//                             re-registers its parameters with the live values as defaults; the moved
//                             node must still be written
//   H  a deletion stays deleted   a parameter that was registered at load and is gone at save (its node
//                             was deleted) is not written back from the loaded copy

#include "app/engine.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "support/gltf_fixture.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

json readJson(const fs::path& p) {
    std::ifstream in(p);
    json doc = json::parse(in, nullptr, false);
    REQUIRE_FALSE(doc.is_discarded());
    return doc;
}

void writeJson(const fs::path& p, const json& doc) {
    std::ofstream out(p);
    out << doc.dump(2) << '\n';
}

std::set<std::string> keysOf(const json& obj) {
    std::set<std::string> out;
    for (const auto& [k, v] : obj.items()) {
        out.insert(k);
    }
    return out;
}

json sceneDoc(const fs::path& glb, json cameraPosition) {
    return json{{"format", "avgen-scene"},
                {"version", 1},
                {"name", "column"},
                {"camera", {{"mode", 1}, {"position", std::move(cameraPosition)}, {"target", {0.0, 2.0, 0.0}}, {"fov", 40.0}}},
                {"nodes",
                 json::array({json{{"name", "column"}, {"kind", "gltf"}, {"asset", glb.generic_string()}, {"position", {0.0, 0.0, 0.0}}},
                              json{{"name", "plinth"}, {"kind", "gltf"}, {"asset", glb.generic_string()}, {"position", {3.0, 0.0, 0.0}}}})}};
}

struct Fixture {
    fs::path dir;
    fs::path glb;
    fs::path scene;
    fs::path project;

    Fixture() {
        dir = testsupport::processTempDir() / "project_overrides";
        fs::remove_all(dir);
        fs::create_directories(dir);
        const auto tmp = testsupport::writeTriangleGlb("project_overrides");
        glb = dir / "tri.glb";
        fs::copy_file(tmp, glb, fs::copy_options::overwrite_existing);
        fs::remove(tmp);
        scene = dir / "column.scene.json";
        project = dir / "column.json";
        writeJson(scene, sceneDoc(glb, {0.0, 2.6, 9.0}));
        // The evaluation's two parameters, plus one this build does not register, plus a path that names a scene value
        // exactly (camera/fov 40 == the scene's 40), plus two top-level keys the engine does not read (both real:
        // `sonicScene` is in 41 tracked projects and `_note` in 64, and an app save deleted both).
        writeJson(project, json{{"format", "avgen-project"},
                                {"version", 4},
                                {"assets", {{"scene", {{"kind", "composition"}, {"path", "column.scene.json"}}}}},
                                {"parameters",
                                 {{"post/bloom/enabled", true},
                                  {"post/bloom/intensity", 0.6},
                                  {"camera/fov", 40.0},
                                  {"nodes/plinth/scale", {2.0, 2.0, 2.0}},
                                  {"future/subsystem/amount", {0.125, 7, true}}}},
                                {"routes", json::array()},
                                {"sonicScene", {{"title", "Column"}, {"vocabulary", json::array({"a", "b"})}}},
                                {"_note", "hand-written; keep me"}});
    }
    ~Fixture() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

void load(app::Engine& engine, const fs::path& project) {
    auto r = engine.loadProject(project);
    INFO((r.has_value() ? std::string() : r.error().message));
    REQUIRE(r.has_value());
    REQUIRE(engine.composition() != nullptr);
}

std::vector<float> baseOf(app::Engine& engine, const std::string& path) {
    const params::IParameter* p = engine.params().find(path);
    REQUIRE(p != nullptr);
    std::vector<float> out;
    for (std::size_t i = 0; i < p->componentCount(); ++i) {
        out.push_back(p->baseComponent(i));
    }
    return out;
}

void setBase(app::Engine& engine, const std::string& path, std::vector<float> values) {
    params::IParameter* p = engine.params().find(path);
    REQUIRE(p != nullptr);
    for (std::size_t i = 0; i < values.size(); ++i) {
        p->setBaseComponent(i, values[i]);
    }
}

} // namespace

TEST_CASE("a save without edits writes the project's own overrides, not every registered value",
          "[project][adr2132]") {
    Fixture f;
    const json original = readJson(f.project);
    app::Engine engine{app::EngineMode::Offline};
    load(engine, f.project);
    REQUIRE(engine.params().size() > 50); // the session registers far more than the file names

    const fs::path out = f.dir / "saved.json";
    REQUIRE(engine.saveProject(out).has_value());
    const json saved = readJson(out);

    // A: exactly the paths the file named, the unknown one included -- no camera, no node transform, no post default.
    CHECK(keysOf(saved["parameters"]) == keysOf(original["parameters"]));
    // D: camera/fov equals the scene's value and is still written: the project named it.
    CHECK(saved["parameters"]["camera/fov"] == json(40.0));
    // E: the unregistered path, byte for byte, and the unknown top-level keys.
    CHECK(saved["parameters"]["future/subsystem/amount"] == original["parameters"]["future/subsystem/amount"]);
    CHECK(saved["sonicScene"] == original["sonicScene"]);
    CHECK(saved["_note"] == original["_note"]);
    // F: no top-level key the file had is lost.
    for (const std::string& key : keysOf(original)) {
        INFO(key);
        CHECK(saved.contains(key));
    }
    // Every key the save wrote is either one this build owns or one it carried for somebody else.
    for (const std::string& key : keysOf(saved)) {
        INFO(key);
        CHECK((app::Engine::isProjectKey(key) || original.contains(key)));
    }
    // The control block holds no machine defaults (ADR-2133): an untouched control map writes nothing.
    CHECK_FALSE(saved.contains("control"));

    // A save of the save is byte-stable.
    app::Engine again{app::EngineMode::Offline};
    load(again, out);
    const fs::path out2 = f.dir / "saved2.json";
    REQUIRE(again.saveProject(out2).has_value());
    CHECK(readJson(out2) == saved);
}

TEST_CASE("an edit persists, and a scene edit to a value the project does not override still takes effect",
          "[project][adr2132]") {
    Fixture f;
    {
        app::Engine engine{app::EngineMode::Offline};
        load(engine, f.project);
        setBase(engine, "nodes/column/rotation", {0.0f, 0.0f, 20.0f});
        setBase(engine, "post/bloom/intensity", {0.9f});
        REQUIRE(engine.saveProject(f.project).has_value());
    }
    const json saved = readJson(f.project);
    // B
    REQUIRE(saved["parameters"].contains("nodes/column/rotation"));
    CHECK(saved["parameters"]["nodes/column/rotation"] == json({0.0, 0.0, 20.0}));
    CHECK(saved["parameters"]["post/bloom/intensity"].get<double>() > 0.89);
    CHECK_FALSE(saved["parameters"].contains("camera/position"));
    CHECK_FALSE(saved["parameters"].contains("nodes/column/position"));

    // C: the scene file moves its camera; the project, saved above, does not pin it.
    writeJson(f.scene, sceneDoc(f.glb, {1.0, 5.0, 12.0}));
    app::Engine reopened{app::EngineMode::Offline};
    load(reopened, f.project);
    CHECK(baseOf(reopened, "camera/position") == std::vector<float>{1.0f, 5.0f, 12.0f});
    CHECK(baseOf(reopened, "nodes/column/rotation") == std::vector<float>{0.0f, 0.0f, 20.0f});
}

TEST_CASE("a composition rebuilt in session keeps the moved node an override", "[project][adr2132]") {
    Fixture f;
    app::Engine engine{app::EngineMode::Offline};
    load(engine, f.project);
    setBase(engine, "nodes/column/position", {0.0f, 1.5f, 0.0f});
    // G: the assistant's rollback path. The rebuilt composition registers `nodes/column/position` with the live value as
    // its default, so a rule of "differs from the default" would lose the move here.
    const json document = engine.composition()->toJson();
    REQUIRE(engine.setCompositionJson(document).has_value());
    setBase(engine, "nodes/column/position", {0.0f, 1.5f, 0.0f}); // as the rollback's second parameter pass does
    const fs::path out = f.dir / "rebuilt.json";
    REQUIRE(engine.saveProject(out).has_value());
    const json saved = readJson(out);
    REQUIRE(saved["parameters"].contains("nodes/column/position"));
    CHECK(saved["parameters"]["nodes/column/position"] == json({0.0, 1.5, 0.0}));
}

TEST_CASE("a parameter whose node was deleted in session is not written back from the loaded copy",
          "[project][adr2132]") {
    Fixture f;
    app::Engine engine{app::EngineMode::Offline};
    load(engine, f.project);
    REQUIRE(engine.params().find("nodes/plinth/scale") != nullptr);
    json document = engine.composition()->toJson();
    json kept = json::array();
    for (const auto& node : document["nodes"]) {
        if (node.value("name", std::string()) != "plinth") {
            kept.push_back(node);
        }
    }
    document["nodes"] = kept;
    REQUIRE(engine.setCompositionJson(document).has_value());
    REQUIRE(engine.params().find("nodes/plinth/scale") == nullptr);
    const fs::path out = f.dir / "deleted.json";
    REQUIRE(engine.saveProject(out).has_value());
    const json saved = readJson(out);
    // H: gone with its node -- while the parameter this build never registered is still kept.
    CHECK_FALSE(saved["parameters"].contains("nodes/plinth/scale"));
    CHECK(saved["parameters"].contains("future/subsystem/amount"));
}

TEST_CASE("an inflated project is reported, not rewritten", "[project][adr2132]") {
    Fixture f;
    // A project an earlier build saved: it pins the scene's camera and both node positions at the scene's own values.
    json doc = readJson(f.project);
    doc["parameters"]["camera/position"] = json({0.0, 2.6, 9.0});
    doc["parameters"]["nodes/column/position"] = json({0.0, 0.0, 0.0});
    doc["parameters"]["nodes/plinth/position"] = json({3.0, 0.0, 0.0});
    writeJson(f.project, doc);

    app::Engine engine{app::EngineMode::Offline};
    load(engine, f.project);
    // The three pinned values (and camera/fov, which the fixture names at the scene's value) are counted...
    const app::Engine::PinnedOverrides pinned = engine.pinnedOverrides();
    const std::set<std::string> pinnedSet(pinned.paths.begin(), pinned.paths.end());
    CHECK(pinnedSet.contains("camera/position"));
    CHECK(pinnedSet.contains("nodes/column/position"));
    CHECK(pinnedSet.contains("nodes/plinth/position"));
    CHECK(pinnedSet.contains("camera/fov"));
    CHECK_FALSE(pinnedSet.contains("nodes/plinth/scale"));
    CHECK_FALSE(pinnedSet.contains("post/bloom/intensity"));
    CHECK_FALSE(pinnedSet.contains("future/subsystem/amount"));
    // ...and a plain save keeps them: a file is never silently rewritten into a different project.
    const fs::path out = f.dir / "plain.json";
    REQUIRE(engine.saveProject(out).has_value());
    CHECK(readJson(out)["parameters"].contains("camera/position"));

    // The explicit cleanup drops exactly those, and nothing that differs from the scene or that this build cannot read.
    CHECK(engine.dropPinnedOverrides() == pinned.paths.size());
    const fs::path cleaned = f.dir / "cleaned.json";
    REQUIRE(engine.saveProject(cleaned).has_value());
    const json c = readJson(cleaned);
    CHECK_FALSE(c["parameters"].contains("camera/position"));
    CHECK_FALSE(c["parameters"].contains("nodes/column/position"));
    CHECK_FALSE(c["parameters"].contains("camera/fov"));
    CHECK(c["parameters"].contains("nodes/plinth/scale"));
    CHECK(c["parameters"].contains("post/bloom/intensity"));
    CHECK(c["parameters"].contains("future/subsystem/amount"));
}
