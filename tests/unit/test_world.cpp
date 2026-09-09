// The world map and terrain (ADR-046). The assertions here are about geography rather than about
// numbers: that the shipped world has relief, that a river's bed is under its own water line along
// its whole length, that a feature moves the ground where it says it does, and that two chunks
// meeting at a seam agree on where the ground is.

#include "world/terrain.hpp"
#include "scene/scene.hpp"
#include "world/biome.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <tuple>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

// The extremes over a coarse survey of the map, which is what "does this have relief" means.
std::pair<float, float> reliefOf(const world::WorldMap& map, int steps = 129) {
    float lo = std::numeric_limits<float>::max();
    float hi = std::numeric_limits<float>::lowest();
    for (int j = 0; j < steps; ++j) {
        for (int i = 0; i < steps; ++i) {
            const glm::vec2 uv((static_cast<float>(i) + 0.5f) / steps, (static_cast<float>(j) + 0.5f) / steps);
            const float h = map.height(map.min() + map.size * uv);
            lo = std::min(lo, h);
            hi = std::max(hi, h);
        }
    }
    return {lo, hi};
}

} // namespace

TEST_CASE("the shipped world is a landscape and not a plane", "[unit][world]") {
    const world::WorldMap map = world::defaultWorld();
    REQUIRE(map.validate().has_value());
    const auto [lo, hi] = reliefOf(map);
    // The whole point of the world phase: a default that is designed geography, not a flat plane
    // waiting for someone to fill it in.
    CHECK(hi - lo > 60.0f);
    CHECK(map.features.size() >= 8);
    // And it is a *valley*: the middle of the basin sits well below the northern rim.
    CHECK(map.height({0.0f, -20.0f}) < map.height({-90.0f, -270.0f}) - 30.0f);
}

TEST_CASE("sampling a world is a pure function of position", "[unit][world]") {
    const world::WorldMap map = world::defaultWorld();
    for (const glm::vec2 p : {glm::vec2(0.0f), glm::vec2(37.0f, -122.0f), glm::vec2(-260.0f, 210.0f)}) {
        CHECK(map.height(p) == map.height(p));
        const world::Sample a = map.sample(p, 0.5f);
        const world::Sample b = map.sample(p, 0.5f);
        CHECK(a.height == b.height);
        CHECK(a.slope == b.slope);
    }
}

TEST_CASE("a river's bed stays under its own water line along the whole course", "[unit][world]") {
    // The failure this pins down: an additive carve subtracts a constant, so a river crossing high
    // ground keeps a bed above its water surface and renders as a chain of disconnected puddles.
    // A water feature has to cut *down to* a level, not *by* an amount.
    world::WorldMap map;
    map.size = {400.0f, 400.0f};
    map.layers = {{0.01f, 60.0f, 0.0f, 0.0f}}; // deliberately violent ground for the river to cross
    world::Feature river;
    river.name = "run";
    river.kind = world::FeatureKind::River;
    river.path = {{-150.0f, 20.0f, -150.0f}, {0.0f, 6.0f, 0.0f}, {150.0f, -8.0f, 150.0f}};
    river.width = 8.0f;
    river.amplitude = 2.0f;
    river.water = true;
    map.features.push_back(river);
    map.prepare();
    REQUIRE(map.validate().has_value());

    int wet = 0;
    const auto& course = map.features[0].samplePath();
    for (std::size_t i = 0; i < course.size(); ++i) {
        const glm::vec2 p(course[i].x, course[i].z);
        const world::Sample s = map.sample(p, 0.5f);
        INFO("course point " << i << " height " << s.height << " water " << s.waterSurface);
        CHECK(s.submerged);
        ++wet;
    }
    CHECK(wet > 8);
}

