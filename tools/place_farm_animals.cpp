// Scatter the farm animals through a world, as nodes a director can move (ADR-209, Part 1).
//
// A build target rather than a Python script, for the reason `make_tree_of_life.cpp` gives: this
// asks the *engine's* questions -- is this walkable, how deep is the water, how tall is the canopy
// here, is there a trunk inside this disc -- against the real `WorldMap`, the real ecology scatter
// and the real per-instance obstacle field the scene builds at load. A script would hold a second
// copy of the terrain noise and the two would drift apart silently, and an animal would end up
// inside a rock in a way nobody could reproduce.
//
// Why nodes rather than a scatter layer. `world::scatter` merges an asset's primitives into one
// mesh per material and the merge drops skin influences (ADR-205), so a farm animal in a scatter
// layer draws in its bind pose, identically, for ever. Anything that has to move -- to wander, or
// to be abducted -- has to arrive as a `gltf` node with an `animation` state. That is what this
// writes, one node and one entity each.
//
// Placement is two populations, not one scatter:
//
//   * **visible** -- low canopy, gentle ground, near the water or in a clearing. These read as
//     animals in a meadow, which is the half of the brief that says "reasonably visible".
//   * **hidden** -- under the canopy, where `canopyHeightAt` says something tall grows. These are
//     the half that says "the player should occasionally discover an animal that was not
//     immediately obvious".
//
// Both populations are rejected against the same hard constraints -- walkable, dry, not too steep,
// not inside a hero, not inside a recorded solid, inside the world's edge, and no closer to another
// animal than a spacing -- so "hidden" never means "inside a tree".
//
// Deterministic: a seeded PCG32 over a fixed candidate sequence. The same seed writes the same
// scene file, which is what makes the fingerprint test's job possible at all.
//
//   avgen_place_farm_animals examples/world/glowmere-valley-2.scene.json \
//       --emit /tmp/farm.json && tools/splice_farm_animals.py /tmp/farm.json

#include "assets/asset_registry.hpp"
#include "core/log.hpp"
#include "core/rng.hpp"
#include "entity/action.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"
#include "world/terrain_query.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

// One species, and how much room it needs. `height` is the metres the GLB occupies at rest, which
// `assets/farm.manifest.json` measured; the scene places at scale 1, so it is also the scale.
struct Species {
    const char* name;
    const char* file;
    float height;  // metres, from the manifest's naturalSize.y
    float radius;  // the disc it needs to stand in, generously
    float speed;   // walking m/s -- a chick does not cross a meadow at a horse's pace
    float territory; // metres it will wander from where it was put
    // Four feet or two, and it decides `slopeAlign` -- the one number the ground follower has for
    // "how far does this body lie along the surface it stands on".
    //
    // `GroundSettings::slopeAlign` defaults to 0.55 under a comment that reads "a walker leans into
    // a slope, it does not become part of it". That is right for a BIPED: two feet, one roughly
    // under the body, and the torso stays near upright while the legs take the grade. It is wrong
    // for a quadruped, whose four feet are planted along a whole body length, so its spine sits
    // roughly parallel to the ground or its front legs are buried in it.
    //
    // Measured, because the difference is not small. A body of length L on a slope t, pitched by p,
    // puts its nose (L/2)(tan t - tan p) below the surface, and p is exactly `slopeAlign` x t. At
    // 0.55 on a 22 degree slope a bull -- 2.65 m long, scaled 3.6 by the scene, so 9.54 m -- buries
    // its nose 0.94 m and floats its tail by the same; at 34 degrees it is 1.67 m. At 1.0 the
    // residual is 0.145 m and is terrain curvature rather than alignment, which no single pitch can
    // fix. Measured across footprint 0.55 m to 4.77 m the change is 0.0004 m, so the footprint --
    // the other suspect -- is not what this is.
    bool quadruped;
};

// Ordered biggest to smallest so the hard-to-place animals get first refusal on the good ground.
constexpr Species kSpecies[] = {
    {"bull", "bull", 1.7710f, 1.6f, 0.75f, 11.0f, true},
    {"horse", "horse", 1.7523f, 1.6f, 1.35f, 15.0f, true},
    {"cow", "cow", 1.5739f, 1.5f, 0.70f, 11.0f, true},
    {"sheep", "sheep", 0.9412f, 0.9f, 0.85f, 9.0f, true},
    {"goat", "goat", 0.7441f, 0.8f, 1.00f, 10.0f, true},
    {"pig", "pig", 0.6900f, 0.9f, 0.65f, 7.0f, true},
    {"rooster", "rooster", 0.4342f, 0.5f, 0.70f, 6.0f, false},
    {"chicken", "chicken", 0.3368f, 0.5f, 0.65f, 5.5f, false},
    {"chick", "chick", 0.1100f, 0.35f, 0.55f, 4.0f, false},
};
constexpr int kSpeciesCount = static_cast<int>(std::size(kSpecies));

