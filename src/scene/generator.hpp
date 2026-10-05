#pragma once

// The generator distribution (ADR-1117): a ProceduralGeometry whose records are written every frame
// by a GPU kernel (shaders/generator.wgsl) from a compact description, for the cells of a window
// around the camera, instead of being generated on the CPU at flatten and uploaded once.
//
// The world is a lattice of square cells of `cellSize` metres on the generator's XZ plane. Each cell
// holds at most one element, decided by hashing the cell's INTEGER coordinates with the seed: presence,
// then position (jitter inside the cell), size, yaw, tilt, the four random lanes and the colour and
// emission variation. Height comes from a value-noise ground. Nothing is stored between frames; the
// window moves with the camera, so an unbounded world costs what its window costs.
//
// The CPU MIRROR is this file. It is the rule, not the population: a pure function answers what is in
// one cell, in a region, along a ray, or how many elements a window holds. It holds no records, so it
// can never become a second copy of a million elements. Identity is exact: presence uses integer
// arithmetic only (hashes, an integer cluster lerp, integer region bounds), so the CPU and the GPU
// agree on which cells hold an element. Floats (position, size, rotation) agree to the last few bits.
// The one float decision is the optional disc (`regionRadius`): an element within an ulp of its rim
// may be classified differently by the two sides.
//
// Spaces: generator space is the generator's own XZ lattice (y up). A record is
//   object = distributionTransform * generator,
// and the object's world matrix is applied by the renderer as for every procedural. The camera is
// brought into generator space to centre the window.
//
// There is ONE kernel, "cells". A registry of generators is deliberately not built: the trigger for one
// is a second kernel (ADR-1117, "two use cases before a shared abstraction").

#include "core/error.hpp"
#include "scene/scene_types.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace avgen::scene {

struct GeneratorSpec {
    std::string name = "cells";       // the kernel; "cells" is the only one
    std::uint32_t version = 1;        // bumped when the kernel changes what a description means
    float cellSize = 1.0f;            // metres, > 0: at most one element per cell
    float viewDistance = 60.0f;       // the window's half side about the camera (metres)
    float presence = 0.6f;            // [0, 1]: the chance a fertile cell holds an element
    float clusterSize = 0.0f;         // metres; > 0 groups cells into regions that share a fertility
    float clusterContrast = 0.0f;     // [0, 1]: how barren the least fertile region is (1 = empty)
    float jitter = 1.0f;              // [0, 1] of a cell: how far from the cell centre an element may sit
    float sizeMin = 1.0f;             // uniform scale range
    float sizeMax = 1.0f;
    float tilt = 0.0f;                // radians: the largest lean, in a random direction
    bool bounded = false;             // true: only cells inside the region exist
    glm::vec2 regionMin{-50.0f};      // generator XZ, when bounded
    glm::vec2 regionMax{50.0f};
    float regionRadius = 0.0f;        // > 0 (bounded): also a disc of this radius about the region's centre
    float groundHeight = 0.0f;        // y of the ground
    float groundAmplitude = 0.0f;     // value-noise relief, metres (0 = flat)
    float groundFrequency = 0.02f;    // 1 / metres
    std::uint32_t groundSeed = 1;     // the ground's own seed: layers that share it stand on one ground

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t structuralHash() const; // what changes the buffer size: cell size, window, bounds
    [[nodiscard]] std::uint64_t hash() const;           // everything
};

// The kernels the scene may name. The validated list, not a registry.
inline constexpr const char* kGeneratorCells = "cells";
inline constexpr std::uint32_t kGeneratorCellsVersion = 1;
// A window may not hold more cells than this (records are 96 B: 4M cells = 384 MB). Refused at load.
inline constexpr std::uint64_t kMaxGeneratorCells = 4ull * 1024ull * 1024ull;

// The window of cells the kernel evaluates this frame.
struct GeneratorWindow {
    std::int32_t originX = 0; // lowest cell index on x
    std::int32_t originZ = 0;
    std::int32_t countX = 0;  // cells along x (records = countX * countZ)
    std::int32_t countZ = 0;
    [[nodiscard]] std::uint64_t cells() const {
        return static_cast<std::uint64_t>(countX) * static_cast<std::uint64_t>(countZ);
    }
};

// Variation the kernel applies to a present element (from the object's MaterialVariation).
struct GeneratorVariation {
    float valueRandom = 0.0f;
    float emissiveRandom = 0.0f;
    float emissiveSparsity = 0.0f;
};