TEST_CASE("features move the ground where they say they do", "[unit][world]") {
    world::WorldMap map;
    map.size = {400.0f, 400.0f};
    map.layers.clear(); // no noise: the feature is the only thing that can move the ground

    SECTION("a ridge raises its line and leaves distant ground alone") {
        world::Feature ridge;
        ridge.kind = world::FeatureKind::Ridge;
        ridge.path = {{-100.0f, 0.0f, 0.0f}, {100.0f, 0.0f, 0.0f}};
        ridge.width = 30.0f;
        ridge.amplitude = 25.0f;
        map.features.push_back(ridge);
        map.prepare();
        CHECK_THAT(map.height({0.0f, 0.0f}), WithinAbs(25.0f, 0.01f));
        CHECK_THAT(map.height({0.0f, 60.0f}), WithinAbs(0.0f, 0.01f));
        CHECK(map.height({0.0f, 15.0f}) > 0.0f);       // inside the shoulder
        CHECK(map.height({0.0f, 15.0f}) < 25.0f);      // but not at full height
    }

    SECTION("a flat blends toward its level") {
        world::Feature flat;
        flat.kind = world::FeatureKind::Flat;
        flat.path = {{0.0f, -7.0f, 0.0f}};
        flat.width = 40.0f;
        flat.flatten = 1.0f;
        map.baseHeight = 12.0f;
        map.features.push_back(flat);
        map.prepare();
        CHECK_THAT(map.height({0.0f, 0.0f}), WithinAbs(-7.0f, 0.01f));
        CHECK_THAT(map.height({0.0f, 80.0f}), WithinAbs(12.0f, 0.01f));
    }
}

TEST_CASE("smoothing bends a path without moving it outside its own levels", "[unit][world]") {
    // Chaikin stays inside the convex hull of the control polygon. That is the property that lets a
    // river be smoothed without any risk of the smoothed course climbing above a level it was
    // authored to descend through.
    world::WorldMap map;
    world::Feature f;
    f.kind = world::FeatureKind::River;
    f.path = {{0.0f, 30.0f, 0.0f}, {40.0f, 20.0f, 40.0f}, {0.0f, 10.0f, 80.0f}, {40.0f, 0.0f, 120.0f}};
    f.smoothing = 3;
    map.features.push_back(f);
    map.prepare();
    const auto& curve = map.features[0].samplePath();
    REQUIRE(curve.size() > f.path.size());
    for (const glm::vec3& q : curve) {
        CHECK(q.y <= 30.0f + 1e-4f);
        CHECK(q.y >= 0.0f - 1e-4f);
    }
    // Levels still descend monotonically along the smoothed course.
    for (std::size_t i = 1; i < curve.size(); ++i) {
        CHECK(curve[i].y <= curve[i - 1].y + 1e-4f);
    }
}

TEST_CASE("a world map round-trips through json", "[unit][world]") {
    const world::WorldMap original = world::defaultWorld();
    const nlohmann::json j = world::worldMapToJson(original);
    auto parsed = world::worldMapFromJson(j);
    REQUIRE(parsed.has_value());
    CHECK(parsed->structuralHash() == original.structuralHash());
    for (const glm::vec2 p : {glm::vec2(0.0f), glm::vec2(88.0f, -164.0f), glm::vec2(-212.0f, 96.0f)}) {
        CHECK_THAT(parsed->height(p), WithinAbs(original.height(p), 1e-3f));
    }
}

TEST_CASE("the structural hash follows what a sample depends on", "[unit][world]") {
    const world::WorldMap base = world::defaultWorld();
    world::WorldMap moved = base;
    moved.features[0].path[0].x += 5.0f;
    CHECK(moved.structuralHash() != base.structuralHash());
    world::WorldMap reseeded = base;
    reseeded.seed += 1;
    CHECK(reseeded.structuralHash() != base.structuralHash());
    world::WorldMap eroded = base;
    eroded.erosion = base.erosion + 0.2f;
    CHECK(eroded.structuralHash() != base.structuralHash());
    world::WorldMap same = base;
    CHECK(same.structuralHash() == base.structuralHash());
}

