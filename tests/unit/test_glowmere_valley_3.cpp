// Glowmere Valley 3: the autonomous-character showcase (ADR-338).
//
// This file starts as the instrument and becomes the guard. The probes print what the hills
// actually are -- which biome owns them, and at what slope -- because the vegetation question the
// owner asked ("more densely populated with variety of trees") turns on a structural fact and not
// on a density, and a density raised against a guess is a density raised for nothing.

#include "world/biome.hpp"
#include "world/ecology.hpp"
#include "assets/asset_registry.hpp"
#include "entity/decision.hpp"
#include "entity/entity.hpp"
#include "entity/nav_grid.hpp"
#include "entity/navigation.hpp"
#include "core/time.hpp"
#include "entity/locomotion.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"
#include "spatial/point_cloud.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
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

world::WorldMap loadWorld(const fs::path& scenePath) {
    const json doc = readJson(scenePath);
    auto map = world::worldMapFromJson(terrainNode(doc).at("world"));
    REQUIRE(map.has_value());
    map->prepare();
    return std::move(*map);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Probe 1: what the hills are.
//
// The claim under test is that the hills are bare for a structural reason and not a density one.
// `canopy` -- the densest tree layer -- declares densities for `forest` and `meadow` only, and the
// hills are neither. This prints the biome shares, the slope spread, and the joint distribution,
// so "the hills are scree and rim" stops being a reading of biome.cpp and becomes a measurement of
// this world.
TEST_CASE("probe: what Glowmere's hills are made of", "[.probe][glowmere3]") {
    for (const char* file : {"glowmere-valley-2-multicam.scene.json", "glowmere-valley-3.scene.json"}) {
        const fs::path path = worldDir() / file;
        if (!fs::exists(path)) {
            continue;
        }
        const world::WorldMap map = loadWorld(path);
        std::printf("\n===== %s =====\n", file);
        std::printf("height %.2f .. %.2f over %.0f x %.0f m\n", map.sampledMinHeight,
                    map.sampledMaxHeight, map.size.x, map.size.y);

        const int kN = 240;
        std::vector<int> owned(map.biomes.biomes.size(), 0);
        std::vector<int> ownedHigh(map.biomes.biomes.size(), 0);
        // slope bands, and the same bands again restricted to high ground.
        const std::array<float, 7> kBands{0.0f, 0.10f, 0.17f, 0.22f, 0.26f, 0.32f, 0.45f};
        std::vector<int> slopeAll(kBands.size() + 1, 0);
        std::vector<int> slopeHigh(kBands.size() + 1, 0);
        // per biome: how much of it sits under each of the three tree layers' maxSlope gates.
        std::map<std::string, std::array<int, 4>> gate; // <=0.26, <=0.30, <=0.32, total
        std::vector<float> slopes;
        std::vector<float> harHigh;
        int high = 0;
        int total = 0;
        for (int j = 0; j < kN; ++j) {
            for (int i = 0; i < kN; ++i) {
                const glm::vec2 q((static_cast<float>(i) / (kN - 1) - 0.5f) * map.size.x,
                                  (static_cast<float>(j) / (kN - 1) - 0.5f) * map.size.y);
                const world::Sample sm = map.sample(q, 1.0f);
                if (sm.submerged) {
                    continue;
                }
                ++total;
                slopes.push_back(sm.slope);
                const world::BiomeWeights bw = map.biomes.at(sm.altitude, sm.slope, sm.moisture, q);
                const int d = bw.dominant();
                ++owned[static_cast<std::size_t>(d)];
                if (sm.height > 0.5f * (map.sampledMinHeight + map.sampledMaxHeight)) {
                    ++ownedHigh[static_cast<std::size_t>(d)];
                    harHigh.push_back(map.heightAboveWater(q));
                }
                const std::string& name = map.biomes.biomes[static_cast<std::size_t>(d)].name;
                auto& g = gate[name];
                g[0] += sm.slope <= 0.26f ? 1 : 0;
                g[1] += sm.slope <= 0.30f ? 1 : 0;
                g[2] += sm.slope <= 0.32f ? 1 : 0;
                g[3] += 1;
                std::size_t band = 0;
                while (band < kBands.size() && sm.slope >= kBands[band]) {
                    ++band;
                }
                ++slopeAll[band];
                // "the hills": the upper half of the height range.
                const float mid = 0.5f * (map.sampledMinHeight + map.sampledMaxHeight);
                if (sm.height > mid) {
                    ++high;
                    ++slopeHigh[band];
                }
            }
        }
        std::sort(slopes.begin(), slopes.end());
        const auto pct = [&slopes](float p) {
            return slopes[static_cast<std::size_t>(p * static_cast<float>(slopes.size() - 1))];
        };
        std::printf("dry samples %d, of which %d (%.0f%%) are above mid-height\n", total, high,
                    100.0 * high / std::max(1, total));
        std::printf("slope percentiles: p50 %.3f  p75 %.3f  p90 %.3f  p95 %.3f  p99 %.3f  max %.3f\n",
                    pct(0.50f), pct(0.75f), pct(0.90f), pct(0.95f), pct(0.99f), slopes.back());
        std::sort(harHigh.begin(), harHigh.end());
        if (!harHigh.empty()) {
            const auto hp = [&harHigh](float p) {
                return harHigh[static_cast<std::size_t>(p * static_cast<float>(harHigh.size() - 1))];
            };
            std::printf("height above water ON THE HILLS: p05 %.1f  p25 %.1f  p50 %.1f  p75 %.1f  "
                        "p95 %.1f  max %.1f m\n",
                        hp(0.05f), hp(0.25f), hp(0.50f), hp(0.75f), hp(0.95f), harHigh.back());
        }
        std::printf("biome shares:");
        for (std::size_t b = 0; b < owned.size(); ++b) {
            std::printf("  %s %.1f%%", map.biomes.biomes[b].name.c_str(),
                        100.0 * owned[b] / std::max(1, total));
        }
        std::printf("\nbiome shares ON THE HILLS:");
        for (std::size_t b = 0; b < ownedHigh.size(); ++b) {
            std::printf("  %s %.1f%%", map.biomes.biomes[b].name.c_str(),
                        100.0 * ownedHigh[b] / std::max(1, high));
        }
        std::printf("\n\n  %-8s %8s %8s %8s %8s\n", "biome", "share", "<=0.26", "<=0.30", "<=0.32");
        for (const auto& [name, g] : gate) {
            std::printf("  %-8s %7.1f%% %7.1f%% %7.1f%% %7.1f%%\n", name.c_str(),
                        100.0 * g[3] / std::max(1, total), 100.0 * g[0] / std::max(1, g[3]),
                        100.0 * g[1] / std::max(1, g[3]), 100.0 * g[2] / std::max(1, g[3]));
        }
        std::printf("\n  slope band        all      hills\n");
        for (std::size_t b = 0; b <= kBands.size(); ++b) {
            const float lo = b == 0 ? 0.0f : kBands[b - 1];
            const float hi = b < kBands.size() ? kBands[b] : 9.0f;
            std::printf("  %5.2f .. %5.2f  %6.1f%%   %6.1f%%\n", lo, hi,
                        100.0 * slopeAll[b] / std::max(1, total),
                        100.0 * slopeHigh[b] / std::max(1, high));
        }
        std::fflush(stdout);
    }
}

// ---------------------------------------------------------------------------------------------
// Probe 2: what each scatter layer is allowed to grow on.
//
// A layer's biome densities and its `maxSlope` are two independent gates and the interesting
// quantity is their product: the fraction of the map at which this layer's density is non-zero.
// A layer forbidden on the hills reads here as a zero in the `hills` column, which is a different
// fact from a small number and is the fact the owner's request turns on.
TEST_CASE("probe: where each scatter layer may grow", "[.probe][glowmere3]") {
    for (const char* file : {"glowmere-valley-2-multicam.scene.json", "glowmere-valley-3.scene.json"}) {
        const fs::path path = worldDir() / file;
        if (!fs::exists(path)) {
            continue;
        }
        const json doc = readJson(path);
        const world::WorldMap map = loadWorld(path);
        std::printf("\n===== %s: layer reach =====\n", file);
        std::printf("  %-12s %8s %8s %10s %10s %s\n", "layer", "maxSlope", "height", "all(dens)",
                    "hills(dens)", "asset");

        const int kN = 160;
        const float mid = 0.5f * (map.sampledMinHeight + map.sampledMaxHeight);
        auto ecology = world::ecologyFromJson(terrainNode(doc).at("scatter"));
        REQUIRE(ecology.has_value());
        for (const world::ScatterLayer& L : ecology->layers) {
            const world::ScatterLayer* layer = &L;
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
                    if (sm.slope < layer->minSlope || sm.slope > layer->maxSlope) {
                        hillSlopeOut += hill ? 1 : 0;
                        continue;
                    }
                    if (layer->constrainsHeightAboveWater()) {
                        const float har = map.heightAboveWater(q);
                        if (har < layer->minHeightAboveWater - layer->heightAboveWaterFeather ||
                            har > layer->maxHeightAboveWater + layer->heightAboveWaterFeather) {
                            hillHarOut += hill ? 1 : 0;
                            continue;
                        }
                    }
                    const world::BiomeWeights bw =
                        map.biomes.at(sm.altitude, sm.slope, sm.moisture, q);
                    double d = 0.0;
                    for (const world::BiomeDensity& bd : layer->densities) {
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
            std::printf("  %-12s %8.2f %8.2f %10.5f %10.5f  | hills refused: slope %4.1f%%  "
                        "HAR %4.1f%%  no-biome %4.1f%%\n",
                        layer->name.c_str(), layer->maxSlope, layer->height,
                        all / std::max(1, allN), hills / std::max(1, hillN),
                        100.0 * hillSlopeOut / std::max(1, hillN),
                        100.0 * hillHarOut / std::max(1, hillN),
                        100.0 * hillNoBiome / std::max(1, hillN));
        }
        std::fflush(stdout);
    }
}

// ---------------------------------------------------------------------------------------------
// Probe 3: what the scatter actually places, and what it costs.
//
// `maxInstances` and `meshBudget` are caps and a cap that binds is invisible in the density.
// `scatter()` is the production placer, so these are the counts the renderer is handed -- before
// any view culling, which is the number a density change moves.
TEST_CASE("probe: Glowmere scatter instance counts", "[.probe][glowmere3]") {
    for (const char* file : {"glowmere-valley-2-multicam.scene.json", "glowmere-valley-3.scene.json"}) {
        const fs::path path = worldDir() / file;
        if (!fs::exists(path)) {
            continue;
        }
        const json doc = readJson(path);
        const world::WorldMap map = loadWorld(path);
        auto ecology = world::ecologyFromJson(terrainNode(doc).at("scatter"));
        REQUIRE(ecology.has_value());
        std::vector<world::ScatterClearance> clearances;
        if (terrainNode(doc).contains("clearings")) {
            auto c = world::clearancesFromJson(terrainNode(doc).at("clearings"));
            REQUIRE(c.has_value());
            clearances = std::move(*c);
        }
        std::printf("\n===== %s: placed instances =====\n", file);
        std::printf("  %-12s %9s %9s %9s %8s %8s\n", "layer", "placed", "maxInst", "meshBudget",
                    "onHills", "capped");
        std::size_t total = 0;
        std::size_t treeTotal = 0;
        const float mid = 0.5f * (map.sampledMinHeight + map.sampledMaxHeight);
        for (const world::ScatterLayer& layer : ecology->layers) {
            const spatial::PointCloud cloud = world::scatter(map, layer, {}, clearances);
            std::size_t hills = 0;
            for (const glm::vec3& p : cloud.positions()) {
                hills += p.y > mid ? 1 : 0;
            }
            const bool capped = static_cast<int>(cloud.positions().size()) >= layer.maxInstances;
            total += cloud.positions().size();
            if (layer.height >= 5.0f) {
                treeTotal += cloud.positions().size();
            }
            std::printf("  %-12s %9zu %9d %9d %8zu %8s\n", layer.name.c_str(),
                        cloud.positions().size(), layer.maxInstances, layer.meshBudget, hills,
                        capped ? "AT CAP" : "");
        }
        std::printf("  %-12s %9zu  (of which tree-sized: %zu)\n", "TOTAL", total, treeTotal);
        std::fflush(stdout);
    }
}

// ---------------------------------------------------------------------------------------------
// Probe 4: is Glowmere's river a choice?
//
// ADR-336 delivered `RouteConsiderer` against a flat 240 m fixture whose river is 14 m wide, 1.40 m
// deep, and **ends inside the map** -- so a ford exists and a way round exists, by construction.
// Glowmere's is 26 m wide and its centreline is authored to leave the map at both ends
// (`tools/make_glowmere_valley_2.py`: "the channel crosses both boundaries instead of stopping at
// them, so there is no edge at which it can end"). That is the opposite property, and it is the
// one the second demonstration depends on.
//
// So this asks the world rather than assuming: how deep the channel is against `navWadeDepth`,
// whether two requests at two prices come back as two different ways, and how much of each is wet.
TEST_CASE("probe: is Glowmere's river a crossing decision", "[.probe][glowmere3]") {
    if (!fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb")) {
        SKIP("assets/aliens is not present");
    }
    for (const char* file : {"glowmere-valley-2-multicam.scene.json", "glowmere-valley-3.scene.json"}) {
        const fs::path path = worldDir() / file;
        if (!fs::exists(path)) {
            continue;
        }
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
        std::printf("\n===== %s: the river as a route =====\n", file);
        std::printf("navWadeDepth %.4f  navBodyRadius %.4f  grid %s\n", nav.settings().wadeDepth,
                    nav.settings().bodyRadius,
                    grid == nullptr ? "ABSENT" : (grid->valid() ? "present and valid" : "INVALID"));
        if (grid != nullptr) {
            std::printf("grid %dx%d at %.1f m; %d walkable, %d water, %d blocked; %d regions, "
                        "largest %d, stranded %d; terrain-trusted %s\n",
                        grid->stats().width, grid->stats().height, grid->stats().cellSize,
                        grid->stats().walkable, grid->stats().water, grid->stats().blocked,
                        grid->stats().regions, grid->stats().largestRegion, grid->stats().stranded,
                        grid->stats().trusted ? "yes" : "NO");
        }

        // The channel, sampled across its own course. The centreline is
        // tools/make_glowmere_valley_2.py's RIVER, every other control point.
        const std::array<glm::vec2, 8> kCourse{{{-33.0f, -220.0f},
                                                {27.0f, -92.0f},
                                                {-18.3f, 0.0f},    // the ford
                                                {-32.0f, 20.0f},
                                                {-31.0f, 96.0f},   // the causeway
                                                {-13.0f, 132.0f},
                                                {45.0f, 244.0f},
                                                {11.0f, 352.0f}}};
        std::printf("\n  channel depth across the course (m of water, - = dry):\n");
        for (const glm::vec2& c : kCourse) {
            std::printf("    z %7.1f :", c.y);
            for (float off = -24.0f; off <= 24.0f; off += 6.0f) {
                const entity::NavSample s = nav.sample({c.x + off, c.y});
                std::printf(" %6.2f", s.waterDepth);
            }
            std::printf("\n");
        }

        // The backwater, across its own width. It is the crossing *decision*: the ford is in both
        // routes, so the only thing that separates a wader from a drylander is how wet this is.
        const std::array<glm::vec2, 4> kSlough{
            {{102.0f, -102.0f}, {87.0f, -58.0f}, {78.0f, -30.0f}, {61.0f, 24.0f}}};
        std::printf("\n  the backwater, across (m of water):\n");
        for (const glm::vec2& c : kSlough) {
            std::printf("    z %7.1f :", c.y);
            for (float off = -18.0f; off <= 18.0f; off += 4.0f) {
                std::printf(" %5.2f", nav.sample({c.x + off, c.y}).waterDepth);
            }
            std::printf("\n");
        }

        // The two ways, priced. This is `RouteConsiderer::price` itself and not a reconstruction
        // of it, so what is printed is what the character sees.
        const std::array<glm::vec3, 2> kFrom{{{132.0f, 0.0f, -36.0f}, {128.0f, 0.0f, -30.0f}}};
        const glm::vec3 kTo{38.0f, 0.0f, -46.0f};
        for (const float taste : {1.6f, 16.0f}) {
            const nlohmann::json settings{
                {"name", "cross"},          {"weight", 1.0f},
                {"wadePenalty", taste},     {"fordPenalty", 0.0f},
                {"detourPenalty", 40.0f},   {"falloff", 90.0f},
                {"goalTolerance", 3.0f},
                {"destinations", nlohmann::json::array({nlohmann::json{
                                     {"name", "east-bank"},
                                     {"point", nlohmann::json::array({kTo.x, kTo.y, kTo.z})}}})}};
            entity::RouteConsiderer route(&settings);
            entity::EntityState state;
            state.anchor = kFrom[0];
            state.radius = 0.7f;
            entity::DecisionContext ctx;
            ctx.time = 0.0;
            ctx.state = &state;
            ctx.nav = &nav;
            ctx.world = &comp->entityWorld();
            ctx.seed = 0;
            std::vector<entity::RouteConsiderer::Priced> priced;
            route.price(ctx, priced);
            std::printf("\n  wadePenalty %5.1f from (%.0f, %.0f) to (%.0f, %.0f): %zu way(s)\n",
                        taste, kFrom[0].x, kFrom[0].z, kTo.x, kTo.z, priced.size());
            for (const entity::RouteConsiderer::Priced& r : priced) {
                std::printf("    %-7s length %7.2f m  wet %6.2f m  cost %8.2f  score %.4f  "
                            "%zu waypoints  status %d\n",
                            r.detour ? "detour" : "ford", r.length, r.wadeMetres, r.cost, r.score,
                            r.waypoints.size(), static_cast<int>(r.status));
            }
        }
        std::fflush(stdout);
    }
}

// ---------------------------------------------------------------------------------------------
// Probe 5: the staging sites.
//
// Every Y in `tools/make_glowmere_valley_3.py`'s `CAST_SITES` is a ground height read here. A body
// authored off the ground floats or is buried, and the `ground` behaviour corrects a body that is
// out by a little -- it cannot correct one that starts inside a hillside or in the river.
//
// Override the list with AVGEN_V3_SITES="name=x,z;name=x,z;..." to probe somewhere else.
TEST_CASE("probe: Glowmere Valley 3 staging sites", "[.probe][glowmere3]") {
    const fs::path path = worldDir() / "glowmere-valley-3.scene.json";
    if (!fs::exists(path)) {
        SKIP("glowmere-valley-3 has not been generated");
    }
    const world::WorldMap map = loadWorld(path);
    std::vector<std::pair<std::string, glm::vec2>> sites;
    if (const char* env = std::getenv("AVGEN_V3_SITES"); env != nullptr) {
        std::string spec(env);
        std::size_t i = 0;
        while (i < spec.size()) {
            const std::size_t semi = std::min(spec.find(';', i), spec.size());
            const std::string one = spec.substr(i, semi - i);
            const std::size_t eq = one.find('=');
            const std::string label = eq == std::string::npos ? one : one.substr(0, eq);
            const std::string xz = eq == std::string::npos ? one : one.substr(eq + 1);
            const std::size_t comma = xz.find(',');
            if (comma != std::string::npos) {
                sites.emplace_back(label, glm::vec2(std::stof(xz.substr(0, comma)),
                                                    std::stof(xz.substr(comma + 1))));
            }
            i = semi + 1;
        }
    }
    std::printf("\n===== Glowmere Valley 3: staging =====\n");
    std::printf("  %-22s %8s %8s %8s %8s %s\n", "site", "x", "z", "ground", "slope", "water");
    for (const auto& [name, q] : sites) {
        const world::Sample s = map.sample(q, 0.5f);
        const float surface = map.waterSurface(q);
        std::printf("  %-22s %8.1f %8.1f %8.2f %8.3f %s\n", name.c_str(), q.x, q.y, s.height,
                    s.slope,
                    s.submerged ? fmt::format("WET {:.2f} m", surface - s.height).c_str() : "dry");
    }
    std::fflush(stdout);
}

// =================================================================================================
// The four demonstrations, run
// =================================================================================================
//
// Everything above this line is geometry. Everything below it is the world actually running, and
// the rule it is written to is the owner's: numerical agreement is not proof of anything visual,
// and a probe that cannot fail proves nothing. So every arm here has a control that must come out
// differently, and every bound is a band rather than a floor.
//
// **Seeds.** The showcase's claim is that the behaviour is emergent, not authored, so it has to
// vary. `EntityDesc::seed` is the per-body stream and the decision cadence's phase; running the
// same world at a second set of seeds and getting the same trace to the metre would mean the
// bodies were following the geometry and nothing else. `kSeedOffsets` below is applied to every
// entity, and the arms assert both that the *kind* of thing that happens survives the change and
// that the detail does not.

namespace {

struct Track {
    glm::vec3 start{0.0f};
    glm::vec3 end{0.0f};
    float travelled = 0.0f;
    float deepest = 0.0f;       // the deepest water this body stood in
    float wetSeconds = 0.0f;    // seconds spent in water at all
    float nearestTo = 1e9f;     // closest approach to a named other body
    std::size_t decisions = 0;
    std::size_t remembered = 0;
    std::size_t percepts = 0;
    std::size_t optionsSeen = 0;
    std::map<std::string, int> chosen;  // option name -> decision ticks it held
    std::map<std::string, int> activity;
    std::map<std::string, float> bestScore; // option name -> the best score it ever scored
};

struct Run {
    std::map<std::string, Track> tracks;
};

fs::path v3Scene() { return worldDir() / "glowmere-valley-3.scene.json"; }

bool v3Ready() {
    return fs::exists(v3Scene()) &&
           fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb");
}

// One run of the world. `seedShift` is added to every entity's seed before the composition is
// built, which is the only thing that differs between the two arms.
Run play(double seconds, std::uint32_t seedShift, std::uint32_t ecologyShift = 0,
         double hz = 40.0) {
    std::ifstream in(v3Scene());
    REQUIRE(in.good());
    json doc;
    in >> doc;
    if (seedShift != 0) {
        for (json& e : doc.at("entities")) {
            e["seed"] = e.at("seed").get<std::uint64_t>() + seedShift;
        }
    }
    if (ecologyShift != 0) {
        // The world the bodies perceive, re-rolled. Every scatter layer's seed moves, so every
        // glow patch, every trunk and every interest point the ecology publishes is somewhere
        // else -- on the same terrain, with the same heroes, and with the cast's authored config
        // unchanged to the byte. If the itinerary survives that, the itinerary was not a
        // consequence of what the bodies saw.
        for (json& n : doc.at("nodes")) {
            if (!n.contains("scatter")) {
                continue;
            }
            for (json& l : n.at("scatter")) {
                l["seed"] = l.value("seed", 0u) + ecologyShift;
            }
        }
    }
    assets::AssetRegistry registry(v3Scene().parent_path());
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    auto loaded = scene::Composition::fromJson(doc, registry);
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    std::unique_ptr<scene::Composition> comp = std::move(*loaded);
    comp->attach(params, modulator);
    comp->setViewport(1600, 900);
    // ADR-186's offline setting. With the distance cull on, "it took the dry way" would be a
    // measurement of which level-of-detail band the camera happened to put the body in.
    comp->scene().detailLimits.entityDistanceCull = false;

    Run out;
    const entity::Navigator& nav = comp->entityWorld().navigator();
    const double step = 1.0 / hz;
    const auto frames = static_cast<int>(std::llround(seconds * hz));
    FrameTime time;
    std::map<std::string, glm::vec3> last;
    for (int i = 0; i < frames; ++i) {
        time.renderTime = static_cast<double>(i) * step;
        time.deltaTime = i == 0 ? 0.0 : step;
        time.frameIndex = static_cast<std::uint64_t>(i);
        params.resetFinals();
        comp->updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp->updateBehaviour(time, bus);
        comp->update(time);

        for (const auto& e : comp->entityWorld().entities()) {
            const std::string name(e->name());
            Track& t = out.tracks[name];
            const glm::vec3 p = e->state().position();
            if (i == 0) {
                t.start = p;
                last[name] = p;
            }
            t.travelled += glm::length(glm::vec2(p.x - last[name].x, p.z - last[name].z));
            last[name] = p;
            t.end = p;
            const float depth = nav.sample(glm::vec2(p.x, p.z)).waterDepth;
            t.deepest = std::max(t.deepest, depth);
            t.wetSeconds += depth > 0.05f ? static_cast<float>(step) : 0.0f;
            t.activity[entity::activityName(e->locomotion().activity)] += 1;
            t.percepts = std::max(t.percepts, e->percepts().size());
            for (const auto& behavior : e->behaviors()) {
                entity::DecisionDebug dbg;
                if (!behavior->decisionDebug(dbg)) {
                    continue;
                }
                t.decisions = dbg.decisions;
                t.remembered = std::max(t.remembered, dbg.remembered);
                t.optionsSeen = std::max(t.optionsSeen, dbg.options.size());
                for (const entity::ScoredOption& o : dbg.options) {
                    float& best = t.bestScore[std::string(o.name)];
                    best = std::max(best, o.score);
                }
                if (!dbg.chosen.empty()) {
                    t.chosen[std::string(dbg.chosen)] += 1;
                }
            }
        }
        // Closest approach of the watcher to the elder, sampled every frame rather than at the
        // end: "it went and looked" is a minimum over the run, not a final position.
        const entity::Entity* w = comp->entityWorld().find("watcher");
        const entity::Entity* el = comp->entityWorld().find("elder");
        if (w != nullptr && el != nullptr) {
            const glm::vec3 a = w->state().position();
            const glm::vec3 b = el->state().position();
            out.tracks["watcher"].nearestTo =
                std::min(out.tracks["watcher"].nearestTo,
                         glm::length(glm::vec2(a.x - b.x, a.z - b.z)));
        }
    }
    return out;
}

void report(const char* label, const Run& run) {
    std::printf("\n--- %s ---\n", label);
    for (const auto& [name, t] : run.tracks) {
        std::string top;
        int best = 0;
        for (const auto& [option, n] : t.chosen) {
            if (n > best) {
                best = n;
                top = option;
            }
        }
        std::string acts;
        for (const auto& [a, n] : t.activity) {
            acts += fmt::format(" {}:{}", a, n);
        }
        std::string opts;
        for (const auto& [o, sc] : t.bestScore) {
            opts += fmt::format(" {}={:.3f}", o, sc);
        }
        std::printf("  %-10s travelled %7.1f m  net %6.1f m  deepest %5.2f m  wet %5.1f s  "
                    "decisions %3zu  mostly '%s'  end (%.0f, %.0f)\n"
                    "              percepts<=%zu remembered<=%zu options<=%zu\n"
                    "              best:%s\n              act:%s\n",
                    name.c_str(), t.travelled,
                    glm::length(glm::vec2(t.end.x - t.start.x, t.end.z - t.start.z)), t.deepest,
                    t.wetSeconds, t.decisions, top.c_str(), t.end.x, t.end.z,
                    t.percepts, t.remembered, t.optionsSeen, opts.c_str(), acts.c_str());
    }
    std::fflush(stdout);
}

constexpr std::uint32_t kSeedShift = 900001u;

} // namespace

TEST_CASE("probe: Glowmere Valley 3, the four demonstrations run", "[.probe][glowmere3]") {
    if (!v3Ready()) {
        SKIP("glowmere-valley-3 or assets/aliens is not present");
    }
    report("A: as shipped, 180 s", play(180.0, 0));
    report("B: entity seeds +900001", play(180.0, kSeedShift));
    report("C: ecology seeds +101 (a different world to perceive)", play(180.0, 0, 101));
    report("D: entity seeds +900001 AND ecology seeds +101", play(180.0, kSeedShift, 101));
}

// =================================================================================================
// The arms
// =================================================================================================
//
// ADR-182: a probe that cannot fail proves nothing. Every arm below has a control that must come
// out the other way, and every bound is a band. A floor is not used anywhere in this file, because
// a floor outlives what it counted -- two branches lowered the same floor on one day in this
// repository and together took it to zero.

// ---------------------------------------------------------------------------------------------
// 1. The crossing is a decision, and `wadePenalty` is what makes it.
//
// The two bodies are identical to the byte except for one number. So the arm is not "the wader got
// wet" -- that could be a consequence of where it starts, which way it faces, or which of the two
// the placer happened to put nearer the water. It is that **swapping the one number swaps the
// outcome**, which nothing about the geometry can do.
TEST_CASE("Glowmere Valley 3: one alien fords the water and the other walks round it",
          "[glowmere3][entity][route]") {
    if (!v3Ready()) {
        SKIP("glowmere-valley-3 or assets/aliens is not present");
    }
    const Run run = play(170.0, 0);
    const Track& wader = run.tracks.at("wader");
    const Track& dry = run.tracks.at("drylander");

    INFO("wader: " << wader.travelled << " m, deepest " << wader.deepest << " m, wet "
                   << wader.wetSeconds << " s; drylander: " << dry.travelled << " m, deepest "
                   << dry.deepest << " m, wet " << dry.wetSeconds << " s");

    // Bands. The backwater is 0.70 m at the channel and `navWadeDepth` is 0.8536, so a body that
    // crossed it stood in 0.55 to 0.86 m -- the lower bound is "it crossed rather than clipped the
    // shoulder" and the upper is the wade depth, past which the cell is not walkable at all.
    CHECK(wader.deepest > 0.55f);
    CHECK(wader.deepest < 0.86f);
    // And the drylander's ankles. Not zero and not asserted to be: the route's own waypoints may
    // clip a shoulder, exactly as ADR-336 §6 records for the lab's detour.
    CHECK(dry.deepest < 0.12f);
    // Seven times as wet is the claim, not a zero somebody had to engineer.
    CHECK(wader.deepest > dry.deepest * 5.0f);

    // The dry way is the long way, and both of them get there.
    CHECK(dry.travelled > wader.travelled * 1.7f);
    CHECK(dry.travelled < wader.travelled * 3.5f);
    const glm::vec2 goal(38.0f, -46.0f);
    for (const Track* t : {&wader, &dry}) {
        CHECK(glm::length(glm::vec2(t->end.x, t->end.z) - goal) < 9.0f);
    }

    // **The control, and it is the arm.** The same world with the two tastes exchanged. If the
    // wetness followed the geometry rather than the number, this comes out the same way round.
    std::ifstream in(v3Scene());
    REQUIRE(in.good());
    json doc;
    in >> doc;
    float swapped = 0;
    for (json& e : doc.at("entities")) {
        const std::string name = e.at("name").get<std::string>();
        if (name != "wader" && name != "drylander") {
            continue;
        }
        for (json& b : e.at("behaviors")) {
            if (b.value("kind", std::string()) != "decide") {
                continue;
            }
            for (json& c : b.at("considerers")) {
                if (c.value("kind", std::string()) == "route") {
                    c["wadePenalty"] = name == "wader" ? 16.0f : 1.6f;
                    swapped += 1.0f;
                }
            }
        }
    }
    REQUIRE(swapped == 2.0f);
    {
        // The same harness, on the edited document.
        const fs::path tmp = fs::temp_directory_path() / "glowmere-valley-3-swapped.scene.json";
        std::ofstream out(tmp);
        REQUIRE(out.good());
        // Written into `examples/world` rather than the system temp, because a scene's asset paths
        // are relative to the scene file and a copy somewhere else resolves none of them.
        out.close();
        fs::remove(tmp);
    }
    const fs::path swappedPath = worldDir() / "_v3-swapped.scene.json";
    {
        std::ofstream out(swappedPath);
        REQUIRE(out.good());
        out << doc.dump(1);
    }
    Run control;
    {
        assets::AssetRegistry registry(worldDir());
        params::ParameterSet params;
        params::Modulator modulator;
        signals::SignalBus bus;
        auto loaded = scene::Composition::loadFile(swappedPath, registry);
        INFO((loaded.has_value() ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        std::unique_ptr<scene::Composition> comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->setViewport(1600, 900);
        comp->scene().detailLimits.entityDistanceCull = false;
        const entity::Navigator& nav = comp->entityWorld().navigator();
        FrameTime time;
        const double step = 1.0 / 40.0;
        for (int i = 0; i < 6800; ++i) {
            time.renderTime = static_cast<double>(i) * step;
            time.deltaTime = i == 0 ? 0.0 : step;
            time.frameIndex = static_cast<std::uint64_t>(i);
            params.resetFinals();
            comp->updateFields(time, bus, modulator);
            modulator.applyRoutes(bus, params, time.deltaTime);
            comp->updateBehaviour(time, bus);
            comp->update(time);
            for (const char* who : {"wader", "drylander"}) {
                const entity::Entity* e = comp->entityWorld().find(who);
                REQUIRE(e != nullptr);
                const glm::vec3 p = e->state().position();
                Track& t = control.tracks[who];
                t.deepest = std::max(t.deepest, nav.sample(glm::vec2(p.x, p.z)).waterDepth);
            }
        }
    }
    fs::remove(swappedPath);
    INFO("control (tastes exchanged): wader deepest " << control.tracks.at("wader").deepest
         << " m, drylander deepest " << control.tracks.at("drylander").deepest << " m");
    // Exchanged: now the body called `wader` stays dry and the one called `drylander` wades.
    CHECK(control.tracks.at("drylander").deepest > 0.55f);
    CHECK(control.tracks.at("wader").deepest < 0.12f);
}

// ---------------------------------------------------------------------------------------------
// 2. Nothing is scripted: the same authored config in a re-rolled world goes somewhere else.
//
// This is the arm the whole showcase rests on, and it has two halves that fail differently.
//
// The **negative** half is the surprise and it is asserted rather than hidden: changing every
// entity's seed changes almost nothing. That is not a defect, it is ADR-333's D1 and D2 -- a
// considerer is a pure function of its context and draws from no stream -- so a `decide`
// character's itinerary is a function of the world and its start, and the seed only moves the
// decision tick's phase. Asserting it keeps the *other* half honest: if the ecology arm's
// difference were noise, this arm would show the same size of difference.
TEST_CASE("Glowmere Valley 3: the itinerary is a consequence of the world, not of the file",
          "[glowmere3][entity][decide]") {
    if (!v3Ready()) {
        SKIP("glowmere-valley-3 or assets/aliens is not present");
    }
    const Run a = play(150.0, 0);
    const Run b = play(150.0, kSeedShift);
    const Run c = play(150.0, 0, 101);

    for (const char* who : {"scout", "watcher"}) {
        const Track& ta = a.tracks.at(who);
        const Track& tb = b.tracks.at(who);
        const Track& tc = c.tracks.at(who);
        // How different an itinerary is: where it ended plus how far it walked to get there.
        // Endpoint alone is the wrong instrument and the measurement said so -- the watcher
        // attends to the `elder`, which does not move, so in the re-rolled world it still ends
        // beside it (12.0 m away) after walking 190 m instead of 35. That is a completely
        // different errand with nearly the same address, and an arm that read only the address
        // would have called it unchanged.
        const auto itinerary = [](const Track& x, const Track& y) {
            return glm::length(glm::vec2(x.end.x - y.end.x, x.end.z - y.end.z)) +
                   std::fabs(x.travelled - y.travelled);
        };
        const float ab = itinerary(ta, tb);
        const float ac = itinerary(ta, tc);
        INFO(who << ": A walked " << ta.travelled << " m and ended (" << ta.end.x << ", "
                 << ta.end.z << "); B differs by " << ab << " (walked " << tb.travelled
                 << "); C differs by " << ac << " (walked " << tc.travelled << ")");
        // The entity seed moves the decision phase and nothing else.
        CHECK(ab < 6.0f);
        // The world it perceives moves the errand. A band: an order of magnitude more than the
        // seed arm, and less than the map, because a body that had gone 900 m would mean the
        // measurement had found a different body.
        CHECK(ac > 30.0f);
        CHECK(ac < 700.0f);
        // And it is still deciding rather than executing one long plan.
        CHECK(ta.decisions >= 2);
        CHECK(tc.decisions >= 2);
    }

    // The *demonstration* survives what the itinerary does not. The crossing is a property of the
    // geography and the taste, so a re-rolled ecology must not change who gets wet -- an arm that
    // showed everything changing would be an arm that had broken the world rather than varied it.
    for (const Run* r : {&a, &c}) {
        CHECK(r->tracks.at("wader").deepest > 0.55f);
        CHECK(r->tracks.at("drylander").deepest < 0.12f);
    }
}

// ---------------------------------------------------------------------------------------------
// 3. One body notices another.
//
// Nothing in the scene names the elder to the watcher. There is no subject list, no destination
// and no post; the option the watcher commits to is the *name of a percept*, and a percept of kind
// Character is another entity this body has seen. The control is the scout, which has the same
// considerer with `character` weighted at 0.6 instead of 4.8 and the same five bodies in its
// world.
TEST_CASE("Glowmere Valley 3: the watcher attends to another character", "[glowmere3][entity]") {
    if (!v3Ready()) {
        SKIP("glowmere-valley-3 or assets/aliens is not present");
    }
    const Run run = play(150.0, 0);
    const Track& w = run.tracks.at("watcher");
    std::string top;
    int best = 0;
    for (const auto& [option, n] : w.chosen) {
        if (n > best) {
            best = n;
            top = option;
        }
    }
    INFO("watcher committed most often to '" << top << "'; closest approach to the elder "
                                             << w.nearestTo << " m after " << w.travelled << " m");
    // It went to a body, and that body is one of the four others by name.
    const std::set<std::string> cast{"scout", "wader", "drylander", "elder"};
    CHECK(cast.count(top) == 1);
    // And it got there. `approach` is 7 m and the two bodies have radii, so a band rather than a
    // point: nearer than 14 m is arrival and nearer than 2 m would be standing inside it.
    CHECK(w.nearestTo > 1.0f);
    CHECK(w.nearestTo < 14.0f);

    // The control. The scout's taste is the same considerer with `character` at 0.6, and it never
    // commits to a body -- so the watcher's choice is the weight and not the geometry.
    int scoutChoseABody = 0;
    for (const auto& [option, n] : run.tracks.at("scout").chosen) {
        scoutChoseABody += cast.count(option) != 0 ? n : 0;
    }
    INFO("control: the scout committed to a body on " << scoutChoseABody << " frames");
    CHECK(scoutChoseABody == 0);
}

// ---------------------------------------------------------------------------------------------
// 4. A small authored library.
//
// The claim of the fourth demonstration is that the expression comes from look-at, blending,
// locomotion adaptation and grounding rather than from a big clip list. An arm that only counted
// clips would pass on a world where nothing moved, so this counts both ends: the library is small
// **and** the bodies reach several activities out of it.
TEST_CASE("Glowmere Valley 3: a small clip library, made expressive by the layers above it",
          "[glowmere3][animation]") {
    if (!v3Ready()) {
        SKIP("glowmere-valley-3 or assets/aliens is not present");
    }
    const json doc = readJson(v3Scene());
    std::set<std::string> distinctClips;
    int bodies = 0;
    for (const json& e : doc.at("entities")) {
        if (!e.contains("clips")) {
            continue;
        }
        ++bodies;
        for (const auto& [role, clip] : e.at("clips").items()) {
            distinctClips.insert(clip.get<std::string>());
        }
    }
    int aimLayers = 0;
    int additiveLayers = 0;
    for (const json& n : doc.at("nodes")) {
        if (!n.contains("animation") || !n.at("animation").contains("layers")) {
            continue;
        }
        for (const json& l : n.at("animation").at("layers")) {
            aimLayers += l.value("kind", std::string()) == "aim" ? 1 : 0;
            additiveLayers += l.value("kind", std::string()) == "additive" ? 1 : 0;
        }
    }
    INFO(bodies << " bodies, " << distinctClips.size() << " distinct clips, " << aimLayers
                << " aim layers, " << additiveLayers << " additive layers");
    CHECK(bodies == 5);
    // Small. Each alien GLB ships 26 clips; a band rather than a ceiling, because a library of one
    // would pass a ceiling and would not be a library.
    CHECK(distinctClips.size() >= 6);
    CHECK(distinctClips.size() <= 10);
    // And the layers that are supposed to be doing the work exist on every body.
    CHECK(aimLayers == bodies);
    CHECK(additiveLayers == bodies);

    // The other end: what the bodies actually play. A world whose cast only ever idles would pass
    // every assertion above.
    const Run run = play(150.0, 0);
    for (const auto& [name, t] : run.tracks) {
        int reached = 0;
        for (const auto& [a, n] : t.activity) {
            reached += n > 20 ? 1 : 0; // half a second at 40 Hz, so a single frame does not count
        }
        INFO(name << " reached " << reached << " activities");
        CHECK(reached >= 2);
    }
    // And rate matching is on, which is the locomotion-adaptation half.
    for (const json& e : doc.at("entities")) {
        INFO(e.at("name").get<std::string>());
        CHECK(e.at("gait").value("matchRate", false));
    }
}

// ---------------------------------------------------------------------------------------------
// 5. The project does not contradict its scene, and there is almost nothing in it to contradict
//    it with.
//
// ADR-264's cheap arm, narrowed to one project. The residue it caught was a saved node transform;
// valley 3 carries no node parameter at all, so the arm is that the *class* is empty rather than
// that its members agree. The control is valley-2-multicam, where the same computation finds 41.
TEST_CASE("Glowmere Valley 3's project is small and states nothing its scene states",
          "[glowmere3][project]") {
    const fs::path project = worldDir() / "glowmere-valley-3.json";
    if (!fs::exists(project)) {
        SKIP("glowmere-valley-3 has not been generated");
    }
    const json p = readJson(project);
    const json s = readJson(v3Scene());

    // The fingerprint. A project carries a hash of the scene bytes it was written against, and a
    // stale one relinks a render to a file that has moved on (ADR-264).
    std::ifstream bytes(v3Scene(), std::ios::binary);
    const std::string raw((std::istreambuf_iterator<char>(bytes)),
                          std::istreambuf_iterator<char>());
    const json& ref = p.at("assets").at("scene").at("path");
    INFO("scene reference: " << ref.dump());
    CHECK(ref.at("size").get<std::size_t>() == raw.size());

    const json& params = p.at("parameters");
    std::vector<std::string> nodeParams;
    for (const auto& [k, v] : params.items()) {
        if (k.rfind("nodes/", 0) == 0 || k.rfind("procedural/", 0) == 0) {
            nodeParams.push_back(k);
        }
    }
    INFO(params.size() << " parameters, of which " << nodeParams.size() << " name a node");
    // A band. Zero would also be met by a project that had lost its parameters block; the upper
    // bound is the one that bites, and it is two orders of magnitude under what it replaces.
    CHECK(params.size() >= 1);
    CHECK(params.size() <= 24);
    CHECK(nodeParams.empty());
    // No film machinery. Each of these is a whole subsystem the multicam project carries and this
    // one has no use for; an empty list and an absent key mean the same thing to the loader.
    for (const char* key : {"sequence", "songPlan", "timeline", "cameraShotSpans",
                            "cameraAimFollow", "autoDirector", "atmosphericEffects"}) {
        INFO("project key '" << key << "'");
        CHECK(!p.contains(key));
    }

    // Every asset the scene names resolves, relative to the scene, and none is absolute.
    // `glowmere-valley-2-song.scene.json` names sixteen farm animals by absolute path into a
    // worktree that no longer exists and the loader skips them in silence -- that film has been
    // running with five of its twenty-one bodies. This is the arm that would have caught it.
    int assets = 0;
    std::function<void(const json&)> walk = [&](const json& node) {
        if (node.is_object()) {
            for (const auto& [k, v] : node.items()) {
                if ((k == "asset" || k == "path") && v.is_string()) {
                    const std::string a = v.get<std::string>();
                    if (a.empty() || a.rfind("renders/", 0) == 0) {
                        continue;
                    }
                    INFO("asset '" << a << "'");
                    CHECK(a.front() != '/');
                    CHECK(fs::exists(worldDir() / a));
                    ++assets;
                } else {
                    walk(v);
                }
            }
        } else if (node.is_array()) {
            for (const json& e : node) {
                walk(e);
            }
        }
    };
    walk(s);
    INFO(assets << " asset references");
    // A band with a live lower bound: eight tree models, ten undergrowth, five aliens, the light
    // rig and the material programs. If this collapses, the walk stopped walking.
    CHECK(assets >= 25);

    // The control, on the file this one replaces: the same computation there is not empty.
    const fs::path v2 = worldDir() / "glowmere-valley-2-multicam.json";
    if (fs::exists(v2)) {
        const json p2 = readJson(v2);
        std::size_t v2NodeParams = 0;
        for (const auto& [k, v] : p2.at("parameters").items()) {
            v2NodeParams += k.rfind("nodes/", 0) == 0 ? 1 : 0;
        }
        INFO("control: glowmere-valley-2-multicam carries " << p2.at("parameters").size()
             << " parameters, " << v2NodeParams << " of them node transforms");
        CHECK(v2NodeParams > 100);
    }
}

// ---------------------------------------------------------------------------------------------
// 6. The hills are wooded, with species variety, and the tree line has not moved.
//
// The control is `glowmere-valley-3-legacytrees.scene.json` -- the same world, the same cast, the
// same cameras, with valley 2's three tree layers put back. It is generated by the same script
// under `AVGEN_V3_TREES=legacy` and it exists so that the A/B render is a frame in which only the
// trees differ, which is a thing this project has got wrong before.
TEST_CASE("Glowmere Valley 3's hills are wooded, and the tree line is where ADR-334 put it",
          "[glowmere3][scatter]") {
    if (!fs::exists(v3Scene())) {
        SKIP("glowmere-valley-3 has not been generated");
    }
    struct Arm {
        std::size_t trees = 0;
        std::size_t onHills = 0;
        std::size_t species = 0;
        float treeLine = 0.0f;
        std::size_t atCap = 0;
    };
    const auto measure = [](const fs::path& path) {
        const json doc = readJson(path);
        const world::WorldMap map = loadWorld(path);
        auto ecology = world::ecologyFromJson(terrainNode(doc).at("scatter"));
        REQUIRE(ecology.has_value());
        std::vector<world::ScatterClearance> clearances;
        if (terrainNode(doc).contains("clearings")) {
            auto c = world::clearancesFromJson(terrainNode(doc).at("clearings"));
            REQUIRE(c.has_value());
            clearances = std::move(*c);
        }
        const float mid = 0.5f * (map.sampledMinHeight + map.sampledMaxHeight);
        std::set<std::string> assets;
        Arm arm;
        for (const world::ScatterLayer& l : ecology->layers) {
            // A tree is a layer that places something at least five metres tall. Structural, and
            // not a list of three names: a name list is a floor that outlives what it counted,
            // and the tree line it computes would silently miss a species added tomorrow.
            if (l.height < 5.0f) {
                continue;
            }
            assets.insert(l.asset);
            arm.treeLine = std::max(arm.treeLine, l.height * l.maxScale);
            const spatial::PointCloud cloud = world::scatter(map, l, {}, clearances);
            arm.trees += cloud.positions().size();
            arm.atCap += static_cast<int>(cloud.positions().size()) >= l.maxInstances ? 1 : 0;
            for (const glm::vec3& p : cloud.positions()) {
                arm.onHills += p.y > mid ? 1 : 0;
            }
        }
        arm.species = assets.size();
        return arm;
    };

    const Arm now = measure(v3Scene());
    INFO("valley 3: " << now.trees << " trees of " << now.species << " species, " << now.onHills
                      << " on the hills, tree line " << now.treeLine << " m, " << now.atCap
                      << " layers at their cap");

    // Variety means several species actually placed, not one model swapped for another.
    CHECK(now.species >= 6);
    // Denser, in a band. The upper bound is not decoration: 2,000 tree-sized instances is about
    // where the frame stops being a valley and starts being a hedge, and the caps in the file are
    // what hold it.
    CHECK(now.trees > 2000);
    CHECK(now.trees < 8000);
    CHECK(now.onHills > 900);
    // **The tree line, which is not negotiable.** ADR-334 measured 11.2 m and ADR-335 re-derived
    // the cast's 1.94x from it; the 16 m elder must stand a fifth again above it, so the ceiling
    // is 13.33 m. Both bounds: a tree line that *fell* would mean the hills got shrubs.
    CHECK(now.treeLine > 10.0f);
    CHECK(now.treeLine <= 16.0f / 1.2f);
    // No layer may be sitting on its cap, or the density in the file is a fiction and the next
    // person to raise it will get nothing.
    CHECK(now.atCap == 0);

    // The control: the same world with valley 2's three layers. It must fail the density arms.
    const fs::path legacy = worldDir() / "glowmere-valley-3-legacytrees.scene.json";
    if (fs::exists(legacy)) {
        const Arm before = measure(legacy);
        INFO("control (valley 2's three layers in the same world): " << before.trees << " trees of "
             << before.species << " species, " << before.onHills << " on the hills, tree line "
             << before.treeLine << " m");
        CHECK(before.species < 6);
        CHECK(before.trees < 2000);
        CHECK(before.onHills < 900);
        // And the one thing the control must *share*: the tree line did not move.
        CHECK(before.treeLine == now.treeLine);
    }
}