struct Placed {
    std::string name;
    const Species* species = nullptr;
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    bool hidden = false;
    bool glade = false;
    float canopy = 0.0f;
};

[[nodiscard]] json vec3(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }

// Rounded, because a scene file that carries fourteen significant figures of a noise sample is a
// diff nobody can read and a number nobody meant.
[[nodiscard]] float round2(float v) { return std::round(v * 100.0f) / 100.0f; }

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: avgen_place_farm_animals <scene.json> --emit <fragment.json> [--seed N] [--dry-run]\n");
        return 2;
    }
    const fs::path scenePath = argv[1];
    std::uint32_t seed = 20260915u;
    bool dryRun = false;
    fs::path emit;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--emit" && i + 1 < argc) {
            emit = argv[++i];
        } else if (arg == "--dry-run") {
            dryRun = true;
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        }
    }

    log::setLevel(log::Level::Warn);
    assets::AssetRegistry registry{scenePath.parent_path()};
    auto loaded = scene::Composition::loadFile(scenePath, registry);
    if (!loaded) {
        std::fprintf(stderr, "could not load '%s': %s\n", scenePath.string().c_str(),
                     loaded.error().message.c_str());
        return 1;
    }
    scene::Composition& comp = **loaded;
    params::ParameterSet params;
    params::Modulator modulator;
    comp.attach(params, modulator);

    const world::TerrainQuery terrain = comp.terrainQuery();
    if (!terrain.valid()) {
        std::fprintf(stderr, "'%s' has no terrain to place on\n", scenePath.string().c_str());
        return 1;
    }
    std::printf("obstacles: %s\n", terrain.hasObstacles() ? "yes (per-instance)" : "NO -- heroes only");

    // The glades the world declares (`clearings` on the terrain node). They are the most literal
    // reading of the brief's "clearings, meadow areas": the author already said where the world
    // opens out, and an animal standing in one is seen without anything having to guess.
    std::vector<world::ScatterClearance> clearings;
    for (const auto& nodePtr : comp.nodes()) {
        if (nodePtr->kind == scene::NodeKind::Terrain) {
            clearings = nodePtr->ecology.clearances;
            break;
        }
    }
    std::printf("clearings: %zu\n", clearings.size());

    // ---- the candidate sweep --------------------------------------------------------------------
    //
    // Sampled over the map rather than over a disc, because the brief asks for animals "throughout
    // the world" and a brush stroke is the wrong shape for that. The sequence is a seeded draw, so
    // it is reproducible; the *filters* are what make the result look placed rather than sprinkled.
    const glm::vec2 lo = terrain.map->size * -0.5f + glm::vec2(30.0f);
    const glm::vec2 hi = terrain.map->size * 0.5f - glm::vec2(30.0f);

    Rng rng(seed);
    std::vector<Placed> placed;
    // How many of each, and how far apart. Punctuation in a landscape, not groundcover.
    constexpr int kGladeWanted = 5;
    constexpr int kVisibleWanted = 6;
    constexpr int kHiddenWanted = 7;
    constexpr float kSpacing = 26.0f;   // metres between two animals
    constexpr float kHeroMargin = 18.0f; // metres to stay off a hero
    // The canopy bands. Below `kOpen` is meadow and grass -- a thing standing there is seen. Above
    // `kUnder` something tall grows, and a thing standing there is found rather than seen.
    constexpr float kOpen = 3.2f;
    constexpr float kUnder = 6.0f;

    int rejectedWalkable = 0, rejectedOccupied = 0, rejectedSpacing = 0, rejectedHero = 0,
        rejectedBand = 0;

    const auto tooClose = [&](glm::vec2 p, float spacing) {
        for (const Placed& other : placed) {
            if (glm::length(glm::vec2(other.position.x, other.position.z) - p) < spacing) {
                return true;
            }
        }
        return false;
    };
    const auto nearHero = [&](glm::vec2 p) {
        for (const world::HeroPoint& hero : comp.heroes()) {
            if (glm::length(glm::vec2(hero.position.x, hero.position.z) - p) <
                hero.radius + kHeroMargin) {
                return true;
            }
        }
        return false;
    };

    // Three passes over the same sampler, in order of how scarce the ground each wants is:
    //
    //   0  the glades -- candidates drawn *inside* a declared clearing, which is the scarcest
    //      ground and the most deliberate: these are the animals a camera finds in the open;
    //   1  under the canopy -- the concealed population;
    //   2  open ground anywhere -- the rest of the visible half.
    //
    // Scarcest first, because taking the abundant ground first would leave the scarce ground
    // already spoken for by the spacing rule.
    for (int pass = 0; pass < 3; ++pass) {
        const bool glade = pass == 0;
        const bool hidden = pass == 1;
        const int wanted = glade ? kGladeWanted : hidden ? kHiddenWanted : kVisibleWanted;
        if (glade && clearings.empty()) {
            continue;
        }
        int found = 0;
        for (int attempt = 0; attempt < 120000 && found < wanted; ++attempt) {
            glm::vec2 p(rng.range(lo.x, hi.x), rng.range(lo.y, hi.y));
            if (glade) {
                // Inside one of the declared glades, drawn over its disc rather than rejected into
                // it: a rejection sampler over a 640 m map looking for a 20 m glade spends all its
                // attempts on the forest.
                const world::ScatterClearance& c =
                    clearings[rng.nextU32() % static_cast<std::uint32_t>(clearings.size())];
                const float a = rng.range(0.0f, 6.2831853f);
                const float r = std::sqrt(rng.nextFloat()) * std::max(c.radius * 0.8f, 2.0f);
                p = c.center + glm::vec2(std::cos(a), std::sin(a)) * r;
                if (p.x < lo.x || p.x > hi.x || p.y < lo.y || p.y > hi.y) {
                    continue;
                }
            }
            // Round-robin across the nine, over the whole set rather than per pass, so the world
            // gets one of everything before it gets two of anything.
            const Species& species = kSpecies[static_cast<int>(placed.size()) % kSpeciesCount];

            const world::TerrainPoint sample = terrain.at(p);
            if (!sample.walkable || sample.water || sample.waterDepth > 0.0f) {
                ++rejectedWalkable;
                continue;
            }
            // Hard: nothing solid in the disc the animal stands in, plus a margin. With the real
            // obstacle field attached this is per-trunk and per-rock rather than statistical.
            if (terrain.isOccupied(p, species.radius + 1.2f)) {
                ++rejectedOccupied;
                continue;
            }
            if (nearHero(p)) {
                ++rejectedHero;
                continue;
            }
            if (tooClose(p, kSpacing)) {
                ++rejectedSpacing;
                continue;
            }
            // The band that decides which population this is. `canopyHeightAt` is statistical
            // (ADR-080) -- it says what grows around here, not that there is a trunk at this spot --
            // which is exactly the right question for "would a camera notice this animal", and
            // exactly the wrong one for "is it inside a tree". The obstacle test above is the second
            // one, asked separately.
            const float canopy = sample.canopy;
            if (!glade && (hidden ? canopy < kUnder : canopy > kOpen)) {
                ++rejectedBand;
                continue;
            }
            // And the territory has to be somewhere it can actually walk: an animal placed on the
            // one standable square metre in a boulder field would spend the film failing to pick a
            // destination. Four probes at its own wander radius; three have to pass.
            int walkableAround = 0;
            for (int k = 0; k < 4; ++k) {
                const float a = static_cast<float>(k) * 1.5707963f;
                const glm::vec2 probe = p + glm::vec2(std::cos(a), std::sin(a)) * species.territory;
                if (terrain.isWalkable(probe) && !terrain.isOccupied(probe, species.radius)) {
                    ++walkableAround;
                }
            }
            if (walkableAround < 3) {
                ++rejectedWalkable;
                continue;
            }

            Placed entry;
            entry.species = &species;
            entry.position = glm::vec3(p.x, sample.height, p.y);
            entry.yaw = rng.range(-180.0f, 180.0f);
            entry.hidden = hidden;
            entry.glade = glade;
            entry.canopy = canopy;
            entry.name = fmt::format("{}-{}", species.name, placed.size() + 1);
            placed.push_back(std::move(entry));
            ++found;
        }
        std::printf("%-8s %d of %d\n", glade ? "glade:" : hidden ? "hidden:" : "open:", found, wanted);
    }

    std::printf("rejected: %d unwalkable, %d occupied, %d too near a hero, %d too near each other, "
                "%d wrong canopy band\n",
                rejectedWalkable, rejectedOccupied, rejectedHero, rejectedSpacing, rejectedBand);

    if (placed.empty()) {
        std::fprintf(stderr, "nothing could be placed\n");
        return 1;
    }

    // ---- emit -----------------------------------------------------------------------------------
    //
    // A *fragment*, not the scene. Reading the scene into nlohmann and writing it back out again
    // reorders every key in the file (its object is a sorted map) and reformats every float, which
    // is a 9,000-line diff for an 18-node change and makes the result unreviewable. So this writes
    // what it decided and `tools/splice_farm_animals.py` puts it in, preserving the order and the
    // formatting of everything it did not touch -- the same reason `refresh_scene_fingerprint.py`
    // is a regex rather than a JSON round trip.
    json doc;
    doc["nodes"] = json::array();
    doc["entities"] = json::array();

    int visible = 0;
    int hiddenCount = 0;
    for (const Placed& p : placed) {
        (p.hidden ? hiddenCount : visible) += 1;
        json node;
        node["name"] = p.name;
        node["kind"] = "gltf";
        node["asset"] = fmt::format("../../assets/farm/{}.glb", p.species->file);
        node["position"] = vec3({round2(p.position.x), round2(p.position.y), round2(p.position.z)});
        node["rotation"] = json::array({0.0f, round2(p.yaw), 0.0f});
        node["scale"] = json::array({1.0f, 1.0f, 1.0f});
        // `Walk` is the one clip every one of the nine shipped with (ADR-205). Naming it here is
        // what puts the rig on the animated path at all; the *entity* names activities, never clips.
        // The rig's cull distance is past the entity's, deliberately. The engine warns when it is
        // the other way round -- "the rig stops being posed at 220 m but the entity keeps
        // simulating to 260 m; between them the character travels in a frozen pose" -- and it is
        // right to: a walking animal with its legs stopped is the exact defect ADR-205 warns about
        // for a scattered one.
        node["animation"] = json{{"state", "Walk"},      {"blend", 0.3},
                                 {"speed", 1.0},         {"updateHz", 30},
                                 {"nearDistance", 20.0}, {"farHz", 15.0},
                                 {"cullDistance", 280.0}};
        node["visible"] = true;
        doc["nodes"].push_back(std::move(node));

        json entity;
        entity["name"] = p.name;
        entity["node"] = p.name;
        // A per-animal seed, so "do not have every animal move simultaneously" is structural rather
        // than hoped for: each draws its own pauses and destinations from its own stream.
        entity["seed"] = seed + static_cast<std::uint32_t>(doc["entities"].size() * 7919u + 13u);
        entity["tags"] = json::array({"animal", "farm", p.species->name});
        entity["clips"] = json{{"idle", "Walk"}, {"walk", "Walk"}, {"run", "Walk"}, {"turn", "Walk"}};
        // Rate matching is what stops the legs sliding: the clip was authored at some speed and the
        // gait scales its playback to whatever the animal is actually doing.
        entity["gait"] = json{{"matchRate", true},
                              {"walkSpeed", p.species->speed},
                              {"runSpeed", p.species->speed * 2.2f},
                              {"moveEnter", 0.06},
                              {"moveExit", 0.03},
                              {"accel", 1.6},
                              {"decel", 2.4}};
        // `wander` with a home radius is the territory the brief asks for, and it is the behaviour
        // that already exists: it asks the navigation layer for a destination, steers round what is
        // in the way, drops the destination and waits a beat when every way out is blocked, and
        // keeps its state when a director takes the body. Nothing here is new.
        entity["behaviors"] = json::array({json{{"kind", "wander"},
                                                {"speed", p.species->speed},
                                                {"runSpeed", p.species->speed * 2.2f},
                                                {"turnRate", 110.0},
                                                {"arrive", 0.6},
                                                {"minRange", p.species->territory * 0.25f},
                                                {"maxRange", p.species->territory},
                                                {"pauseMin", 3.0},
                                                {"pauseMax", 26.0},
                                                {"homeRadius", p.species->territory}},
                                           json{{"kind", "ground"},
                                                {"slopeAlign", p.species->quadruped ? 1.0 : 0.55}}});
        entity["fullDetailDistance"] = 90.0;
        entity["coarseInterval"] = 0.1;
        entity["cullDistance"] = 260.0;
        doc["entities"].push_back(std::move(entity));
    }

    std::printf("placed %zu animals: %d visible, %d hidden\n", placed.size(), visible, hiddenCount);
    for (const Placed& p : placed) {
        std::printf("  %-12s %-7s (%7.1f, %6.1f, %7.1f)  canopy %5.1f m\n", p.name.c_str(),
                    p.glade ? "glade" : p.hidden ? "hidden" : "open", p.position.x, p.position.y,
                    p.position.z, p.canopy);
    }
    if (dryRun || emit.empty()) {
        return 0;
    }
    std::ofstream out(emit);
    out << doc.dump(1) << "\n";
    out.close();
    std::printf("wrote %s\n", emit.string().c_str());
    return 0;
}