TEST_CASE("erosion redistributes detail without changing the map's identity", "[unit][world]") {
    world::WorldMap plain = world::defaultWorld();
    plain.erosion = 0.0f;
    world::WorldMap eroded = plain;
    eroded.erosion = 1.0f;
    // It must actually do something. Not everywhere: a point inside a flattened feature is pinned
    // to its level whatever the octaves do, which is the whole point of a flattener. So the claim
    // is that the open ground moves.
    float largest = 0.0f;
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 p(-300.0f + static_cast<float>(i) * 9.4f, 140.0f + static_cast<float>(i % 7) * 11.0f);
        largest = std::max(largest, std::fabs(plain.height(p) - eroded.height(p)));
    }
    INFO("largest height difference between erosion 0 and 1: " << largest);
    CHECK(largest > 0.5f);
    // ...but not turn the world into a different place: the large features are still where they were.
    const auto [lo, hi] = reliefOf(eroded, 65);
    CHECK(hi - lo > 60.0f);
}

// ---- terrain -----------------------------------------------------------------------------------

TEST_CASE("the chunk grid covers the world", "[unit][terrain]") {
    const world::WorldMap map = world::defaultWorld();
    world::TerrainSettings settings;
    settings.chunkSize = 40.0f;
    REQUIRE(settings.validate().has_value());
    const auto coords = world::chunkGrid(map, settings);
    CHECK(coords.size() == 16u * 16u);
    CHECK(world::chunkOrigin(map, settings, coords.front()) == map.min());
    const glm::vec2 lastCorner = world::chunkOrigin(map, settings, coords.back()) + glm::vec2(settings.chunkSize);
    CHECK_THAT(lastCorner.x, WithinAbs(map.max().x, 1e-3f));
    CHECK_THAT(lastCorner.y, WithinAbs(map.max().y, 1e-3f));
}

TEST_CASE("neighbouring chunks meet exactly at the seam", "[unit][terrain]") {
    // Two chunks at the same level must agree on the ground along their shared edge to the bit, or
    // the seam shows as a hairline crack that no amount of skirt hides at close range.
    const world::WorldMap map = world::defaultWorld();
    world::TerrainSettings settings;
    settings.chunkSize = 40.0f;
    settings.resolution = 8;
    const scene::MeshData a = world::buildChunkMesh(map, settings, {4, 5}, 0);
    const scene::MeshData b = world::buildChunkMesh(map, settings, {5, 5}, 0);
    const int side = settings.resolution + 1;
    int matched = 0;
    for (int j = 0; j <= settings.resolution; ++j) {
        const scene::Vertex& east = a.vertices[static_cast<std::size_t>(j) * side + settings.resolution];
        const scene::Vertex& west = b.vertices[static_cast<std::size_t>(j) * side];
        CHECK(east.position == west.position);
        CHECK(east.normal == west.normal);
        ++matched;
    }
    CHECK(matched == settings.resolution + 1);
}

TEST_CASE("every level of a chunk spans the same footprint", "[unit][terrain]") {
    const world::WorldMap map = world::defaultWorld();
    world::TerrainSettings settings;
    settings.skirtDepth = 0.0f;
    const glm::vec2 origin = world::chunkOrigin(map, settings, {6, 7});
    for (int lod = 0; lod < settings.lodLevels; ++lod) {
        const scene::MeshData mesh = world::buildChunkMesh(map, settings, {6, 7}, lod);
        REQUIRE(mesh.valid());
        const auto [lo, hi] = mesh.bounds();
        CHECK_THAT(lo.x, WithinAbs(origin.x, 1e-3f));
        CHECK_THAT(lo.z, WithinAbs(origin.y, 1e-3f));
        CHECK_THAT(hi.x, WithinAbs(origin.x + settings.chunkSize, 1e-3f));
        CHECK_THAT(hi.z, WithinAbs(origin.y + settings.chunkSize, 1e-3f));
        // A coarser level really is coarser.
        if (lod > 0) {
            const scene::MeshData finer = world::buildChunkMesh(map, settings, {6, 7}, lod - 1);
            CHECK(mesh.indices.size() < finer.indices.size());
        }
    }
}

