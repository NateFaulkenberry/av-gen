// The Tree of Life Floating Island showcase: the hierarchy, the motion contract, and the two
// things about it that are easy to break without anything failing.
//
// This file replaces `test_tree_example.cpp`, which guarded the *previous* Tree of Life -- a scene
// assembled in C++ from a parameter vector. That project is retired (ADR-337); the tree is now an
// externally authored hero export and this project presents it.
//
// Two of the checks here exist because of failures this repository has already had.
//
// A node whose `asset` is an absolute path into somebody's worktree loads fine on the machine that
// wrote it and is *silently skipped* everywhere else: `glowmere-valley-2-song.scene.json` names
// sixteen animals that way and that film has been rendering with five of its twenty-one bodies
// since the day it was saved. No error, no warning that anybody read -- just a thinner frame. So
// this asserts the shape of every asset reference, not merely that the scene loads.
//
// And the tree must be *carried* by the island, not animated alongside it. Two nodes given the
// same rotation independently look identical in a still and drift apart the moment anything --
// a route, a timeline, an audio reaction -- reaches one of them and not the other. The hierarchy
// test below is paired with a control that fails if the island stops moving, because a test that
// compares two things that are both constant passes whatever the code does (ADR-182).

#include "app/examples.hpp"
#include "assets/asset_registry.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/norm.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using namespace avgen;
using Catch::Approx;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr const char* kExampleName = "Tree of Life - Floating Island";
constexpr const char* kProjectRel = "treeisland/tree-of-life-floating-island.json";
constexpr const char* kSceneRel = "treeisland/tree-of-life-floating-island.scene.json";

fs::path examplesDir() {
#ifdef AVGEN_SOURCE_DIR
    return fs::path(AVGEN_SOURCE_DIR) / "examples";
#else
    return {};
#endif
}

json readJson(const fs::path& path) {
    std::ifstream in(path);
    REQUIRE(in.good());
    return json::parse(in);
}

} // namespace

TEST_CASE("The floating island showcase is in the examples menu", "[treeisland][example]") {
    const fs::path dir = examplesDir();
    if (dir.empty()) {
        SKIP("AVGEN_SOURCE_DIR not defined");
    }
    const auto examples = app::loadExampleIndex(dir / "index.json");
    REQUIRE(examples.has_value());

    const app::ExampleInfo* entry = nullptr;
    for (const app::ExampleInfo& e : *examples) {
        if (e.name == kExampleName) {
            entry = &e;
        }
        // The project this one replaces must be gone from the menu, not merely unreferenced --
        // an index entry pointing at a deleted file is the failure mode `loadExampleIndex` cannot
        // see, because it resolves paths without opening them.
        CHECK(e.name != "The Tree of Life");
        INFO("example '" << e.name << "' -> " << e.file.string());
        CHECK(fs::exists(e.file));
    }
    REQUIRE(entry != nullptr);
    CHECK_FALSE(entry->description.empty());
    CHECK(entry->category == "Showcase");
    CHECK(entry->file == dir / kProjectRel);
}

TEST_CASE("The showcase scene names only relative, in-repository assets", "[treeisland][example]") {
    const fs::path dir = examplesDir();
    if (dir.empty()) {
        SKIP("AVGEN_SOURCE_DIR not defined");
    }
    const json scene = readJson(dir / kSceneRel);
    REQUIRE(scene.contains("nodes"));

    int assetNodes = 0;
    for (const json& node : scene.at("nodes")) {
        if (!node.contains("asset")) {
            continue;
        }
        ++assetNodes;
        const auto asset = node.at("asset").get<std::string>();
        INFO("node '" << node.value("name", std::string{}) << "' asset '" << asset << "'");
        // Not absolute, and not somebody's home directory reached by a different spelling.
        CHECK_FALSE(fs::path(asset).is_absolute());
        CHECK(asset.find('~') == std::string::npos);
        CHECK(asset.find("/Users/") == std::string::npos);
        CHECK(asset.find("Desktop") == std::string::npos);
        // And it resolves, as the loader will resolve it: relative to the scene file.
        const fs::path resolved = (dir / kSceneRel).parent_path() / asset;
        // The GLBs are gitignored for their 194 MB (assets/treeisle.manifest.json says how to
        // rebuild them), so their absence is a worktree state and not a fault. Their *path* being
        // wrong is a fault, and that is what is checked when they are present.
        if (fs::exists(resolved.parent_path())) {
            INFO("resolved " << resolved.string());
            CHECK(fs::exists(resolved));
        }
    }
    CHECK(assetNodes == 2); // the island and the tree, and nothing else reaches outside the repo
}

