// The semantic asset library and world recipes (ADR-060). The point of these tests is less that the
// JSON parses and more that the two promises hold: the manifest already in the repository keeps
// working untouched, and a recipe that names three fields is a legal recipe.

#include "assets/asset_library.hpp"
#include "world/world_recipe.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <system_error>

using namespace avgen;
namespace fs = std::filesystem;

namespace {
nlohmann::json flatManifest() {
    return nlohmann::json::parse(R"({
      "source": "test", "license": "CC0",
      "assets": [
        {"name": "hero_bloom", "file": "flora/hero_bloom.glb", "category": "flora",
         "archetype": "night_bloom", "tags": ["hero", "focal", "bioluminescent"],
         "naturalSize": [3.0, 5.5, 3.0], "triangles": 4200,
         "visualImportance": 0.95, "preferredScale": 9.0, "preferredDensity": 0.0004,
         "material": {"emissive": 0.9, "translucency": 0.6, "roughness": 0.4, "tint": [0.3, 0.9, 0.8]},
         "audioResponse": {"energy": 0.4, "impact": 0.9, "sway": 0.2, "bloom": 1.0},
         "variation": {"scale": 0.25, "hue": 0.4, "emissive": 0.3, "yaw": 1.0, "lean": 0.1}},
        {"name": "ground_fern", "file": "flora/fern.glb", "category": "flora",
         "tags": ["foreground", "delicate"], "visualImportance": 0.2,
         "preferredScale": 0.8, "preferredDensity": 0.35},
        {"name": "shelf_cap", "file": "fungi/shelf.glb", "category": "fungi",
         "tags": ["midground", "bioluminescent"], "visualImportance": 0.5, "preferredScale": 1.4},
        {"name": "boulder", "file": "rock/boulder.glb", "category": "rock",
         "tags": ["midground", "massive"], "visualImportance": 0.35, "preferredScale": 4.0}
      ]})");
}
} // namespace

TEST_CASE("The asset library reads a flat manifest and answers queries about visual role",
          "[assets][library]") {
    auto lib = assets::AssetLibrary::fromJson(flatManifest(), "/tmp/lib");
    REQUIRE(lib.has_value());
    CHECK(lib->size() == 4);
    CHECK(lib->license() == "CC0");

    const auto* hero = lib->find("hero_bloom");
    REQUIRE(hero != nullptr);
    CHECK(hero->category == assets::AssetCategory::Flora);
    CHECK(hero->archetype == "night_bloom");
    CHECK(hero->hasTag("focal"));
    CHECK(!hero->hasTag("background"));
    CHECK_THAT(hero->visualImportance, Catch::Matchers::WithinAbs(0.95, 1e-6));
    CHECK_THAT(hero->material.translucency, Catch::Matchers::WithinAbs(0.6, 1e-6));
    CHECK_THAT(hero->audioResponse.impact, Catch::Matchers::WithinAbs(0.9, 1e-6));
    CHECK_THAT(hero->variation.hue, Catch::Matchers::WithinAbs(0.4, 1e-6));
    // preferredScale wins over the mesh's own bounds; that is the point of authoring it.
    CHECK_THAT(hero->effectiveHeight(), Catch::Matchers::WithinAbs(9.0, 1e-6));
    CHECK(lib->resolve(*hero) == fs::path("/tmp/lib/flora/hero_bloom.glb"));

    SECTION("category and tag queries") {
        assets::AssetQuery flora;
        flora.category = assets::AssetCategory::Flora;
        CHECK(lib->select(flora).size() == 2);

        assets::AssetQuery glowing;
        glowing.anyTags = {"bioluminescent"};
        CHECK(glowing.allTags.empty());
        CHECK(lib->select(glowing).size() == 2);

        assets::AssetQuery bothTags;
        bothTags.allTags = {"midground", "bioluminescent"};
        const auto both = lib->select(bothTags);
        REQUIRE(both.size() == 1);
        CHECK(both[0]->name == "shelf_cap");
    }
    SECTION("a composer asks for something that can carry a focal region") {
        assets::AssetQuery focal;
        focal.minImportance = 0.8f;
        const auto hits = lib->select(focal);
        REQUIRE(hits.size() == 1);
        CHECK(hits[0]->name == "hero_bloom");
    }
    SECTION("and for something small enough to sit at the viewer's feet") {
        assets::AssetQuery small;
        small.maxHeight = 1.0f;
        const auto hits = small.maxHeight ? lib->select(small) : std::vector<const assets::AssetDescriptor*>{};
        REQUIRE(hits.size() == 1);
        CHECK(hits[0]->name == "ground_fern");
    }
    SECTION("an empty query is the whole library, in manifest order") {
        const auto all = lib->select({});
        REQUIRE(all.size() == 4);
        CHECK(all[0]->name == "hero_bloom");
        CHECK(all[3]->name == "boulder");
    }
}

