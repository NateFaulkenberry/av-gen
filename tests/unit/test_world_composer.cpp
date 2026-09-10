// The world composer (ADR-061). These tests are about hierarchy, not about placement: the ecology
// already has tests for where instances land. What is new and worth guarding is that a fern does
// not end up on the ridge line, that a recipe's weights actually reach the layers, that the same
// recipe composes the same world twice, and that the composer's output is the type the existing
// ecology consumes rather than a parallel one.

#include "assets/asset_library.hpp"
#include "world/world_composer.hpp"
#include "world/world_recipe.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>

using namespace avgen;

namespace {
assets::AssetLibrary testLibrary() {
    const auto doc = nlohmann::json::parse(R"({
      "source": "test", "license": "CC0",
      "assets": [
        {"name": "hero_bloom", "category": "flora", "tags": ["hero", "focal"],
         "file": "a.glb", "visualImportance": 0.97, "preferredScale": 11.0,
         "preferredDensity": 0.0003, "naturalSize": [3.0, 6.0, 3.0], "triangles": 4000,
         "material": {"emissive": 0.9, "tint": [0.3, 0.9, 0.8]},
         "variation": {"scale": 0.3, "hue": 0.5, "yaw": 1.0}},
        {"name": "ridge_pine", "category": "flora", "file": "b.glb",
         "visualImportance": 0.4, "preferredScale": 14.0, "naturalSize": [2.0, 9.0, 2.0]},
        {"name": "mid_bush", "category": "flora", "file": "c.glb",
         "visualImportance": 0.3, "preferredScale": 3.0, "naturalSize": [1.2, 1.6, 1.2]},
        {"name": "ground_fern", "category": "flora", "file": "d.glb",
         "visualImportance": 0.15, "preferredScale": 0.7, "preferredDensity": 0.4,
         "naturalSize": [0.9, 0.6, 0.9]},
        {"name": "cap_fungus", "category": "fungi", "file": "e.glb",
         "visualImportance": 0.35, "preferredScale": 0.5, "preferredDensity": 0.05,
         "material": {"emissive": 0.7, "tint": [0.4, 0.2, 1.0]}},
        {"name": "boulder", "category": "rock", "file": "f.glb",
         "visualImportance": 0.25, "preferredScale": 3.5, "naturalSize": [3.0, 2.4, 3.0]},
        {"name": "spire", "category": "structure", "file": "g.glb",
         "visualImportance": 0.5, "preferredScale": 20.0}
      ]})");
    auto lib = assets::AssetLibrary::fromJson(doc, "/tmp/lib");
    REQUIRE(lib.has_value());
    return std::move(*lib);
}

world::WorldRecipe testRecipe() {
    const auto doc = nlohmann::json::parse(R"({
      "world": "valley", "seed": 4242, "extent": 400.0,
      "composition": {"foreground": 0.9, "midground": 0.7, "background": 0.5,
                      "negative_space": 0.33, "focalStrength": 0.9},
      "ecology": {"flora": 0.9, "fungi": 0.6, "rock": 0.4, "structure": 0.0}})");
    auto r = world::WorldRecipe::fromJson(doc);
    REQUIRE(r.has_value());
    return *r;
}
} // namespace

TEST_CASE("Assets land in the band their size and tags imply", "[world][composer]") {
    const auto lib = testLibrary();
    CHECK(world::bandForAsset(*lib.find("ground_fern")) == world::DepthBand::Foreground);
    CHECK(world::bandForAsset(*lib.find("cap_fungus")) == world::DepthBand::Foreground);
    CHECK(world::bandForAsset(*lib.find("mid_bush")) == world::DepthBand::Midground);
    CHECK(world::bandForAsset(*lib.find("ridge_pine")) == world::DepthBand::Background);
    CHECK(world::bandForAsset(*lib.find("hero_bloom")) == world::DepthBand::Background);

    SECTION("an explicit tag overrules the height, because it is somebody stating an intention") {
        const auto doc = nlohmann::json::parse(R"({"assets": [
          {"name": "huge_but_near", "category": "flora", "preferredScale": 30.0,
           "tags": ["foreground"]}]})");
        auto tagged = assets::AssetLibrary::fromJson(doc, "/tmp");
        REQUIRE(tagged.has_value());
        CHECK(world::bandForAsset(tagged->assets()[0]) == world::DepthBand::Foreground);
    }
}