TEST_CASE("The tree is carried by the island, not animated beside it", "[treeisland][example]") {
    const fs::path dir = examplesDir();
    if (dir.empty()) {
        SKIP("AVGEN_SOURCE_DIR not defined");
    }
    assets::AssetRegistry registry(dir / "treeisland");
    auto loaded = scene::Composition::loadFile(dir / kSceneRel, registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;

    const scene::CompositionNode* root = comp.findNode("world-root");
    scene::CompositionNode* island = comp.findNode("floating-island");
    const scene::CompositionNode* surface = comp.findNode("island-surface");
    const scene::CompositionNode* tree = comp.findNode("tree-of-life");
    REQUIRE(root != nullptr);
    REQUIRE(island != nullptr);
    REQUIRE(surface != nullptr);
    REQUIRE(tree != nullptr);

    // The shape brief §20 asks for: WorldRoot -> FloatingIsland -> {the landmass, the tree}.
    CHECK(root->kind == scene::NodeKind::Group);
    CHECK(island->kind == scene::NodeKind::Group);
    CHECK(island->parent == "world-root");
    CHECK(surface->parent == "floating-island");
    CHECK(tree->parent == "floating-island");
    CHECK(tree->kind == scene::NodeKind::Gltf);

    const scene::Transform treeLocal = tree->transform;

    // Sample the pair through a third of a revolution, a bob and a tilt, by moving the island's
    // transform -- which is the only thing the project's routes ever write.
    struct Sample {
        glm::vec3 position;
        glm::vec3 euler;
    };
    const Sample samples[] = {
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
        {{0.0f, 1.7f, 0.0f}, {0.4f, 60.0f, -0.3f}},
        {{0.0f, -2.4f, 0.0f}, {-1.1f, 120.0f, 0.9f}},
        {{0.0f, 0.8f, 0.0f}, {0.6f, 180.0f, -0.7f}},
    };

    scene::Transform previousIslandWorld{};
    bool havePrevious = false;
    float islandTravelled = 0.0f;

    for (const Sample& s : samples) {
        island->transform.position = s.position;
        island->transform.rotation = scene::quatFromEulerDegrees(s.euler);

        const scene::Transform islandWorld = comp.nodeWorldTransform(*island);
        const scene::Transform treeWorld = comp.nodeWorldTransform(*tree);

        // 1. The tree's own transform is never touched. If a later change starts animating the
        //    tree to fake the island's motion, this is what notices.
        CHECK(tree->transform.position == treeLocal.position);
        CHECK(tree->transform.scale == treeLocal.scale);
        CHECK(glm::length(tree->transform.rotation - treeLocal.rotation) == Approx(0.0f).margin(1e-6));

        // 2. The tree's world transform is exactly the island's world transform applied to that
        //    unchanging local one. This is the "no detachment, no drift" property, stated as an
        //    equation rather than looked for in a frame.
        const glm::mat4 expected = islandWorld.matrix() * treeLocal.matrix();
        const glm::mat4 actual = treeWorld.matrix();
        float worst = 0.0f;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                worst = std::max(worst, std::abs(expected[c][r] - actual[c][r]));
            }
        }
        INFO("island at y=" << s.position.y << " yaw=" << s.euler.y);
        CHECK(worst == Approx(0.0f).margin(1e-4));

        if (havePrevious) {
            islandTravelled += glm::distance(previousIslandWorld.position, islandWorld.position);
            islandTravelled += glm::length(previousIslandWorld.rotation - islandWorld.rotation);
        }
        previousIslandWorld = islandWorld;
        havePrevious = true;
    }

    // The control. Everything above compares the tree against the island; if the island never
    // moved, every one of those checks would pass with the hierarchy deleted. This asserts the
    // samples actually exercised it -- a band, not a floor: four samples across 180 degrees of yaw
    // and 4.1 units of bob cannot plausibly sum to less than 3 or more than 30.
    CHECK(islandTravelled > 3.0f);
    CHECK(islandTravelled < 30.0f);
}

