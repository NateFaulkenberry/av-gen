// Art-direction profiles (ADR-070).
//
// The point of these tests is not that the structs hold numbers. It is that the two decisions the
// Glowmere audit identified as carrying the look are enforced rather than merely written down: the
// emission ladder has a *gap* rather than being a ramp, and the accent colour is reserved. Both are
// the kind of thing that gets edited away by someone reasonably asking for "a bit more glow".

#include "world/art_direction.hpp"
#include "world/world_composer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <map>

using namespace avgen;

TEST_CASE("Every built-in art profile is valid", "[world][art]") {
    const auto& profiles = world::artProfiles();
    REQUIRE(profiles.size() >= 3);
    for (const auto& p : profiles) {
        INFO("profile '" << p.name << "'");
        auto ok = p.validate();
        INFO((ok ? std::string() : ok.error().message));
        CHECK(ok.has_value());
        CHECK(!p.description.empty());
    }
}

TEST_CASE("The profiles are genuinely different worlds, not one world with knobs", "[world][art]") {
    // Glowmere must not be the only thing the vocabulary can say. If these ever collapse toward
    // each other, the profile system has become a Glowmere configuration file.
    const auto* glow = world::findArtProfile("glowmere");
    const auto* ember = world::findArtProfile("emberwaste");
    const auto* pale = world::findArtProfile("palefen");
    REQUIRE(glow != nullptr);
    REQUIRE(ember != nullptr);
    REQUIRE(pale != nullptr);

    // A night lit by its own flora, against a day lit by a sun.
    CHECK(glow->emission.brightest > ember->emission.brightest * 2.0f);
    CHECK(ember->lighting.keyIntensity > glow->lighting.keyIntensity);
    // Glowmere's ambient stays out of the key's way; the overcast fen's ambient *is* the light.
    CHECK(glow->lighting.keyToAmbient() > 5.0f);
    CHECK(pale->lighting.keyToAmbient() < 2.0f);
    // Glowmere's mist has no direction; the ember waste's does.
    CHECK(glow->atmosphere.volumeAnisotropy < 0.2f);
    CHECK(ember->atmosphere.volumeAnisotropy > 0.5f);
    // Their palettes do not agree about anything.
    CHECK(glm::length(glow->palette.primary - ember->palette.primary) > 0.5f);
    CHECK(glm::length(glow->palette.foliage - pale->palette.foliage) > 0.4f);
}

TEST_CASE("A ladder without a gap is rejected", "[world][art]") {
    world::EmissionLadder ladder;   // Glowmere's, which has a 13x gap
    REQUIRE(ladder.validate().has_value());
    CHECK(ladder.gap() > 10.0f);

    // The failure this exists to prevent: everything nudged upward until "bright" and "ordinary"
    // are a smooth ramp and there is no hierarchy left to read.
    world::EmissionLadder flattened = ladder;
    flattened.noticeable = 2.0f;
    auto flat = flattened.validate();
    REQUIRE(!flat.has_value());
    INFO(flat.error().message);
    CHECK(flat.error().message.find("ramp") != std::string::npos);

    // Out of order is rejected too: a rung dimmer than the one below it is not a hierarchy.
    world::EmissionLadder inverted;
    inverted.beacon = 0.001f;
    CHECK(!inverted.validate().has_value());

    // A world where nothing glows at all is legal -- the fen is one -- and must not trip the gap
    // check, which only applies once there is ordinary emission to measure against.
    world::EmissionLadder dark;
    dark.inert = 0.0f;
    dark.silhouette = 0.0f;
    dark.groundCover = 0.0f;
    dark.noticeable = 0.0f;
    dark.special = 0.0f;
    dark.rare = 0.0f;
    dark.beacon = 0.0f;
    dark.brightest = 0.0f;
    CHECK(dark.validate().has_value());
}

TEST_CASE("An unknown profile name is an error, not a silent Glowmere", "[world][art]") {
    world::ArtDirection art;
    art.profile = "glowmear";   // a plausible typo
    auto resolved = world::resolveArtDirection(art);
    REQUIRE(!resolved.has_value());
    // The message has to name what *is* available, or the reader has to go and read the source.
    CHECK(resolved.error().message.find("glowmere") != std::string::npos);
    CHECK(resolved.error().message.find("emberwaste") != std::string::npos);
}

