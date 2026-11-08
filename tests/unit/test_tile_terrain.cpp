// ADR-1950: Musical Geography's ground. The deterministic properties the brief's §13 asks of the terrain, on the CPU
// half of the one rule (scene/tile_terrain.cpp); test_tile_terrain_gpu holds the GPU half to it.

#include "scene/generator.hpp"
#include "scene/procedural.hpp"
#include "scene/tile_terrain.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace avgen;
using scene::TileSample;
using scene::TileTerrainSpec;

namespace {

TileTerrainSpec withPath() {
    TileTerrainSpec s;
    s.path = {{-200.0f, -40.0f}, {-60.0f, 10.0f}, {80.0f, -15.0f}, {220.0f, 60.0f}};
    return s;
}

} // namespace

TEST_CASE("tiles: the same description gives the same ground", "[geography][tiles][adr1950]") {
    const TileTerrainSpec s = withPath();
    for (int i = -50; i < 50; i += 7) {
        for (int j = -50; j < 50; j += 5) {
            const float x = static_cast<float>(i) * 13.0f + 0.5f;
            const float z = static_cast<float>(j) * 11.0f + 0.5f;
            const TileSample a = scene::tileSample(s, x, z);
            const TileSample b = scene::tileSample(s, x, z);
            CHECK(a.height == b.height);
            CHECK(a.water == b.water);
            CHECK(a.path == b.path);
        }
    }
    TileTerrainSpec other = s;
    other.seed = 2;
    int differ = 0;
    for (int i = 0; i < 64; ++i) {
        differ += scene::tileSample(s, i * 37.0f, i * 19.0f).height != scene::tileSample(other, i * 37.0f, i * 19.0f).height;
    }
    CHECK(differ > 32); // a seed is a different world (sea tiles are flat in both, so not every sample differs)
}

TEST_CASE("tiles: every value is finite and every channel is in [0, 1]", "[geography][tiles][adr1950]") {
    const TileTerrainSpec s = withPath();
    for (int i = -300; i <= 300; i += 3) {
        for (int j = -300; j <= 300; j += 3) {
            const TileSample t = scene::tileSample(s, static_cast<float>(i) + 0.5f, static_cast<float>(j) + 0.5f);
            REQUIRE(std::isfinite(t.height));
            for (const float c : {t.water, t.shore, t.flat, t.path}) {
                REQUIRE(c >= 0.0f);
                REQUIRE(c <= 1.0f);
            }
            REQUIRE(t.height >= s.seaLevel - s.seaDepth - 1e-4f);
            REQUIRE(t.height <= s.seaLevel + s.shoreRise + s.hillHeight * (1.0f + s.regionVariation) + s.pathLift + 1e-3f);
        }
    }
}

TEST_CASE("tiles: rolling land -- no cliffs, and neighbours form a coherent surface", "[geography][tiles][adr1950]") {
    // The owner's decision: rolling hills, no cliffs. On a 1 m lattice over a 600 m window the steepest step between
    // two neighbouring tiles stays under 0.6 m, and 99% of them under 0.3 m (a thin slab's thickness, so no crack
    // opens under a tile at rest).
    const TileTerrainSpec s = withPath();
    std::vector<float> steps;
    float worst = 0.0f;
    const int n = 600;
    std::vector<float> h(static_cast<std::size_t>(n * n));
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            h[static_cast<std::size_t>(j * n + i)] =
                scene::tileHeightAt(s, 1.0f, static_cast<float>(i - n / 2) + 0.5f, static_cast<float>(j - n / 2) + 0.5f);
        }
    }
    for (int j = 0; j + 1 < n; ++j) {
        for (int i = 0; i + 1 < n; ++i) {
            const float c = h[static_cast<std::size_t>(j * n + i)];
            const float dx = std::abs(h[static_cast<std::size_t>(j * n + i + 1)] - c);
            const float dz = std::abs(h[static_cast<std::size_t>((j + 1) * n + i)] - c);
            steps.push_back(dx);
            steps.push_back(dz);
            worst = std::max({worst, dx, dz});
        }
    }
    std::sort(steps.begin(), steps.end());
    const float p99 = steps[steps.size() * 99 / 100];
    CAPTURE(worst, p99);
    CHECK(worst < 0.6f);
    CHECK(p99 < 0.3f);
    // ...and it is not flat: real relief across the window.
    const auto [lo, hi] = std::minmax_element(h.begin(), h.end());
    CHECK(*hi - *lo > 5.0f);
}

TEST_CASE("tiles: the formations coexist", "[geography][tiles][adr1950]") {
    // Over a wide sample, a default world holds sea, shore, land, crowns and (where it runs) the path.
    const TileTerrainSpec s = withPath();
    int sea = 0, shore = 0, land = 0, crown = 0, path = 0, total = 0;
    for (int i = -1500; i <= 1500; i += 10) {
        for (int j = -1500; j <= 1500; j += 10) {
            const TileSample t = scene::tileSample(s, static_cast<float>(i) + 0.5f, static_cast<float>(j) + 0.5f);
            ++total;
            sea += t.water > 0.99f;
            shore += t.shore > 0.5f;
            land += t.water < 0.01f;
            crown += t.flat > 0.5f;
        }
    }
    for (int i = -200; i <= 220; ++i) {
        // along the path's own polyline: where it crosses land it is lifted and flagged
        const float x = static_cast<float>(i) + 0.5f;
        const float z = -40.0f + (static_cast<float>(i) + 200.0f) * (50.0f / 140.0f);
        if (i <= -60) {
            const TileSample t = scene::tileSample(s, x, z);
            path += t.path > 0.5f;
        }
    }
    CAPTURE(total, sea, shore, land, crown, path);
    CHECK(sea > total / 20);
    CHECK(land > total / 4);
    CHECK(shore > 0);
    CHECK(crown > 0);
    CHECK(path > 0);
}

