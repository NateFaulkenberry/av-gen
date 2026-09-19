// Glowmere Valley 2 - Multi-Camera: the ported tree work and the ported cast (ADR-344).
//
// ADR-340 built `glowmere-valley-3` to answer two questions -- why Glowmere's hills are bare, and
// whether a `decide` character can carry a world -- and the owner then cut the scene for its
// terrain and asked for the answers to be folded back into the film they came from. The scene is
// gone; the two instruments it was built around are here, pointed at the film.
//
// **Why the control arm is `glowmere-valley-2` and not a constant.** Every probe and every guard
// below reads two scenes: `glowmere-valley-2-multicam`, which now carries eight tree layers, and
// `glowmere-valley-2`, which still carries the three it was authored with. They are generated from
// the same terrain -- the same `world` block, seed, layers, features and water table, asserted
// below -- so the difference between their columns is the layer table and nothing else. That is
// the like-for-like ADR-340 had to build a second scene (`-legacytrees`) to get, and it is free
// here because the sibling film was left alone on purpose.
//
// ADR-182: a probe that cannot fail proves nothing. The guards below are two-sided -- the counts
// must rise *and* the tree line must not move -- and the control arm is a scene where the same
// computation gives the old answer.
//
// GPU-free: the world map, the scatter placer and the JSON are all CPU.

#include "assets/asset_registry.hpp"
#include "entity/entity.hpp"
#include "entity/nav_grid.hpp"
#include "entity/navigation.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "spatial/point_cloud.hpp"
#include "world/biome.hpp"
#include "world/ecology.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path worldDir() { return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world"; }

json readJson(const fs::path& path) {
    std::ifstream in(path);
    REQUIRE(in.good());
    json doc;
    in >> doc;
    return doc;
}

const json& terrainNode(const json& scene) {
    for (const json& n : scene.at("nodes")) {
        if (n.contains("world")) {
            return n;
        }
    }
    FAIL("no node in the scene declares a world");
    return scene;
}

world::WorldMap loadWorld(const json& doc) {
    auto map = world::worldMapFromJson(terrainNode(doc).at("world"));
    REQUIRE(map.has_value());
    map->prepare();
    return std::move(*map);
}

// The film, and the sibling it is measured against.
constexpr const char* kFilm = "glowmere-valley-2-multicam.scene.json";
constexpr const char* kControl = "glowmere-valley-2.scene.json";

// **A tree is a layer that places something at least five metres tall** -- the same structural
// definition `test_glowmere_scale.cpp` derives its tree line from, and for the same reason: the
// film has eight tree layers of which one is still called `canopy`, so a list of names is a floor
// that outlives what it counted.
constexpr float kTreeMetres = 5.0f;

struct Placement {
    std::size_t total = 0;
    std::size_t trees = 0;
    std::size_t treesOnHills = 0;
    std::vector<std::string> atCap;
    float treeLine = 0.0f;
    int treeLayers = 0;
    std::set<std::string> treeAssets;
};

