// ADR-1950: Musical Geography's ground. The deterministic properties the brief's §13 asks of the terrain, on the CPU
// half of the one rule (scene/tile_terrain.cpp); test_tile_terrain_gpu holds the GPU half to it.

#include "scene/flight.hpp"
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
    s.rivers = {scene::TileRiver{.name = "river", .points = {{-300.0f, 260.0f}, {-120.0f, 160.0f}, {40.0f, 200.0f},
                                                      {260.0f, 120.0f}}}};
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

// ADR-1995: the morph replaced the seam band (ADR-1976 amended), which counted cells from the snapped window edge, so a
// tile's weight jumped each time the window snapped. The morph is a function of the camera.
TEST_CASE("tiles: a ring morphs into its coarser ring by the camera's distance, never by the window",
          "[geography][tiles][adr1995]") {
    scene::GeneratorSpec fine;
    fine.name = scene::kGeneratorTiles;
    fine.tiles = withPath();
    fine.cellSize = 1.0f;
    fine.viewDistance = 40.0f;
    fine.align = 4;
    fine.morphWidth = 8.0f;
    fine.morphRatio = 2;
    REQUIRE(fine.validate().has_value());
    scene::GeneratorSpec coarse = fine;
    coarse.cellSize = 2.0f;
    coarse.finePitch = 1.0f;
    coarse.morphWidth = 0.0f;
    // Every window the camera snaps to: its edge tiles are fully the coarser cell, its deep tiles untouched.
    for (const glm::vec3 cam : {glm::vec3(-317.2f, 30.0f, 88.9f), glm::vec3(0.0f), glm::vec3(12.99f, 5.0f, -3.01f)}) {
        const scene::GeneratorWindow w = scene::generatorWindow(fine, cam);
        int edgeChecked = 0;
        for (std::int32_t z = 0; z < w.countZ; ++z) {
            for (std::int32_t x = 0; x < w.countX; ++x) {
                const std::int32_t ix = w.originX + x;
                const std::int32_t iz = w.originZ + z;
                const scene::TileSample t = scene::generatorTileMorphed(fine, ix, iz, cam);
                if (x == 0 || z == 0 || x == w.countX - 1 || z == w.countZ - 1) {
                    CHECK(scene::generatorTileMorph(fine, ix, iz, cam) == 1.0f);
                    const scene::TileSample block =
                        scene::generatorTile(coarse, scene::generatorFloorDiv(ix, 2), scene::generatorFloorDiv(iz, 2));
                    CHECK(t.height == Catch::Approx(block.height).margin(1e-4));
                    CHECK(t.water == Catch::Approx(block.water).margin(1e-5));
                    ++edgeChecked;
                }
                const float cx = (static_cast<float>(ix) + 0.5f) - cam.x;
                const float cz = (static_cast<float>(iz) + 0.5f) - cam.z;
                if (std::max(std::abs(cx), std::abs(cz)) < 40.0f - 0.5f - 8.0f) {
                    CHECK(t.height == scene::generatorTile(fine, ix, iz).height);
                }
            }
        }
        CHECK(edgeChecked > 100);
    }
    // Continuity: a tile followed while the camera slides 20 m in 2 cm steps (its window snaps five times) never
    // moves more than the ground's own slope over the slide allows -- no step at a snap.
    const std::int32_t ix = 50; // 50.5 m out: the slide takes it from 50.5 to 30.5 m, through the 31.5..39.5 m band
    const std::int32_t iz = -6;
    float worstStep = 0.0f;
    float lo = 1e9f;
    float hi = -1e9f;
    float prev = scene::generatorTileMorphed(fine, ix, iz, glm::vec3(0.0f, 10.0f, 0.0f)).height;
    for (int i = 1; i <= 1000; ++i) {
        const glm::vec3 cam(0.02f * static_cast<float>(i), 10.0f, 0.0f);
        const float h = scene::generatorTileMorphed(fine, ix, iz, cam).height;
        worstStep = std::max(worstStep, std::abs(h - prev));
        lo = std::min(lo, scene::generatorTileMorph(fine, ix, iz, cam));
        hi = std::max(hi, scene::generatorTileMorph(fine, ix, iz, cam));
        prev = h;
    }
    const float span = std::abs(scene::generatorTile(fine, ix, iz).height -
                                scene::generatorTile(coarse, scene::generatorFloorDiv(ix, 2),
                                                     scene::generatorFloorDiv(iz, 2)).height);
    CAPTURE(worstStep, lo, hi, span);
    CHECK(lo == 0.0f); // the control: the slide carries the tile through the whole band
    CHECK(hi == 1.0f);
    CHECK(worstStep <= 0.01f * std::max(span, 0.05f)); // 2 cm of an 8 m band: ~0.4% of the span at the steepest
}

