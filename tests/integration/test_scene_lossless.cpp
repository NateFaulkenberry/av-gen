// A scene save keeps what it does not read, and writes back everything it does (ADR-2151).
//
// Measured before this, at 414d47790: `Composition::toJson` wrote 34 of the 36 top-level keys the parser reads and
// nothing it did not. `looks` (ADR-1886) was read and never written, and every `_note` and every key a later build
// might add was dropped. `toJson` is not only File > Save Scene: it is the assistant's rollback snapshot
// (`ai::SnapshotStore::captureDocument`), restored through `Engine::setCompositionJson`, so an assistant task that
// rolled back also deleted the scene's looks and notes.
//
// The arms:
//   A  save         File > Save Scene (`Engine::saveComposition`) writes `_note`, an unknown object, an unknown
//                   scalar and `looks` back byte for byte
//   B  rollback     a snapshot, a parameter edit, a node added and a node removed, then the rollback: the document
//                   comes back with its looks and unknown keys, the edit undone and the node set restored
//   C  overrides    a parameter edit, `toJson`, `setCompositionJson`: what each step keeps, pinned
//   D  write-back   every shipped scene that loads: no top-level key is lost, and a second round trip is stable
//   E  nested       the nested losses the sweep found: a node with a missing asset, a node's `_note`, an orb's
//                   material, `proceduralSkyBackground`, the volume block with the march off
//   (hidden) `[.][scene-audit]` prints the nested losses one level down (environment, camera, lights[], nodes[])

#include "ai/transaction.hpp"
#include "app/engine.hpp"
#include "assets/asset_registry.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "support/gltf_fixture.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

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

// Every number here is exact in a float, because a scene's numbers are floats in memory (ADR-2151 lists that as a
// normalisation); the test is about which keys survive, not about float precision.
json sceneDoc(const fs::path& glb) {
    return json{
        {"format", "avgen-scene"},
        {"version", 1},
        {"name", "lossless"},
        {"_note", "hand-written; keep me"},
        {"_todo", json::array({"a", "b"})},
        {"futureBlock", {{"mode", "spiral"}, {"strength", 0.25}, {"layers", json::array({1, 2, 3})}}},
        {"futureScalar", 7},
        {"camera", {{"mode", 1}, {"position", {0.0, 2.5, 9.0}}, {"target", {0.0, 2.0, 0.0}}, {"fov", 40.0}}},
        {"nodes",
         json::array({json{{"name", "column"}, {"kind", "gltf"}, {"asset", glb.filename().generic_string()}, {"position", {0.0, 0.0, 0.0}}},
                      json{{"name", "plinth"}, {"kind", "gltf"}, {"asset", glb.filename().generic_string()}, {"position", {3.0, 0.0, 0.0}}}})},
        {"looks",
         json::array({json{{"name", "dusk"},
                           {"from", 1},
                           {"params",
                            {{"nodes/column/visible", true},
                             {"nodes/plinth/visible", false},
                             {"env/intensity", 0.5},
                             {"scene/fogColor", {1.0, 0.25, 0.0}},
                             {"post/one", json::array({0.75})}}}},
                      json{{"name", "night"}, {"from", 2}, {"params", {{"env/intensity", 0.125}}}}})}};
}

const std::vector<std::string> kKeptKeys = {"_note", "_todo", "futureBlock", "futureScalar", "looks"};

struct Fixture {
    fs::path dir;
    fs::path glb;
    fs::path scene;