TEST_CASE("The library round-trips through JSON", "[assets][library]") {
    auto lib = assets::AssetLibrary::fromJson(flatManifest(), "/tmp/lib");
    REQUIRE(lib.has_value());
    auto again = assets::AssetLibrary::fromJson(lib->toJson(), "/tmp/lib");
    REQUIRE(again.has_value());
    REQUIRE(again->size() == lib->size());
    for (std::size_t i = 0; i < lib->size(); ++i) {
        const auto& a = lib->assets()[i];
        const auto& b = again->assets()[i];
        INFO(a.name);
        CHECK(a.id == b.id);
        CHECK(a.category == b.category);
        CHECK(a.tags == b.tags);
        CHECK(a.archetype == b.archetype);
        CHECK_THAT(a.visualImportance, Catch::Matchers::WithinAbs(static_cast<double>(b.visualImportance), 1e-6));
        CHECK_THAT(a.preferredDensity, Catch::Matchers::WithinAbs(static_cast<double>(b.preferredDensity), 1e-6));
        CHECK_THAT(a.material.emissive, Catch::Matchers::WithinAbs(static_cast<double>(b.material.emissive), 1e-6));
        CHECK_THAT(a.audioResponse.bloom, Catch::Matchers::WithinAbs(static_cast<double>(b.audioResponse.bloom), 1e-6));
        CHECK_THAT(a.variation.scale, Catch::Matchers::WithinAbs(static_cast<double>(b.variation.scale), 1e-6));
    }
}

TEST_CASE("The grouped manifest already in the repository still loads", "[assets][library]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    // This is the promise the whole design rests on: a semantic layer that required every existing
    // manifest to be rewritten before anything worked would not be worth having.
    // Named the way the command line names it: relative to wherever the process happens to be.
    // An absolute manifest path masks this entirely -- its parent is already absolute, so the old
    // code produced absolute paths too and the test would pass against the bug.
    const fs::path absolute = fs::path(AVGEN_SOURCE_DIR) / "assets" / "manifest.json";
    REQUIRE(fs::exists(absolute));
    std::error_code ec;
    fs::path manifest = fs::relative(absolute, fs::current_path(), ec);
    if (ec || manifest.empty()) {
        SKIP("no relative path from the working directory to the manifest");
    }
    REQUIRE(fs::exists(manifest));
    auto lib = assets::AssetLibrary::loadFile(manifest);
    INFO((lib ? std::string() : lib.error().message));
    REQUIRE(lib.has_value());
    CHECK(lib->size() > 0);
    CHECK(lib->license() == "CC0");

    // The group names carry a category and a placement hint even though no entry names either.
    const auto* tree = lib->find("tree_tall");
    REQUIRE(tree != nullptr);
    CHECK(tree->category == assets::AssetCategory::Flora);
    CHECK(tree->hasTag("background"));
    CHECK(tree->triangles == 72);
    CHECK(tree->naturalSize.y > 1.0f);
    // With no preferredScale authored, height falls back to the mesh's own bounds.
    CHECK_THAT(tree->effectiveHeight(), Catch::Matchers::WithinAbs(static_cast<double>(tree->naturalSize.y), 1e-6));

    assets::AssetQuery rocks;
    rocks.category = assets::AssetCategory::Rock;
    CHECK(!lib->select(rocks).empty());
#endif
}

TEST_CASE("A duplicate asset id is rejected rather than silently shadowed", "[assets][library]") {
    auto doc = flatManifest();
    doc["assets"].push_back(nlohmann::json{{"name", "ground_fern"}, {"category", "flora"}});
    auto lib = assets::AssetLibrary::fromJson(doc, "/tmp/lib");
    REQUIRE(!lib.has_value());
    CHECK(lib.error().message.find("twice") != std::string::npos);
}

