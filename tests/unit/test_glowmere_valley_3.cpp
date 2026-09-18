// Glowmere Valley 3: the autonomous-character showcase (ADR-338).
//
// This file starts as the instrument and becomes the guard. The probes print what the hills
// actually are -- which biome owns them, and at what slope -- because the vegetation question the
// owner asked ("more densely populated with variety of trees") turns on a structural fact and not
// on a density, and a density raised against a guess is a density raised for nothing.

#include "world/biome.hpp"
#include "world/ecology.hpp"
#include "spatial/point_cloud.hpp"
#include "world/world_map.hpp"

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
