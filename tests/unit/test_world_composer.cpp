// The world composer (ADR-061). These tests are about hierarchy, not about placement: the ecology
// already has tests for where instances land. What is new and worth guarding is that a fern does
// not end up on the ridge line, that a recipe's weights actually reach the layers, that the same
// recipe composes the same world twice, and that the composer's output is the type the existing
// ecology consumes rather than a parallel one.

#include "assets/asset_library.hpp"
#include "world/world_composer.hpp"
#include "world/world_recipe.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>

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
        if (v.clearsAbove > 0.0f) {
            // A canopy clearing is *supposed* to sit on a hero -- that is what gives a hero room to
            // read when the camera arrives, and heroes did not exist when this test was written.
            // Only regions that empty the ground as well are holes.
            continue;
        }
        // No void that clears everything may sit on top of the one thing worth looking at.
        CHECK(glm::length(v.center - plan.focal[0].center) >= (v.radius + plan.focal[0].radius) * 0.9f);
    }

    SECTION("negative space of zero asks for no discretionary voids") {
        auto recipe = testRecipe();
        recipe.composition.negativeSpace = 0.0f;
        auto w = world::composeWorld(recipe, lib);
        REQUIRE(w.has_value());
        // The corridor is not discretionary -- the brief makes it mandatory and it is what keeps a
        // dense world from putting a trunk across the lens -- and nor are the clearings around the
        // heroes. So `negativeSpace` sets how generous the discretionary emptiness is, not whether
        // any emptiness exists, and what must be gone is every region the weight actually governs.
        CHECK(!w->plan.corridor.empty());
        const auto isCorridor = [&](const world::VoidRegion& v) {
            return std::any_of(w->plan.corridor.begin(), w->plan.corridor.end(),
                               [&](const world::VoidRegion& c) {
                                   return glm::length(c.center - v.center) < 1e-3f &&
                                          std::abs(c.radius - v.radius) < 1e-3f;
                               });
        };
        const auto isHeroClearing = [&](const world::VoidRegion& v) {
            return std::any_of(w->plan.heroes.begin(), w->plan.heroes.end(),
                               [&](const world::HeroPoint& h) {
                                   return glm::length(glm::vec2(h.position.x, h.position.z) - v.center) < 1e-3f;
                               });
        };
        for (const auto& v : w->plan.voids) {
            INFO("void at " << v.center.x << ", " << v.center.y << " r=" << v.radius);
            CHECK((isCorridor(v) || isHeroClearing(v)));
        }
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

TEST_CASE("The composition clears a corridor from its viewpoint to its subject",
          "[world][composer]") {
    // Section 14 of the brief calls the negative-space corridor mandatory. It is also the thing
    // that keeps a dense world from putting a tree trunk across the lens: the composer picks the
    // viewpoint, so it is the only thing that can promise a clear line from it.
    auto library = testLibrary();
    world::WorldRecipe recipe;
    recipe.world = "corridor";
    recipe.seed = 7u;
    recipe.extent = 400.0f;
    recipe.composition.negativeSpace = 0.0f;   // asks for none; still gets a corridor
    auto composed = world::composeWorld(recipe, library);
    REQUIRE(composed.has_value());
    REQUIRE(!composed->plan.focal.empty());
    CHECK(!composed->plan.corridor.empty());
    CHECK(composed->plan.viewpointClearance > 0.0f);

    const glm::vec2 eye = composed->plan.viewpoint;
    const glm::vec2 subject = composed->plan.focal.front().center;
    CHECK(glm::length(eye - subject) > 1.0f);

    // Every void region either leaves the viewpoint alone or is part of the corridor. A viewpoint
    // standing inside an ordinary void renders as a bald hillside: everywhere is dense except the
    // one place the world is seen from, which is precisely what it did.
    for (const auto& v : composed->plan.voids) {
        const bool isCorridor =
            std::any_of(composed->plan.corridor.begin(), composed->plan.corridor.end(),
                        [&](const world::VoidRegion& c) {
                            return glm::length(c.center - v.center) < 1e-3f &&
                                   std::abs(c.radius - v.radius) < 1e-3f;
                        });
        if (!isCorridor) {
            INFO("void at " << v.center.x << "," << v.center.y << " r=" << v.radius);
            CHECK(glm::length(v.center - eye) >= v.radius + v.softness);
        }
    }

    // The lane spans the distance. Sampled along the line: nothing tall may grow anywhere on it.
    const auto& clearances = composed->clearances;
    REQUIRE(!clearances.empty());
    // Sampled from the viewpoint to where the corridor actually ends, which is short of the
    // subject on purpose: the lane leaves the ground the landmark stands on, so the last stretch is
    // allowed to be planted and asserting over the whole distance would be asserting the wrong
    // contract.
    const glm::vec2 laneEnd = composed->plan.corridor.back().center;
    CHECK(glm::length(laneEnd - eye) > glm::length(subject - eye) * 0.5f);
    for (int i = 0; i <= 10; ++i) {
        const float t = static_cast<float>(i) / 10.0f;
        const glm::vec2 p = eye + (laneEnd - eye) * t;
        INFO("t=" << t);
        CHECK(world::clearanceWeight(clearances, p, 20.0f) < 0.5f);
    }
    // ...while the ground cover in the lane survives, because a lane through a forest has a floor.
    CHECK(world::clearanceWeight(clearances, eye, 0.4f) > 0.5f);
}

TEST_CASE("A composed world's landmark is an asset, a size and a place", "[world][composer]") {
    auto library = testLibrary();
    world::WorldRecipe recipe;
    recipe.world = "landmark";
    recipe.seed = 3u;
    auto composed = world::composeWorld(recipe, library);
    REQUIRE(composed.has_value());
    REQUIRE(!composed->plan.focal.empty());
    const auto& focal = composed->plan.focal.front();
    // A focal region that names a spot and nothing else is a note about a composition rather than
    // a composition. The first generated valley had exactly that: one marked region, empty.
    CHECK(!focal.landmarkPath.empty());
    CHECK(focal.landmarkHeight > 20.0f);
    CHECK(focal.landmarkScale > 1.0f);
    // Sized from the mesh's own bounds, so the same intent survives a differently-authored pack.
    const auto* hero = library.find(focal.assetId);
    REQUIRE(hero != nullptr);
    CHECK_THAT(focal.landmarkScale * hero->naturalSize.y,
               Catch::Matchers::WithinRel(focal.landmarkHeight, 1e-4f));
}

TEST_CASE("A composed world plans its atmosphere from the recipe", "[world][composer]") {
    auto library = testLibrary();
    world::WorldRecipe recipe;
    recipe.world = "air";
    recipe.atmosphere.fog = 0.1f;
    recipe.lighting.key = 0.2f;
    auto thin = world::composeWorld(recipe, library);
    REQUIRE(thin.has_value());
    recipe.atmosphere.fog = 0.9f;
    recipe.lighting.key = 0.9f;
    auto thick = world::composeWorld(recipe, library);
    REQUIRE(thick.has_value());

    CHECK(thick->environment.fogDensity > thin->environment.fogDensity);
    CHECK(thick->environment.sunIntensity > thin->environment.sunIntensity);
    // Fog densities are per metre over a world hundreds of metres across. The first pass reached
    // 0.055, which is total fog by fifty metres -- the whole valley in a glass of milk.
    CHECK(thick->environment.fogDensity < 0.02f);
    CHECK(thin->environment.fogDensity > 0.0f);
}

TEST_CASE("Ecological zones make different places, not just denser patches", "[world][composer][zone]") {
    // A world the camera travels through should pass through recognisably different chapters. The
    // failure this guards is a set of zones that all say "more of everything", which reads as noise
    // in the scatter rather than as arriving somewhere.
    auto library = testLibrary();
    world::WorldRecipe recipe;
    recipe.world = "chapters";
    recipe.seed = 21u;
    recipe.extent = 400.0f;
    recipe.composition.focalStrength = 0.9f;
    recipe.ecology.fungi = 0.6f;
    recipe.ecology.rock = 0.5f;
    auto composed = world::composeWorld(recipe, library);
    REQUIRE(composed.has_value());
    const auto& zones = composed->plan.zones;
    REQUIRE(zones.size() >= 2);

    // Every zone is anchored on a hero: a zone the camera has no reason to enter is a zone that
    // does not exist.
    for (const auto& z : zones) {
        const bool anchored =
            std::any_of(composed->plan.heroes.begin(), composed->plan.heroes.end(),
                        [&](const world::HeroPoint& h) {
                            return glm::length(glm::vec2(h.position.x, h.position.z) - z.center) < 1e-3f;
                        });
        INFO("zone '" << z.name << "'");
        CHECK(anchored);
        CHECK(z.radius > 20.0f);   // a place, not a patch
        CHECK(!z.emphasis.empty());
    }

    // The zones disagree with each other. If they did not, one zone would do.
    const auto emphasisFor = [](const world::EcologicalZone& z, const std::string& category) {
        for (const auto& [name, scale] : z.emphasis) {
            if (name == category) {
                return scale;
            }
        }
        return 1.0f;
    };
    bool anyDisagreement = false;
    for (std::size_t i = 1; i < zones.size(); ++i) {
        for (const char* category : {"flora", "fungi", "rock"}) {
            if (std::abs(emphasisFor(zones[i], category) - emphasisFor(zones[0], category)) > 0.2f) {
                anyDisagreement = true;
            }
        }
    }
    CHECK(anyDisagreement);
    // And at least one zone subtracts rather than adds: a world where every chapter is denser than
    // the baseline has no baseline.
    const bool anySubtracts = std::any_of(zones.begin(), zones.end(), [&](const auto& z) {
        return std::any_of(z.emphasis.begin(), z.emphasis.end(),
                           [](const auto& e) { return e.second < 0.8f; });
    });
    CHECK(anySubtracts);
}

TEST_CASE("A zone's emphasis reaches the placer, by category", "[world][composer][zone]") {
    auto library = testLibrary();
    world::WorldRecipe recipe;
    recipe.world = "reaches";
    recipe.seed = 21u;
    recipe.extent = 400.0f;
    recipe.composition.focalStrength = 0.9f;
    recipe.ecology.fungi = 0.6f;
    recipe.ecology.rock = 0.5f;
    auto composed = world::composeWorld(recipe, library);
    REQUIRE(composed.has_value());
    REQUIRE(composed->plan.zones.size() >= 2);
    REQUIRE(!composed->clearances.empty());

    // Find the zone that most emphasises fungi, and check the placer's own weighting function
    // agrees that fungi are commoner at its centre than far away -- and that flora are not equally
    // affected, which is the whole point of the category filter.
    const world::EcologicalZone* hollow = nullptr;
    float best = 0.0f;
    for (const auto& z : composed->plan.zones) {
        for (const auto& [name, scale] : z.emphasis) {
            if (name == "fungi" && scale > best) {
                best = scale;
                hollow = &z;
            }
        }
    }
    REQUIRE(hollow != nullptr);
    REQUIRE(best > 1.2f);

    const auto& regions = composed->clearances;
    const glm::vec2 far = hollow->center + glm::vec2(hollow->radius * 6.0f, 0.0f);
    // Sampled at a height below every clearance's minHeight, so canopy clearings do not confound it.
    const float fungiHere = world::clearanceWeight(regions, hollow->center, 0.3f, "fungi");
    const float fungiFar = world::clearanceWeight(regions, far, 0.3f, "fungi");
    const float floraHere = world::clearanceWeight(regions, hollow->center, 0.3f, "flora");
    INFO("fungi here " << fungiHere << ", far " << fungiFar << "; flora here " << floraHere);
    CHECK(fungiHere > fungiFar);
    // The category filter does its job: the hollow does not simply thicken everything.
    CHECK(fungiHere > floraHere);
}

TEST_CASE("A region with no density scale is still a clearing", "[world][composer][zone]") {
    // Backwards compatibility, asserted rather than assumed: densityScale defaults to 0, so every
    // clearance authored before zones existed still empties its region completely.
    world::ScatterClearance c;
    c.center = glm::vec2(0.0f);
    c.radius = 10.0f;
    const std::array<world::ScatterClearance, 1> one{c};
    CHECK(world::clearanceWeight(one, glm::vec2(0.0f)) == 0.0f);
    CHECK(world::clearanceWeight(one, glm::vec2(40.0f, 0.0f)) == 1.0f);

    // And a region that thickens does the opposite.
    world::ScatterClearance dense = c;
    dense.densityScale = 2.5f;
    const std::array<world::ScatterClearance, 1> thick{dense};
    CHECK(world::clearanceWeight(thick, glm::vec2(0.0f)) > 2.0f);
}

TEST_CASE("A zone is a share of the world, so extent scales density and nothing else",
          "[world][composer]") {
    auto lib = testLibrary();

    const auto composeAt = [&](float extent) {
        world::WorldRecipe r = testRecipe();
        r.extent = extent;
        auto w = world::composeWorld(r, lib);
        REQUIRE(w.has_value());
        return std::move(*w);
    };

    const world::ComposedWorld small = composeAt(400.0f);
    const world::ComposedWorld large = composeAt(1200.0f);

    // Zones are anchored on heroes and sized off the world, so tripling the extent triples them.
    // They used to be sized off `activationRadius`, which descends from the hero's own height and
    // is the same handful of metres at any extent -- so four zones blanketed a small world and
    // dappled a large one. Because overlapping zones multiply their emphasis, that made the *same
    // recipe* mean a different density at every extent: measured on the shipped Glowmere presets,
    // four times the ground produced ten to twelve times the flora instead of four.
    REQUIRE(!small.plan.zones.empty());
    REQUIRE(small.plan.zones.size() == large.plan.zones.size());
    for (std::size_t i = 0; i < small.plan.zones.size(); ++i) {
        const float ratio = large.plan.zones[i].radius / small.plan.zones[i].radius;
        CHECK(ratio == Catch::Approx(3.0f).epsilon(0.35));
    }

    // The consequence that actually matters: whatever the extent, a zone covers a bounded share of
    // the world. The share is not identical for every zone -- the hero's own scale still leans on
    // the size, which is deliberate -- but it can no longer run away, and that is what stops four
    // zones blanketing one world and dappling another.
    //
    // Bounds are the clamp in the composer: 0.7 to 1.3 of a 0.13 share, with a little slack.
    for (const world::ComposedWorld* w : {&small, &large}) {
        const float extent = w == &small ? 400.0f : 1200.0f;
        for (const world::EcologicalZone& z : w->plan.zones) {
            const float share = z.radius / extent;
            CHECK(share >= 0.085f);
            CHECK(share <= 0.175f);
        }
    }
}

TEST_CASE("focalStrength zero means no emphasis, not no subject", "[world][composer]") {
    auto lib = testLibrary();
    world::WorldRecipe r = testRecipe();
    r.composition.focalStrength = 0.0f;
    auto composed = world::composeWorld(r, lib);
    REQUIRE(composed.has_value());

    // The focal region is what the viewpoint is framed on, so gating its existence on the strength
    // meant `focalStrength: 0.0` composed a world that nothing ever looked at.
    REQUIRE(!composed->plan.focal.empty());
    CHECK(composed->plan.focal.front().strength == 0.0f);
    CHECK(composed->plan.focal.front().radius > 0.0f);
}