TEST_CASE("tiles: the morph reads and writes as JSON, and the seam band is refused", "[geography][tiles][json][adr1995]") {
    const auto parse = [](const char* gen) {
        const std::string text = std::string(R"({"name": "ground", "slabs": true,
            "source": {"kind": "box", "size": [0.92, 0.18, 0.92]},
            "distribution": {"kind": "generator", "generator": {"name": "tiles", "version": 1, "cellSize": 4.0,
                "viewDistance": 300.0, "align": 4, "finePitch": 1.0, )") + gen + "}}}";
        return scene::ProceduralGeometry::fromJson(nlohmann::json::parse(text));
    };
    auto g = parse(R"("morph": {"width": 64, "ratio": 4, "pitch": 4})");
    REQUIRE(g.has_value());
    const scene::GeneratorSpec& gs = g->distribution.generator;
    CHECK(gs.morphWidth == 64.0f);
    CHECK(gs.morphRatio == 4);
    CHECK(gs.morphPitch == 4.0f);
    auto back = scene::ProceduralGeometry::fromJson(g->toJson());
    REQUIRE(back.has_value());
    CHECK(back->distribution.generator.hash() == gs.hash());
    CHECK_FALSE(parse(R"("blend": {"cells": 4, "ratio": 2})").has_value());     // ADR-441: gone, not ignored
    CHECK_FALSE(parse(R"("morph": {"width": 64, "ratio": 3})").has_value());    // a block must fit the workgroup
    CHECK_FALSE(parse(R"("morph": {"width": 64, "ratio": 8})").has_value());    // align 4 is not a multiple of 8
    CHECK_FALSE(parse(R"("morph": {"width": 64, "pitch": 3})").has_value());    // 4 m is not whole 3 m samples
    CHECK_FALSE(parse(R"("morph": {"width": 400})").has_value());               // wider than the ring
    CHECK_FALSE(parse(R"("morph": {"width": 64, "cells": 2})").has_value());    // an unknown key
    CHECK_FALSE(parse(R"("morph": {"width": 64}, "childrenMin": true)").has_value()); // an underlay does not morph
}

// ADR-1995: every ring of one ground measures the far-tile rule at the lattice's pitch, so a swap shades alike.
TEST_CASE("tiles: the slab far-tile rule measures the lattice pitch in every ring", "[geography][tiles][adr1995]") {
    scene::ProceduralGeometry fine;
    fine.distribution.kind = scene::DistributionKind::Generator;
    fine.distribution.generator.name = scene::kGeneratorTiles;
    fine.distribution.generator.cellSize = 4.0f;
    fine.source.size = {3.66f, 1.2f, 3.66f};
    scene::ProceduralGeometry coarse = fine;
    coarse.distribution.generator.cellSize = 8.0f;
    coarse.distribution.generator.finePitch = 4.0f;
    coarse.source.size = {8.0f, 1.44f, 8.0f};
    // a 4 m span measured in both: the fine box x 4/3.66, the 8 m box x 1/2
    CHECK(scene::slabRuleScale(fine) * 3.66f == Catch::Approx(4.0f));
    CHECK(scene::slabRuleScale(coarse) * 8.0f == Catch::Approx(4.0f));
    scene::ProceduralGeometry other;
    CHECK(scene::slabRuleScale(other) == 1.0f);
}

// ADR-1995: a contour side eases in and out as a level crosses either end of the step, instead of switching.
TEST_CASE("tiles: a contour side's strength is continuous as a level crosses the step", "[geography][tiles][adr1995]") {
    // A 2 m step (own 5, neighbour 3), interval 4: a level sweeping up through it by its offset.
    float prev = scene::tileContourStrength(5.0f, 3.0f, 4.0f, 2.9f, 0.15f);
    float worst = 0.0f;
    float peak = 0.0f;
    for (int i = 1; i <= 2200; ++i) {
        const float offset = 2.9f + 0.001f * static_cast<float>(i); // 2.9 .. 5.1: the level enters, crosses, leaves
        const float s = scene::tileContourStrength(5.0f, 3.0f, 4.0f, offset, 0.15f);
        // the strength only matters where the level is inside the step (the binary mask's edge); there it is smooth
        const bool inside = offset > 3.0f && offset <= 5.0f;
        if (inside) {
            worst = std::max(worst, std::abs(s - prev));
            peak = std::max(peak, s);
        }
        prev = s;
    }
    CAPTURE(worst, peak);
    CHECK(peak == 1.0f);
    CHECK(worst < 0.02f); // 1 mm of level per step: no switch
    // at the step's ends it is 0, in the middle 1; fade 0 is the old binary edge
    CHECK(scene::tileContourStrength(5.0f, 3.0f, 4.0f, 3.0f + 1e-4f, 0.15f) < 0.01f);
    CHECK(scene::tileContourStrength(5.0f, 3.0f, 4.0f, 5.0f, 0.15f) == 0.0f);
    CHECK(scene::tileContourStrength(5.0f, 3.0f, 4.0f, 4.0f, 0.15f) == 1.0f);
    CHECK(scene::tileContourStrength(5.0f, 3.0f, 4.0f, 3.01f, 0.0f) == 1.0f);
    // a step spanning several levels keeps the one nearest its middle: full strength
    CHECK(scene::tileContourStrength(20.0f, 3.0f, 4.0f, 0.0f, 0.15f) == 1.0f);
    // the packing: 6 bits a side, dimming in 63rds, exact in a float
    CHECK(scene::tilePackedSides({1.0f, 1.0f, 1.0f, 1.0f}) == 0.0f);
    CHECK(scene::tilePackedSides({0.0f, 0.0f, 0.0f, 0.0f}) == 16777215.0f);
    CHECK(scene::tilePackedSides({1.0f, 0.0f, 1.0f, 1.0f}) == 63.0f * 64.0f);
}

