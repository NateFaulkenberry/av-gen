// The world map and terrain (ADR-046). The assertions here are about geography rather than about
// numbers: that the shipped world has relief, that a river's bed is under its own water line along
// its whole length, that a feature moves the ground where it says it does, and that two chunks
// meeting at a seam agree on where the ground is.

#include "world/terrain.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"
#include "world/biome.hpp"
#include "core/noise.hpp"
#include "world/ecology.hpp"
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

// Catch2's floating-point matchers are double-typed; this keeps float values from
// promoting implicitly at every call site (the house pattern, see test_spline.cpp).
double d(float v) {
    return static_cast<double>(v);
}


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
        CHECK_THAT(d(map.height({0.0, 0.0f})), WithinAbs(25.0, 0.01));
        CHECK_THAT(d(map.height({0.0, 60.0f})), WithinAbs(0.0, 0.01));
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
        CHECK_THAT(d(map.height({0.0, 0.0f})), WithinAbs(-7.0, 0.01));
        CHECK_THAT(d(map.height({0.0, 80.0f})), WithinAbs(12.0, 0.01));
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
        CHECK_THAT(d(parsed->height(p)), WithinAbs(d(original.height(p)), 1e-3));
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
    CHECK_THAT(d(lastCorner.x), WithinAbs(d(map.max().x), 1e-3));
    CHECK_THAT(d(lastCorner.y), WithinAbs(d(map.max().y), 1e-3));
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
        CHECK_THAT(d(lo.x), WithinAbs(d(origin.x), 1e-3));
        CHECK_THAT(d(lo.z), WithinAbs(d(origin.y), 1e-3));
        CHECK_THAT(d(hi.x), WithinAbs(d(origin.x + settings.chunkSize), 1e-3));
        CHECK_THAT(d(hi.z), WithinAbs(d(origin.y + settings.chunkSize), 1e-3));
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
            CHECK_THAT(d(v.uv.y), WithinAbs(d(std::clamp(1.0f - v.normal.y, 0.0f, 1.0f)), 1e-5));
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
            CHECK_THAT(d(v.uv.x), WithinAbs(d(map.altitude01(v.position.y)), 1e-5));
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
    CHECK_THAT(d(lo.y), WithinAbs(d(plainLo.y - settings.skirtDepth), 1e-3));
    CHECK_THAT(d(hi.y), WithinAbs(d(plainHi.y), 1e-3)); // the skirt never rises above the surface
}

TEST_CASE("lod level follows apparent size and stops at the last level", "[unit][terrain]") {
    world::TerrainSettings settings;
    settings.lodDistance = 80.0f;
    settings.lodLevels = 4;
    // With no viewport information the reference lens applies, which is what `lodDistance` means.
    CHECK(world::chunkLod(settings, 0.0f) == 0);
    CHECK(world::chunkLod(settings, 79.0f) == 0);
    CHECK(world::chunkLod(settings, 81.0f) == 1);
    CHECK(world::chunkLod(settings, 161.0f) == 2);
    CHECK(world::chunkLod(settings, 321.0f) == 3);
    CHECK(world::chunkLod(settings, 100000.0f) == 3); // clamped, never past the last level built
    settings.lodLevels = 1;
    CHECK(world::chunkLod(settings, 100000.0f) == 0);
}

