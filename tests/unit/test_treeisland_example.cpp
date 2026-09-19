// The Tree of Life Floating Island showcase: the hierarchy, the motion contract, and the two
// things about it that are easy to break without anything failing.
//
// This file replaces `test_tree_example.cpp`, which guarded the *previous* Tree of Life -- a scene
// assembled in C++ from a parameter vector. That project is retired (ADR-338); the tree is now an
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

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
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
    std::vector<std::string> missing;
    int presentCount = 0;
    int missingCount = 0;
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
        // wrong is a fault.
        //
        // This was `if (fs::exists(resolved.parent_path()))` -- the directory standing in for "the
        // assets were generated here". That proxy fails on a *partial* set, which is exactly what
        // `tools/link-worktree-assets.sh` leaves behind: it links the gitignored files main had at
        // the moment it ran, so a worktree cut before main gained the ADR-339 layers has the
        // directory and only some of the files. Two agents hit it, and it cannot pass in CI.
        //
        // The rule that distinguishes the two cases: if *none* of them resolve, this is a worktree
        // without generated assets and there is nothing to check. If *some* resolve and others do
        // not, a path is wrong -- a set that is half-present is not a worktree state.
        if (fs::exists(resolved)) {
            ++presentCount;
        } else {
            ++missingCount;
            missing.push_back(resolved.string());
        }
    }
    {
        INFO(presentCount << " of " << (presentCount + missingCount) << " asset(s) resolve");
        for (const std::string& m : missing) {
            UNSCOPED_INFO("missing: " << m);
        }
        if (presentCount == 0) {
            UNSCOPED_INFO("no treeisle GLB resolves -- regenerate with the command in "
                          "assets/treeisle.manifest.json, or ignore on a worktree that never had them");
        }
        // Falsifiable either way: a wrong path in a populated worktree fails here, and so does a
        // scene that names six assets of which five were generated.
        CHECK((presentCount == 0 || missingCount == 0));
    }
    // The island, plus the tree's five Glowmere emission layers (ADR-339), and nothing else
    // reaches outside the repo.
    CHECK(assetNodes == 6);
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
    // ADR-339: the tree is one group carrying five glTF layers, because `emissiveBoost` is
    // per-node and scalar and the brief's §8 wants five separate emission channels. The group is
    // what keeps §16's hierarchy a hierarchy -- there is still exactly one transform between the
    // island and the whole tree.
    CHECK(tree->kind == scene::NodeKind::Group);

    // The five layers must stay *registered* with each other: they are one model cut into five
    // files, so any layer carrying a transform of its own would shear the tree apart. Each one is
    // asserted to be an identity local transform under the group, which is the property that
    // makes the split safe rather than merely convenient.
    const char* kLayers[] = {"tree-wood", "tree-twigs", "tree-tracery", "tree-foliage", "tree-lumens"};
    for (const char* name : kLayers) {
        const scene::CompositionNode* layer = comp.findNode(name);
        INFO("layer '" << name << "'");
        REQUIRE(layer != nullptr);
        CHECK(layer->kind == scene::NodeKind::Gltf);
        CHECK(layer->parent == "tree-of-life");
        CHECK(layer->transform.position == glm::vec3(0.0f));
        CHECK(layer->transform.scale == glm::vec3(1.0f));
        CHECK(glm::length(layer->transform.rotation - glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
              == Approx(0.0f).margin(1e-6));
        // Every layer's world transform is the group's, exactly -- the registration, stated as an
        // equation rather than trusted to the identity transforms above.
        const glm::mat4 groupWorld = comp.nodeWorldTransform(*tree).matrix();
        const glm::mat4 layerWorld = comp.nodeWorldTransform(*layer).matrix();
        float worst = 0.0f;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                worst = std::max(worst, std::abs(groupWorld[c][r] - layerWorld[c][r]));
            }
        }
        CHECK(worst == Approx(0.0f).margin(1e-5));
    }

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

    // Brief §22: the island's own motion is deterministic and time-based. `LfoSource::update` is
    // `renderTime * rate + phase` with no integration, which is why a render that starts at t = 40 s
    // agrees with one that started at 0 -- so the two sources that drive the island must be LFOs.
    //
    // This once asserted that EVERY source was an LFO. It is not the same claim, and the owner
    // disproved it by working: the project now also carries a `control` source, a `beat.pulse` route
    // onto the comet and a `state.progress` route onto the hue shift. A test that says "and there is
    // nothing else in this project" is a photograph of one afternoon's art direction, and it fails
    // the next time somebody adds a route -- which tells you nothing about the invariant it was
    // written to protect.
    REQUIRE(project.contains("sources"));
    std::map<std::string, std::string> sourceKind;
    for (const json& s : project.at("sources")) {
        sourceKind[s.value("name", std::string{})] = s.value("kind", std::string{});
    }
    CHECK(sourceKind.count("spin") == 1);
    CHECK(sourceKind.count("bob") == 1);
    CHECK(sourceKind["spin"] == "lfo");
    CHECK(sourceKind["bob"] == "lfo");

    // Brief §20: nothing routes to the tree. This is the half of the hierarchy contract that lives
    // in the project rather than the scene, and the half a scene-only test cannot see.
    REQUIRE(project.contains("routes"));
    bool sawSpin = false;
    bool sawBob = false;
    for (const json& r : project.at("routes")) {
        const auto target = r.value("target", std::string{});
        INFO("route " << r.value("source", std::string{}) << " -> " << target);
        // The contract is about the *hierarchy*: the island is animated and the tree rides it. So
        // nothing may aim at the tree, and any route that aims at a node must aim at the island.
        // Routes onto post, atmospherics or world effects are a different subject entirely and this
        // test has no business having an opinion about them.
        CHECK(target.find("tree") == std::string::npos);
        if (target.rfind("nodes/", 0) != 0) {
            continue;
        }
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

    // The cosmos is dark *relative to the tree*, which is the property that actually matters and
    // is what the original "< 0.01" was reaching for. ADR-339 replaced near-black with a layered
    // ethereal background (brief §17-§21), so the bound moves -- but it stays a bound, because
    // the failure mode it guards is real: a background that climbs to the tree's brightness stops
    // being a background. The tree's lit leaves sit near 0.5 scene-linear and its emissive specks
    // above 1.0, so 0.05 is still more than an order of magnitude below the subject.
    //
    // Note this only constrains the *scene's* colours. The broad haze is a background user-shader
    // layer (shaders/glowmere-cosmos.wgsl) and is not reachable from here; what keeps that honest
    // is the rendered control arm `_ctl-no-cosmos-shader`, not an assertion.
    constexpr float kBackgroundCeiling = 0.05f;
    const json& env = scene.at("environment");
    for (const float c : env.at("background").get<std::vector<float>>()) {
        CHECK(c >= 0.0f);
        CHECK(c < kBackgroundCeiling);
    }
    const json& sky = env.at("sky");
    CHECK(sky.value("enabled", false));
    // One ceiling again, for all three.
    //
    // This was briefly split, with `groundColor` given a looser bound of its own, because ADR-358's
    // lighting pass had taken the sky's ground hemisphere up in three doublings to 0.096 on blue and
    // the strict bound failed on it. The split was wrong, and wrong in an instructive way: the
    // project overrides `env/sky/groundColor` with the pre-ADR-358 value, so by ADR-264 those
    // doublings were never in a rendered frame at all. The loosening accommodated a number that
    // nothing ever saw. The scene and the project now agree on the value that actually renders, and
    // all three components are back under the strict bound with nothing special-cased.
    for (const char* key : {"zenithColor", "horizonColor", "groundColor"}) {
        for (const float c : sky.at(key).get<std::vector<float>>()) {
            INFO("sky." << key);
            CHECK(c >= 0.0f);
            CHECK(c < kBackgroundCeiling);
        }
    }

    // No sun disc: this is space, and §12 forbids giant distracting objects.
    CHECK(sky.value("sunIntensity", 1.0) == Approx(0.0));

    // Not one light any more, but still not a rig. ADR-339 added a teal rim and an indigo fill
    // because the reference's cream-white canopy is the .blend's warm *key* and the island's
    // underside was reading as a black silhouette (§22). The guard that survives is the one that
    // was meant: a small, hand-countable set, with exactly one key and exactly one shadow caster.
    REQUIRE(scene.contains("lights"));
    const json& lights = scene.at("lights");
    CHECK(lights.size() <= 3);
    int keys = 0;
    int shadowCasters = 0;
    for (const json& light : lights) {
        CHECK(light.value("type", std::string{}) == "directional");
        if (light.value("role", std::string{}) == "key") {
            ++keys;
        }
        if (light.value("castsShadow", false)) {
            ++shadowCasters;
        }
    }
    CHECK(keys == 1);
    CHECK(shadowCasters == 1);

    const json& key = lights.at(0);
    CHECK(key.value("type", std::string{}) == "directional");
    CHECK(key.value("role", std::string{}) == "key");
    // From above, and at a cinematic angle rather than overhead.
    //
    // This once required the downward component to dominate *both* horizontal axes, which is the
    // same as demanding an elevation above 45 degrees. ADR-358 deliberately moved the key to 35, a
    // low raking angle that is the whole point of a key light -- an overhead key flattens the tree
    // and puts its own shadow under itself. The old assertion was not protecting an invariant, it
    // was recording the elevation the light happened to have.
    //
    // So: a band, not a floor (ADR-182). The key must come from above the horizon and must not be
    // so shallow that it grazes, nor so steep that it is a toplight. Anything in 10..80 degrees is
    // a key somebody chose; outside it, something has gone wrong rather than been art-directed.
    const auto dir3 = key.at("direction").get<std::vector<float>>();
    REQUIRE(dir3.size() == 3);
    CHECK(dir3[1] < 0.0f); // travels downward, i.e. the light is above the horizon
    const float len = std::sqrt(dir3[0] * dir3[0] + dir3[1] * dir3[1] + dir3[2] * dir3[2]);
    REQUIRE(len > 0.0f);
    const float elevationDeg = std::asin(std::clamp(-dir3[1] / len, -1.0f, 1.0f)) * 180.0f
                               / 3.14159265358979323846f;
    INFO("key elevation " << elevationDeg << " degrees");
    CHECK(elevationDeg > 10.0f);
    CHECK(elevationDeg < 80.0f);
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