TEST_CASE("tiles: the river valley -- water on the line, smooth banks, no walls", "[geography][tiles][adr1950]") {
    const TileTerrainSpec s = withPath();
    int onLand = 0;
    for (int i = 0; i <= 100; ++i) {
        // along the river's second segment
        const glm::vec2 a = s.rivers[0].points[1];
        const glm::vec2 b = s.rivers[0].points[2];
        const glm::vec2 p = a + (b - a) * (static_cast<float>(i) / 100.0f);
        TileTerrainSpec dry = s;
        dry.rivers.clear();
        const TileSample without = scene::tileSample(dry, p.x, p.y);
        if (without.water > 0.01f) {
            continue; // the sea is already water
        }
        ++onLand;
        const TileSample t = scene::tileSample(s, p.x, p.y);
        CHECK(t.water > 0.99f);                                       // the channel is water
        CHECK(t.height == Catch::Approx(s.seaLevel - s.seaDepth).margin(1e-3)); // at the bed
    }
    CHECK(onLand > 20);
    // Far from the river the land is untouched.
    TileTerrainSpec dry = s;
    dry.rivers.clear();
    CHECK(scene::tileSample(s, 600.5f, -700.5f).height == scene::tileSample(dry, 600.5f, -700.5f).height);
}

TEST_CASE("tiles: heightScale scales the land's relief, not the sea", "[geography][tiles][adr1950]") {
    TileTerrainSpec s;
    TileTerrainSpec tall = s;
    tall.heightScale = 2.0f;
    int checked = 0;
    for (int i = 0; i < 400; ++i) {
        const float x = static_cast<float>(i) * 13.0f - 2600.0f + 0.5f;
        const float z = static_cast<float>(i) * 7.0f - 1400.0f + 0.5f;
        const TileSample a = scene::tileSample(s, x, z);
        const TileSample b = scene::tileSample(tall, x, z);
        if (a.water > 0.999f) {
            CHECK(b.height == a.height);
        } else if (a.water < 0.001f && a.height > s.shoreRise + 1.0f) {
            // land above the shore: the relief above shoreRise doubles
            CHECK(b.height - s.shoreRise == Catch::Approx(2.0f * (a.height - s.shoreRise)).epsilon(1e-3));
            ++checked;
        }
    }
    CHECK(checked > 20);
}

// ---- ADR-1965: rivers, ponds, dry valleys and the water body's class ------------------------------------------------

namespace {

// The highest land point within 160 m of the origin (a 4 m scan), and its height.
std::pair<glm::vec2, float> highLand(const TileTerrainSpec& s) {
    glm::vec2 p(0.0f);
    float best = -1e9f;
    for (float z = -160.0f; z <= 160.0f; z += 4.0f) {
        for (float x = -160.0f; x <= 160.0f; x += 4.0f) {
            const TileSample t = scene::tileSample(s, x, z);
            if (t.water < 0.01f && t.height > best) {
                best = t.height;
                p = {x, z};
            }
        }
    }
    return {p, best};
}

TileTerrainSpec landWorld() {
    TileTerrainSpec s;
    s.seed = 11;
    s.continentWavelength = 260.0f;
    return s;
}

} // namespace

