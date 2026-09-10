// Heroes (ADR-072). What is worth pinning here is not that a struct holds numbers but that hero
// placement produces a set a camera director can actually *choose* from, and that the two mistakes
// the renders caught stay fixed: clearances sized off camera distance emptied the world, and
// placement blind to the terrain put the subject and the camera on bare scree.

#include "world/hero.hpp"
#include "world/world_composer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>

using namespace avgen;

namespace {
assets::AssetLibrary heroLibrary() {
    const auto doc = nlohmann::json::parse(R"({
      "source": "test", "license": "CC0",
      "assets": [
        {"name": "elder", "category": "flora", "file": "a.glb", "visualImportance": 0.95,
         "preferredScale": 9.0, "naturalSize": [3.0, 2.0, 3.0], "preferredDensity": 0.004,
         "material": {"emissive": 0.3, "tint": [0.2, 0.6, 0.4]}},
        {"name": "spire", "category": "flora", "file": "b.glb", "visualImportance": 0.8,
         "preferredScale": 7.0, "naturalSize": [1.0, 2.0, 1.0], "preferredDensity": 0.006},
        {"name": "bloom", "category": "flora", "file": "c.glb", "visualImportance": 0.6,
         "preferredScale": 2.0, "naturalSize": [1.0, 1.0, 1.0], "preferredDensity": 0.02},
        {"name": "cap", "category": "fungi", "file": "d.glb", "visualImportance": 0.4,
         "preferredScale": 0.6, "naturalSize": [0.3, 0.3, 0.3], "preferredDensity": 0.05},
        {"name": "moss", "category": "flora", "file": "e.glb", "visualImportance": 0.2,
         "preferredScale": 0.4, "naturalSize": [0.4, 0.2, 0.4], "preferredDensity": 0.4}
      ]})");
    auto lib = assets::AssetLibrary::fromJson(doc, "/tmp/lib");
    REQUIRE(lib.has_value());
    return std::move(*lib);
}

world::WorldRecipe heroRecipe() {
    world::WorldRecipe r;
    r.world = "heroes";
    r.seed = 4242u;
    r.extent = 400.0f;
    r.composition.focalStrength = 0.85f;
    r.art.profile = "glowmere";
    return r;
}
} // namespace

TEST_CASE("A hero validates the things that would make it undiscoverable", "[world][hero]") {
    world::HeroPoint h;
    h.name = "elder";
    h.assetId = "elder";
    REQUIRE(h.validate().has_value());

    // Something has to stand here.
    world::HeroPoint empty = h;
    empty.assetId.clear();
    CHECK(!empty.validate().has_value());

    // A hero that activates closer than the camera is meant to stand never activates on the shot
    // designed for it, which is a silent failure: the hero is there and simply never does anything.
    world::HeroPoint inside = h;
    inside.preferredCameraDistance = 60.0f;
    inside.activationRadius = 20.0f;
    auto bad = inside.validate();
    REQUIRE(!bad.has_value());
    CHECK(bad.error().message.find("activationRadius") != std::string::npos);

    // A reaction profile nobody has heard of would silently do nothing.
    world::HeroPoint mystery = h;
    mystery.reactionProfile = "wobble";
    CHECK(!mystery.validate().has_value());
}

TEST_CASE("A hero reaction must name a signal that exists", "[world][hero]") {
    world::HeroReaction r;
    r.source = "audio.bass";
    CHECK(r.validate().has_value());

    // The failure this prevents: a reaction that never fires looks exactly like a hero that does
    // not react, so it is caught here rather than by staring at a scene wondering why nothing moves.
    r.source = "audio.wobble";
    auto bad = r.validate();
    REQUIRE(!bad.has_value());
    CHECK(bad.error().message.find("never fire") != std::string::npos);
}

TEST_CASE("A reaction profile refuses to do everything at once", "[world][hero]") {
    world::HeroReactionProfile p;
    p.name = "busy";
    for (int i = 0; i < 5; ++i) {
        p.reactions.push_back(world::HeroReaction{});
    }
    // Not a technical limit: a hero doing five things at once is doing none of them legibly.
    CHECK(!p.validate().has_value());

    // Every built-in profile is legal, and most of them do very little.
    const auto& profiles = world::heroReactionProfiles();
    REQUIRE(profiles.size() >= 3);
    for (const auto& profile : profiles) {
        INFO("profile '" << profile.name << "'");
        CHECK(profile.validate().has_value());
    }
    const auto* still = world::findHeroReactionProfile("still");
    REQUIRE(still != nullptr);
    CHECK(still->reactions.size() == 1);
}