TEST_CASE("lod follows the lens, not just the distance", "[unit][terrain]") {
    // The same ground through a longer lens is bigger on screen and must keep its detail longer.
    // Chosen by distance alone, a `lodDistance` tuned at 50 mm coarsens a telephoto shot exactly
    // where the shot is looking.
    world::TerrainSettings settings;
    settings.lodDistance = 80.0f;
    settings.lodLevels = 4;
    const float reference = world::lodProjectionScale(0.8727f, 900.0f);  // 50 degrees, the default
    const float telephoto = world::lodProjectionScale(0.2094f, 900.0f);  // 12 degrees
    const float wide = world::lodProjectionScale(1.7453f, 900.0f);       // 100 degrees
    REQUIRE(telephoto > reference);
    REQUIRE(wide < reference);

    // At 300 m the reference lens has moved on from level 0; the telephoto has not.
    CHECK(world::chunkLod(settings, 300.0f, reference) > 0);
    CHECK(world::chunkLod(settings, 300.0f, telephoto) == 0);
    // And the wide lens is past where the reference is, at every distance that matters.
    for (const float d : {120.0f, 300.0f, 700.0f}) {
        INFO("distance " << d);
        CHECK(world::chunkLod(settings, d, wide) >= world::chunkLod(settings, d, reference));
        CHECK(world::chunkLod(settings, d, reference) >= world::chunkLod(settings, d, telephoto));
    }
    // A taller viewport shows more detail, so it holds the finer level for longer.
    const float tall = world::lodProjectionScale(0.8727f, 2160.0f);
    CHECK(world::chunkLod(settings, 300.0f, tall) < world::chunkLod(settings, 300.0f, reference));
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
    // Every level of every chunk, plus one water surface for each chunk that has any. A dry chunk
    // emits no water mesh at all, so the count is levels x chunks plus however many are wet.
    const auto wet = static_cast<std::size_t>(std::count_if(
        chunks.begin(), chunks.end(), [](const world::TerrainChunk& c) { return c.water != scene::kInvalidMesh; }));
    CHECK(emitted == chunks.size() * static_cast<std::size_t>(settings.lodLevels) + wet);
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
    CHECK_THAT(d(r.membership(0.45)), WithinAbs(1.0, 1e-5));
    CHECK_THAT(d(r.membership(0.3)), WithinAbs(1.0, 1e-5));
    CHECK_THAT(d(r.membership(0.6)), WithinAbs(1.0, 1e-5));
    CHECK_THAT(d(r.membership(0.19)), WithinAbs(0.0, 1e-5));   // fully outside the fade
    CHECK_THAT(d(r.membership(0.71)), WithinAbs(0.0, 1e-5));
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
    CHECK_THAT(d(valley.weights[1]), WithinAbs(0.0, 1e-5));
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
        CHECK_THAT(d(total), WithinAbs(1.0, 1e-4));
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
    CHECK_THAT(d(nowhere.weights[0]), WithinAbs(0.5, 1e-5));
    CHECK_THAT(d(nowhere.weights[1]), WithinAbs(0.5, 1e-5));
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
    CHECK_THAT(d(set.at(0.02, 0.0, 0.5, {0.0, 0.0f}).axis()), WithinAbs(0.0, 0.05));
    CHECK_THAT(d(set.at(0.98, 0.0, 0.5, {0.0, 0.0f}).axis()), WithinAbs(1.0, 0.05));
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
    CHECK_THAT(d(a.axis()), WithinAbs(d(b.axis()), 1e-5));
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

// ---- ecology -----------------------------------------------------------------------------------

namespace {

world::ScatterLayer testLayer(std::string name, std::vector<world::BiomeDensity> densities) {
    world::ScatterLayer l;
    l.name = std::move(name);
    l.asset = "not-loaded-by-these-tests.gltf";
    l.densities = std::move(densities);
    l.seed = 7;
    return l;
}

} // namespace

TEST_CASE("a scatter sits on the terrain and is a pure function of the world", "[unit][ecology]") {
    const world::WorldMap map = world::defaultWorld();
    world::ScatterLayer layer = testLayer("ferns", {{"forest", 0.01f}, {"marsh", 0.01f}});
    const spatial::PointCloud a = world::scatter(map, layer);
    const spatial::PointCloud b = world::scatter(map, layer);
    REQUIRE(a.count() > 100);
    REQUIRE(a.count() == b.count());
    for (std::size_t i = 0; i < a.count(); ++i) {
        CHECK(a.positions()[i] == b.positions()[i]);
    }
    // Every instance stands on the ground rather than near it.
    for (std::size_t i = 0; i < a.count(); i += 17) {
        const glm::vec3 p = a.positions()[i];
        CHECK_THAT(d(p.y), WithinAbs(d(map.height({p.x, p.z})), 1e-3));
        CHECK(p.x >= map.min().x);
        CHECK(p.x <= map.max().x);
    }
}

TEST_CASE("density decides how much of a layer there is", "[unit][ecology]") {
    const world::WorldMap map = world::defaultWorld();
    const std::size_t sparse = world::scatter(map, testLayer("a", {{"forest", 0.002f}})).count();
    const std::size_t dense = world::scatter(map, testLayer("a", {{"forest", 0.02f}})).count();
    INFO("sparse " << sparse << " dense " << dense);
    CHECK(sparse > 0);
    CHECK(dense > sparse * 4);
    // And a layer that asks for nothing gets nothing rather than a grid of nothings.
    world::ScatterLayer none = testLayer("a", {{"forest", 0.0f}});
    CHECK_FALSE(none.validate().has_value()); // every density zero is an authoring error, not silence
}

TEST_CASE("a scatter goes where its biomes are", "[unit][ecology]") {
    // The point of reading the biome weights rather than having rules of its own: the ground and
    // the thing standing on it cannot disagree.
    const world::WorldMap map = world::defaultWorld();
    const spatial::PointCloud marsh = world::scatter(map, testLayer("m", {{"marsh", 0.02f}}));
    const spatial::PointCloud rim = world::scatter(map, testLayer("r", {{"rim", 0.02f}}));
    REQUIRE(marsh.count() > 50);
    REQUIRE(rim.count() > 20);
    const auto meanMoisture = [&](const spatial::PointCloud& c) {
        float total = 0.0f;
        for (std::size_t i = 0; i < c.count(); ++i) {
            const glm::vec3 p = c.positions()[i];
            total += map.sample({p.x, p.z}, 0.5f).moisture;
        }
        return total / static_cast<float>(c.count());
    };
    const auto meanAltitude = [&](const spatial::PointCloud& c) {
        float total = 0.0f;
        for (std::size_t i = 0; i < c.count(); ++i) {
            total += map.altitude01(c.positions()[i].y);
        }
        return total / static_cast<float>(c.count());
    };
    CHECK(meanMoisture(marsh) > meanMoisture(rim));
    CHECK(meanAltitude(rim) > meanAltitude(marsh));
}

TEST_CASE("scatter filters keep things out of the water and off the cliffs", "[unit][ecology]") {
    const world::WorldMap map = world::defaultWorld();
    world::ScatterLayer layer = testLayer("f", {{"marsh", 0.05f}});
    layer.avoidWater = true;
    layer.shoreOffset = 0.4f;
    layer.maxSlope = 0.12f;
    const spatial::PointCloud cloud = world::scatter(map, layer);
    REQUIRE(cloud.count() > 50);
    for (std::size_t i = 0; i < cloud.count(); ++i) {
        const glm::vec3 p = cloud.positions()[i];
        const world::Sample s = map.sample({p.x, p.z}, 0.5f);
        CHECK_FALSE(s.submerged);
        CHECK(s.height >= s.waterSurface + layer.shoreOffset);
        CHECK(s.slope <= layer.maxSlope + 1e-3f);
    }
}

TEST_CASE("clustering moves a layer into patches without emptying it", "[unit][ecology]") {
    const world::WorldMap map = world::defaultWorld();
    world::ScatterLayer even = testLayer("e", {{"forest", 0.01f}});
    even.clustering = 0.0f;
    world::ScatterLayer clumped = even;
    clumped.clustering = 0.9f;
    clumped.clusterScale = 20.0f;
    const spatial::PointCloud a = world::scatter(map, even);
    const spatial::PointCloud b = world::scatter(map, clumped);
    REQUIRE(a.count() > 200);
    REQUIRE(b.count() > 50);

    // Clumping is measured as it looks: bin the world coarsely and compare how uneven the counts
    // are. A patchy layer has empty bins and crowded ones; an even one does not.
    const auto unevenness = [&](const spatial::PointCloud& c) {
        constexpr int kBins = 24;
        std::vector<int> bins(kBins * kBins, 0);
        for (std::size_t i = 0; i < c.count(); ++i) {
            const glm::vec3 p = c.positions()[i];
            const glm::vec2 uv = (glm::vec2(p.x, p.z) - map.min()) / map.size;
            const int x = std::clamp(static_cast<int>(uv.x * kBins), 0, kBins - 1);
            const int y = std::clamp(static_cast<int>(uv.y * kBins), 0, kBins - 1);
            ++bins[static_cast<std::size_t>(y) * kBins + x];
        }
        const double mean = static_cast<double>(c.count()) / bins.size();
        double variance = 0.0;
        for (const int n : bins) {
            variance += (n - mean) * (n - mean);
        }
        return variance / bins.size() / std::max(mean, 1e-6);  // index of dispersion
    };
    INFO("even " << unevenness(a) << " clumped " << unevenness(b));
    CHECK(unevenness(b) > unevenness(a));
}

TEST_CASE("a density naming a biome that does not exist is an error, not an empty forest",
          "[unit][ecology]") {
    // Silently placing nothing is the worst available outcome: a layer that was authored, parsed,
    // built, and grew no plants, with nothing anywhere saying why.
    const world::BiomeSet biomes = world::defaultBiomes();
    world::Ecology good;
    good.layers.push_back(testLayer("ok", {{"forest", 0.01f}}));
    CHECK(good.validate(biomes).has_value());
    world::Ecology typo;
    typo.layers.push_back(testLayer("oops", {{"forrest", 0.01f}}));
    const auto result = typo.validate(biomes);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("forrest") != std::string::npos);
}