Placement placeAll(const json& doc, const world::WorldMap& map, bool print, const char* label) {
    auto ecology = world::ecologyFromJson(terrainNode(doc).at("scatter"));
    REQUIRE(ecology.has_value());
    std::vector<world::ScatterClearance> clearances;
    if (terrainNode(doc).contains("clearings")) {
        auto c = world::clearancesFromJson(terrainNode(doc).at("clearings"));
        REQUIRE(c.has_value());
        clearances = std::move(*c);
    }
    // "The hills" is the upper half of the sampled height range -- the same split ADR-340 used, so
    // the numbers in that ADR and the numbers here are the same measurement.
    const float mid = 0.5f * (map.sampledMinHeight + map.sampledMaxHeight);
    Placement out;
    if (print) {
        std::printf("\n===== %s: placed instances =====\n", label);
        std::printf("  %-13s %9s %9s %10s %9s %8s\n", "layer", "placed", "maxInst", "meshBudget",
                    "onHills", "capped");
    }
    for (const world::ScatterLayer& layer : ecology->layers) {
        const spatial::PointCloud cloud = world::scatter(map, layer, {}, clearances);
        const std::size_t placed = cloud.positions().size();
        std::size_t hills = 0;
        for (const glm::vec3& p : cloud.positions()) {
            hills += p.y > mid ? 1 : 0;
        }
        const bool capped = static_cast<int>(placed) >= layer.maxInstances;
        out.total += placed;
        if (layer.height >= kTreeMetres) {
            out.trees += placed;
            out.treesOnHills += hills;
            out.treeLine = std::max(out.treeLine, layer.height * layer.maxScale);
            ++out.treeLayers;
            out.treeAssets.insert(layer.asset);
            if (capped) {
                out.atCap.push_back(layer.name);
            }
        }
        if (print) {
            std::printf("  %-13s %9zu %9d %10d %9zu %8s\n", layer.name.c_str(), placed,
                        layer.maxInstances, layer.meshBudget, hills, capped ? "AT CAP" : "");
        }
    }
    if (print) {
        std::printf("  %-13s %9zu   trees %zu, of which %zu on the hills; tree line %.2f m\n",
                    "TOTAL", out.total, out.trees, out.treesOnHills, out.treeLine);
        std::fflush(stdout);
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The premise the whole like-for-like rests on: the two scenes are the same terrain.
//
// If this ever fails, every count below stops being a comparison and becomes two unrelated
// numbers -- which is the failure mode ADR-340 built `-legacytrees` to avoid and the reason it is
// asserted rather than assumed.
TEST_CASE("The multicam and its sibling are generated from one terrain", "[glowmere][multicam]") {
    const json film = readJson(worldDir() / kFilm);
    const json control = readJson(worldDir() / kControl);
    CHECK(terrainNode(film).at("world") == terrainNode(control).at("world"));
    CHECK(terrainNode(film).at("terrain") == terrainNode(control).at("terrain"));
    // The glades are composition and they are shared. A clearing in one and not the other would
    // move instance counts without moving a layer.
    CHECK(terrainNode(film).at("clearings") == terrainNode(control).at("clearings"));
}

// ---------------------------------------------------------------------------------------------
// ADR-344 §2. Eight layers over eight models where there were three over three, and the counts
// that buys, and the ceiling it does not move.
TEST_CASE("The multicam's hills are wooded, and the tree line has not moved",
          "[glowmere][multicam]") {
    const json film = readJson(worldDir() / kFilm);
    const json control = readJson(worldDir() / kControl);
    const world::WorldMap map = loadWorld(film);
    const Placement after = placeAll(film, map, false, kFilm);
    const Placement before = placeAll(control, map, false, kControl);

    INFO("film " << after.treeLayers << " tree layers / " << after.treeAssets.size()
                 << " models, " << after.trees << " trees, " << after.treesOnHills
                 << " on the hills; control " << before.treeLayers << " / "
                 << before.treeAssets.size() << ", " << before.trees << " / "
                 << before.treesOnHills);

    // Variety, and it is variety of *species* and not of layer: two layers on one model would
    // satisfy a layer count and would not answer the owner's request.
    CHECK(after.treeLayers == 8);
    CHECK(after.treeAssets.size() == 8);
    // The control arm. This is the number the arm above would give on the world the film had
    // yesterday, and it must not pass the claim being made.
    CHECK(before.treeLayers == 3);
    CHECK(before.treeAssets.size() == 3);

    // A band, not a floor (ADR-182). The lower end is "the hills carry several times what they
    // did"; the upper is "no layer is resting on its cap", which is asserted directly below and is
    // what would turn a density change into a no-op.
    CHECK(after.trees > before.trees * 3);
    CHECK(after.treesOnHills > before.treesOnHills * 5);
    CHECK(after.atCap.empty());

    // ADR-334 and ADR-335, and the reason every row of the ported table is authored under 11.2 m:
    // the 16 m elder has to stand a fifth again above the tallest instance any tree layer places,
    // and the cast's 1.94x was re-derived from this exact number.
    CHECK(after.treeLine == Catch::Approx(before.treeLine).epsilon(1e-4));
    CHECK(after.treeLine == Catch::Approx(11.2f).epsilon(1e-3));
    CHECK(16.0f >= after.treeLine * 1.2f);
}

// ---------------------------------------------------------------------------------------------
// ADR-340 §3, re-measured on the terrain the film actually has.
//
// The claim ported forward is that the hills were empty because of ADR-174's riparian ladder and
// not because of biome or `maxSlope`. That was measured on valley 3's scene, which had terrain
// edits the film does not have, so it is measured again here rather than assumed. The instrument
// is the joint gate: for each tree layer, what fraction of hill samples each of the three gates
// refuses.
TEST_CASE("probe: what gates a tree off the multicam's hills", "[.probe][glowmere-multicam]") {
    for (const char* file : {kControl, kFilm}) {
        const json doc = readJson(worldDir() / file);
        const world::WorldMap map = loadWorld(doc);
        auto ecology = world::ecologyFromJson(terrainNode(doc).at("scatter"));
        REQUIRE(ecology.has_value());
        std::printf("\n===== %s: what refuses a tree on the hills =====\n", file);
        std::printf("  %-13s %8s %8s %10s %10s   %s\n", "layer", "maxSlope", "height", "all(dens)",
                    "hills(dens)", "hills refused by");

        const int kN = 160;
        const float mid = 0.5f * (map.sampledMinHeight + map.sampledMaxHeight);
        for (const world::ScatterLayer& layer : ecology->layers) {
            if (layer.height < kTreeMetres) {
                continue;
            }
            double all = 0.0;
            double hills = 0.0;
            int allN = 0;
            int hillN = 0;
            int hillSlopeOut = 0;
            int hillHarOut = 0;
            int hillNoBiome = 0;
            for (int j = 0; j < kN; ++j) {
                for (int i = 0; i < kN; ++i) {
                    const glm::vec2 q((static_cast<float>(i) / (kN - 1) - 0.5f) * map.size.x,
                                      (static_cast<float>(j) / (kN - 1) - 0.5f) * map.size.y);
                    const world::Sample sm = map.sample(q, 1.0f);
                    if (sm.submerged) {
                        continue;
                    }
                    const bool hill = sm.height > mid;
                    ++allN;
                    hillN += hill ? 1 : 0;
                    if (sm.slope < layer.minSlope || sm.slope > layer.maxSlope) {
                        hillSlopeOut += hill ? 1 : 0;
                        continue;
                    }
                    if (layer.constrainsHeightAboveWater()) {
                        const float har = map.heightAboveWater(q);
                        if (har < layer.minHeightAboveWater - layer.heightAboveWaterFeather ||
                            har > layer.maxHeightAboveWater + layer.heightAboveWaterFeather) {
                            hillHarOut += hill ? 1 : 0;
                            continue;
                        }
                    }
                    const world::BiomeWeights bw =
                        map.biomes.at(sm.altitude, sm.slope, sm.moisture, q);
                    double d = 0.0;
                    for (const world::BiomeDensity& bd : layer.densities) {
                        for (std::size_t b = 0; b < map.biomes.biomes.size(); ++b) {
                            if (map.biomes.biomes[b].name == bd.biome) {
                                d += static_cast<double>(bd.density) *
                                     static_cast<double>(bw.weights[b]);
                            }
                        }
                    }
                    all += d;
                    if (hill) {
                        hills += d;
                        hillNoBiome += d <= 0.0 ? 1 : 0;
                    }
                }
            }
            std::printf("  %-13s %8.2f %8.2f %10.5f %10.5f   slope %4.1f%%  HAR %4.1f%%  "
                        "no-biome %4.1f%%\n",
                        layer.name.c_str(), layer.maxSlope, layer.height,
                        all / std::max(1, allN), hills / std::max(1, hillN),
                        100.0 * hillSlopeOut / std::max(1, hillN),
                        100.0 * hillHarOut / std::max(1, hillN),
                        100.0 * hillNoBiome / std::max(1, hillN));
        }
        std::fflush(stdout);
    }
}

// ---------------------------------------------------------------------------------------------
// The counts, printed. `maxInstances` and `meshBudget` are caps, and a cap that binds is invisible
// in the density -- so the placer is run rather than the table read.
//
//   ./build/release/tests/avgen_tests "[.probe][glowmere-multicam]"
TEST_CASE("probe: Glowmere multicam scatter instance counts", "[.probe][glowmere-multicam]") {
    const json film = readJson(worldDir() / kFilm);
    const world::WorldMap map = loadWorld(film);
    placeAll(readJson(worldDir() / kControl), map, true, kControl);
    placeAll(film, map, true, kFilm);
}

// ---------------------------------------------------------------------------------------------
// ADR-340 §2, re-measured on the terrain the film actually has -- and this is the probe whose
// answer decides what the ported cast can be.
//
// Valley 3 authored a ford and a backwater into its terrain so that `RouteConsiderer` had two ways
// to price. **The film never got those edits**, so the river it has is the one ADR-340 measured
// before them: a channel that leaves the map at both ends, 3.5-3.65 m deep against a wade depth of
// 0.85 m. A route considerer needs a ford *or* a way round; this prints whether either exists, per
// alien, so "the crossing demonstration does not port" is a measurement and not an assumption.
//
//   ./build/release/tests/avgen_tests "[.probe][glowmere-multicam]"
TEST_CASE("probe: the multicam's river, and where each of the cast can get to",
          "[.probe][glowmere-multicam]") {
    const fs::path path = worldDir() / kFilm;
    assets::AssetRegistry registry(path.parent_path());
    params::ParameterSet params;
    params::Modulator modulator;
    auto loaded = scene::Composition::loadFile(path, registry);
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    std::unique_ptr<scene::Composition> comp = std::move(*loaded);
    comp->attach(params, modulator);
    const entity::Navigator& nav = comp->entityWorld().navigator();
    const entity::NavGrid* grid = nav.grid();

    std::printf("\n===== %s: the river as a route =====\n", kFilm);
    std::printf("navWadeDepth %.4f  navBodyRadius %.4f  grid %s\n", nav.settings().wadeDepth,
                nav.settings().bodyRadius,
                grid == nullptr ? "ABSENT" : (grid->valid() ? "present and valid" : "INVALID"));
    if (grid != nullptr) {
        std::printf("grid %dx%d at %.1f m; %lld walkable, %lld water, %lld blocked; %lld regions, "
                    "largest %lld, stranded %lld; terrain-trusted %s\n",
                    grid->stats().width, grid->stats().height,
                    static_cast<double>(grid->stats().cellSize),
                    static_cast<long long>(grid->stats().walkable),
                    static_cast<long long>(grid->stats().water),
                    static_cast<long long>(grid->stats().blocked),
                    static_cast<long long>(grid->stats().regions),
                    static_cast<long long>(grid->stats().largestRegion),
                    static_cast<long long>(grid->stats().stranded),
                    grid->stats().trusted ? "yes" : "NO");
    }

    // The channel, sampled across its own course. The centreline is
    // `tools/make_glowmere_valley_2.py`'s RIVER, every other control point.
    const std::array<glm::vec2, 8> kCourse{{{-33.0f, -220.0f},
                                            {27.0f, -92.0f},
                                            {-18.3f, 0.0f},
                                            {-32.0f, 20.0f},
                                            {-31.0f, 96.0f},
                                            {-13.0f, 132.0f},
                                            {45.0f, 244.0f},
                                            {11.0f, 352.0f}}};
    std::printf("\n  channel depth across the course (m of water, 0 = dry):\n");
    for (const glm::vec2& c : kCourse) {
        std::printf("    z %7.1f :", c.y);
        for (float off = -24.0f; off <= 24.0f; off += 6.0f) {
            std::printf(" %6.2f", nav.sample({c.x + off, c.y}).waterDepth);
        }
        std::printf("\n");
    }

    // Where the five bodies stand, and what each can walk to. The destinations are the film's own
    // hero fungi -- the things a curious character is actually going to want to go and look at --
    // so a row of failures here is the itinerary the deciders cannot have.
    struct Site {
        const char* name;
        float x;
        float z;
    };
    const std::array<Site, 5> kCast{{{"rook", -74.0f, -18.0f},
                                     {"tide", -28.0f, -46.0f},
                                     {"sage", -112.0f, 16.0f},
                                     {"ember", 12.0f, 34.0f},
                                     {"vane", 20.0f, -10.0f}}};
    std::vector<std::pair<std::string, glm::vec2>> goals;
    for (const world::HeroPoint& h : comp->heroes()) {
        goals.emplace_back(h.name, glm::vec2(h.position.x, h.position.z));
    }
    std::printf("\n  who can reach what (path length in m, or the refusal):\n");
    std::printf("    %-8s %8s %8s  ", "body", "ground", "depth");
    for (const auto& [name, p] : goals) {
        std::printf("%14s", name.c_str());
    }
    std::printf("\n");
    for (const Site& s : kCast) {
        const entity::NavSample sm = nav.sample({s.x, s.z});
        std::printf("    %-8s %8.2f %8.2f  ", s.name, sm.ground, sm.waterDepth);
        for (const auto& [name, p] : goals) {
            entity::PathRequest req;
            req.from = {s.x, s.z};
            req.to = p;
            req.goalTolerance = 8.0f;
            const entity::PathResult r = nav.requestPath(req);
            if (r.ok()) {
                std::printf("%14.1f", r.length);
            } else {
                std::printf("%14s", entity::pathStatusName(r.status));
            }
        }
        std::printf("\n");
    }
    std::fflush(stdout);
}
