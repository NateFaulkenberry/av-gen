// Character Animation Lab -- the grounding layer (ADR-260).
//
// The question this file exists to answer is not "is the character's y correct?" but "correct
// against *what surface*?". A character is grounded against `world::WorldMap::height()`, which is
// an analytic function with no resolution at all. It is *drawn* standing on `buildChunkMesh`, a
// piecewise-linear approximation of that same function whose vertex spacing depends on how far the
// camera happens to be. Those are two different surfaces, and nothing in this repository has ever
// compared them.
//
// Where they differ, the difference has a sign, and the sign is the symptom:
//
//     concave ground (a valley, a hollow)   chord lies ABOVE the analytic surface
//                                           -> feet are under the drawn floor: CLIPPING
//
//     convex ground (a ridge, a hilltop)    chord lies BELOW the analytic surface
//                                           -> feet are over the drawn floor: FLOATING
//
// That is §19 of the lab brief -- the valley/convexity test -- and it is measurable on the CPU with
// no renderer involved, because both surfaces are pure functions of the map.
//
// ADR-182: every arm here has a control. The control is LOD 0. If the deviation were an artefact of
// the way this file samples a triangle, LOD 0 would show it too; LOD 0 is the same surface sampled
// eight times as finely, so a measurement that grows with the level and vanishes at the bottom of
// it is a measurement of the level and not of the method.

#include "scene/scene_types.hpp"
#include "world/terrain.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

using namespace avgen;

namespace {

// Height of the drawn surface at `p`, read off the triangles themselves.
//
// Deliberately brute force and deliberately barycentric: interpolating the triangle is what the
// rasteriser does, so this reports the surface a pixel would land on rather than the nearest vertex,
// which would understate the error by exactly the thing being measured.
std::optional<float> meshHeightAt(const scene::MeshData& mesh, glm::vec2 p) {
    const auto edge = [](glm::vec2 a, glm::vec2 b, glm::vec2 c) {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    };
    std::optional<float> best;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const glm::vec3 p0 = mesh.vertices[mesh.indices[i + 0]].position;
        const glm::vec3 p1 = mesh.vertices[mesh.indices[i + 1]].position;
        const glm::vec3 p2 = mesh.vertices[mesh.indices[i + 2]].position;
        const glm::vec2 a{p0.x, p0.z};
        const glm::vec2 b{p1.x, p1.z};
        const glm::vec2 c{p2.x, p2.z};
        const float area = edge(a, b, c);
        if (std::fabs(area) < 1e-12f) {
            continue; // a skirt curtain is vertical: zero footprint, nothing to stand on
        }
        const float w0 = edge(b, c, p) / area;
        const float w1 = edge(c, a, p) / area;
        const float w2 = edge(a, b, p) / area;
        if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f) {
            continue;
        }
        const float h = w0 * p0.y + w1 * p1.y + w2 * p2.y;
        // A chunk carries a skirt whose triangles project onto the same footprint as the surface
        // above them. The surface is the highest of them, which is what a character stands on.
        best = best ? std::max(*best, h) : h;
    }
    return best;
}

struct Deviation {
    float worstBelow = 0.0f; // most the drawn surface sits ABOVE analytic (character clips in)
    float worstAbove = 0.0f; // most the drawn surface sits BELOW analytic (character floats)
    float meanAbs = 0.0f;
    int samples = 0;
    glm::vec2 worstBelowAt{0.0f};
};

// Sweeps the interior of one chunk and compares the two surfaces.
//
// The chunk interior only: a point outside the chunk's own footprint is another chunk's to draw,
// and including the boundary would measure the seam rather than the surface.
Deviation surveyChunk(const world::WorldMap& map, const world::TerrainSettings& settings,
                      glm::ivec2 coord, int lod, int steps = 61) {
    const scene::MeshData mesh = world::buildChunkMesh(map, settings, coord, lod);
    const glm::vec2 origin = world::chunkOrigin(map, settings, coord);
    Deviation dev;
    double sumAbs = 0.0;
    for (int j = 1; j < steps; ++j) {
        for (int i = 1; i < steps; ++i) {
            const glm::vec2 p = origin + glm::vec2(static_cast<float>(i), static_cast<float>(j)) /
                                             static_cast<float>(steps) * settings.chunkSize;
            const std::optional<float> drawn = meshHeightAt(mesh, p);
            if (!drawn) {
                continue;
            }
            const float analytic = map.height(p);
            const float delta = *drawn - analytic; // positive = drawn floor above the feet
            if (delta > dev.worstBelow) {
                dev.worstBelow = delta;
                dev.worstBelowAt = p;
            }
            dev.worstAbove = std::max(dev.worstAbove, -delta);
            sumAbs += static_cast<double>(std::fabs(delta));
            ++dev.samples;
        }
    }
    if (dev.samples > 0) {
        dev.meanAbs = static_cast<float>(sumAbs / dev.samples);
    }
    return dev;
}

} // namespace