TEST_CASE("scatter proximity rules reject malformed habitat ranges", "[unit][ecology][habitat]") {
    const nlohmann::json layer = {{"name", "ferns"}, {"asset", "fern.gltf"},
                                  {"densities", {{"forest", 0.2}}}};
    for (const auto& proximity : {
             nlohmann::json("canopy"),
             nlohmann::json{{"layer", "canopy"}, {"minDistance", 8.0}, {"maxDistance", 2.0}},
             nlohmann::json{{"layer", ""}, {"maxDistance", 8.0}},
             nlohmann::json{{"layer", "canopy"}, {"maxDistance", -1.0}}}) {
        auto invalid = layer;
        invalid["proximity"] = proximity;
        CHECK_FALSE(world::ecologyFromJson(nlohmann::json::array({invalid})).has_value());
    }
}

TEST_CASE("an ecology round-trips through json", "[unit][ecology]") {
    world::Ecology original;
    world::ScatterLayer layer = testLayer("fungi", {{"marsh", 0.03f}, {"forest", 0.008f}});
    layer.height = 0.28f;
    layer.clustering = 0.85f;
    layer.emissiveColor = {0.3f, 0.1f, 0.8f};
    layer.emissiveIntensity = 5.5f;
    layer.tint = {0.7f, 0.6f, 0.9f};
    layer.avoidWater = false;
    original.layers.push_back(layer);
    auto parsed = world::ecologyFromJson(world::ecologyToJson(original));
    REQUIRE(parsed.has_value());
    CHECK(parsed->structuralHash() == original.structuralHash());
}