TEST_CASE("chunk uv carries the biome axis and the slope", "[unit][terrain]") {
    world::WorldMap map = world::defaultWorld();
    world::TerrainSettings settings;
    settings.skirtDepth = 0.0f;
    settings.resolution = 8;

    SECTION("uv.y is the shading slope and uv.x is somewhere along the biome order") {
        const scene::MeshData mesh = world::buildChunkMesh(map, settings, {8, 8}, 0);
        bool varied = false;
        const float first = mesh.vertices.front().uv.x;
        for (const scene::Vertex& v : mesh.vertices) {
            CHECK_THAT(v.uv.y, WithinAbs(std::clamp(1.0f - v.normal.y, 0.0f, 1.0f), 1e-5f));
            CHECK(v.uv.x >= 0.0f);
            CHECK(v.uv.x <= 1.0f);
            varied = varied || std::fabs(v.uv.x - first) > 1e-4f;
        }
        // A chunk that reports one biome everywhere would satisfy the range check and mean nothing.
        CHECK(varied);
    }

    SECTION("a world with no biomes leaves the axis at the altitude") {
        map.biomes.biomes.clear();
        const scene::MeshData mesh = world::buildChunkMesh(map, settings, {8, 8}, 0);
        for (const scene::Vertex& v : mesh.vertices) {
            CHECK_THAT(v.uv.x, WithinAbs(map.altitude01(v.position.y), 1e-5f));
        }
    }
}

TEST_CASE("the biome axis follows the hillside, not the ripple on it", "[unit][terrain]") {
    // Biome rules read a slope measured over about eight metres rather than over one vertex. Fed
    // the per-vertex slope, a rule puts a boundary on every bump and a ridge comes out as a
    // sawtooth of alternating biomes. The claim is that the axis varies more smoothly along a row
    // than the shading slope does over the same ground.
    const world::WorldMap map = world::defaultWorld();
    world::TerrainSettings settings;
    settings.skirtDepth = 0.0f;
    settings.resolution = 32;
    const float step = settings.chunkSize / static_cast<float>(settings.resolution);
    const int spread = std::max(1, static_cast<int>(std::lround(8.0f / step)));
    REQUIRE(spread > 1); // or the two measurements below are the same measurement

    // Over the flanks of both massifs, because slope only decides anything where the ground is
    // actually steep: measured on the flat valley floor the two numbers come out identical, and a
    // test that compares a thing to itself passes without meaning anything.
    const std::vector<glm::ivec2> steep = {{4, 2}, {5, 3}, {11, 2}, {12, 3}};
    const auto totalVariation = [&](int useSpread) {
        float total = 0.0f;
        for (const glm::ivec2 coord : steep) {
            const world::ChunkField field = world::sampleChunkField(map, settings, coord);
            for (int j = 0; j <= settings.resolution; ++j) {
                float previous = 0.0f;
                for (int i = 0; i <= settings.resolution; ++i) {
                    const glm::vec2 p = world::chunkOrigin(map, settings, coord) +
                                        glm::vec2(static_cast<float>(i), static_cast<float>(j)) * step;
                    const float altitude = map.altitude01(field.at(i, j));
                    const float slope = std::clamp(1.0f - field.normalAt(i, j, useSpread).y, 0.0f, 1.0f);
                    const float axis = map.biomes.at(altitude, slope, map.moisture(p, altitude), p).axis();
                    if (i > 0) {
                        total += std::fabs(axis - previous);
                    }
                    previous = axis;
                }
            }
        }
        return total;
    };
    const float fine = totalVariation(1);
    const float coarse = totalVariation(spread);
    INFO("axis variation from a one-vertex slope " << fine << " vs an eight-metre slope " << coarse);
    CHECK(coarse < fine * 0.8f);
}