TEST_CASE("tiles: an upland stream runs at its own level, easing from the source down to the sea floor at its mouth",
          "[geography][tiles][adr1965]") {
    TileTerrainSpec s = landWorld();
    const auto [p, top] = highLand(s);
    REQUIRE(top > s.seaLevel + s.shoreRise + 2.0f);
    // from the high ground out, 200 m, and on to wherever it ends (its mouth level is the sea floor's, whatever the
    // land there is: the eased bed cuts through)
    const glm::vec2 a = p;
    const glm::vec2 b = p + glm::vec2(120.0f, 40.0f);
    const glm::vec2 c = p + glm::vec2(220.0f, 120.0f);
    s.rivers = {scene::TileRiver{.name = "stream", .points = {a, b, c}, .width = 4.0f, .valleyWidth = 20.0f,
                                 .bedDepth = 1.5f}};
    REQUIRE(s.validate().has_value());
    s.resolveWater();
    TileTerrainSpec dry = s;
    dry.rivers.clear();
    // the source: the land there (without the river) less the bed depth -- "auto"
    const float source = scene::tileSample(dry, a.x, a.y).height - 1.5f;
    CHECK(scene::tileRiverSourceLevel(s, 0) == Catch::Approx(source).margin(1e-4));
    CHECK(scene::tileRiverMouthLevel(s) == s.seaLevel - s.seaDepth);
    const TileSample atSource = scene::tileSample(s, a.x, a.y);
    CHECK(atSource.river > 0.99f);
    CHECK(atSource.height == Catch::Approx(source).margin(1e-3));
    CHECK(scene::tileWaterClass(atSource) == 1);
    // down the line the water's level falls monotonically (where the stream is on land)
    float last = 1e9f;
    int checked = 0;
    for (int i = 0; i <= 50; ++i) {
        const glm::vec2 q = a + (b - a) * (static_cast<float>(i) / 50.0f);
        const TileSample t = scene::tileSample(s, q.x, q.y);
        if (t.river < 0.99f) continue; // out in the sea
        CHECK(t.height <= last + 1e-4f);
        last = t.height;
        ++checked;
    }
    CHECK(checked > 8);
    // a set source level is honoured
    s.rivers[0].sourceLevel = top + 5.0f;
    s.resolveWater();
    CHECK(scene::tileSample(s, a.x, a.y).height == Catch::Approx(top + 5.0f).margin(1e-3));
    // the default (bedDepth < 0) is the CP-A river: at the sea floor all along
    s.rivers[0].bedDepth = -1.0f;
    s.rivers[0].sourceLevel.reset();
    s.resolveWater();
    CHECK(scene::tileSample(s, a.x, a.y).height == Catch::Approx(s.seaLevel - s.seaDepth).margin(1e-3));
}

TEST_CASE("tiles: a pond sits in a basin at its level -- auto is the rim's lowest land less a metre",
          "[geography][tiles][adr1965]") {
    TileTerrainSpec s = landWorld();
    const auto [p, top] = highLand(s);
    s.ponds = {scene::TilePond{.name = "tarn", .centre = p, .radius = 6.0f, .basinWidth = 8.0f}};
    REQUIRE(s.validate().has_value());
    s.resolveWater();
    TileTerrainSpec dry = s;
    dry.ponds.clear();
    float rim = 1e9f;
    for (int k = 0; k < 32; ++k) {
        const float ang = 6.28318530718f * static_cast<float>(k) / 32.0f;
        rim = std::min(rim, scene::tileSample(dry, p.x + 14.0f * std::cos(ang), p.y + 14.0f * std::sin(ang)).height);
    }
    CHECK(scene::tilePondLevel(s, 0) == Catch::Approx(rim - 1.0f).margin(1e-3));
    CHECK(scene::tilePondLevel(s, 0) > s.seaLevel); // up on the land, above the sea
    const TileSample centre = scene::tileSample(s, p.x, p.y);
    CHECK(centre.pond > 0.99f);
    CHECK(centre.water > 0.99f);
    CHECK(scene::tileWaterClass(centre) == 2);
    CHECK(centre.height == Catch::Approx(rim - 1.0f).margin(1e-3));
    // beyond the basin the land is untouched; within it, it eases (no wall: a 1 m step bounds the slope)
    const glm::vec2 far = p + glm::vec2(6.0f + 8.0f + 1.0f, 0.0f);
    CHECK(scene::tileSample(s, far.x, far.y).height == scene::tileSample(dry, far.x, far.y).height);
    float worstStep = 0.0f;
    for (float r = 0.0f; r < 16.0f; r += 0.5f) {
        const float h0 = scene::tileSample(s, p.x + r, p.y).height;
        const float h1 = scene::tileSample(s, p.x + r + 0.5f, p.y).height;
        worstStep = std::max(worstStep, std::abs(h1 - h0));
    }
    CAPTURE(worstStep);
    CHECK(worstStep < 1.5f);
    // a set level, and a noisy shore that still makes water at the centre
    s.ponds[0].level = top - 3.0f;
    s.ponds[0].shoreNoise = 0.6f;
    s.resolveWater();
    CHECK(scene::tileSample(s, p.x, p.y).height == Catch::Approx(top - 3.0f).margin(1e-3));
}

TEST_CASE("tiles: a dry valley sinks the land along its line, by depth or a fraction of the relief, with no water",
          "[geography][tiles][adr1965]") {
    TileTerrainSpec s = landWorld();
    const auto [p, top] = highLand(s);
    const glm::vec2 a = p + glm::vec2(-30.0f, 0.0f);
    const glm::vec2 b = p + glm::vec2(30.0f, 0.0f);
    s.valleys = {scene::TileValley{.name = "dale", .points = {a, b}, .valleyWidth = 20.0f, .depth = 2.0f}};
    REQUIRE(s.validate().has_value());
    TileTerrainSpec dry = s;
    dry.valleys.clear();
    const TileSample on = scene::tileSample(s, p.x, p.y);
    const TileSample was = scene::tileSample(dry, p.x, p.y);
    CHECK(on.height == Catch::Approx(was.height - 2.0f).margin(1e-3));
    CHECK(on.water == was.water); // dry
    CHECK(on.river == 0.0f);
    CHECK(on.pond == 0.0f);
    // half the relief above the shore
    s.valleys[0].depthFraction = 0.5f;
    const float relief = was.height - (s.seaLevel + s.shoreRise);
    CHECK(scene::tileSample(s, p.x, p.y).height == Catch::Approx(was.height - 0.5f * relief).margin(1e-3));
    // never below the shore's top, however deep
    s.valleys[0].depthFraction = 0.0f;
    s.valleys[0].depth = 500.0f;
    CHECK(scene::tileSample(s, p.x, p.y).height >= s.seaLevel + s.shoreRise - 1e-3f);
    // 11 m off the line (beyond half its width) the land is untouched
    CHECK(scene::tileSample(s, p.x, p.y + 11.0f).height == scene::tileSample(dry, p.x, p.y + 11.0f).height);
}