TEST_CASE("habitat placement agrees with a brute force horizontal distance band",
          "[unit][ecology][habitat]") {
    auto map = world::defaultWorld();
    map.size = {96.0f, 96.0f};
    auto layer = testLayer("ferns", {{"forest", 0.2f}, {"marsh", 0.2f}, {"meadow", 0.2f},
                                    {"scree", 0.2f}, {"rim", 0.2f}});
    layer.maxSlope = 1.0f;
    layer.avoidWater = false;
    const auto independent = world::scatter(map, layer);
    const std::vector<glm::vec3> anchors{{-12.0f, 900.0f, -14.0f}, {18.0f, -900.0f, 17.0f}};
    layer.proximity = world::ScatterProximity{"canopy", 2.0f, 14.0f, 0.0f, 1.0f};
    const auto nearest = [&](glm::vec3 position) {
        float distance = std::numeric_limits<float>::max();
        for (const glm::vec3 anchor : anchors) {
            distance = std::min(distance, glm::length(glm::vec2(position.x - anchor.x, position.z - anchor.z)));
        }
        return distance;
    };
    const auto related = world::scatter(map, layer, anchors);
    const auto repeated = world::scatter(map, layer, anchors);
    REQUIRE(related.count() > 50);
    REQUIRE(related.count() < independent.count());
    REQUIRE(related.count() == repeated.count());
    std::size_t expected = 0;
    for (const auto position : independent.positions()) {
        const float distance = nearest(position);
        if (distance >= 2.0f && distance < 14.0f) {
            REQUIRE(expected < related.count());
            CHECK(related.positions()[expected] == position);
            ++expected;
        }
    }
    CHECK(related.count() == expected);
    for (std::size_t index = 0; index < related.count(); ++index) {
        CHECK(related.positions()[index] == repeated.positions()[index]);
        CHECK(related.scales()[index] == repeated.scales()[index]);
        CHECK(related.rotations()[index] == repeated.rotations()[index]);
    }
    CHECK(world::scatter(map, layer).count() == 0);

    SECTION("zero influence preserves the original population exactly") {
        layer.proximity->strength = 0.0f;
        const auto bypass = world::scatter(map, layer);
        REQUIRE(bypass.count() == independent.count());
        for (std::size_t index = 0; index < bypass.count(); ++index) {
            CHECK(bypass.positions()[index] == independent.positions()[index]);
            CHECK(bypass.rotations()[index] == independent.rotations()[index]);
            CHECK(bypass.scales()[index] == independent.scales()[index]);
        }
    }
    SECTION("soft edges thin the band without changing surviving transforms") {
        layer.proximity->fade = 4.0f;
        const auto faded = world::scatter(map, layer, anchors);
        REQUIRE(faded.count() > 10);
        CHECK(faded.count() < related.count());
        for (const auto position : faded.positions()) {
            CHECK(nearest(position) >= 2.0f);
            CHECK(nearest(position) < 14.0f);
            CHECK(std::ranges::find(related.positions(), position) != related.positions().end());
        }
    }
    SECTION("partial influence retains some independent growth without anchors") {
        layer.proximity->strength = 0.5f;
        const auto partial = world::scatter(map, layer);
        CHECK(partial.count() > independent.count() / 4);
        CHECK(partial.count() < independent.count() * 3 / 4);
    }
}

