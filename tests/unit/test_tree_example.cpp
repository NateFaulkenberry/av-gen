// The shipped showcase: that it exists in the menu, loads, and survives a save.
//
// These guard the gap this work closed. The Tree of Life was absent from the examples dropdown for
// the whole of its development because it was assembled in C++, and `examples/index.json` addresses
// a project or a recipe -- a scene built by calling a function is neither. Nothing failed; it simply
// was not there, which is the kind of absence a test catches and a person does not.

#include "app/examples.hpp"
#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/tree_generated.hpp"
#include "scene/tree_generator.hpp"
#include "search/candidate_search.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <filesystem>

using namespace avgen;
namespace fs = std::filesystem;

namespace {
fs::path examplesDir() {
#ifdef AVGEN_SOURCE_DIR
    return fs::path(AVGEN_SOURCE_DIR) / "examples";
#else
    return {};
#endif
}
} // namespace

TEST_CASE("The Tree of Life is in the examples menu", "[tree][example]") {
    const fs::path dir = examplesDir();
    if (dir.empty()) {
        SKIP("AVGEN_SOURCE_DIR not defined");
    }
    const auto examples = app::loadExampleIndex(dir / "index.json");
    REQUIRE(examples.has_value());
    const app::ExampleInfo* tree = nullptr;
    for (const app::ExampleInfo& e : *examples) {
        if (e.name == "The Tree of Life") {
            tree = &e;
        }
    }
    REQUIRE(tree != nullptr);
    CHECK_FALSE(tree->description.empty());
    CHECK(fs::exists(tree->file));
}

TEST_CASE("The shipped tree scene loads, regenerates, and re-saves", "[tree][example]") {
    const fs::path dir = examplesDir();
    if (dir.empty()) {
        SKIP("AVGEN_SOURCE_DIR not defined");
    }
    scene::registerTreeGenerator();
    assets::AssetRegistry registry(dir / "tree");
    auto loaded = scene::Composition::loadFile(dir / "tree" / "tree.scene.json", registry);
    REQUIRE(loaded.has_value());

    // Eight tree parts plus the ground and the ring of distant trees. The environment travels with
    // the tree deliberately: without it the hero loads into an empty world, and an empty ground
    // plane has no size, so a thirty-metre tree reads as an eight-metre one.
    REQUIRE((*loaded)->nodes().size() == 10);

    int generated = 0;
    const auto schema = scene::treeSchema();
    for (const auto& node : (*loaded)->nodes()) {
        const auto& source = node->procedural.source;
        if (source.kind != scene::PrimitiveKind::Generated) {
            continue;
        }
        ++generated;
        INFO("node " << node->name);
        // The scene stores the PARAMETER VECTOR, not geometry. A node holding a baked mesh would
        // not survive one `applyParameters`, could not be keyed, modulated, undone or saved.
        CHECK(source.generated.values.size() == schema.parameters.size());
        CHECK(source.generated.schemaHash == schema.hash());
        CHECK(source.generated.index == scene::kHeroCandidate);
        CHECK_FALSE(source.generated.generator.empty());
        CHECK(scene::hasGenerator(source.generated.generator));
    }
    CHECK(generated == 10);

    // It has to actually build. A scene naming a generator nobody registered, or a parameter vector
    // the schema rejects, fails here rather than as an empty frame.
    params::ParameterSet parameters;
    params::Modulator modulator;
    (*loaded)->attach(parameters, modulator);
    (*loaded)->update(FrameTime{});
    const scene::Scene& built = (*loaded)->scene();
    REQUIRE(built.procedurals.size() == 10);
    std::size_t triangles = 0;
    for (const scene::ProceduralGeometry& pg : built.procedurals) {
        const auto mesh = pg.resolveSourceMesh();
        INFO("procedural " << pg.name << (mesh ? "" : ": " + mesh.error().message));
        REQUIRE(mesh.has_value());
        triangles += mesh->indices.size() / 3;
    }
    INFO(fmt::format("{} triangles across {} nodes", triangles, built.procedurals.size()));
    CHECK(triangles > 50000);

    // And survive being saved again, which is the requirement an editable showcase has to meet and
    // a different one from "the file loads". A scene that opens correctly and saves wrong loses the
    // artist's work rather than the author's.
    const fs::path resaved = fs::temp_directory_path() / "avgen_tree_example_resave.scene.json";
    REQUIRE((*loaded)->saveFile(resaved).has_value());
    assets::AssetRegistry again(dir / "tree");
    auto reloaded = scene::Composition::loadFile(resaved, again);
    REQUIRE(reloaded.has_value());
    REQUIRE((*reloaded)->nodes().size() == 10);
    for (const auto& node : (*reloaded)->nodes()) {
        const auto& source = node->procedural.source;
        INFO("re-saved node " << node->name);
        REQUIRE(source.kind == scene::PrimitiveKind::Generated);
        CHECK(source.generated.values.size() == schema.parameters.size());
        CHECK(source.generated.schemaHash == schema.hash());
    }
    fs::remove(resaved);
}

TEST_CASE("A procedural material can express alpha cutout", "[tree][example]") {
    // The gap that kept the canopy opaque when it moved into a scene file: a procedural node's
    // material JSON parsed no alphaMode, no cutoff and no texture, so leaves were rectangles. Only
    // visible once a hero assembled in C++ -- where `Material` is built directly and can say
    // anything -- crossed into the data format.
    const fs::path dir = examplesDir();
    if (dir.empty()) {
        SKIP("AVGEN_SOURCE_DIR not defined");
    }
    assets::AssetRegistry registry(dir / "tree");
    auto loaded = scene::Composition::loadFile(dir / "tree" / "tree.scene.json", registry);
    REQUIRE(loaded.has_value());
    int cutout = 0;
    for (const auto& node : (*loaded)->nodes()) {
        if (node->name.rfind("tree-foliage", 0) != 0) {
            continue;
        }
        INFO("node " << node->name);
        CHECK(node->procedural.material.alphaMode == scene::AlphaMode::Mask);
        CHECK(node->procedural.material.alphaCutoff > 0.0f);
        CHECK_FALSE(node->procedural.baseColorTexturePath.empty());
        ++cutout;
    }
    CHECK(cutout == scene::kFoliageTints);
}