TEST_CASE("A recipe's own words beat the profile it named", "[world][art]") {
    world::ArtDirection art;
    art.profile = "glowmere";
    auto plain = world::resolveArtDirection(art);
    REQUIRE(plain.has_value());
    CHECK(plain->name == "glowmere");
    // Named alone, the profile supplies everything.
    CHECK_THAT(plain->emission.brightest, Catch::Matchers::WithinRel(6.89f, 1e-4f));

    // Naming a profile is a starting point, not a cage.
    art.name = "my valley";
    art.palette = {"black", "rose", "gold", "moss", "ice"};
    auto overridden = world::resolveArtDirection(art);
    REQUIRE(overridden.has_value());
    CHECK(overridden->name == "my valley");
    CHECK(glm::length(overridden->palette.primary - plain->palette.primary) > 0.1f);
    // A recipe that states its own palette states its own accent with it, or the reserved colour
    // would still be the profile's and would no longer be absent from the world.
    CHECK_THAT(glm::length(overridden->heroAccent - overridden->palette.accent),
               Catch::Matchers::WithinAbs(0.0, 1e-6));
    // What it did not override, it keeps.
    CHECK_THAT(overridden->emission.brightest, Catch::Matchers::WithinRel(6.89f, 1e-4f));
}

TEST_CASE("A profile with no name at all still resolves", "[world][art]") {
    world::ArtDirection art;   // no profile, no palette: a recipe that says nothing about its look
    auto resolved = world::resolveArtDirection(art);
    REQUIRE(resolved.has_value());
    CHECK(!resolved->name.empty());
    CHECK(resolved->validate().has_value());
}

TEST_CASE("A profile's rig keeps its key-to-ambient ratio and does not follow the camera",
          "[world][art]") {
    const auto* glow = world::findArtProfile("glowmere");
    REQUIRE(glow != nullptr);
    const scene::LightRig rig = world::rigFor(*glow);
    REQUIRE(rig.validate().has_value());
    REQUIRE(rig.lights.size() >= 2);

    // The ratio is the art direction. 7.5:1 is what makes Glowmere a night.
    CHECK_THAT(rig.keyIntensity / rig.ambientIntensity,
               Catch::Matchers::WithinRel(glow->lighting.keyToAmbient(), 1e-4f));
    const auto key = std::find_if(rig.lights.begin(), rig.lights.end(),
                                  [](const scene::RigLight& l) { return l.name == "key"; });
    REQUIRE(key != rig.lights.end());
    CHECK_THAT(key->elevationDegrees,
               Catch::Matchers::WithinRel(glow->lighting.keyElevationDegrees, 1e-4f));
    // A key that follows the camera cannot rake across a landscape: every shot gets the same
    // relationship to the light, which is the flat look a low elevation exists to avoid.
    CHECK(!key->followCamera);
    CHECK(key->castsShadow);
}

TEST_CASE("A profile changes what a composed world emits", "[world][art][composer]") {
    // The end-to-end claim: naming a different profile produces different layers from the same
    // library and the same recipe. If it does not, the profile is decoration.
    const auto doc = nlohmann::json::parse(R"({
      "source": "test", "license": "CC0",
      "assets": [
        {"name": "tree", "category": "flora", "tags": ["background"], "file": "a.glb",
         "preferredScale": 9.0, "naturalSize": [1.0, 2.0, 1.0], "preferredDensity": 0.01,
         "material": {"emissive": 0.4, "tint": [0.2, 0.5, 0.3]}},
        {"name": "cap", "category": "fungi", "file": "b.glb", "preferredScale": 0.5,
         "naturalSize": [0.2, 0.2, 0.2], "preferredDensity": 0.05,
         "material": {"emissive": 1.0, "tint": [0.8, 0.3, 0.5]}},
        {"name": "spark", "category": "fungi", "tags": ["rare"], "file": "c.glb",
         "preferredScale": 0.7, "naturalSize": [0.2, 0.3, 0.2], "preferredDensity": 0.01,
         "material": {"emissive": 1.0, "tint": [0.9, 0.4, 0.6]}}
      ]})");
    auto library = assets::AssetLibrary::fromJson(doc, "/tmp/lib");
    REQUIRE(library.has_value());

    const auto emissionOf = [&](const char* profile) {
        world::WorldRecipe recipe;
        recipe.world = "w";
        recipe.art.profile = profile;
        recipe.lighting.bioluminescence = 1.0f;
        auto composed = world::composeWorld(recipe, *library);
        REQUIRE(composed.has_value());
        std::map<std::string, float> out;
        for (const auto& l : composed->layers) {
            out[l.name] = l.emissiveIntensity;
        }
        return out;
    };

    const auto glow = emissionOf("glowmere");
    const auto ember = emissionOf("emberwaste");
    REQUIRE(glow.count("spark") == 1);
    REQUIRE(ember.count("spark") == 1);

    // Same recipe, same library, very different worlds.
    CHECK(glow.at("spark") > ember.at("spark") * 1.8f);
    // The ladder's shape survives composition: the rare accent outranks the ordinary fungus, which
    // outranks the canopy, in both worlds.
    CHECK(glow.at("spark") > glow.at("cap"));
    CHECK(glow.at("cap") > glow.at("tree"));
    CHECK(ember.at("spark") > ember.at("cap"));
    // And the gap survives it too, which is the property the whole ladder exists for.
    CHECK(glow.at("cap") / std::max(glow.at("tree"), 1e-6f) > 4.0f);
}