TEST_CASE("habitat dependencies are ordered, unique, serializable and structural",
          "[unit][ecology][habitat]") {
    world::Ecology ecology;
    ecology.layers.push_back(testLayer("canopy", {{"forest", 0.002f}}));
    auto ferns = testLayer("ferns", {{"forest", 0.03f}});
    ferns.proximity = world::ScatterProximity{"canopy", 1.0f, 12.0f, 2.0f, 0.9f};
    ecology.layers.push_back(ferns);
    const auto biomes = world::defaultBiomes();
    REQUIRE(ecology.validate(biomes));
    const auto parsed = world::ecologyFromJson(world::ecologyToJson(ecology));
    REQUIRE(parsed);
    CHECK(parsed->structuralHash() == ecology.structuralHash());
    for (auto member : {&world::ScatterProximity::minDistance, &world::ScatterProximity::maxDistance,
                        &world::ScatterProximity::fade, &world::ScatterProximity::strength}) {
        auto changed = ecology;
        (*changed.layers[1].proximity).*member += 0.1f;
        CHECK(changed.structuralHash() != ecology.structuralHash());
    }
    SECTION("unknown, self and forward dependencies fail") {
        ecology.layers[1].proximity->layer = "missing";
        CHECK_FALSE(ecology.validate(biomes));
        ecology.layers[1].proximity->layer = "ferns";
        CHECK_FALSE(ecology.validate(biomes));
        ecology.layers[1].proximity->layer = "canopy";
        std::swap(ecology.layers[0], ecology.layers[1]);
        CHECK_FALSE(ecology.validate(biomes));
    }
    SECTION("duplicate names cannot resolve ambiguously") {
        ecology.layers.push_back(ecology.layers[0]);
        CHECK_FALSE(ecology.validate(biomes));
    }
    SECTION("nonfinite and impossible fade settings fail") {
        for (const float invalid : {-1.0f, 9.0f, std::numeric_limits<float>::infinity(),
                                     std::numeric_limits<float>::quiet_NaN()}) {
            ferns.proximity->fade = invalid;
            CHECK_FALSE(ferns.validate());
        }
    }
}

