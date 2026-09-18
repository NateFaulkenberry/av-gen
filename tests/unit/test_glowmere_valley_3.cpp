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
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
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
#include <map>
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