TEST_CASE("Composing produces ecology layers, not a parallel placement system", "[world][composer]") {
    const auto lib = testLibrary();
    auto composed = world::composeWorld(testRecipe(), lib);
    INFO((composed ? std::string() : composed.error().message));
    REQUIRE(composed.has_value());
    REQUIRE(!composed->layers.empty());

    // The output is world::ScatterLayer, which is what world::Ecology already places. If this ever
    // becomes a different type, the composer has forked the placement logic and this test is the
    // warning.
    static_assert(std::is_same_v<decltype(composed->layers)::value_type, world::ScatterLayer>);

    for (const auto& layer : composed->layers) {
        INFO(layer.name);
        CHECK(!layer.asset.empty());
        CHECK(!layer.densities.empty());
        CHECK(layer.height > 0.0f);
        CHECK(layer.maxScale >= layer.minScale);
        CHECK(layer.viewDistance > 0.0f);
        CHECK(layer.maxInstances > 0);
        for (const auto& d : layer.densities) {
            CHECK(d.density > 0.0f);
        }
    }
}

TEST_CASE("A recipe's weights reach the layers", "[world][composer]") {
    const auto lib = testLibrary();

    SECTION("a category the recipe sets to zero is absent, not merely rare") {
        auto composed = world::composeWorld(testRecipe(), lib);
        REQUIRE(composed.has_value());
        const auto& l = composed->layers;
        // structure is 0.0 in the recipe
        CHECK(std::none_of(l.begin(), l.end(),
                           [](const world::ScatterLayer& s) { return s.name == "spire"; }));
        CHECK(std::any_of(l.begin(), l.end(),
                          [](const world::ScatterLayer& s) { return s.name == "boulder"; }));
    }

    SECTION("halving a band's weight halves the density of everything in it") {
        auto full = world::composeWorld(testRecipe(), lib);
        REQUIRE(full.has_value());
        auto recipe = testRecipe();
        recipe.composition.foreground *= 0.5f;
        auto half = world::composeWorld(recipe, lib);
        REQUIRE(half.has_value());

        const auto densityOf = [](const world::ComposedWorld& w, const char* name) {
            for (const auto& l : w.layers) {
                if (l.name == name) {
                    return l.densities.front().density;
                }
            }
            return 0.0f;
        };
        CHECK_THAT(static_cast<double>(densityOf(*half, "ground_fern")),
                   Catch::Matchers::WithinRel(static_cast<double>(densityOf(*full, "ground_fern")) * 0.5, 1e-5));
        // ...and leaves the other bands alone.
        CHECK_THAT(static_cast<double>(densityOf(*half, "mid_bush")),
                   Catch::Matchers::WithinRel(static_cast<double>(densityOf(*full, "mid_bush")), 1e-5));
    }

    SECTION("bioluminescence scales the emission the material profile asked for") {
        auto dark = testRecipe();
        dark.lighting.bioluminescence = 0.25f;
        auto bright = testRecipe();
        bright.lighting.bioluminescence = 1.0f;
        const auto emissionOf = [&lib](const world::WorldRecipe& r) {
            auto w = world::composeWorld(r, lib);
            REQUIRE(w.has_value());
            for (const auto& l : w->layers) {
                if (l.name == "cap_fungus") {
                    return l.emissiveIntensity;
                }
            }
            return 0.0f;
        };
        CHECK(emissionOf(bright) > emissionOf(dark) * 3.0f);
    }
}