TEST_CASE("tiles: water bodies read, write and refuse", "[geography][tiles][adr1965]") {
    // ADR-441: the CP-A single `river` key is not read (no alias); it is refused like any unknown key
    CHECK_FALSE(scene::tileTerrainFromJson(nlohmann::json::parse(R"({"river": [[0, 0], [50, 10], [90, 40]]})"))
                    .has_value());
    CHECK_FALSE(scene::tileTerrainFromJson(nlohmann::json::parse(R"({"riverWidth": 6})")).has_value());
    // a round trip
    auto spec = scene::tileTerrainFromJson(nlohmann::json::parse(R"({
        "rivers": [{"name": "brook", "points": [[0, 0], [40, 10]], "width": 3, "valleyWidth": 14, "bedDepth": 1,
                    "sourceLevel": "auto"},
                   {"points": [[100, 0], [140, 30], [160, 90]], "sourceLevel": 12, "bedDepth": 0.5}],
        "ponds": [{"name": "tarn", "centre": [20, -40], "radius": 6, "basinWidth": 9, "level": "auto",
                   "shoreNoise": 0.3},
                  {"centre": [-60, 10], "radius": 4, "basinWidth": 5, "level": 3.5}],
        "valleys": [{"name": "dale", "points": [[-10, -80], [60, -100]], "valleyWidth": 30, "depthFraction": 0.4},
                    {"points": [[0, 90], [30, 120]], "valleyWidth": 12, "depth": 3}]})"));
    REQUIRE(spec.has_value());
    CHECK(spec->riverIndex("brook") == 0);
    CHECK(spec->riverIndex("river1") == 1);
    CHECK(spec->riverIndex("1") == 1);
    CHECK(spec->riverIndex("river2") == -1);
    CHECK(spec->riverIndex("river99999999999") == -1);
    CHECK_FALSE(spec->rivers[0].sourceLevel.has_value());
    CHECK(spec->rivers[1].sourceLevel == 12.0f);
    CHECK_FALSE(spec->ponds[0].level.has_value());
    CHECK(spec->ponds[1].level == 3.5f);
    auto again = scene::tileTerrainFromJson(scene::tileTerrainToJson(*spec));
    REQUIRE(again.has_value());
    CHECK(scene::tileTerrainToJson(*again) == scene::tileTerrainToJson(*spec));
    CHECK(again->hash() == spec->hash());
    for (const glm::vec2 q : {glm::vec2(10.5f, 2.5f), glm::vec2(20.5f, -40.5f), glm::vec2(20.5f, -90.5f)}) {
        CHECK(scene::tileSample(*again, q.x, q.y).height == scene::tileSample(*spec, q.x, q.y).height);
    }
    // the caps
    TileTerrainSpec many;
    for (int i = 0; i < 5; ++i) many.rivers.push_back({.points = {{0.0f, 0.0f}, {10.0f, 0.0f}}});
    CHECK_FALSE(many.validate().has_value());
    TileTerrainSpec longRiver;
    longRiver.rivers.push_back({});
    for (int i = 0; i < 33; ++i) longRiver.rivers[0].points.push_back({static_cast<float>(i), 0.0f});
    CHECK_FALSE(longRiver.validate().has_value());
    TileTerrainSpec pool;
    for (int r = 0; r < 3; ++r) {
        pool.rivers.push_back({});
        for (int i = 0; i < 30; ++i) pool.rivers.back().points.push_back({static_cast<float>(i), 10.0f * r});
    }
    CHECK_FALSE(pool.validate().has_value()); // 90 points against a pool of 64
}