TEST_CASE("the skirt hangs below the chunk and only below it", "[unit][terrain]") {
    const world::WorldMap map = world::defaultWorld();
    world::TerrainSettings settings;
    settings.resolution = 8;
    settings.skirtDepth = 6.0f;
    const scene::MeshData plain = [&] {
        world::TerrainSettings s = settings;
        s.skirtDepth = 0.0f;
        return world::buildChunkMesh(map, s, {3, 9}, 0);
    }();
    const scene::MeshData skirted = world::buildChunkMesh(map, settings, {3, 9}, 0);
    CHECK(skirted.vertices.size() > plain.vertices.size());
    CHECK(skirted.indices.size() > plain.indices.size());
    const auto [plainLo, plainHi] = plain.bounds();
    const auto [lo, hi] = skirted.bounds();
    CHECK_THAT(lo.y, WithinAbs(plainLo.y - settings.skirtDepth, 1e-3f));
    CHECK_THAT(hi.y, WithinAbs(plainHi.y, 1e-3f)); // the skirt never rises above the surface
}

TEST_CASE("lod level follows distance and stops at the last level", "[unit][terrain]") {
    world::TerrainSettings settings;
    settings.lodDistance = 80.0f;
    settings.lodLevels = 4;
    CHECK(world::chunkLod(settings, 0.0f) == 0);
    CHECK(world::chunkLod(settings, 79.0f) == 0);
    CHECK(world::chunkLod(settings, 81.0f) == 1);
    CHECK(world::chunkLod(settings, 161.0f) == 2);
    CHECK(world::chunkLod(settings, 321.0f) == 3);
    CHECK(world::chunkLod(settings, 100000.0f) == 3); // clamped, never past the last level built
    settings.lodLevels = 1;
    CHECK(world::chunkLod(settings, 100000.0f) == 0);
}

TEST_CASE("frustum culling keeps what is in front and drops what is behind", "[unit][terrain]") {
    scene::Camera camera;
    camera.position = {0.0f, 0.0f, 0.0f};
    camera.target = {0.0f, 0.0f, -1.0f};
    camera.nearPlane = 0.1f;
    camera.farPlane = 500.0f;
    camera.fovYRadians = 0.9f;
    const world::FrustumPlanes planes = world::frustumPlanes(camera.projection(1.777f) * camera.view());
    CHECK(world::aabbVisible(planes, {-5.0f, -5.0f, -60.0f}, {5.0f, 5.0f, -50.0f}));   // ahead
    CHECK_FALSE(world::aabbVisible(planes, {-5.0f, -5.0f, 50.0f}, {5.0f, 5.0f, 60.0f})); // behind
    CHECK_FALSE(world::aabbVisible(planes, {-5.0f, -5.0f, -900.0f}, {5.0f, 5.0f, -800.0f})); // past far
    CHECK_FALSE(world::aabbVisible(planes, {600.0f, -5.0f, -60.0f}, {620.0f, 5.0f, -50.0f})); // off to the side
}

TEST_CASE("building a terrain emits every level of every chunk", "[unit][terrain]") {
    world::WorldMap map = world::defaultWorld();
    map.size = {160.0f, 160.0f}; // four by four chunks keeps the test quick
    map.prepare();
    world::TerrainSettings settings;
    settings.chunkSize = 40.0f;
    settings.resolution = 8;
    std::size_t emitted = 0;
    const auto chunks = world::buildTerrain(map, settings, [&](std::size_t, int, scene::MeshData&& mesh) {
        REQUIRE(mesh.valid());
        return static_cast<scene::MeshId>(emitted++);
    });
    CHECK(chunks.size() == 16u);
    CHECK(emitted == chunks.size() * static_cast<std::size_t>(settings.lodLevels));
    for (const world::TerrainChunk& c : chunks) {
        CHECK(c.boundsMax.y > c.boundsMin.y);
        for (int lod = 0; lod < settings.lodLevels; ++lod) {
            CHECK(c.meshes[static_cast<std::size_t>(lod)] != scene::kInvalidMesh);
        }
    }
}