    Fixture() {
        dir = testsupport::processTempDir() / "scene_lossless";
        fs::remove_all(dir);
        fs::create_directories(dir);
        const auto tmp = testsupport::writeTriangleGlb("scene_lossless");
        glb = dir / "tri.glb";
        fs::copy_file(tmp, glb, fs::copy_options::overwrite_existing);
        fs::remove(tmp);
        scene = dir / "lossless.scene.json";
        writeJson(scene, sceneDoc(glb));
    }
    ~Fixture() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

void open(app::Engine& engine, const fs::path& scene) {
    auto r = engine.loadComposition(scene);
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

void setBase(app::Engine& engine, const std::string& path, const std::vector<float>& values) {
    params::IParameter* p = engine.params().find(path);
    REQUIRE(p != nullptr);
    for (std::size_t i = 0; i < values.size(); ++i) {
        p->setBaseComponent(i, values[i]);
    }
}

std::set<std::string> nodeNames(const json& doc) {
    std::set<std::string> out;
    for (const json& n : doc.value("nodes", json::array())) {
        out.insert(n.value("name", std::string()));
    }
    return out;
}

fs::path repoRoot() { return fs::path(AVGEN_SOURCE_DIR); }

// Top-level keys a scene may carry that `toJson` writes somewhere else or not at all, on purpose. Each is a
// normalisation recorded in ADR-2151; anything else missing from the output is a loss.
const std::map<std::string, std::string>& documentedNormalisations() {
    static const std::map<std::string, std::string> m = {
        {"lightRig", "written as environment.lightRig, which the parser also reads"},
    };
    return m;
}

// The gitignored asset folders (.gitignore; the CI assets decision: commercial packs are never committed). A scene
// whose load fails naming a file under one of these is skipped, not failed: CI and a worktree without
// tools/link-worktree-assets.sh do not have them. Any other load failure fails the sweep.
const std::vector<std::string>& ignoredAssetRoots() {
    static const std::vector<std::string> roots = {
        "assets/nature/",       "assets/terrain/",       "assets/chronos/",          "assets/quaternius/",
        "assets/hdri/",         "assets/environments/",  "assets/audio/",            "assets/kenney/city/",
        "assets/imported/",     "assets/aliens/",        "assets/farm/",             "assets/musicians/",
        "assets/treeisle/",     "assets/100style",       "assets/rainbow-world/",    "assets/talking-heads",
        "assets/space/",        "assets/strange-attractor/", "_thirdparty/"};
    return roots;
}

bool missingIgnoredAsset(const std::string& message) {
    return std::any_of(ignoredAssetRoots().begin(), ignoredAssetRoots().end(),
                       [&](const std::string& root) { return message.find(root) != std::string::npos; });
}

// Two instabilities a second round trip still shows, both outside this change's files and recorded in ADR-2151 as
// follow-ups. Each is tolerated only in its exact shape, so anything else that moves fails the sweep.
//  - Rotation: a rotation is kept as a quaternion and written as Euler degrees, so each save moves a component in its
//    sixth significant figure, a procedural transform's 180 can come back as -180, and at the pole (-90, 86, -90) can
//    come back as (90, 94, 90). Tolerated only when the two spellings are the same turn.
//  - EmptyBiomes: `world::worldMapToJson` (src/world/world_map.cpp) leaves out `"biomes": []`, and a world read without
//    the key gets the default biomes, so the second save adds them (digital-mosh's two terrains).
enum class KnownDrift { None, Rotation, EmptyBiomes };

// The rotation a node's (or a procedural transform's) `[x, y, z]` degrees describe, so two spellings of one turn --
// 180 and -180, or (-90, 86, -90) and (90, 94, 90) at the pole -- compare equal.
bool sameRotation(const json& a, const json& b) {
    if (!a.is_array() || !b.is_array() || a.size() != 3 || b.size() != 3) return false;
    const auto q = [](const json& v) {
        return scene::quatFromEulerDegrees(glm::vec3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>()));
    };
    return std::abs(glm::dot(q(a), q(b))) > 1.0f - 1e-6f;
}

KnownDrift knownDrift(const std::string& op, const std::string& path, const json& once, const json& twice) {
    if (const auto at = path.rfind("/rotation/"); op == "replace" && at != std::string::npos) {
        const json::json_pointer rotation(path.substr(0, at + std::string("/rotation").size()));
        if (once.contains(rotation) && twice.contains(rotation) && sameRotation(once.at(rotation), twice.at(rotation))) {
            return KnownDrift::Rotation;
        }
    }
    if (op == "add" && path.ends_with("/world/biomes")) return KnownDrift::EmptyBiomes;
    return KnownDrift::None;
}

struct SweepScene {
    fs::path path;
    json input;
    json once;
    json twice;
};

// Loads every shipped scene (examples/**/*.scene.json, not the generated `_bench/`), round-trips it twice, and
// returns the ones that loaded. `failures` names the ones that did not and are not on the skip list.
std::vector<SweepScene> sweep(std::vector<std::string>& failures, int& skipped) {
    std::vector<SweepScene> out;
    const fs::path examples = repoRoot() / "examples";
    REQUIRE(fs::is_directory(examples));
    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(examples)) {
        if (!entry.is_regular_file() || !entry.path().filename().string().ends_with(".scene.json")) {
            continue;
        }
        if (entry.path().string().find("/_bench/") != std::string::npos) {
            continue; // generated, gitignored (as test_scene_authored_lights.cpp's sweep)
        }
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        const std::string rel = fs::relative(file, examples).generic_string();
        std::ifstream in(file);
        json input = json::parse(in, nullptr, false);
        if (input.is_discarded() || !input.is_object() || input.value("format", std::string()) != "avgen-scene") {
            continue; // not a scene document (a fragment, a template); nothing to round-trip
        }
        assets::AssetRegistry registry(file.parent_path());
        auto loaded = scene::Composition::loadFile(file, registry);
        if (!loaded) {
            if (missingIgnoredAsset(loaded.error().message)) {
                ++skipped;
            } else {
                failures.push_back(rel + ": " + loaded.error().message);
            }
            continue;
        }
        SweepScene s;
        s.path = file;
        s.input = std::move(input);
        s.once = (*loaded)->toJson();
        auto again = scene::Composition::fromJson(s.once, registry);
        if (!again) {
            failures.push_back(rel + ": the written document does not load: " + again.error().message);
            continue;
        }
        s.twice = (*again)->toJson();
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace

TEST_CASE("File > Save Scene keeps _ keys, unknown keys and looks byte for byte", "[scene][lossless][adr2151]") {
    Fixture f;
    const json original = readJson(f.scene);
    app::Engine engine{app::EngineMode::Offline};
    open(engine, f.scene);

    const fs::path out = f.dir / "saved.scene.json";
    REQUIRE(engine.saveComposition(out).has_value());
    const json saved = readJson(out);
    for (const std::string& key : kKeptKeys) {
        INFO(key);
        REQUIRE(saved.contains(key));
        CHECK(saved.at(key).dump() == original.at(key).dump());
    }

    // And a save of the save is the same document: nothing is kept twice or drifts.
    app::Engine again{app::EngineMode::Offline};
    open(again, out);
    const fs::path out2 = f.dir / "saved2.scene.json";
    REQUIRE(again.saveComposition(out2).has_value());
    CHECK(readJson(out2) == saved);
}

TEST_CASE("A key toJson writes itself is never overwritten by a kept copy", "[scene][lossless][adr2151]") {
    Fixture f;
    app::Engine engine{app::EngineMode::Offline};
    open(engine, f.scene);
    // Only keys the parser does not read are kept, so every key `toJson` writes comes from live state: a camera move
    // is what the document says afterwards, and the kept keys ride beside it unchanged.
    setBase(engine, "camera/fov", {55.0f});
    const json doc = engine.composition()->toJson();
    CHECK(doc.at("camera").at("fov") == json(55.0));
    CHECK(doc.at("futureScalar") == json(7));
    // The guard's premise: everything `toJson` writes is a key the parser reads, so no kept key can shadow one.
    const auto known = scene::sceneFileKeys();
    for (const auto& [key, value] : doc.items()) {
        if (std::find(kKeptKeys.begin(), kKeptKeys.end(), key) != kKeptKeys.end() && key != "looks") continue;
        INFO(key);
        CHECK(std::find(known.begin(), known.end(), key) != known.end());
    }

    // ADR-2151: kept keys come from a document. A composition built in code (`newComposition`) has none, so it writes
    // none: there is nothing to carry and nothing to invent.
    engine.newComposition();
    REQUIRE(engine.composition() != nullptr);
    const json fresh = engine.composition()->toJson();
    CHECK_FALSE(fresh.contains("futureScalar"));
    for (const auto& [key, value] : fresh.items()) {
        INFO(key);
        CHECK_FALSE(key.starts_with('_'));
        CHECK(std::find(known.begin(), known.end(), key) != known.end());
    }
}

TEST_CASE("An assistant rollback restores the scene's looks and unknown keys", "[scene][lossless][adr2151][ai]") {
    Fixture f;
    const json original = readJson(f.scene);
    app::Engine engine{app::EngineMode::Offline};
    open(engine, f.scene);
    const std::vector<float> columnAt = baseOf(engine, "nodes/column/position");

    const json snapshot = ai::SnapshotStore::captureDocument(engine);
    REQUIRE(snapshot.contains("composition"));

    // The task: a parameter edit, a node added, a node removed.
    setBase(engine, "nodes/column/position", {0.0f, 4.0f, 0.0f});
    scene::CompositionNode added;
    added.name = "added";
    added.asset = f.glb;
    REQUIRE(engine.addNode(std::move(added)).has_value());
    engine.removeNode("plinth");
    REQUIRE(nodeNames(engine.composition()->toJson()) == std::set<std::string>{"column", "added"});

    // ... rolled back.
    REQUIRE(ai::SnapshotStore::applyDocument(engine, snapshot).has_value());
    const json restored = engine.composition()->toJson();
    CHECK(nodeNames(restored) == std::set<std::string>{"column", "plinth"});
    CHECK(baseOf(engine, "nodes/column/position") == columnAt);
    for (const std::string& key : kKeptKeys) {
        INFO(key);
        REQUIRE(restored.contains(key));
        CHECK(restored.at(key).dump() == original.at(key).dump());
    }
    // And the restored document is the snapshot's, key for key.
    CHECK(restored == snapshot.at("composition"));
}

TEST_CASE("A parameter edit, toJson and setCompositionJson: what each step keeps", "[scene][lossless][adr2151]") {
    // The interaction a `document.patch` will be built on: toJson -> patch -> setCompositionJson -> parameters.
    // Pinned behaviour (what the code intends, ADR-2132 and ai/transaction.cpp):
    //  1. A node transform is scene content. `toJson` writes the live base, so an edit made through the parameter
    //     (as `parameter.set` does) is IN the document, and `setCompositionJson` of that document keeps it.
    //  2. A `post` value is not scene content written from the parameter: `toJson` writes the post block the scene
    //     was read with, and `setCompositionJson` re-applies that block. So the document alone puts the edit BACK.
    //     That is why `SnapshotStore::applyDocument` applies the parameter half a second time after the composition:
    //     the parameters, not the scene document, are what restore a post edit.
    //  3. ADR-2132's basis is not retaken by `setCompositionJson` (an in-session rebuild), so the moved node is
    //     still an override in a project save afterwards.
    Fixture f;
    app::Engine engine{app::EngineMode::Offline};
    open(engine, f.scene);
    REQUIRE(engine.params().find("post/bloom/intensity") != nullptr);
    const std::vector<float> bloomBefore = baseOf(engine, "post/bloom/intensity");

    setBase(engine, "nodes/column/position", {0.0f, 1.5f, 0.0f});
    setBase(engine, "post/bloom/intensity", {bloomBefore[0] + 0.5f});
    const json document = engine.composition()->toJson();
    CHECK(document.at("nodes").at(0).at("position") == json({0.0, 1.5, 0.0})); // 1
    CHECK(document.at("futureBlock") == readJson(f.scene).at("futureBlock"));

    REQUIRE(engine.setCompositionJson(document).has_value());
    CHECK(baseOf(engine, "nodes/column/position") == std::vector<float>{0.0f, 1.5f, 0.0f}); // 1
    CHECK(baseOf(engine, "post/bloom/intensity") == bloomBefore);                             // 2
    CHECK(engine.composition()->toJson() == document); // the kept keys and looks come through the rebuild

    // 2, the other half: the snapshot path restores the post edit because it carries the parameters too.
    setBase(engine, "post/bloom/intensity", {bloomBefore[0] + 0.5f});
    const json snapshot = ai::SnapshotStore::captureDocument(engine);
    setBase(engine, "post/bloom/intensity", {bloomBefore[0]});
    REQUIRE(ai::SnapshotStore::applyDocument(engine, snapshot).has_value());
    CHECK(baseOf(engine, "post/bloom/intensity") == std::vector<float>{bloomBefore[0] + 0.5f});
    CHECK(baseOf(engine, "nodes/column/position") == std::vector<float>{0.0f, 1.5f, 0.0f});

    // 3
    const fs::path project = f.dir / "after-rebuild.json";
    REQUIRE(engine.saveProject(project).has_value());
    const json saved = readJson(project);
    REQUIRE(saved.contains("parameters"));
    CHECK(saved.at("parameters").contains("nodes/column/position"));
}

TEST_CASE("The nested losses the audit found are written back", "[scene][lossless][adr2151]") {
    // Each of these was read, used, and dropped by a save at 414d47790 (ADR-2151's audit table):
    //  - a node whose asset is missing (singularity-figures.scene.json without its gitignored souls lost all ten)
    //  - a node's `_note` (nine shipped nodes carry one)
    //  - an orb's `material`, and `alphaMode` on any material (RendererQA's transparent orb)
    //  - `environment.proceduralSkyBackground` (tree-of-life-ocean-world)
    //  - the volume block's values with the march off (`fogHeight` shapes the surface fog; `shadowCascades`)
    Fixture f;
    json doc = readJson(f.scene);
    doc["nodes"].push_back(json{{"name", "ghost"}, {"kind", "gltf"}, {"asset", "not-here.glb"}, {"position", {1.0, 2.0, 3.0}}, {"_note", "a missing pack"}});
    doc["nodes"][0]["_note"] = "the column";
    doc["nodes"].push_back(json{{"name", "glass"},
                                {"kind", "orb"},
                                {"_note", "transparent"},
                                {"material", {{"baseColor", {0.25, 0.5, 1.0}}, {"opacity", 0.5}, {"alphaMode", "blend"}, {"roughness", 0.25}}}});
    doc["environment"] = json{{"proceduralSkyBackground", true}, {"fogHeightAmount", 0.5}, {"fogHeight", 16.0}, {"fogHeightFalloff", 0.25}, {"shadowCascades", 3}};
    writeJson(f.scene, doc);

    app::Engine engine{app::EngineMode::Offline};
    open(engine, f.scene);
    const fs::path out = f.dir / "nested.scene.json";
    REQUIRE(engine.saveComposition(out).has_value());
    const json saved = readJson(out);

    const auto nodeNamed = [&](const std::string& name) {
        for (const json& n : saved.at("nodes")) {
            if (n.value("name", std::string()) == name) return n;
        }
        FAIL("node '" << name << "' is not in the saved file");
        return json();
    };
    CHECK(nodeNamed("ghost") == doc["nodes"][2]); // verbatim
    CHECK(nodeNamed("column").at("_note") == "the column");
    const json glass = nodeNamed("glass");
    CHECK(glass.at("_note") == "transparent");
    REQUIRE(glass.contains("material"));
    CHECK(glass.at("material").at("alphaMode") == "blend");
    CHECK(glass.at("material").at("opacity") == json(0.5));
    CHECK(glass.at("material").at("baseColor") == json({0.25, 0.5, 1.0}));
    const json& env = saved.at("environment");
    CHECK(env.value("proceduralSkyBackground", false));
    CHECK(env.value("fogHeight", 0.0) == 16.0);
    CHECK(env.value("fogHeightFalloff", 0.0) == 0.25);
    CHECK(env.value("shadowCascades", 0) == 3);

    // And stable: a save of the save is the same file.
    app::Engine again{app::EngineMode::Offline};
    open(again, out);
    const fs::path out2 = f.dir / "nested2.scene.json";
    REQUIRE(again.saveComposition(out2).has_value());
    CHECK(readJson(out2) == saved);
}

TEST_CASE("Every shipped scene writes back every top-level key it was read with, stably",
          "[scene][lossless][adr2151][examples]") {
    std::vector<std::string> failures;
    int skipped = 0;
    const std::vector<SweepScene> scenes = sweep(failures, skipped);
    for (const std::string& f : failures) {
        UNSCOPED_INFO(f);
    }
    CHECK(failures.empty());
    // A floor, so a sweep that silently loads nothing (a moved directory, a parser that fails everything) fails.
    // 268 load here with the assets linked; roughly 80 name a gitignored asset and are skipped where it is missing.
    CHECK(scenes.size() >= 150);
    int lost = 0;
    int unstable = 0;
    std::map<KnownDrift, int> drifted;
    for (const SweepScene& s : scenes) {
        const std::string rel = fs::relative(s.path, repoRoot() / "examples").generic_string();
        for (const auto& [key, value] : s.input.items()) {
            if (s.once.contains(key) || documentedNormalisations().contains(key)) {
                continue;
            }
            // The second documented normalisation: an empty list the parser reads (`"lights": []`, `"entities": []`)
            // means what its absence means, and the writer leaves it out (ADR-278's "written only when there are any").
            const auto known = scene::sceneFileKeys();
            if (value.is_array() && value.empty() && std::find(known.begin(), known.end(), key) != known.end()) {
                continue;
            }
            ++lost;
            UNSCOPED_INFO(rel << ": top-level '" << key << "' is not written back");
        }
        if (s.twice != s.once) {
            std::string ops;
            bool fails = false;
            for (const json& op : json::diff(s.once, s.twice)) {
                const std::string path = op.at("path").get<std::string>();
                const json::json_pointer ptr(path);
                const json was = s.once.contains(ptr) ? s.once.at(ptr) : json();
                const json now = op.value("value", json());
                if (const KnownDrift drift = knownDrift(op.at("op").get<std::string>(), path, s.once, s.twice);
                    drift != KnownDrift::None) {
                    ++drifted[drift];
                    continue;
                }
                fails = true;
                ops += "\n    " + op.at("op").get<std::string>() + " " + path + ": " + was.dump().substr(0, 60) + " -> " +
                       now.dump().substr(0, 60);
            }
            if (fails) {
                ++unstable;
                UNSCOPED_INFO(rel << ": a second round trip changes the document:" << ops);
            }
        }
    }
    // What the tolerance above lets through, so it is seen rather than assumed (ADR-2151 lists both as follow-ups).
    WARN("known drift: " << drifted[KnownDrift::Rotation] << " rotation component(s) respelled as the same turn; " << drifted[KnownDrift::EmptyBiomes] << " emptied biome set(s)");
    CHECK(lost == 0);
    CHECK(unstable == 0);
}

TEST_CASE("Scene write-back audit, one level down", "[.][scene-audit]") {
    // Not a check: the report the schema typing table starts from. Prints every nested key a shipped scene had in
    // environment, environment.sky, camera, lights[] and nodes[] that `toJson` did not write back.
    std::vector<std::string> failures;
    int skipped = 0;
    const std::vector<SweepScene> scenes = sweep(failures, skipped);
    std::map<std::string, std::set<std::string>> lostIn; // "block.key" -> scenes
    std::map<std::string, std::set<std::string>> lostValues; // "block.key" -> the values it had (a few)
    const auto compare = [&](const std::string& block, const json& in, const json& out, const std::string& rel, const std::string& who = "") {
        if (!in.is_object()) return;
        for (const auto& [key, value] : in.items()) {
            if (!out.is_object() || !out.contains(key)) {
                lostIn[block + "." + key].insert(rel);
                auto& values = lostValues[block + "." + key];
                if (values.size() < 4) values.insert((who.empty() ? "" : who + "=") + value.dump().substr(0, 40));
            }
        }
    };
    for (const SweepScene& s : scenes) {
        const std::string rel = fs::relative(s.path, repoRoot() / "examples").generic_string();
        for (const auto& [key, value] : s.input.items()) {
            if (!s.once.contains(key)) lostIn["(top)." + key].insert(rel);
        }
        for (const char* block : {"environment", "camera", "post", "cameraDirection", "composition", "staging"}) {
            if (s.input.contains(block)) compare(block, s.input.at(block), s.once.value(block, json()), rel);
        }
        if (s.input.contains("environment") && s.input.at("environment").contains("sky")) {
            compare("environment.sky", s.input.at("environment").at("sky"),
                    s.once.value("environment", json::object()).value("sky", json()), rel);
        }
        for (const char* list : {"lights", "nodes"}) {
            if (!s.input.contains(list) || !s.input.at(list).is_array()) continue;
            const json outList = s.once.value(list, json::array());
            for (const json& item : s.input.at(list)) {
                if (!item.is_object()) continue;
                const std::string name = item.value("name", std::string());
                json match;
                for (const json& o : outList) {
                    if (o.is_object() && o.value("name", std::string()) == name) {
                        match = o;
                        break;
                    }
                }
                const std::string kind = item.value("kind", std::string());
                compare(std::string(list) + (kind.empty() ? "" : "[" + kind + "]"), item, match, rel, name);
            }
        }
    }
    std::string report = "loaded " + std::to_string(scenes.size()) + ", skipped " + std::to_string(skipped) +
                         ", failed " + std::to_string(failures.size()) + "\n";
    for (const std::string& f : failures) report += "FAILED " + f + "\n";
    for (const auto& [key, files] : lostIn) {
        std::string values;
        for (const std::string& v : lostValues[key]) values += (values.empty() ? "" : " | ") + v;
        report += key + " (" + std::to_string(files.size()) + "): " + *files.begin() + (files.size() > 1 ? ", ..." : "") +
                  "  values: " + values + "\n";
    }
    WARN(report);
}