TEST_CASE("tiles: the waves weigh each water body, and a river drifts downstream", "[geography][tiles][adr1965]") {
    scene::TileDynamics d;
    scene::TileWave w;
    w.amplitude = 1.0f;
    w.wavelength = 40.0f;
    w.speed = 0.0f;
    w.onLand = 0.0f;
    w.onWater = 1.0f;
    d.waves = {w};
    d.riverWaves = 0.25f;
    d.pondWaves = 0.5f;
    TileSample sea;
    sea.water = 1.0f;
    TileSample river = sea;
    river.river = 1.0f;
    TileSample pond = sea;
    pond.pond = 1.0f;
    for (const float x : {3.0f, 9.0f, 17.0f}) {
        const float hs = scene::tileMotion(d, {}, x, 0.0f, 0.0f, sea, 1.0f).x;
        CHECK(scene::tileMotion(d, {}, x, 0.0f, 0.0f, river, 1.0f).x == Catch::Approx(0.25f * hs).margin(1e-5));
        CHECK(scene::tileMotion(d, {}, x, 0.0f, 0.0f, pond, 1.0f).x == Catch::Approx(0.5f * hs).margin(1e-5));
    }
    // the drift: a travelling ripple along the river's arc length, only on river water
    scene::TileDynamics drift;
    drift.riverDriftAmplitude = 0.3f;
    drift.riverDriftWavelength = 10.0f;
    drift.riverDriftSpeed = 2.0f;
    CHECK(drift.active());
    REQUIRE(scene::validateTileDynamics(drift).has_value());
    river.riverS = 4.0f;
    TileSample downstream = river;
    downstream.riverS = 4.0f + 2.0f * 1.5f; // where the same crest is 1.5 s later
    CHECK(scene::tileMotion(drift, {}, 0.0f, 0.0f, 1.5f, downstream, 1.0f).x ==
          Catch::Approx(scene::tileMotion(drift, {}, 0.0f, 0.0f, 0.0f, river, 1.0f).x).margin(1e-5));
    CHECK(scene::tileMotion(drift, {}, 0.0f, 0.0f, 0.7f, sea, 1.0f).x == 0.0f);
    // and it round-trips
    auto again = scene::tileDynamicsFromJson(scene::tileDynamicsToJson(drift));
    REQUIRE(again.has_value());
    CHECK(again->riverDriftAmplitude == 0.3f);
    CHECK(again->riverDriftSpeed == 2.0f);
    CHECK_FALSE(scene::tileDynamicsFromJson(nlohmann::json::parse(R"({"water": {"riverWaves": 1, "drift": 2}})"))
                    .has_value());
}

// ---- ADR-1992: ridges and rises, the raised land ----------------------------------------------------------------

namespace {

// The open-sea point nearest the origin (a 4 m scan): water, off the shore band.
glm::vec2 openSea(const TileTerrainSpec& s) {
    glm::vec2 best(1e9f);
    for (float z = -400.0f; z <= 400.0f; z += 4.0f) {
        for (float x = -400.0f; x <= 400.0f; x += 4.0f) {
            const TileSample t = scene::tileSample(s, x, z);
            if (!(t.water > 0.999f && t.shore < 0.01f && glm::length(glm::vec2(x, z)) < glm::length(best))) continue;
            bool open = true; // the whole 40 m around it is sea too
            for (int k = 0; k < 16 && open; ++k) {
                const float a = 6.2831853f * static_cast<float>(k) / 16.0f;
                for (const float r : {20.0f, 40.0f}) {
                    open = open && scene::tileSample(s, x + r * std::cos(a), z + r * std::sin(a)).water > 0.999f;
                }
            }
            if (open) best = {x, z};
        }
    }
    return best;
}

} // namespace

TEST_CASE("tiles: a rise in the sea is an island, its height added to the land, its edge soft",
          "[geography][tiles][adr1992]") {
    TileTerrainSpec s = landWorld();
    const glm::vec2 sea = openSea(s);
    REQUIRE(scene::tileSample(s, sea.x, sea.y).water > 0.999f);
    TileTerrainSpec isle = s;
    isle.rises = {scene::TileRise{.name = "isle", .centre = sea, .radius = 12.0f, .height = 18.0f, .edge = 20.0f}};
    REQUIRE(isle.validate().has_value());
    const TileSample top = scene::tileSample(isle, sea.x, sea.y);
    CHECK(top.water < 1e-3f); // land
    CHECK(top.height > s.seaLevel + 15.0f);
    // ADR-1993: the island is land with a coast of its own, so the continent's hills stand on it too (its inland); the
    // rise's own profile still eases out to nothing at radius + edge
    CHECK(top.height >= scene::tileSample(s, sea.x, sea.y).height + 18.0f - 1e-3f);
    // beyond radius + edge the world is untouched
    const glm::vec2 far = sea + glm::vec2(33.0f, 0.0f);
    CHECK(scene::tileSample(isle, far.x, far.y).height == scene::tileSample(s, far.x, far.y).height);
    CHECK(scene::tileSample(isle, far.x, far.y).water == scene::tileSample(s, far.x, far.y).water);
}

TEST_CASE("tiles: a ridge raises land along its line and makes land across the sea under its core",
          "[geography][tiles][adr1992]") {
    TileTerrainSpec s = landWorld();
    const auto [p, top] = highLand(s);
    const glm::vec2 sea = openSea(s);
    TileTerrainSpec r = s;
    r.ridges = {scene::TileRidge{.name = "spine", .points = {p, sea}, .height = 25.0f, .width = 40.0f}};
    REQUIRE(r.validate().has_value());
    // on land: the height adds, the water does not change
    const TileSample on = scene::tileSample(r, p.x, p.y);
    CHECK(on.height == Catch::Approx(top + 25.0f).margin(1e-3));
    // in the sea under the core: land
    const TileSample out = scene::tileSample(r, sea.x, sea.y);
    CHECK(out.water < 1e-3f);
    CHECK(out.height > s.seaLevel + 20.0f);
    // 21 m off the line (beyond half its width) nothing changes
    const glm::vec2 dir = glm::normalize(sea - p);
    const glm::vec2 side = 0.5f * (p + sea) + glm::vec2(-dir.y, dir.x) * 21.0f;
    CHECK(scene::tileSample(r, side.x, side.y).height == scene::tileSample(s, side.x, side.y).height);
}