TEST_CASE("mesh bounds are cached against the mesh version", "[unit][scene]") {
    // Scene::bounds() runs every frame and a mesh's bounds are a scan of all its vertices. With a
    // world's worth of terrain that is hundreds of thousands of vertex reads per frame to recompute
    // numbers that have not changed. The cache has to follow meshVersion exactly: too eager and the
    // renderer sizes its shadow cascades to geometry that is gone.
    scene::Scene s;
    scene::MeshData a;
    a.vertices = {{{-1.0f, -2.0f, -3.0f}, {0, 1, 0}, {0, 0}}, {{4.0f, 5.0f, 6.0f}, {0, 1, 0}, {0, 0}}};
    a.indices = {0, 1, 0};
    const scene::MeshId id = s.addMesh(std::move(a));
    CHECK(s.meshBounds(id).first == glm::vec3(-1.0f, -2.0f, -3.0f));
    CHECK(s.meshBounds(id).second == glm::vec3(4.0f, 5.0f, 6.0f));

    // Adding a mesh bumps the version, so the new one is measured rather than read off the end.
    scene::MeshData b;
    b.vertices = {{{10.0f, 10.0f, 10.0f}, {0, 1, 0}, {0, 0}}, {{12.0f, 14.0f, 16.0f}, {0, 1, 0}, {0, 0}}};
    b.indices = {0, 1, 0};
    const scene::MeshId second = s.addMesh(std::move(b));
    CHECK(s.meshBounds(second).second == glm::vec3(12.0f, 14.0f, 16.0f));
    CHECK(s.meshBounds(id).first == glm::vec3(-1.0f, -2.0f, -3.0f));

    // An edit that bumps the version is picked up; an out-of-range id is not a crash.
    s.meshes[id].vertices[1].position = glm::vec3(40.0f, 50.0f, 60.0f);
    ++s.meshVersion;
    CHECK(s.meshBounds(id).second == glm::vec3(40.0f, 50.0f, 60.0f));
    CHECK(s.meshBounds(scene::kInvalidMesh).first == glm::vec3(0.0f));
}

// ---- biomes ------------------------------------------------------------------------------------

TEST_CASE("a biome range is a band with soft edges", "[unit][biome]") {
    world::Range r{0.3f, 0.6f, 0.1f};
    CHECK_THAT(r.membership(0.45f), WithinAbs(1.0f, 1e-5f));
    CHECK_THAT(r.membership(0.3f), WithinAbs(1.0f, 1e-5f));
    CHECK_THAT(r.membership(0.6f), WithinAbs(1.0f, 1e-5f));
    CHECK_THAT(r.membership(0.19f), WithinAbs(0.0f, 1e-5f));   // fully outside the fade
    CHECK_THAT(r.membership(0.71f), WithinAbs(0.0f, 1e-5f));
    // Inside the fade it is between, and it is monotone -- a hard edge is what puts a visible line
    // on the ground where two biomes meet.
    CHECK(r.membership(0.25f) > 0.0f);
    CHECK(r.membership(0.25f) < 1.0f);
    CHECK(r.membership(0.26f) > r.membership(0.24f));
    CHECK(r.membership(0.64f) > r.membership(0.66f));
}

TEST_CASE("a biome has to satisfy every one of its rules", "[unit][biome]") {
    // The score is a product, not a sum. Summed, alpine scree turns up on the valley floor on the
    // strength of its slope term alone.
    world::BiomeSet set;
    world::Biome low;
    low.name = "low";
    low.rule.altitude = {0.0f, 0.2f, 0.05f};
    low.rule.slope = {0.0f, 1.0f, 0.05f};
    low.rule.moisture = {0.0f, 1.0f, 0.05f};
    world::Biome steepHigh;
    steepHigh.name = "steepHigh";
    steepHigh.rule.altitude = {0.8f, 1.0f, 0.05f};
    steepHigh.rule.slope = {0.5f, 1.0f, 0.05f};
    steepHigh.rule.moisture = {0.0f, 1.0f, 0.05f};
    set.biomes = {low, steepHigh};
    REQUIRE(set.validate().has_value());

    // Steep but low: `steepHigh` matches the slope and fails the altitude, so it is not here.
    const world::BiomeWeights valley = set.at(0.05f, 0.9f, 0.5f, {0.0f, 0.0f});
    CHECK(valley.dominant() == 0);
    CHECK_THAT(valley.weights[1], WithinAbs(0.0f, 1e-5f));
    // High and steep: both terms hold.
    CHECK(set.at(0.95f, 0.9f, 0.5f, {0.0f, 0.0f}).dominant() == 1);
}