TEST_CASE("Only the island is animated, and it turns at a cinematic rate", "[treeisland][example]") {
    const fs::path dir = examplesDir();
    if (dir.empty()) {
        SKIP("AVGEN_SOURCE_DIR not defined");
    }
    const json project = readJson(dir / kProjectRel);

    // Brief §22: deterministic time-based motion only, no audio yet. Every source is an LFO, and
    // `LfoSource::update` is `renderTime * rate + phase` with no integration -- which is why a
    // render that starts at t = 40 s agrees with one that started at 0.
    REQUIRE(project.contains("sources"));
    std::set<std::string> lfos;
    for (const json& s : project.at("sources")) {
        CHECK(s.value("kind", std::string{}) == "lfo");
        lfos.insert(s.value("name", std::string{}));
    }
    CHECK(lfos.count("spin") == 1);
    CHECK(lfos.count("bob") == 1);

    // Brief §20: nothing routes to the tree. This is the half of the hierarchy contract that lives
    // in the project rather than the scene, and the half a scene-only test cannot see.
    REQUIRE(project.contains("routes"));
    bool sawSpin = false;
    bool sawBob = false;
    for (const json& r : project.at("routes")) {
        const auto target = r.value("target", std::string{});
        INFO("route " << r.value("source", std::string{}) << " -> " << target);
        CHECK(target.rfind("nodes/", 0) == 0);
        CHECK(target.find("tree") == std::string::npos);
        CHECK(target.rfind("nodes/floating-island/", 0) == 0);
        if (r.value("source", std::string{}) == "lfo.spin") {
            sawSpin = true;
            CHECK(target == "nodes/floating-island/rotation");
            CHECK(r.value("component", -1) == 1);        // yaw
            CHECK(r.value("amount", 0.0) == Approx(360.0)); // a whole turn, so the saw's wrap is seamless
        }
        if (r.value("source", std::string{}) == "lfo.bob.bipolar") {
            sawBob = true;
            CHECK(target == "nodes/floating-island/position");
            CHECK(r.value("component", -1) == 1);
            // Gentle bobbing, not an elevator (§18). The island is ~47 units deep; a metre or two.
            const double amount = std::abs(r.value("amount", 0.0));
            CHECK(amount > 0.5);
            CHECK(amount < 6.0);
        }
    }
    CHECK(sawSpin);
    CHECK(sawBob);

    // Brief §19: one revolution every two to five minutes. A band on both sides -- a rate of zero
    // would pass a "slower than two minutes" floor, and a still island is the failure being
    // guarded against, not the goal.
    REQUIRE(project.contains("parameters"));
    const auto rate = project.at("parameters").value("sources/spin/rate", 0.0);
    const double periodSeconds = rate > 0.0 ? 1.0 / rate : 0.0;
    INFO("spin rate " << rate << " Hz = one revolution every " << periodSeconds << " s");
    CHECK(periodSeconds >= 120.0);
    CHECK(periodSeconds <= 300.0);
}