TEST_CASE("The reserved accent does not leak into the vegetation", "[world][art][composer]") {
    // One warm light in a cool world is the mechanism that makes a hero findable from anywhere in
    // frame. A scatter layer wearing that colour is the cheapest possible way to lose it.
    const auto doc = nlohmann::json::parse(R"({
      "source": "test", "license": "CC0",
      "assets": [
        {"name": "spark", "category": "fungi", "tags": ["rare"], "file": "c.glb",
         "preferredScale": 0.7, "naturalSize": [0.2, 0.3, 0.2], "preferredDensity": 0.02,
         "material": {"emissive": 1.0, "tint": [0.9, 0.4, 0.6]}}
      ]})");
    auto library = assets::AssetLibrary::fromJson(doc, "/tmp/lib");
    REQUIRE(library.has_value());

    world::WorldRecipe recipe;
    recipe.world = "w";
    recipe.art.profile = "glowmere";
    auto composed = world::composeWorld(recipe, *library);
    REQUIRE(composed.has_value());
    REQUIRE(composed->layers.size() == 1);
    REQUIRE(composed->profile.reserveAccent);

    const glm::vec3 accent = composed->profile.heroAccent;
    const glm::vec3 used = composed->layers[0].emissiveColor;
    INFO("accent " << accent.r << "," << accent.g << "," << accent.b << "  used " << used.r << ","
                   << used.g << "," << used.b);
    CHECK(glm::length(used - accent) > 0.2f);
}

TEST_CASE("The repository's own library composes into a real hierarchy", "[world][art][composer]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    // The end of the chain, on the actual manifest rather than a fixture. `material.emissive` is a
    // fraction of a rung, so a canopy tree may legitimately carry a weight of 1.0 -- what has to
    // hold is that after composition it is still a silhouette, and that the ladder's gap survives
    // real data. This is the assertion test_asset_library.cpp used to make on the raw weight, moved
    // to where the ladder is in scope and it can mean something.
    const std::filesystem::path manifest =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "manifest.json";
    auto library = assets::AssetLibrary::loadFile(manifest);
    REQUIRE(library.has_value());

    world::WorldRecipe recipe;
    recipe.world = "hierarchy";
    recipe.art.profile = "glowmere";
    recipe.lighting.bioluminescence = 1.0f;
    recipe.ecology.fungi = 0.7f;
    recipe.ecology.rock = 0.35f;
    auto composed = world::composeWorld(recipe, *library);
    REQUIRE(composed.has_value());

    std::map<std::string, float> emission;
    for (const auto& l : composed->layers) {
        emission[l.name] = l.emissiveIntensity;
    }
    REQUIRE(emission.count("tree_tall") == 1);

    // A canopy that glows is a canopy that stops being a silhouette. In the Glowmere profile the
    // whole rung is 0.035.
    INFO("tree_tall composes to " << emission.at("tree_tall"));
    CHECK(emission.at("tree_tall") < 0.1f);
    CHECK(emission.at("tree_tall") > 0.0f);

    // Several layers emit exactly nothing. That is what the bright things are bright against, and
    // it is a decision rather than an oversight.
    const auto dark = std::count_if(composed->layers.begin(), composed->layers.end(),
                                    [](const world::ScatterLayer& l) {
                                        return l.emissiveIntensity == 0.0f;
                                    });
    CHECK(dark >= 3);

    // And the ladder spans: the brightest species is far above the ordinary vegetation, with the
    // gap intact on real data rather than only on a fixture.
    float brightest = 0.0f;
    for (const auto& [name, value] : emission) {
        brightest = std::max(brightest, value);
    }
    CHECK(brightest > 3.0f);
    CHECK(brightest > emission.at("tree_tall") * 50.0f);
#endif
}