TEST_CASE("An unknown category is an error, not a silent Unknown", "[assets][library]") {
    auto doc = flatManifest();
    doc["assets"][0]["category"] = "vegetable";
    auto lib = assets::AssetLibrary::fromJson(doc, "/tmp/lib");
    REQUIRE(!lib.has_value());
    CHECK(lib.error().message.find("vegetable") != std::string::npos);
}

// ---- world recipes ---------------------------------------------------------------------------

TEST_CASE("A world recipe naming three fields is a legal recipe", "[world][recipe]") {
    const auto doc = nlohmann::json::parse(R"({
      "world": "sparse", "ecology": {"flora": 0.8}})");
    auto r = world::WorldRecipe::fromJson(doc);
    INFO((r ? std::string() : r.error().message));
    REQUIRE(r.has_value());
    CHECK(r->world == "sparse");
    CHECK_THAT(r->ecology.flora, Catch::Matchers::WithinAbs(0.8, 1e-6));
    // ...and everything it did not name keeps its default rather than becoming zero.
    CHECK_THAT(r->composition.negativeSpace, Catch::Matchers::WithinAbs(0.3, 1e-6));
    CHECK_THAT(r->lighting.key, Catch::Matchers::WithinAbs(0.5, 1e-6));
    CHECK(r->seed == 1);
}

TEST_CASE("A world recipe round-trips and accepts the spec's spellings", "[world][recipe]") {
    // The upgrade brief writes negative_space, crystals, creatures, floating_elements and moon;
    // the struct uses singular camelCase. Both are accepted so a recipe copied out of the brief
    // loads, which is the only reason the aliases exist.
    const auto doc = nlohmann::json::parse(R"({
      "world": "bioluminescent_valley",
      "composition": {"foreground": 0.9, "midground": 0.8, "background": 0.6, "negative_space": 0.3},
      "ecology": {"flora": 0.9, "fungi": 0.7, "crystals": 0.2, "creatures": 0.4},
      "atmosphere": {"fog": 0.4, "spores": 0.5, "floating_elements": 0.25},
      "lighting": {"moon": 0.7, "bioluminescence": 1.0},
      "art": {"name": "bioluminescent_cinematic",
              "palette": ["deep_blue", "violet", "cyan", "magenta"],
              "contrast": 1.1, "saturation": 1.05, "organicMotion": 0.8, "chaos": 0.2}})");
    auto r = world::WorldRecipe::fromJson(doc);
    INFO((r ? std::string() : r.error().message));
    REQUIRE(r.has_value());
    CHECK_THAT(r->composition.negativeSpace, Catch::Matchers::WithinAbs(0.3, 1e-6));
    CHECK_THAT(r->ecology.crystal, Catch::Matchers::WithinAbs(0.2, 1e-6));
    CHECK_THAT(r->ecology.creature, Catch::Matchers::WithinAbs(0.4, 1e-6));
    CHECK_THAT(r->atmosphere.floating, Catch::Matchers::WithinAbs(0.25, 1e-6));
    CHECK_THAT(r->lighting.key, Catch::Matchers::WithinAbs(0.7, 1e-6));
    CHECK(r->art.palette.size() == 4);

    auto again = world::WorldRecipe::fromJson(r->toJson());
    REQUIRE(again.has_value());
    CHECK(again->world == r->world);
    CHECK(again->art.palette == r->art.palette);
    for (std::size_t i = 0; i < r->weights().size(); ++i) {
        INFO(r->weights()[i].first);
        CHECK_THAT(again->weights()[i].second,
                   Catch::Matchers::WithinAbs(static_cast<double>(r->weights()[i].second), 1e-6));
    }
}

TEST_CASE("A world recipe rejects what would compose to nothing", "[world][recipe]") {
    SECTION("no name") {
        auto r = world::WorldRecipe::fromJson(nlohmann::json::parse(R"({"ecology": {"flora": 0.5}})"));
        REQUIRE(!r.has_value());
        CHECK(r.error().message.find("world") != std::string::npos);
    }
    SECTION("an empty ecology, which renders as a blank frame and reads as a bug") {
        auto r = world::WorldRecipe::fromJson(nlohmann::json::parse(
            R"({"world": "void", "ecology": {"flora": 0.0, "fungi": 0.0, "rock": 0.0}})"));
        REQUIRE(!r.has_value());
        CHECK(r.error().message.find("nothing would be placed") != std::string::npos);
    }
    SECTION("a weight outside 0..1") {
        auto r = world::WorldRecipe::fromJson(nlohmann::json::parse(
            R"({"world": "loud", "ecology": {"flora": 4.0}})"));
        REQUIRE(!r.has_value());
        CHECK(r.error().message.find("0..1") != std::string::npos);
    }
    SECTION("a non-positive extent") {
        auto r = world::WorldRecipe::fromJson(nlohmann::json::parse(
            R"({"world": "flat", "extent": 0.0, "ecology": {"flora": 0.5}})"));
        REQUIRE(!r.has_value());
        CHECK(r.error().message.find("extent") != std::string::npos);
    }
}