TEST_CASE("weights are normalised and a point is always somewhere", "[unit][biome]") {
    const world::BiomeSet set = world::defaultBiomes();
    REQUIRE(set.validate().has_value());
    for (const auto& [altitude, slope, moisture] :
         {std::tuple{0.0f, 0.0f, 1.0f}, std::tuple{0.5f, 0.3f, 0.4f}, std::tuple{1.0f, 0.9f, 0.0f}}) {
        const world::BiomeWeights w = set.at(altitude, slope, moisture, {900.0f, 900.0f});
        float total = 0.0f;
        for (int i = 0; i < w.count; ++i) {
            CHECK(w.weights[static_cast<std::size_t>(i)] >= 0.0f);
            total += w.weights[static_cast<std::size_t>(i)];
        }
        CHECK_THAT(total, WithinAbs(1.0f, 1e-4f));
        CHECK(w.axis() >= 0.0f);
        CHECK(w.axis() <= 1.0f);
    }
    // A combination no biome claims still lands somewhere, evenly, rather than leaving a hole that
    // would snap the axis to zero and put one biome's colour in the gaps of every other.
    world::BiomeSet narrow;
    world::Biome only;
    only.name = "only";
    only.rule.altitude = {0.9f, 1.0f, 0.0f};
    narrow.biomes = {only, only};
    narrow.biomes[1].name = "other";
    const world::BiomeWeights nowhere = narrow.at(0.0f, 0.0f, 0.0f, {0.0f, 0.0f});
    CHECK_THAT(nowhere.weights[0], WithinAbs(0.5f, 1e-5f));
    CHECK_THAT(nowhere.weights[1], WithinAbs(0.5f, 1e-5f));
}

TEST_CASE("the biome axis walks the authored order", "[unit][biome]") {
    world::BiomeSet set;
    for (int i = 0; i < 5; ++i) {
        world::Biome b;
        b.name = "b" + std::to_string(i);
        // Five bands up the altitude axis, in order, so the axis should track altitude.
        b.rule.altitude = {static_cast<float>(i) * 0.2f, static_cast<float>(i + 1) * 0.2f, 0.05f};
        set.biomes.push_back(b);
    }
    float previous = -1.0f;
    for (float a = 0.05f; a < 1.0f; a += 0.05f) {
        const float axis = set.at(a, 0.0f, 0.5f, {0.0f, 0.0f}).axis();
        CHECK(axis >= previous - 1e-4f); // monotone: the axis is an order, not a lookup
        previous = axis;
    }
    CHECK_THAT(set.at(0.02f, 0.0f, 0.5f, {0.0f, 0.0f}).axis(), WithinAbs(0.0f, 0.05f));
    CHECK_THAT(set.at(0.98f, 0.0f, 0.5f, {0.0f, 0.0f}).axis(), WithinAbs(1.0f, 0.05f));
}

TEST_CASE("a region puts a biome somewhere the rules would not", "[unit][biome]") {
    // The point of regions: a grove goes where a shot needs it, not where the moisture landed.
    world::BiomeSet set;
    world::Biome ordinary;
    ordinary.name = "ordinary";
    world::Biome grove;
    grove.name = "grove";
    grove.rule.altitude = {0.9f, 1.0f, 0.01f}; // its rules exclude the whole valley
    world::BiomeRegion here;
    here.path = {{40.0f, -20.0f}};
    here.width = 30.0f;
    here.strength = 4.0f;
    grove.regions.push_back(here);
    set.biomes = {ordinary, grove};

    CHECK(set.at(0.1f, 0.0f, 0.5f, {40.0f, -20.0f}).dominant() == 1);  // inside the region
    CHECK(set.at(0.1f, 0.0f, 0.5f, {200.0f, -20.0f}).dominant() == 0); // well outside it
    // And the edge is a fade, not a wall.
    const float edge = set.at(0.1f, 0.0f, 0.5f, {62.0f, -20.0f}).weights[1];
    CHECK(edge > 0.0f);
    CHECK(edge < 1.0f);
}