// ---- the reproduction ---------------------------------------------------------------------------

TEST_CASE("the drawn ground and the ground a character is placed on are not the same surface",
          "[unit][charlab][grounding]") {
    const world::WorldMap map = world::defaultWorld();
    world::TerrainSettings settings; // the shipped defaults: 40 m chunks, 32 quads at LOD 0

    // A chunk with real relief in it. Picked once and then fixed, so this is a regression pin
    // rather than a search: a test that hunts for the worst chunk every run measures the hunt.
    const glm::ivec2 coord{6, 7};

    const Deviation lod0 = surveyChunk(map, settings, coord, 0);
    const Deviation lod3 = surveyChunk(map, settings, coord, 3);

    REQUIRE(lod0.samples > 1000);
    REQUIRE(lod3.samples > 1000);

    INFO("LOD0 mean |dev| " << lod0.meanAbs << " m, drawn-above-feet " << lod0.worstBelow
                            << " m, drawn-below-feet " << lod0.worstAbove << " m");
    INFO("LOD3 mean |dev| " << lod3.meanAbs << " m, drawn-above-feet " << lod3.worstBelow
                            << " m, drawn-below-feet " << lod3.worstAbove << " m");

    // The control. At LOD 0 the vertex spacing is 1.25 m and the two surfaces are close; this is
    // what says the measurement is of the tessellation rather than of the sampling method.
    CHECK(lod0.meanAbs < 0.20f);

    // The arm. Coarsening the same chunk three levels -- which the renderer does on distance alone,
    // to ground a character may well be standing on -- pulls the drawn surface away from the one
    // grounding believes in.
    CHECK(lod3.meanAbs > lod0.meanAbs * 2.0f);

    // And the direction that produces the reported symptom: ground drawn *above* where the feet
    // were put. This is characters clipping into terrain, and it is not an animation bug.
    CHECK(lod3.worstBelow > 0.5f);
}

TEST_CASE("terrain LOD deviation is what decides whether a character clips or floats",
          "[unit][charlab][grounding]") {
    const world::WorldMap map = world::defaultWorld();
    world::TerrainSettings settings;

    // Both signs occur, and they are a property of curvature rather than of the level: a chord
    // across a hollow lies above the ground, a chord across a ridge lies below it.
    float sawClipping = 0.0f;
    float sawFloating = 0.0f;
    for (const glm::ivec2 coord : {glm::ivec2{5, 5}, glm::ivec2{6, 7}, glm::ivec2{8, 8}, glm::ivec2{4, 9}}) {
        const Deviation dev = surveyChunk(map, settings, coord, 3, 41);
        sawClipping = std::max(sawClipping, dev.worstBelow);
        sawFloating = std::max(sawFloating, dev.worstAbove);
    }
    INFO("worst clip-in " << sawClipping << " m, worst float " << sawFloating << " m");
    CHECK(sawClipping > 0.25f);
    CHECK(sawFloating > 0.25f);
}

TEST_CASE("the deviation grows monotonically with the level the renderer picks",
          "[unit][charlab][grounding]") {
    const world::WorldMap map = world::defaultWorld();
    world::TerrainSettings settings;
    const glm::ivec2 coord{6, 7};

    std::vector<float> mean;
    for (int lod = 0; lod < world::kMaxTerrainLods; ++lod) {
        mean.push_back(surveyChunk(map, settings, coord, lod, 41).meanAbs);
    }
    INFO("mean |dev| by level: " << mean[0] << " " << mean[1] << " " << mean[2] << " " << mean[3]);
    // Each level doubles the vertex spacing, so each level is worse than the one below it. If this
    // ever stops being true the LOD ladder has changed and the numbers above need re-deriving.
    for (std::size_t i = 1; i < mean.size(); ++i) {
        CHECK(mean[i] > mean[i - 1]);
    }
}