TEST_CASE("tiles: raised land comes before the carving -- a valley cuts a ridge, a pond's auto level reads it",
          "[geography][tiles][adr1992]") {
    TileTerrainSpec s = landWorld();
    const auto [p, top] = highLand(s);
    s.ridges = {scene::TileRidge{.points = {p + glm::vec2(-60.0f, 0.0f), p + glm::vec2(60.0f, 0.0f)}, .height = 30.0f,
                                 .width = 60.0f}};
    TileTerrainSpec cut = s;
    cut.valleys = {scene::TileValley{.points = {p + glm::vec2(0.0f, -40.0f), p + glm::vec2(0.0f, 40.0f)},
                                     .valleyWidth = 16.0f, .depth = 10.0f}};
    CHECK(scene::tileSample(cut, p.x, p.y).height ==
          Catch::Approx(scene::tileSample(s, p.x, p.y).height - 10.0f).margin(1e-3));
    TileTerrainSpec tarn = s;
    tarn.ponds = {scene::TilePond{.centre = p, .radius = 6.0f, .basinWidth = 6.0f}};
    tarn.resolveWater();
    CHECK(scene::tilePondLevel(tarn, 0) > top + 10.0f); // up on the ridge, not on the land beneath it
}

TEST_CASE("tiles: ridges, rises and the larger valley budget read, write and refuse", "[geography][tiles][adr1992]") {
    auto spec = scene::tileTerrainFromJson(nlohmann::json::parse(R"({
        "ridges": [{"name": "spine", "points": [[0, 0], [100, 20], [180, 90]], "height": 30, "width": 80}],
        "rises": [{"name": "isle", "centre": [-300, 40], "radius": 25, "height": 14, "edge": 30}]})"));
    REQUIRE(spec.has_value());
    auto again = scene::tileTerrainFromJson(scene::tileTerrainToJson(*spec));
    REQUIRE(again.has_value());
    CHECK(scene::tileTerrainToJson(*again) == scene::tileTerrainToJson(*spec));
    CHECK(again->hash() == spec->hash());
    TileTerrainSpec moved = *spec;
    moved.rises[0].height = 15.0f;
    CHECK(moved.hash() != spec->hash());
    CHECK_FALSE(scene::tileTerrainFromJson(nlohmann::json::parse(R"({"rises": [{"centre": [0, 0], "hight": 3}]})"))
                    .has_value());
    // 8 valleys of 8 points (64) are allowed now; 9 valleys, or 65 points, are not
    TileTerrainSpec v;
    for (int i = 0; i < 8; ++i) {
        v.valleys.push_back({});
        for (int k = 0; k < 8; ++k) v.valleys.back().points.push_back({10.0f * k, 30.0f * i});
    }
    CHECK(v.validate().has_value());
    v.valleys.back().points.push_back({90.0f, 0.0f});
    CHECK_FALSE(v.validate().has_value());
    v.valleys.back().points.pop_back();
    v.valleys.push_back({.points = {{0.0f, 0.0f}, {1.0f, 0.0f}}});
    CHECK_FALSE(v.validate().has_value());
    // 9 ridges or rises are refused
    TileTerrainSpec many;
    for (int i = 0; i < 9; ++i) many.rises.push_back({});
    CHECK_FALSE(many.validate().has_value());
}