TEST_CASE("tiles: the tile under a point is the tile whose cell holds it", "[geography][tiles][adr1950]") {
    const TileTerrainSpec s;
    CHECK(scene::tileIndexAt(1.0f, 0.2f, -0.2f) == glm::ivec2(0, -1));
    CHECK(scene::tileIndexAt(2.0f, -3.9f, 3.9f) == glm::ivec2(-2, 1));
    CHECK(scene::tileCentre(2.0f, {-2, 1}) == glm::vec2(-3.0f, 3.0f));
    // Flat tops: every point of a tile reads the same height.
    const float a = scene::tileHeightAt(s, 1.0f, 10.01f, 20.01f);
    const float b = scene::tileHeightAt(s, 1.0f, 10.99f, 20.99f);
    CHECK(a == b);
    CHECK(a == scene::tileSample(s, 10.5f, 20.5f).height);
}

TEST_CASE("tiles: a coarse ring cell is the mean of the fine tiles it covers", "[geography][tiles][adr1976]") {
    scene::GeneratorSpec g;
    g.name = scene::kGeneratorTiles;
    g.tiles = withPath();
    g.cellSize = 4.0f;
    g.finePitch = 1.0f;
    REQUIRE(scene::generatorTileChildren(g) == 4);
    const TileSample coarse = scene::generatorTile(g, 7, -3);
    float sum = 0.0f;
    for (int b = 0; b < 4; ++b) {
        for (int a = 0; a < 4; ++a) {
            sum += scene::tileSample(g.tiles, 28.0f + a + 0.5f, -12.0f + b + 0.5f).height;
        }
    }
    CHECK(coarse.height == Catch::Approx(sum / 16.0f).margin(1e-4));
}

TEST_CASE("tiles: rings line up -- the hole is exactly the finer ring's window", "[geography][tiles][adr1976]") {
    scene::GeneratorSpec fine;
    fine.name = scene::kGeneratorTiles;
    fine.cellSize = 1.0f;
    fine.viewDistance = 100.0f;
    fine.align = 4;
    scene::GeneratorSpec coarse = fine;
    coarse.cellSize = 4.0f;
    coarse.finePitch = 1.0f;
    coarse.viewDistance = 400.0f;
    coarse.align = 1;
    coarse.holeCellSize = 1.0f;
    coarse.holeViewDistance = 100.0f;
    coarse.holeAlign = 4;
    REQUIRE(coarse.validate().has_value());
    for (const glm::vec3 cam : {glm::vec3(0.0f), glm::vec3(13.7f, 50.0f, -241.2f), glm::vec3(-1003.1f, 9.0f, 77.7f)}) {
        const scene::GeneratorWindow w = scene::generatorWindow(fine, cam);
        CHECK(w.originX % 4 == 0);
        CHECK(w.originZ % 4 == 0);
        CHECK(w.countX % 4 == 0);
        CHECK(w.countZ % 4 == 0);
        const scene::GeneratorTileHole hole = scene::generatorTileHole(coarse, cam);
        REQUIRE(hole.valid);
        // In metres the hole is the fine window, exactly.
        CHECK(static_cast<float>(hole.minX) * 4.0f == static_cast<float>(w.originX));
        CHECK(static_cast<float>(hole.minZ) * 4.0f == static_cast<float>(w.originZ));
        CHECK(static_cast<float>(hole.maxX + 1) * 4.0f == static_cast<float>(w.originX + w.countX));
        CHECK(static_cast<float>(hole.maxZ + 1) * 4.0f == static_cast<float>(w.originZ + w.countZ));
        // The window covers the camera's cell and the view distance.
        CHECK(static_cast<float>(w.originX) <= cam.x - 100.0f);
        CHECK(static_cast<float>(w.originX + w.countX) >= cam.x + 100.0f);
        // The capacity bounds every snapped window.
        CHECK(w.cells() <= scene::generatorCapacity(fine).cells());
    }
}

TEST_CASE("tiles: the generator and its terrain read and round-trip as JSON", "[geography][tiles][json][adr1950]") {
    const char* text = R"({
        "name": "ground",
        "slabs": true,
        "source": {"kind": "box", "size": [0.92, 0.18, 0.92]},
        "distribution": {"kind": "generator", "generator": {
            "name": "tiles", "version": 1, "cellSize": 2.0, "viewDistance": 300.0, "align": 1, "finePitch": 1.0,
            "hole": {"cellSize": 1.0, "viewDistance": 120.0, "align": 2},
            "tiles": {"seed": 7, "hillHeight": 12.0, "hillOctaves": 3, "path": [[0, 0], [50, 20]]}}}
    })";
    auto g = scene::ProceduralGeometry::fromJson(nlohmann::json::parse(text));
    REQUIRE(g.has_value());
    const scene::GeneratorSpec& gs = g->distribution.generator;
    CHECK(gs.isTiles());
    CHECK(gs.tiles.seed == 7u);
    CHECK(gs.tiles.hillHeight == 12.0f);
    CHECK(gs.tiles.hillOctaves == 3);
    REQUIRE(gs.tiles.path.size() == 2);
    CHECK(gs.holeAlign == 2);
    CHECK(gs.finePitch == 1.0f);
    auto back = scene::ProceduralGeometry::fromJson(g->toJson());
    REQUIRE(back.has_value());
    CHECK(back->distribution.generator.hash() == gs.hash());

    CHECK_FALSE(scene::tileTerrainFromJson(nlohmann::json::parse(R"({"canyonDepth": 4})")).has_value());
    CHECK_FALSE(scene::tileTerrainFromJson(nlohmann::json::parse(R"({"hillOctaves": 9})")).has_value());
}