TEST_CASE("A recipe's asset library is resolved against the recipe's own directory",
          "[world][recipe]") {
    const auto dir = fs::temp_directory_path() / "avgen_recipe_test";
    fs::create_directories(dir / "packs");
    const auto path = dir / "valley.recipe.json";
    std::ofstream(path) << R"({"world": "valley", "ecology": {"flora": 0.7},
                               "assetLibrary": "packs/manifest.json"})";
    auto r = world::WorldRecipe::loadFile(path);
    INFO((r ? std::string() : r.error().message));
    REQUIRE(r.has_value());
    CHECK(r->assetLibrary == (dir / "packs" / "manifest.json").lexically_normal());
    fs::remove_all(dir);
}

TEST_CASE("The shipped recipe loads, and its library resolves", "[world][recipe]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    // Valid JSON is not the same as a recipe the engine accepts, and an example that does not load
    // is worse than no example. This also pins the aliases: the file is written in the brief's
    // spelling (negative_space, crystals, creatures, floating_elements, moon).
    const fs::path path =
        fs::path(AVGEN_SOURCE_DIR) / "examples" / "recipes" / "bioluminescent-valley.recipe.json";
    REQUIRE(fs::exists(path));
    auto r = world::WorldRecipe::loadFile(path);
    INFO((r ? std::string() : r.error().message));
    REQUIRE(r.has_value());
    CHECK(r->world == "bioluminescent_valley");
    CHECK(r->art.palette.size() == 5);
    CHECK_THAT(static_cast<double>(r->composition.negativeSpace),
               Catch::Matchers::WithinAbs(0.32, 1e-6));
    CHECK_THAT(static_cast<double>(r->lighting.bioluminescence),
               Catch::Matchers::WithinAbs(1.0, 1e-6));
    // Its asset library must exist and load, or the recipe points at nothing.
    REQUIRE(fs::exists(r->assetLibrary));
    auto lib = assets::AssetLibrary::loadFile(r->assetLibrary);
    INFO((lib ? std::string() : lib.error().message));
    REQUIRE(lib.has_value());
    CHECK(lib->size() > 0);
#endif
}

TEST_CASE("a library loaded from a file resolves assets independently of the working directory",
          "[assets][library]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    // The bug this pins: `resolve()` returned a path relative to the manifest, and its one consumer
    // -- the AssetRegistry -- then resolved that *again* against its own base directory. For a
    // composition that was never saved to disk that base is a temporary directory, so every layer
    // of every generated world pointed at a file under /var/folders/.../T/ that has never existed.
    // The render succeeded, reported no error, and drew bare terrain.
    //
    // Asserting "is_absolute" alone would be a weaker test than it looks: it would pass on a path
    // that is absolute and wrong. So the file is opened.
    // Named the way the command line names it: relative to wherever the process happens to be.
    // An absolute manifest path masks this entirely -- its parent is already absolute, so the old
    // code produced absolute paths too and the test would pass against the bug.
    const fs::path absolute = fs::path(AVGEN_SOURCE_DIR) / "assets" / "manifest.json";
    REQUIRE(fs::exists(absolute));
    std::error_code ec;
    fs::path manifest = fs::relative(absolute, fs::current_path(), ec);
    if (ec || manifest.empty()) {
        SKIP("no relative path from the working directory to the manifest");
    }
    REQUIRE(fs::exists(manifest));
    auto lib = assets::AssetLibrary::loadFile(manifest);
    REQUIRE(lib.has_value());
    REQUIRE(lib->size() > 0);

    std::size_t checked = 0;
    for (const auto& asset : lib->assets()) {
        if (asset.file.empty()) {
            continue;
        }
        const fs::path resolved = lib->resolve(asset);
        INFO("asset '" << asset.id << "' -> " << resolved.string());
        CHECK(resolved.is_absolute());
        CHECK(fs::exists(resolved));
        ++checked;
    }
    CHECK(checked > 0);
#endif
}