TEST_CASE("The composition has a focal subject and deliberate empty regions", "[world][composer]") {
    const auto lib = testLibrary();
    auto composed = world::composeWorld(testRecipe(), lib);
    REQUIRE(composed.has_value());
    const auto& plan = composed->plan;

    REQUIRE(plan.focal.size() == 1);
    // The most important asset in the library gets the focal region; that is what importance is for.
    CHECK(plan.focal[0].assetId == "hero_bloom");
    // Off-centre, because a subject in the middle of the world is the composition nobody chose.
    CHECK(glm::length(plan.focal[0].center) > 10.0f);

    CHECK(!plan.voids.empty());
    CHECK(plan.emptyFraction > 0.0f);
    for (const auto& v : plan.voids) {
        // No void may sit on top of the one thing worth looking at.
        CHECK(glm::length(v.center - plan.focal[0].center) >= (v.radius + plan.focal[0].radius) * 0.9f);
    }

    SECTION("negative space of zero asks for no voids") {
        auto recipe = testRecipe();
        recipe.composition.negativeSpace = 0.0f;
        auto w = world::composeWorld(recipe, lib);
        REQUIRE(w.has_value());
        CHECK(w->plan.voids.empty());
        CHECK_THAT(static_cast<double>(w->plan.emptyFraction), Catch::Matchers::WithinAbs(0.0, 1e-9));
    }
}

TEST_CASE("A small shade-dweller is given a relation to something taller", "[world][composer]") {
    const auto lib = testLibrary();
    auto composed = world::composeWorld(testRecipe(), lib);
    REQUIRE(composed.has_value());
    for (const auto& l : composed->layers) {
        if (l.name == "cap_fungus") {
            REQUIRE(l.proximity.has_value());
            // It grows near the tallest thing that is not itself underfoot -- which is what makes
            // this a world rather than a collection of independent scatters.
            CHECK(l.proximity->layer == "ridge_pine");
            CHECK(l.proximity->maxDistance > 1.0f);
            CHECK(l.proximity->strength > 0.0f);
            return;
        }
    }
    FAIL("cap_fungus was not composed");
}

TEST_CASE("Composition is a pure function of recipe and library", "[world][composer]") {
    const auto lib = testLibrary();
    auto a = world::composeWorld(testRecipe(), lib);
    auto b = world::composeWorld(testRecipe(), lib);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->layers.size() == b->layers.size());
    for (std::size_t i = 0; i < a->layers.size(); ++i) {
        CHECK(a->layers[i].name == b->layers[i].name);
        CHECK_THAT(static_cast<double>(a->layers[i].densities.front().density),
                   Catch::Matchers::WithinAbs(static_cast<double>(b->layers[i].densities.front().density), 1e-9));
    }
    REQUIRE(a->plan.focal.size() == b->plan.focal.size());
    CHECK_THAT(static_cast<double>(a->plan.focal[0].center.x),
               Catch::Matchers::WithinAbs(static_cast<double>(b->plan.focal[0].center.x), 1e-9));

    SECTION("and a different seed composes a different world") {
        auto recipe = testRecipe();
        recipe.seed = 99;
        auto c = world::composeWorld(recipe, lib);
        REQUIRE(c.has_value());
        CHECK(glm::length(c->plan.focal[0].center - a->plan.focal[0].center) > 1.0f);
        // ...but the same layers, because the seed moves the composition, not the ecology.
        CHECK(c->layers.size() == a->layers.size());
    }
}

TEST_CASE("Composing rejects inputs that would produce an empty world", "[world][composer]") {
    SECTION("an empty library") {
        const auto doc = nlohmann::json::parse(R"({"assets": [{"name": "x", "category": "flora"}]})");
        auto lib = assets::AssetLibrary::fromJson(doc, "/tmp");
        REQUIRE(lib.has_value());
        auto recipe = testRecipe();
        recipe.ecology.flora = 0.0f;
        recipe.ecology.fungi = 0.0f;
        recipe.ecology.rock = 0.0f;
        recipe.ecology.crystal = 0.0f;
        recipe.ecology.creature = 0.0f;
        recipe.ecology.structure = 0.5f; // valid, but nothing in the library is a structure
        auto w = world::composeWorld(recipe, *lib);
        REQUIRE(!w.has_value());
        CHECK(w.error().message.find("match nothing") != std::string::npos);
    }
}