TEST_CASE("Clearance is sized from the hero, not from how far the camera stands off",
          "[world][hero]") {
    // The bug this pins: clearance scaled off preferredCameraDistance, which is about three times a
    // hero's height, so a tall hero cleared a radius comparable to its own stand-off. Five heroes
    // emptied a four-hundred-metre world and the render was a bare hillside with objects on it.
    world::HeroPoint h;
    h.name = "elder";
    h.assetId = "elder";
    h.radius = 3.0f;
    h.height = 20.0f;
    h.focalWeight = 0.85f;
    h.preferredCameraDistance = 60.0f;
    const float near = world::heroClearanceRadius(h);

    world::HeroPoint distant = h;
    distant.preferredCameraDistance = 400.0f;
    distant.activationRadius = 900.0f;
    CHECK_THAT(world::heroClearanceRadius(distant), Catch::Matchers::WithinRel(near, 1e-5f));

    // It is a clearing around the hero, not a hole in the world.
    CHECK(near < h.height);
    CHECK(near > h.radius);

    // A hero the shot is about gets more room than one that merely exists.
    world::HeroPoint incidental = h;
    incidental.focalWeight = 0.0f;
    CHECK(world::heroClearanceRadius(incidental) < near);
}

TEST_CASE("A hero round-trips through JSON", "[world][hero]") {
    world::HeroPoint h;
    h.name = "elder";
    h.assetId = "elder";
    h.position = glm::vec3(12.5f, 1.0f, -30.25f);
    h.yaw = 1.25f;
    h.scale = 3.5f;
    h.radius = 4.0f;
    h.height = 22.0f;
    h.importance = 0.9f;
    h.focalWeight = 0.8f;
    h.preferredCameraDistance = 66.0f;
    h.activationRadius = 200.0f;
    h.colorAccent = glm::vec3(1.0f, 0.47f, 0.15f);
    h.reactionProfile = "organism";
    REQUIRE(h.validate().has_value());

    auto back = world::HeroPoint::fromJson(h.toJson());
    REQUIRE(back.has_value());
    CHECK(back->name == h.name);
    CHECK(back->reactionProfile == h.reactionProfile);
    CHECK_THAT(back->height, Catch::Matchers::WithinRel(h.height, 1e-5f));
    CHECK_THAT(back->radius, Catch::Matchers::WithinRel(h.radius, 1e-5f));
    CHECK_THAT(back->importance, Catch::Matchers::WithinRel(h.importance, 1e-5f));
    CHECK_THAT(glm::length(back->position - h.position), Catch::Matchers::WithinAbs(0.0, 1e-5));
}

TEST_CASE("A composed world's heroes are a set a director can choose between",
          "[world][hero][composer]") {
    const auto library = heroLibrary();
    auto composed = world::composeWorld(heroRecipe(), library);
    REQUIRE(composed.has_value());
    const auto& heroes = composed->plan.heroes;
    REQUIRE(heroes.size() >= 3);

    // Strictly descending importance. Heroes that all claim the same importance are heroes among
    // which nothing can be chosen, which is the same as having none.
    for (std::size_t i = 1; i < heroes.size(); ++i) {
        INFO("hero " << i << " '" << heroes[i].name << "'");
        CHECK(heroes[i].importance < heroes[i - 1].importance);
    }
    // The most important hero is the library's most important asset.
    CHECK(heroes.front().assetId == "elder");

    // They differ in the ways a shot depends on: size, stand-off and where they are.
    const float tallest = std::max_element(heroes.begin(), heroes.end(),
                                           [](const auto& a, const auto& b) {
                                               return a.height < b.height;
                                           })->height;
    const float shortest = std::min_element(heroes.begin(), heroes.end(),
                                            [](const auto& a, const auto& b) {
                                                return a.height < b.height;
                                            })->height;
    CHECK(tallest > shortest * 2.0f);
    for (std::size_t i = 0; i < heroes.size(); ++i) {
        for (std::size_t j = i + 1; j < heroes.size(); ++j) {
            CHECK(glm::length(heroes[i].position - heroes[j].position) > 1.0f);
        }
        CHECK(heroes[i].validate().has_value());
        // Sized so its own stand-off keeps the world in frame: foreground vegetation is culled at
        // ninety metres, and a hero big enough to push the camera past that renders as an object on
        // an empty hillside.
        INFO("hero '" << heroes[i].name << "' stands off " << heroes[i].preferredCameraDistance);
        CHECK(heroes[i].preferredCameraDistance < 90.0f);
    }
    // Most heroes do almost nothing; the ones that react are noticeable only because the rest do not.
    const auto lively = std::count_if(heroes.begin(), heroes.end(), [](const world::HeroPoint& h) {
        return h.reactionProfile != "still" && !h.reactionProfile.empty();
    });
    CHECK(lively <= 2);
}

