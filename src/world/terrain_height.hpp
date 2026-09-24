#pragma once

// The terrain's height, baked once into a grid the GPU can read (ADR-715, ADR-575 §18).
//
// Terrain reaches the GPU as chunk meshes, and "where is the ground under this point" was a CPU
// question only -- `WorldMap::height`, `TerrainQuery::heightAt`. The volumetric march samples its
// density tens of times per pixel, so a fog layer that follows the ground needs the answer on the
// GPU, and ADR-575 §18 forbade getting it by per-frame CPU work. The terrain is static, so the
// answer is a bake: sampled once when the terrain is built, cached on `TerrainProducts` beside the
// chunk meshes it is a sibling of, uploaded once, and read by texel fetch.
//
// **The grid is vertex-aligned.** Sample (i, j) is the height at `origin + (i, j) * spacing`
// exactly, and the first and last samples sit ON the map's edges -- so the bake reproduces
// `WorldMap::height` bit for bit at every grid point, and bilinear interpolation between them is
// the whole of its approximation. A texel-centred grid would put no sample on the edge at all.
//
// **Sampled the same way on both sides.** `TerrainGround::groundAt` is the CPU statement of the
// WGSL `terrainGroundAt` in `shaders/height_fog.wgsl`: a manual bilinear of four texel loads, then
// the node's height transform, then a fade to zero outside the footprint. Manual rather than a
// filtering sampler because an R32F texture is not filterable without an optional device feature,
// and because four loads are the same arithmetic on every GPU, which a hardware filter's 8-bit
// fractional weights are not.

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace avgen::world {

struct WorldMap;

// The bake in the terrain's own (node-local) space: heights in metres, row-major from -X/-Z.
struct TerrainHeightField {
    glm::vec2 origin{0.0f};  // local XZ of sample (0, 0) -- the map's min corner
    float spacing = 1.0f;    // metres between samples, the same along X and Z
    std::uint32_t width = 0; // samples along X
    std::uint32_t depth = 0; // samples along Z
    std::vector<float> heights;
    // What the bake was taken from: the same key `TerrainProducts` is cached under. The renderer
    // re-uploads when this moves and at no other time.
    std::uint64_t hash = 0;

    [[nodiscard]] bool empty() const { return width < 2 || depth < 2 || heights.empty(); }
    [[nodiscard]] float at(std::uint32_t i, std::uint32_t j) const {
        return heights[static_cast<std::size_t>(j) * width + i];
    }
    // The far corner, local XZ.
    [[nodiscard]] glm::vec2 extentMax() const {
        return origin + glm::vec2(static_cast<float>(width - 1), static_cast<float>(depth - 1)) * spacing;
    }
    // Bilinear between the four samples around `local`, clamped to the grid's edge.
    [[nodiscard]] float bilinear(glm::vec2 local) const;
};

// The resolution chosen for a map `size` metres across (ADR-715): 2 m between samples, coarsened
// only when that would pass 1024 samples on a side. See the ADR for why 2 m.
constexpr float kTerrainHeightSpacing = 2.0f;
constexpr std::uint32_t kTerrainHeightMaxSamples = 1024;
[[nodiscard]] float terrainHeightSpacing(glm::vec2 size);

// Samples `map.height` on the vertex-aligned grid over the map's whole extent. Threaded over rows;
// every sample is independent and the result does not depend on the thread count.
[[nodiscard]] TerrainHeightField bakeTerrainHeight(const WorldMap& map, std::uint64_t hash = 0);

// The bake placed in the world: which field, and the terrain node's transform reduced to what a
// height field can honour -- a translation and a per-axis scale. A rotated terrain node is not
// baked at all (the builder says so); a height field under a yaw would need the inverse rotation
// per sample, and no scene has asked for one.
struct TerrainGround {
    std::shared_ptr<const TerrainHeightField> field;
    glm::vec3 translation{0.0f};
    glm::vec3 scale{1.0f};
    // Metres past the footprint over which the ground fades to 0, so the layer eases back to the
    // flat plane rather than stepping down a cliff at the world's edge. Set by `placeTerrainGround`.
    float fade = 32.0f;

    [[nodiscard]] bool valid() const { return field != nullptr && !field->empty(); }
    // World XZ of the footprint's two corners.
    [[nodiscard]] glm::vec2 worldMin() const;
    [[nodiscard]] glm::vec2 worldMax() const;
    // The ground height at world XZ, in world metres: the bake's bilinear inside the footprint,
    // the edge's height fading to 0 across `fade` metres outside it, and 0 beyond that -- which is
    // what makes a ground-following fog fall back to the flat plane off the terrain. 0 when there
    // is no field. The CPU twin of `terrainGroundAt` in shaders/height_fog.wgsl.
    [[nodiscard]] float groundAt(glm::vec2 worldXZ) const;
    // The two vec4 lanes the shaders read (FrameUniforms::terrainMap0/1, the particle mirror).
    //   map0 = (world origin x, world origin z, 1 / world spacing x, 1 / world spacing z)
    //   map1 = (height scale, height offset, fade metres, 1 when valid else 0)
    [[nodiscard]] glm::vec4 map0() const;
    [[nodiscard]] glm::vec4 map1() const;
};

// Places `field` under a node transform (translation, rotation, scale). Returns an invalid ground
// when the rotation is not the identity, for the reason on `TerrainGround`.
[[nodiscard]] TerrainGround placeTerrainGround(std::shared_ptr<const TerrainHeightField> field,
                                               const glm::vec3& translation, const glm::vec3& scale,
                                               bool rotated);

} // namespace avgen::world