TEST_CASE("a scatter distribution draws the cloud it was given", "[unit][procedural][ecology]") {
    // The distribution kind exists so an ecology pass can hand placements to the ordinary
    // instancing path. If the cloud does not come out the other side, everything above it is moot.
    scene::ProceduralGeometry pg;
    pg.name = "supplied";
    pg.distribution.kind = scene::DistributionKind::Scatter;
    auto cloud = std::make_shared<spatial::PointCloud>(3);
    cloud->positions()[0] = {1.0f, 2.0f, 3.0f};
    cloud->positions()[1] = {-4.0f, 5.0f, 6.0f};
    cloud->positions()[2] = {7.0f, -8.0f, 9.0f};
    for (int i = 0; i < 3; ++i) {
        cloud->rotations()[static_cast<std::size_t>(i)] = {0.0f, 0.0f, 0.0f, 1.0f};
        cloud->scales()[static_cast<std::size_t>(i)] = glm::vec3(1.0f);
    }
    pg.distribution.scatterCloud = cloud;
    pg.distribution.scatterHash = 12345;
    REQUIRE(pg.validate().has_value());
    CHECK(pg.distribution.instanceCount() == 3);
    const spatial::PointCloud built = pg.generateCloud();
    REQUIRE(built.count() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(built.positions()[i] == cloud->positions()[i]);
    }
    // The hash the generator supplied is what makes a changed cloud a structural change.
    const std::uint64_t before = pg.structuralHash();
    pg.distribution.scatterHash = 999;
    CHECK(pg.structuralHash() != before);
}

TEST_CASE("a water surface has no vertical faces in it", "[unit][water]") {
    // The artefact this pins down, and the one the shipped world produced: a body whose level sits
    // above the ground beside it reaches only to the edge of its own carve and stops there, and the
    // quads of that edge stand up as a wall -- on screen, a flat slab with a row of vertical fins
    // under it. A body that ends where the ground rises through it has no such faces.
    //
    // Deliberately not measured as "depth at a vertex": a six-metre-deep channel is deep water, not
    // a wall, and the first version of this test failed the world for having a river in it.
    const world::WorldMap map = world::defaultWorld();
    world::TerrainSettings settings;
    settings.resolution = 32;
    const float spacing = settings.chunkSize / static_cast<float>(settings.resolution);

    int wetChunks = 0;
    float worstStep = 0.0f;
    for (const glm::ivec2 coord : world::chunkGrid(map, settings)) {
        const scene::MeshData mesh = world::buildChunkWater(map, settings, coord, nullptr);
        if (!mesh.valid()) {
            continue;
        }
        ++wetChunks;
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const glm::vec3 a = mesh.vertices[mesh.indices[t]].position;
            const glm::vec3 b = mesh.vertices[mesh.indices[t + 1]].position;
            const glm::vec3 c = mesh.vertices[mesh.indices[t + 2]].position;
            const float step = std::max({std::fabs(a.y - b.y), std::fabs(b.y - c.y), std::fabs(a.y - c.y)});
            worstStep = std::max(worstStep, step);
        }
    }
    INFO("wet chunks " << wetChunks << ", worst height step across one " << spacing << " m quad " << worstStep);
    CHECK(wetChunks > 4); // there is a river, or this proves nothing
    // A river descending a valley tilts its surface a little; a wall jumps metres in one quad.
    CHECK(worstStep < spacing * 0.5f);
}

