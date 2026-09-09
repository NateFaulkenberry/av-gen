#pragma once

// Terrain (ADR-046): a WorldMap turned into drawable geometry. A fixed grid of square chunks over
// the world extent, each built at several resolutions, so the renderer can pick a resolution per
// chunk per frame and skip the chunks the camera cannot see.
//
// Why chunks and not one mesh. A 640 m world at 1.25 m spacing is half a million triangles; drawn
// as one mesh it is always entirely resident, entirely rasterised, and entirely uniform in detail.
// Split into chunks it becomes: cull what is behind you, coarsen what is far away, and keep the
// full resolution only where the camera is standing. The cost of that is cracks at the seam between
// two resolutions, which `skirtDepth` hides by dropping a short vertical curtain around every
// chunk -- lit as the terrain around it, so it is invisible even when it is on screen.
//
// Everything here is a pure function of the map and the settings. Chunk meshes are built once and
// live in the Scene's mesh list; per frame only the chunk-to-mesh mapping and visibility change,
// which is why terrain costs nothing to move the camera through.

#include "core/error.hpp"
#include "scene/material_program.hpp"
#include "scene/scene_types.hpp"
#include "world/world_map.hpp"

#include <array>
#include <functional>
#include <cstdint>
#include <vector>

namespace avgen::world {

constexpr int kMaxTerrainLods = 4;

struct TerrainSettings {
    float chunkSize = 40.0f;    // metres per chunk edge
    int resolution = 32;        // quads per chunk edge at LOD 0 (halves per level, floor 2)
    int lodLevels = 4;          // 1..kMaxTerrainLods
    float lodDistance = 80.0f;  // metres at which LOD 1 begins; each level doubles the distance
    float viewDistance = 460.0f;// metres beyond which a chunk is not drawn at all
    float skirtDepth = 4.0f;    // metres the seam curtain hangs below the chunk edge

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t structuralHash() const;
};

// One chunk of the world. `meshes` is filled by whoever uploads the meshes (the Composition);
// terrain building itself does not know what a MeshId is.
struct TerrainChunk {
    glm::ivec2 coord{0, 0};
    glm::vec2 center{0.0f};          // world XZ of the chunk centre
    glm::vec3 boundsMin{0.0f};       // world AABB, from the LOD 0 mesh (the tallest of the set)
    glm::vec3 boundsMax{0.0f};
    std::array<scene::MeshId, kMaxTerrainLods> meshes{};
};

// The chunk grid covering the map, in row-major order from -X/-Z. The world extent is covered
// exactly: a partial chunk at the edge is still a whole chunk, clipped by the map's own bounds
// being where the features stop, not by the mesh being cut.
[[nodiscard]] std::vector<glm::ivec2> chunkGrid(const WorldMap& map, const TerrainSettings& settings);
[[nodiscard]] glm::vec2 chunkOrigin(const WorldMap& map, const TerrainSettings& settings, glm::ivec2 coord);

// The mesh for one chunk at one LOD.
//
// Vertex uv carries (biome axis, slope) rather than a texture coordinate. Terrain has no unwrap
// worth having, and the material op set already reaches world space directly through Triplanar and
// WorldProject, so a tiling uv would be the one thing on the vertex nobody reads. What a terrain
// material does need is these two scalars: which kind of place this is, and how steep it is.
//
// The biome axis is the position of this point's biome blend along the authored order of the set
// (ADR-047), so a material blends a palette along it and gets a band at every transition for free.
// It replaced altitude, which is no loss: altitude is one of the three things a biome rule is
// written in, so an altitude-banded look is now an authored biome rather than an implicit one.
// A world with no biomes leaves the axis at the point's altitude, so terrain still has somewhere
// to blend along before any biome is written.
//
// Normals come from central differences at the LOD 0 spacing regardless of the level being built,
// so two chunks meeting at different resolutions shade continuously even though their silhouettes
// differ.
[[nodiscard]] scene::MeshData buildChunkMesh(const WorldMap& map, const TerrainSettings& settings,
                                             glm::ivec2 coord, int lod);

// LOD level for a chunk whose centre is `distance` metres from the camera: 0 within `lodDistance`,
// then one level per doubling. Returns lodLevels-1 at most.
[[nodiscard]] int chunkLod(const TerrainSettings& settings, float distance);

// The ground material for a biome set, generated rather than authored (ADR-047). A biome's colours
// live in the world JSON, and a scene that also wrote them into a material program would have two
// copies of them to keep in step -- which is how a scene ends up with the ground painted in last
// week's palette. The program blends both palettes along the vertex's biome axis and crosses from
// ground to rock on its slope, so an artist retunes a biome and the ground follows with no shader
// editing at all. A scene that wants something else names its own program and this is not used.
[[nodiscard]] scene::MaterialProgram terrainMaterialProgram(const BiomeSet& biomes, std::string name);

// The six frustum planes (left, right, bottom, top, near, far) of a view-projection, in world
// space, normalised, pointing inwards. rendering::frustumPlanes is the same construction for GPU
// instance culling; this copy exists so terrain culling stays in the GPU-free core library.
using FrustumPlanes = std::array<glm::vec4, 6>;
[[nodiscard]] FrustumPlanes frustumPlanes(const glm::mat4& viewProjection);
[[nodiscard]] bool aabbVisible(const FrustumPlanes& planes, const glm::vec3& min, const glm::vec3& max);

// A chunk's height field, sampled once at LOD 0 spacing with a one-cell border. Every level of the
// chunk is then a stride through it, which is what makes the levels agree exactly and costs one
// height evaluation per point instead of the five an analytic normal needs.
struct ChunkField {
    int side = 0;          // resolution + 3: the chunk's grid plus a border cell on each side
    float step = 1.0f;     // LOD 0 spacing, in metres
    glm::vec2 origin{0.0f};// world XZ of grid index (0, 0), one cell before the chunk's corner
    std::vector<float> heights;
    [[nodiscard]] float at(int i, int j) const;         // chunk coordinates: -1 .. resolution + 1
    // `spread` is how many cells the central difference reaches. One gives the surface normal the
    // mesh is shaded with. More gives the slope of the hillside rather than of the ripple on it,
    // which is what a biome rule wants: a rule fed mesh-scale slope puts a biome boundary on every
    // bump, and a hundred one-vertex boundaries across a ridge is a sawtooth, not an ecotone.
    [[nodiscard]] glm::vec3 normalAt(int i, int j, int spread = 1) const;
};
[[nodiscard]] ChunkField sampleChunkField(const WorldMap& map, const TerrainSettings& settings, glm::ivec2 coord);

// Builds every chunk at every level. `emit(chunkIndex, lod, mesh)` receives each mesh in build
// order; the caller decides where meshes live. Returns the chunks with bounds filled in.
//
// The meshes themselves are built on as many threads as the machine has, because a world is a few
// hundred chunks of independent arithmetic and doing it serially is seconds of a cold start. `emit`
// is still called once per mesh, in order, on the calling thread: it hands meshes to the Scene,
// which is not thread safe and does not need to be.
[[nodiscard]] std::vector<TerrainChunk> buildTerrain(
    const WorldMap& map, const TerrainSettings& settings,
    const std::function<scene::MeshId(std::size_t chunkIndex, int lod, scene::MeshData&& mesh)>& emit);

} // namespace avgen::world