// ADR-1993: the coast follows the final land edge -- an island and a headland get a shore, inland and hills.
TEST_CASE("tiles: an island and a headland have a shore, and the flight snaps to it", "[geography][tiles][adr1993]") {
    TileTerrainSpec s = landWorld();
    const glm::vec2 sea = openSea(s);
    TileTerrainSpec isle = s;
    isle.rises = {scene::TileRise{.name = "isle", .centre = sea, .radius = 12.0f, .height = 18.0f, .edge = 20.0f}};
    // the coast is where the land ramp (smoothstep(0.01, 0.15, profile)) centres: profile 0.08, which is
    // radius + 0.81 x edge out from the centre
    const float coastR = 12.0f + 0.81f * 20.0f;
    CHECK(scene::tileSample(isle, sea.x + coastR, sea.y).shore > 0.95f);
    CHECK(scene::tileSample(isle, sea.x, sea.y).shore < 0.05f);       // the island's heart is no shore
    CHECK(scene::tileSample(s, sea.x + coastR, sea.y).shore < 0.3f);   // and little there before (open sea)
    // CPD-R3b: no cliff -- the steepest 1 m out to the sea floor is the profile's own slope (1.5 x height / edge),
    // within a margin for the land ramp at its foot, not the half-height wall R3 had
    float steepest = 0.0f;
    for (float r = 0.0f; r < 12.0f + 20.0f; r += 1.0f) {
        steepest = std::max(steepest, std::abs(scene::tileSample(isle, sea.x + r + 1.0f, sea.y).height -
                                               scene::tileSample(isle, sea.x + r, sea.y).height));
    }
    CAPTURE(steepest);
    CHECK(steepest < 1.25f * 1.5f * 18.0f / 20.0f);
    // the flight's shore snap finds the island's coast from a hint over its middle
    scene::FlightSpec f;
    f.enabled = true;
    scene::FlightWaypoint a;
    a.position = sea + glm::vec2(-200.0f, 0.0f);
    scene::FlightWaypoint snap;
    snap.snap = "shore";
    snap.position = sea + glm::vec2(3.0f, 0.0f);
    snap.snapRadius = 40.0f;
    f.route = {a, snap};
    auto r = scene::resolveFlightRoute(f, isle);
    REQUIRE(r.has_value());
    const glm::vec2 got = r->route.back().position;
    CAPTURE(got.x - sea.x, got.y - sea.y);
    CHECK(scene::tileSample(isle, got.x, got.y).shore > 0.9f);
    CHECK(std::abs(glm::length(got - sea) - coastR) < 6.0f);
    // a headland: a ridge out into the sea has a shore down both its flanks
    const auto [p, top] = highLand(s);
    TileTerrainSpec head = s;
    head.ridges = {scene::TileRidge{.points = {p, sea}, .height = 20.0f, .width = 40.0f}};
    const glm::vec2 dir = glm::normalize(sea - p);
    const glm::vec2 flank = sea - dir * 5.0f + glm::vec2(-dir.y, dir.x) * 16.2f; // profile 0.08 out from the spine
    CHECK(scene::tileSample(head, flank.x, flank.y).shore > 0.9f);
}

// ADR-1994 (CPD-R3c): a ridge's or rise's `noise` lays the hills' own shaped fBm on its surface.
TEST_CASE("tiles: noise on raised land is the hills' own relief, under the profile", "[geography][tiles][adr1994]") {
    TileTerrainSpec s = landWorld();
    const glm::vec2 sea = openSea(s);
    TileTerrainSpec smooth = s;
    smooth.rises = {scene::TileRise{.centre = sea, .radius = 30.0f, .height = 20.0f, .edge = 20.0f}};
    TileTerrainSpec rough = smooth;
    rough.rises[0].noise = 0.8f;
    REQUIRE(rough.validate().has_value());
    // inside the radius the smooth top is flat; the rough one varies by about noise x hillHeight x heightScale / 2
    float lo = 1e9f;
    float hi = -1e9f;
    float sum = 0.0f;
    int n = 0;
    for (float z = -20.0f; z <= 20.0f; z += 4.0f) {
        for (float x = -20.0f; x <= 20.0f; x += 4.0f) {
            const float d = scene::tileSample(rough, sea.x + x, sea.y + z).height -
                            scene::tileSample(smooth, sea.x + x, sea.y + z).height;
            lo = std::min(lo, d);
            hi = std::max(hi, d);
            sum += d;
            ++n;
        }
    }
    CAPTURE(lo, hi, sum / static_cast<float>(n));
    CHECK(hi - lo > 1.0f);
    CHECK(hi - lo < 0.8f * s.hillHeight * s.heightScale * 1.5f + 1e-3f);
    // beyond radius + edge, nothing; noise 0 is exactly the smooth rise
    const glm::vec2 far = sea + glm::vec2(51.0f, 0.0f);
    CHECK(scene::tileSample(rough, far.x, far.y).height == scene::tileSample(smooth, far.x, far.y).height);
    rough.rises[0].noise = 0.0f;
    CHECK(scene::tileSample(rough, sea.x + 3.0f, sea.y).height == scene::tileSample(smooth, sea.x + 3.0f, sea.y).height);
    // read and written, refused out of 0..1
    auto spec = scene::tileTerrainFromJson(nlohmann::json::parse(R"({
        "ridges": [{"points": [[0, 0], [50, 0]], "height": 10, "width": 30, "noise": 0.5}],
        "rises": [{"centre": [9, 9], "radius": 5, "height": 4, "edge": 6, "noise": 0.25}]})"));
    REQUIRE(spec.has_value());
    CHECK(spec->ridges[0].noise == 0.5f);
    CHECK(spec->rises[0].noise == 0.25f);
    auto again = scene::tileTerrainFromJson(scene::tileTerrainToJson(*spec));
    REQUIRE(again.has_value());
    CHECK(again->hash() == spec->hash());
    CHECK_FALSE(scene::tileTerrainFromJson(nlohmann::json::parse(
                    R"({"rises": [{"centre": [0, 0], "radius": 5, "height": 4, "edge": 6, "noise": 1.5}]})"))
                    .has_value());
}