TEST_CASE("Heroes and the viewpoint stand where something grows", "[world][hero][composer]") {
    // The bug this pins: placement was a hash with no knowledge of the ground, so the subject landed
    // on scree about as often as the map is scree -- and the viewpoint went with it, because the
    // viewpoint is chosen to look at the subject. The frame was then a bare slope with objects on
    // it while the rest of the world was dense.
    const auto library = heroLibrary();
    const world::WorldRecipe recipe = heroRecipe();
    auto composed = world::composeWorld(recipe, library);
    REQUIRE(composed.has_value());
    REQUIRE(!composed->plan.heroes.empty());

    const world::WorldMap ground = world::terrainFor(recipe);
    const auto fertility = [&](glm::vec2 p) {
        const world::Sample s = ground.sample(p, 0.5f);
        const world::BiomeWeights w = ground.biomes.at(s.altitude, s.slope, s.moisture, p);
        float planted = 0.0f;
        for (std::size_t b = 0; b < ground.biomes.biomes.size() && static_cast<int>(b) < w.count; ++b) {
            const std::string& name = ground.biomes.biomes[b].name;
            if (name == "forest" || name == "meadow" || name == "marsh") {
                planted += w.weights[b];
            }
        }
        return planted;
    };

    const glm::vec3 subject = composed->plan.heroes.front().position;
    INFO("subject at " << subject.x << ", " << subject.z);
    CHECK(fertility(glm::vec2(subject.x, subject.z)) > 0.25f);
    const glm::vec2 eye = composed->plan.viewpoint;
    INFO("viewpoint at " << eye.x << ", " << eye.y);
    CHECK(fertility(eye) > 0.25f);

    // And inside the map: a viewpoint outside it puts the heightfield's own edge across the frame,
    // which reads as a rendering bug because it is the boundary of the data rather than of a world.
    CHECK(std::abs(eye.x) < recipe.extent * 0.5f);
    CHECK(std::abs(eye.y) < recipe.extent * 0.5f);
}

TEST_CASE("Hero placement is a pure function of the seed", "[world][hero][composer]") {
    const auto library = heroLibrary();
    auto a = world::composeWorld(heroRecipe(), library);
    auto b = world::composeWorld(heroRecipe(), library);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->plan.heroes.size() == b->plan.heroes.size());
    for (std::size_t i = 0; i < a->plan.heroes.size(); ++i) {
        CHECK(a->plan.heroes[i].name == b->plan.heroes[i].name);
        CHECK_THAT(glm::length(a->plan.heroes[i].position - b->plan.heroes[i].position),
                   Catch::Matchers::WithinAbs(0.0, 1e-9));
    }

    world::WorldRecipe other = heroRecipe();
    other.seed = 99u;
    auto c = world::composeWorld(other, library);
    REQUIRE(c.has_value());
    REQUIRE(!c->plan.heroes.empty());
    // A different seed is a different world, or the seed is decoration.
    CHECK(glm::length(c->plan.heroes.front().position - a->plan.heroes.front().position) > 1.0f);
}

TEST_CASE("A behaviour is only wired where it has somewhere real to go", "[world][hero]") {
    using B = world::HeroBehaviour;
    // The four that land on a placed node's own parameters.
    struct Case {
        B behaviour;
        const char* suffix;
        int component;
    };
    for (const Case c : {Case{B::EmissionPulse, "emissiveBoost", -1}, Case{B::Hover, "position", 1},
                         Case{B::ScalePulse, "scale", -1}, Case{B::Rotation, "rotation", 1}}) {
        const auto t = world::heroBehaviourTarget(c.behaviour);
        INFO(world::heroBehaviourName(c.behaviour));
        CHECK(t.supported);
        CHECK(std::string(t.suffix) == c.suffix);
        CHECK(t.component == c.component);
    }
    // A hover is vertical and a rotation is yaw. Routing either to every component would make a
    // hero slide sideways or tumble, which looks like a bug in the modulation rather than a
    // misrouted axis.
    CHECK(world::heroBehaviourTarget(B::Hover).component == 1);
    CHECK(world::heroBehaviourTarget(B::Rotation).component == 1);

    // The four that need machinery a placed glTF node does not have. Each must say what it needs,
    // because "not wired" with no reason is indistinguishable from an oversight -- and routing them
    // at whatever is nearby would give a hero that appears to react while doing something else,
    // which is worse than one that visibly does nothing.
    for (const B behaviour : {B::ColorShift, B::LightBurst, B::ParticleEmission, B::Reveal}) {
        const auto t = world::heroBehaviourTarget(behaviour);
        INFO(world::heroBehaviourName(behaviour));
        CHECK(!t.supported);
        CHECK(std::string(t.suffix).empty());
        CHECK(std::string(t.missing).length() > 8);
    }
}

TEST_CASE("Every built-in reaction profile can actually do something", "[world][hero]") {
    // A profile whose every behaviour is unwired is a profile that silently does nothing, which is
    // exactly the failure the source-name validation exists to prevent one level down.
    for (const auto& profile : world::heroReactionProfiles()) {
        const auto wired = std::count_if(
            profile.reactions.begin(), profile.reactions.end(), [](const world::HeroReaction& r) {
                return world::heroBehaviourTarget(r.behaviour).supported;
            });
        INFO("profile '" << profile.name << "'");
        CHECK(wired > 0);
    }
}
