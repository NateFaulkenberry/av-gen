// The world map and terrain (ADR-046). The assertions here are about geography rather than about
// numbers: that the shipped world has relief, that a river's bed is under its own water line along
// its whole length, that a feature moves the ground where it says it does, and that two chunks
// meeting at a seam agree on where the ground is.

#include "world/terrain.hpp"
#include "scene/scene.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

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

TEST_CASE("chunk uv carries slope and altitude", "[unit][terrain]") {
    const world::WorldMap map = world::defaultWorld();
    world::TerrainSettings settings;
    settings.skirtDepth = 0.0f;
    settings.resolution = 8;
    const scene::MeshData mesh = world::buildChunkMesh(map, settings, {8, 8}, 0);
    for (const scene::Vertex& v : mesh.vertices) {
        CHECK_THAT(v.uv.x, WithinAbs(std::clamp(1.0f - v.normal.y, 0.0f, 1.0f), 1e-5f));
        CHECK_THAT(v.uv.y, WithinAbs(map.altitude01(v.position.y), 1e-5f));
        CHECK(v.uv.y >= 0.0f);
        CHECK(v.uv.y <= 1.0f);
    }
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