TEST_CASE("Glow aggregation reduces a scatter layer to bounded emitters", "[unit][world][ecology]") {
    using namespace avgen;
    world::ScatterLayer layer;
    layer.name = "fungi";
    layer.height = 0.4f;
    layer.emissiveColor = glm::vec3(0.25f, 0.05f, 1.0f);
    layer.emissiveIntensity = 12.0f;

    // Two tight clumps 40 m apart, plus one straggler, all on a 10 m grid.
    spatial::PointCloud cloud(7);
    auto p = cloud.positions();
    auto s = cloud.scales();
    const glm::vec3 clumpA(2.0f, 0.0f, 2.0f);
    const glm::vec3 clumpB(42.0f, 0.0f, 2.0f);
    p[0] = clumpA; p[1] = clumpA + glm::vec3(0.5f, 0.0f, 0.0f); p[2] = clumpA + glm::vec3(0.0f, 0.0f, 0.5f);
    p[3] = clumpB; p[4] = clumpB + glm::vec3(0.4f, 0.0f, 0.3f);
    p[5] = clumpB + glm::vec3(0.2f, 0.0f, 0.1f); p[6] = glm::vec3(-38.0f, 0.0f, -22.0f);
    for (std::size_t i = 0; i < s.size(); ++i) {
        s[i] = glm::vec3(1.0f);
    }

    const auto clusters = world::aggregateGlow(cloud, layer, 10.0f);

    SECTION("one emitter per occupied cell, not per instance") {
        REQUIRE(clusters.size() == 3);
    }

    SECTION("power is the emitting area of the cell, so a clump outweighs a straggler") {
        auto near = std::ranges::find_if(clusters, [&](const world::GlowCluster& g) {
            return std::abs(g.position.x - clumpA.x) < 5.0f;
        });
        auto lone = std::ranges::find_if(clusters, [](const world::GlowCluster& g) {
            return g.position.x < -30.0f;
        });
        REQUIRE(near != clusters.end());
        REQUIRE(lone != clusters.end());
        CHECK(near->power > lone->power * 2.5f);
    }

    SECTION("the emitter sits in the glowing organ, not at the root") {
        CHECK(clusters.front().position.y > 0.0f);
    }

    SECTION("colour is normalised to unit peak so power carries the magnitude") {
        const glm::vec3 c = clusters.front().color;
        CHECK_THAT(std::max({c.x, c.y, c.z}), Catch::Matchers::WithinAbs(1.0, 1e-5));
    }

    SECTION("a layer that does not emit produces nothing to light with") {
        world::ScatterLayer dark = layer;
        dark.emissiveIntensity = 0.0f;
        CHECK(world::aggregateGlow(cloud, dark, 10.0f).empty());
    }

    SECTION("the order does not depend on hash iteration, because callers take a prefix") {
        const auto again = world::aggregateGlow(cloud, layer, 10.0f);
        REQUIRE(again.size() == clusters.size());
        for (std::size_t i = 0; i < again.size(); ++i) {
            CHECK(again[i].position.x == clusters[i].position.x);
            CHECK(again[i].position.z == clusters[i].position.z);
        }
    }
}

TEST_CASE("Colour that clusters in space, and glow that is rare", "[unit][world][ecology]") {
    using namespace avgen;

    SECTION("the region field delivers the amplitude a caller asks for") {
        // Raw fbm bunches around its midpoint, so feeding it straight into a "how far this swings"
        // setting delivers a fraction of it. The field is stretched to fix that; if the stretch is
        // ever removed, the spread collapses and every setting quietly means a third of itself.
        float lo = 1.0f;
        float hi = -1.0f;
        double sum = 0.0;
        int n = 0;
        for (int x = -40; x <= 40; ++x) {
            for (int z = -40; z <= 40; ++z) {
                const float v = noise::regionField(glm::vec3(static_cast<float>(x) * 0.37f, 0.0f,
                                                             static_cast<float>(z) * 0.37f), 7u);
                lo = std::min(lo, v);
                hi = std::max(hi, v);
                sum += static_cast<double>(v);
                ++n;
            }
        }
        CHECK(lo < -0.75f);
        CHECK(hi > 0.75f);
        CHECK(std::abs(sum / n) < 0.2);   // centred, so a hue swing goes both ways
        // Smooth: neighbours agree, which is the whole point of a region.
        const glm::vec3 p(3.1f, 0.0f, -2.4f);
        CHECK(std::abs(noise::regionField(p, 7u) - noise::regionField(p + glm::vec3(0.01f, 0.0f, 0.0f), 7u)) < 0.05f);
    }

    SECTION("sparsity leaves most specimens dark and scales the light they cast") {
        world::ScatterLayer layer;
        layer.name = "canopy";
        layer.height = 12.0f;
        layer.emissiveColor = glm::vec3(0.06f, 1.0f, 0.72f);
        layer.emissiveIntensity = 5.0f;

        spatial::PointCloud cloud(64);
        auto p = cloud.positions();
        auto s = cloud.scales();
        for (std::size_t i = 0; i < p.size(); ++i) {
            p[i] = glm::vec3(static_cast<float>(i % 8), 0.0f, static_cast<float>(i / 8));
            s[i] = glm::vec3(1.0f);
        }
        const auto full = world::aggregateGlow(cloud, layer, 100.0f);
        layer.emissiveSparsity = 0.75f;
        const auto sparse = world::aggregateGlow(cloud, layer, 100.0f);
        REQUIRE(full.size() == 1);
        REQUIRE(sparse.size() == 1);
        // A quarter of the trees light up, so the patch casts a quarter of the light.
        CHECK_THAT(d(sparse.front().power), Catch::Matchers::WithinRel(d(full.front().power) * 0.25, 1e-4));
    }
}