TEST_CASE("the shipped biome set covers the world without one biome owning it", "[unit][biome]") {
    // A biome that owns one per cent of the map is not a biome, and one that owns eighty is the
    // only one. Both happened while these bands were being written; this is the guard rail.
    const world::WorldMap map = world::defaultWorld();
    REQUIRE(map.biomes.biomes.size() == 5);
    std::vector<int> owned(map.biomes.biomes.size(), 0);
    int total = 0;
    constexpr int kSteps = 61;
    for (int j = 0; j < kSteps; ++j) {
        for (int i = 0; i < kSteps; ++i) {
            const glm::vec2 uv((static_cast<float>(i) + 0.5f) / kSteps, (static_cast<float>(j) + 0.5f) / kSteps);
            const glm::vec2 p = map.min() + map.size * uv;
            const world::Sample s = map.sample(p, 0.6f);
            ++owned[static_cast<std::size_t>(map.biomes.at(s.altitude, s.slope, s.moisture, p).dominant())];
            ++total;
        }
    }
    for (std::size_t b = 0; b < owned.size(); ++b) {
        const double share = 100.0 * owned[b] / total;
        INFO(map.biomes.biomes[b].name << " owns " << share << "%");
        CHECK(share > 4.0);
        CHECK(share < 60.0);
    }
}

TEST_CASE("a biome set round-trips through json", "[unit][biome]") {
    const world::BiomeSet original = world::defaultBiomes();
    auto parsed = world::biomeSetFromJson(world::biomeSetToJson(original));
    REQUIRE(parsed.has_value());
    CHECK(parsed->structuralHash() == original.structuralHash());
    const world::BiomeWeights a = original.at(0.3f, 0.2f, 0.6f, {12.0f, -40.0f});
    const world::BiomeWeights b = parsed->at(0.3f, 0.2f, 0.6f, {12.0f, -40.0f});
    CHECK_THAT(a.axis(), WithinAbs(b.axis(), 1e-5f));
}

TEST_CASE("moisture falls away from water and rises toward the valley floor", "[unit][world]") {
    const world::WorldMap map = world::defaultWorld();
    // On the river, and a long way from it at the same height band.
    const float onRiver = map.moisture({6.0f, -12.0f}, 0.2f);
    const float offRiver = map.moisture({260.0f, -12.0f}, 0.2f);
    CHECK(onRiver > offRiver);
    CHECK(onRiver > 0.8f);
    // Low ground is damp even with no water near it, which is what makes a basin floor read as one.
    CHECK(map.moisture({300.0f, 300.0f}, 0.0f) > map.moisture({300.0f, 300.0f}, 1.0f));
}

TEST_CASE("the generated ground material is a valid program built from the palette", "[unit][biome]") {
    const world::BiomeSet set = world::defaultBiomes();
    const scene::MaterialProgram program = world::terrainMaterialProgram(set, "ground");
    REQUIRE(program.validate().has_value());
    CHECK(program.baseColorRegister >= 0);
    CHECK(program.roughnessRegister >= 0);
    CHECK(program.totalOpCount() <= scene::kMaxMaterialOps);
    // It has to actually carry the palette: a program that compiles and paints last week's colours
    // is exactly what generating it was meant to prevent.
    const auto mentions = [&](const glm::vec3& colour) {
        for (const scene::MaterialOp& op : program.ops) {
            for (const glm::vec4& k : {op.constant, op.constant2, op.constant3, op.constant4}) {
                if (glm::length(glm::vec3(k) - colour) < 1e-5f) {
                    return true;
                }
            }
        }
        return false;
    };
    CHECK(mentions(set.biomes.front().groundColor));
    CHECK(mentions(set.biomes.back().groundColor));
    CHECK(mentions(set.biomes.front().rockColor));
    // And an empty set produces an empty program rather than a broken one.
    CHECK(world::terrainMaterialProgram({}, "none").ops.empty());
}
