#pragma once

// CHRONOS (ADR-1933): a scene-level block, `"chronosCanyon"`, that holds a procedural river canyon drawn as engine
// geometry. docs/prototypes/chronos/ holds the brief, the plan and the progress log.
//
// The canyon is carved along a river the scene AUTHORS (Catmull-Rom control points in world xz), not one a seed
// invents: the brief asks for a canyon that feels handcrafted, and the camera that flies it is authored along the same
// line. Everything else -- the stepped sandstone walls, their alcoves and gullies, the benches, the plateau, the far
// ranges that close the horizon, the spires -- is a pure function of `ChronosCanyonGen`, deterministic for its seed.
//
// Strange Attractor's lessons it is built on (docs/prototypes/chronos/02-recon.md §6):
//   * engine triangles, through the shared crack-free RTIN (`scene::rtin`, ADR-1930), the tolerance growing with the
//     distance from the river (the camera's line);
//   * normals from the continuous surface, not from the triangles, so decimation never shows as facets;
//   * the strata coordinate as a vertex attribute (uv.x, in bands) that the triangulation must carry, and the baked
//     ambient visibility in uv.y -- the `canyonRock` program's packing, so the scene's material program reads it;
//   * the river is a flat sheet cut from the same field, so it cannot misalign with its banks.
//
// Not here, on purpose: the temporal imaging system. The canyon knows nothing about it (CHRONOS brief §2.2.9).

#include "core/error.hpp"
#include "scene/water_surface.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace avgen::scene {

// The structural knobs. Any change regenerates the canyon (seconds, on the CPU, at load).
struct ChronosCanyonGen {
    std::uint32_t seed = 1;
    int resolution = 2048;       // height-field texels a side (256..4096)
    float extent = 12000.0f;     // metres across the baked square, centred on the origin
    std::vector<glm::vec2> river; // the river's control points, world xz (at least 2)
    // The canyon's cross-section, metres.
    float floorHalfWidth = 45.0f;  // the river and its banks
    float rimHalfWidth = 480.0f;   // where the walls top out onto the plateau
    float depth = 300.0f;          // the rim's height above the water
    float tierHeight = 42.0f;      // one cliff-and-bench step of the stepped walls
    float riserShare = 0.38f;      // the share of a step that is cliff (0..1); the rest slopes as a bench
    float terraces = 0.85f;        // 0 a smooth V .. 1 fully stepped
    float widthVariation = 0.35f;  // how much the canyon's width swells and pinches along the river (0..1)
    float alcoves = 0.5f;          // the walls' bays and buttresses (0..1)
    float gullies = 0.5f;          // erosion channels down the walls (0..1)
    float plateauRelief = 45.0f;   // metres of rolling relief on the plateau
    float rangeHeight = 1100.0f;   // the far ranges that close the horizon
    float rangeStart = 2600.0f;    // metres from the river where they begin to rise
    float rangeEnd = 6000.0f;      // and where they reach full height
    float band = 9.0f;             // metres per stratum (the uv.x coordinate's unit)
    float strataTilt = 0.006f;     // the strata's dip, metres per metre
    float waterLevel = -1.0f;      // the river's surface
    float bedDepth = 4.0f;         // the bed below it at the channel's centre
    // Free-standing sandstone spires (hoodoos): a footprint, a height above the local ground, a radius.
    struct Spire {
        glm::vec2 at{0.0f};
        float height = 60.0f;
        float radius = 18.0f;
    };
    std::vector<Spire> spires;
    // The mesh (the RTIN's tolerance, ADR-1930): the sampling grid in metres, the tolerated vertical error per metre of
    // distance from the river, and its floor near the river.
    float meshSpacing = 1.5f;
    float meshError = 0.003f;
    float meshMinError = 0.2f;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t hash() const;
};

struct ChronosTerrainChunk {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    // x the strata coordinate in bands; y (canyonRock's packing) a whole floor level 0..16 plus 0.05 x the baked
    // ambient visibility (0..1)
    std::vector<glm::vec2> uvs;
    std::vector<std::uint32_t> indices;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

struct ChronosCanyonMesh {
    std::vector<ChronosTerrainChunk> chunks;
    std::vector<glm::vec3> riverPositions; // the water sheet, over every cell whose ground dips below the water line
    std::vector<std::uint32_t> riverIndices;
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    float sampleSpacing = 0.0f;
    double buildSeconds = 0.0;
};

inline constexpr int kChronosFloorLevels = 16;      // canyonRock's playa levels
inline constexpr float kChronosOcclusionScale = 0.05f;

// What the generator builds. Immutable once made; shared by pointer.
struct ChronosCanyonWorld {
    ChronosCanyonGen gen;
    std::uint64_t hash = 0;
    int res = 0;
    float extent = 0.0f;
    glm::vec2 origin{0.0f};        // world xz of texel (0, 0)'s corner
    std::vector<float> height;     // res*res metres, row-major (z rows, x columns)
    std::vector<float> strata;     // res*res: the strata coordinate in bands
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
    std::vector<glm::vec2> riverLine; // the river's centreline, densely sampled (about 4 m), near to far
    float riverLength = 0.0f;
    std::shared_ptr<const ChronosCanyonMesh> mesh;
    double generateSeconds = 0.0;

    [[nodiscard]] float sample(glm::vec2 xz) const; // bilinear height; clamps outside
};

[[nodiscard]] std::shared_ptr<const ChronosCanyonWorld> generateChronosCanyon(const ChronosCanyonGen& gen);
// The canyon for `gen`: the last one generated when its hash matches, otherwise a fresh one. Thread-safe.
[[nodiscard]] std::shared_ptr<const ChronosCanyonWorld> acquireChronosCanyon(const ChronosCanyonGen& gen);
[[nodiscard]] std::uint64_t chronosCanyonGenerations() noexcept;

struct ChronosCanyonBlock {
    bool enabled = false;
    ChronosCanyonGen gen;
    std::string terrainProgram = "chronosRock"; // the scene file's material program for the walls
    WaterSettings water;
    std::shared_ptr<const ChronosCanyonWorld> world; // set by the Composition
    [[nodiscard]] bool active() const { return enabled && world != nullptr; }
};

[[nodiscard]] Result<ChronosCanyonBlock> chronosCanyonFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json chronosCanyonToJson(const ChronosCanyonBlock& b);

} // namespace avgen::scene