TEST_CASE("The showcase camera is static and the cosmos is dark", "[treeisland][example]") {
    const fs::path dir = examplesDir();
    if (dir.empty()) {
        SKIP("AVGEN_SOURCE_DIR not defined");
    }
    const json scene = readJson(dir / kSceneRel);

    // §17: the island rotates, the camera does not.
    const json& camera = scene.at("camera");
    CHECK(camera.value("mode", -1) == 1); // free: explicit position and target
    CHECK(camera.value("orbitSpeed", 1.0) == Approx(0.0));
    REQUIRE(camera.contains("position"));
    REQUIRE(camera.contains("target"));

    // §12/§13: deep space, dark enough that the tree's emission reads. Every channel of every
    // background colour is well under a hundredth of a unit of scene-linear radiance.
    const json& env = scene.at("environment");
    for (const float c : env.at("background").get<std::vector<float>>()) {
        CHECK(c >= 0.0f);
        CHECK(c < 0.01f);
    }
    const json& sky = env.at("sky");
    CHECK(sky.value("enabled", false));
    for (const char* key : {"zenithColor", "horizonColor", "groundColor"}) {
        for (const float c : sky.at(key).get<std::vector<float>>()) {
            INFO("sky." << key);
            CHECK(c >= 0.0f);
            CHECK(c < 0.01f);
        }
    }
    // No sun disc: this is space, and §12 forbids giant distracting objects.
    CHECK(sky.value("sunIntensity", 1.0) == Approx(0.0));

    // §14: one light. A rig is what this task was told not to build yet.
    REQUIRE(scene.contains("lights"));
    CHECK(scene.at("lights").size() == 1);
    const json& key = scene.at("lights").at(0);
    CHECK(key.value("type", std::string{}) == "directional");
    CHECK(key.value("role", std::string{}) == "key");
    // From above: the downward component dominates.
    const auto dir3 = key.at("direction").get<std::vector<float>>();
    REQUIRE(dir3.size() == 3);
    CHECK(dir3[1] < 0.0f);
    CHECK(std::abs(dir3[1]) > std::abs(dir3[0]));
    CHECK(std::abs(dir3[1]) > std::abs(dir3[2]));
}

TEST_CASE("A procedural material can express alpha cutout", "[procedural][material]") {
    // Inherited from test_tree_example.cpp, which used the retired Tree of Life scene as its
    // fixture. The gap it guards is an engine one and outlived that project: a procedural node's
    // material JSON once parsed no alphaMode, no cutoff and no texture, so every leaf in a scene
    // file was an opaque rectangle. It only showed up when a hero assembled in C++ -- where
    // `Material` is built directly and can say anything -- crossed into the data format.
    //
    // The fixture is written here rather than borrowed from a showcase, so that retiring the next
    // showcase does not take this coverage with it.
    const fs::path tmp = fs::temp_directory_path() / "avgen_alpha_cutout_fixture";
    fs::create_directories(tmp);
    const fs::path scenePath = tmp / "cutout.scene.json";
    {
        std::ofstream out(scenePath);
        out << R"({
  "format": "avgen-scene", "version": 1, "name": "alpha cutout fixture",
  "nodes": [
    { "name": "cutout", "kind": "procedural",
      "procedural": {
        "source": { "kind": "box", "size": [1, 1, 1] },
        "distribution": { "kind": "single" },
        "material": { "baseColor": [0.2, 0.5, 0.3], "roughness": 0.8,
                      "alphaMode": "mask", "alphaCutoff": 0.42,
                      "baseColorTexture": "leaf.png" } } },
    { "name": "control", "kind": "procedural",
      "procedural": {
        "source": { "kind": "box", "size": [1, 1, 1] },
        "distribution": { "kind": "single" },
        "material": { "baseColor": [0.2, 0.5, 0.3], "roughness": 0.8 } } }
  ]
})";
    }

    assets::AssetRegistry registry(tmp);
    auto loaded = scene::Composition::loadFile(scenePath, registry);
    REQUIRE(loaded.has_value());

    const scene::CompositionNode* cutout = (*loaded)->findNode("cutout");
    REQUIRE(cutout != nullptr);
    CHECK(cutout->procedural.material.alphaMode == scene::AlphaMode::Mask);
    CHECK(cutout->procedural.material.alphaCutoff == Approx(0.42f));
    CHECK(cutout->procedural.baseColorTexturePath == "leaf.png");

    // The control. Without it, a parser that hard-coded Mask for every procedural node would pass
    // every assertion above.
    const scene::CompositionNode* control = (*loaded)->findNode("control");
    REQUIRE(control != nullptr);
    CHECK(control->procedural.material.alphaMode == scene::AlphaMode::Opaque);
    CHECK(control->procedural.baseColorTexturePath.empty());

    fs::remove(scenePath);
}