// One element, as the kernel writes it (generator space unless noted).
struct GeneratedElement {
    std::int32_t ix = 0;
    std::int32_t iz = 0;
    glm::vec3 position{0.0f};      // generator space
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float size = 1.0f;
    glm::vec4 random{0.0f};        // the record's random lanes (random.w is what audio fields read)
    float value = 1.0f;            // colour multiplier
    float emission = 1.0f;         // emissive multiplier (0 for a sparse, unlit element)
};

// The integer core, shared with the kernel operation for operation.
[[nodiscard]] std::uint32_t generatorHash(std::int32_t ix, std::int32_t iz, std::uint32_t seed, std::uint32_t channel);
[[nodiscard]] float generatorUnit(std::uint32_t hash); // (hash >> 8) / 2^24, exact on both sides
[[nodiscard]] std::int32_t generatorFloorDiv(std::int32_t a, std::int32_t b);
// The integer threshold a cell's presence hash is compared with (fertility x presence, 32-bit).
[[nodiscard]] std::uint32_t generatorThreshold(const GeneratorSpec& spec, std::uint32_t seed, std::int32_t ix,
                                               std::int32_t iz);
// The cells the cluster fertility lattice spans (0 = no clustering).
[[nodiscard]] std::int32_t generatorClusterCells(const GeneratorSpec& spec);
// The region as inclusive integer cell bounds (only meaningful when bounded).
struct GeneratorRegionCells {
    std::int32_t minX = 0, minZ = 0, maxX = -1, maxZ = -1;
};
[[nodiscard]] GeneratorRegionCells generatorRegionCells(const GeneratorSpec& spec);
// The ground at (x, z) in generator space (seeded by `spec.groundSeed`, not the element seed).
[[nodiscard]] float generatorGroundHeight(const GeneratorSpec& spec, float x, float z);

// ---- the mirror's queries -----------------------------------------------------------------------

// The window centred on `cameraGen` (the camera in generator space), its half side scaled by `reach`
// (the live lever, 1 = as authored), clamped to the region when bounded.
[[nodiscard]] GeneratorWindow generatorWindow(const GeneratorSpec& spec, const glm::vec3& cameraGen, float reach = 1.0f);
// The largest window this description can ever produce (buffer capacity).
[[nodiscard]] GeneratorWindow generatorCapacity(const GeneratorSpec& spec);
// The element in cell (ix, iz), or nothing when the cell is empty (or outside the region).
[[nodiscard]] std::optional<GeneratedElement> generatorElement(const GeneratorSpec& spec, std::uint32_t seed,
                                                              const GeneratorVariation& variation, std::int32_t ix,
                                                              std::int32_t iz);
// Every element whose cell centre lies in [xzMin, xzMax], in cell order, at most `cap` (0 = no cap).
[[nodiscard]] std::vector<GeneratedElement> generatorQueryRegion(const GeneratorSpec& spec, std::uint32_t seed,
                                                                const GeneratorVariation& variation,
                                                                const glm::vec2& xzMin, const glm::vec2& xzMax,
                                                                std::size_t cap = 0);
// How many cells of `window` hold an element (the parity number: the GPU's present count).
[[nodiscard]] std::uint64_t generatorPresentCount(const GeneratorSpec& spec, std::uint32_t seed,
                                                 const GeneratorWindow& window);
// The nearest element whose bounding sphere (centre at the element plus `sourceCentre * size`, radius
// `sourceRadius * size`) the ray enters within `maxDistance`. Ray in generator space.
struct GeneratorHit {
    GeneratedElement element;
    float distance = 0.0f;
};
[[nodiscard]] std::optional<GeneratorHit> generatorRaycast(const GeneratorSpec& spec, std::uint32_t seed,
                                                           const GeneratorVariation& variation,
                                                           const glm::vec3& origin, const glm::vec3& direction,
                                                           float maxDistance, float sourceRadius,
                                                           const glm::vec3& sourceCentre = glm::vec3(0.0f));

// The element nearest `pointGen` whose bounding sphere (as generatorRaycast's) reaches within `slack` of
// it: what a click resolves to, given the world position the depth buffer says was clicked.
[[nodiscard]] std::optional<GeneratedElement> generatorNearest(const GeneratorSpec& spec, std::uint32_t seed,
                                                              const GeneratorVariation& variation,
                                                              const glm::vec3& pointGen, float sourceRadius,
                                                              float slack = 0.25f);

// ADR-1118: a region of a generator as explicit placements in generator space (apply the object's
// distributionTransform as for any distribution): what "bake to points" writes. At most `cap`.
[[nodiscard]] std::vector<Transform> generatorBake(const GeneratorSpec& spec, std::uint32_t seed,
                                                   const glm::vec2& xzMin, const glm::vec2& xzMax, std::size_t cap);

} // namespace avgen::scene
