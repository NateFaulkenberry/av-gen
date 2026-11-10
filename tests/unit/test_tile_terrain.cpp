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

TEST_CASE("tiles: the seam band meets the coarser ring at its height", "[geography][tiles][adr1976]") {
    scene::GeneratorSpec fine;
    fine.name = scene::kGeneratorTiles;
    fine.tiles = withPath();
    fine.cellSize = 1.0f;
    fine.viewDistance = 40.0f;
    fine.align = 4;
    fine.blendCells = 6;
    fine.blendRatio = 2;
    REQUIRE(fine.validate().has_value());
    scene::GeneratorSpec coarse = fine;
    coarse.cellSize = 2.0f;
    coarse.finePitch = 1.0f;
    coarse.blendCells = 0;
    const scene::GeneratorWindow w = scene::generatorWindow(fine, glm::vec3(-317.2f, 30.0f, 88.9f));
    int edgeChecked = 0;
    for (std::int32_t z = 0; z < w.countZ; ++z) {
        for (std::int32_t x = 0; x < w.countX; ++x) {
            const std::int32_t ix = w.originX + x;
            const std::int32_t iz = w.originZ + z;
            const scene::TileSample t = scene::generatorTileInWindow(fine, ix, iz, w);
            const bool edge = x == 0 || z == 0 || x == w.countX - 1 || z == w.countZ - 1;
            const bool deep = x >= 6 && z >= 6 && x < w.countX - 6 && z < w.countZ - 6;
            if (edge) {
                // On the edge cell the fine tile stands exactly where the 2 m block it belongs to stands.
                const scene::TileSample block =
                    scene::generatorTile(coarse, scene::generatorFloorDiv(ix, 2), scene::generatorFloorDiv(iz, 2));
                CHECK(t.height == Catch::Approx(block.height).margin(1e-4));
                ++edgeChecked;
            }
            if (deep) {
                CHECK(t.height == scene::generatorTile(fine, ix, iz).height);
            }
        }
    }
    CHECK(edgeChecked > 100);
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
