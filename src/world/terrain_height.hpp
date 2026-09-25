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

#include <algorithm>
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
    // ADR-717: the same grid low-passed -- `heights` under a Gaussian of `kTerrainBasinSigma` metres
    // (in the field's own units), clamped at the edges. The level a pooling fog layer is measured
    // from: above the real ground in a valley, below it on a ridge. Baked beside `heights` by
    // `bakeTerrainHeight`, so it is made once per terrain build like them. Empty on a field that
    // was never pooled (`poolTerrainHeight` fills it).
    std::vector<float> basin;
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
    [[nodiscard]] bool pooled() const { return basin.size() == heights.size() && !heights.empty(); }
    [[nodiscard]] float basinAt(std::uint32_t i, std::uint32_t j) const {
        return basin[static_cast<std::size_t>(j) * width + i];
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

// ADR-717: the basin a pooling fog layer is measured from. A Gaussian of this standard deviation,
// in the field's own metres, taken over `heights` (half-maximum width 56 m, inside the 50-100 m
// ADR-715 proposed). The basin is the NEIGHBOURHOOD MEAN of the ground, so it stands above concave
// ground (a valley floor, the foot of a hill) and below convex ground (a ridge, a summit), and a
// hillside linear across the kernel is left where it is. Measured on Glowmere, a wider kernel is
// not "more pooling": at 64 m the foot of the big hill hazed over far more (ADR-717's basin-width
// sheet), because the summit enters the mean. ADR-717 says why a fixed length and not a control.
constexpr float kTerrainBasinSigma = 24.0f;
// Fills `field.basin` from `field.heights`: a separable Gaussian, truncated at three sigma and
// renormalised, with the grid's edge samples repeated outward. Deterministic and independent of
// thread count. `bakeTerrainHeight` calls it; a hand-built field (a test's) calls it itself.
void poolTerrainHeight(TerrainHeightField& field, float sigmaMetres = kTerrainBasinSigma);

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
    // ADR-717: a ground a pooling layer can be measured from -- valid, and its basin baked. Every
    // `bakeTerrainHeight` field is; only a hand-built one can be valid and not poolable.
    [[nodiscard]] bool poolable() const { return valid() && field->pooled(); }
    // ADR-717: the pooling lane the three readers get -- `pooling` clamped to 0..1, and 0 whenever
    // there is no basin to pool in, so such a scene takes the branch it always took.
    [[nodiscard]] float poolingLane(float pooling) const {
        return poolable() ? std::clamp(pooling, 0.0f, 1.0f) : 0.0f;
    }
    // World XZ of the footprint's two corners.
    [[nodiscard]] glm::vec2 worldMin() const;
    [[nodiscard]] glm::vec2 worldMax() const;
    // The ground height at world XZ, in world metres: the bake's bilinear inside the footprint,
    // the edge's height fading to 0 across `fade` metres outside it, and 0 beyond that -- which is
    // what makes a ground-following fog fall back to the flat plane off the terrain. 0 when there
    // is no field. The CPU twin of `terrainGroundAt` in shaders/height_fog.wgsl.
    [[nodiscard]] float groundAt(glm::vec2 worldXZ) const;
    // ADR-717: the low-passed ground (`TerrainHeightField::basin`) at world XZ, placed and faded
    // exactly as `groundAt` is. The CPU twin of the `.y` of `terrainGroundPairAt`. 0 when the field
    // has no basin.
    [[nodiscard]] float basinAt(glm::vec2 worldXZ) const;
    // ADR-717: the height the layer's top is measured from -- `mix(follow * ground, basin, pooling)`.
    // The CPU twin of `fogGroundReference` in shaders/height_fog.wgsl.
    [[nodiscard]] float referenceAt(glm::vec2 worldXZ, float follow, float pooling) const;
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
