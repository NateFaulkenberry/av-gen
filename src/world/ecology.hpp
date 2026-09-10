#pragma once

// Ecology (ADR-048): what grows where, decided by the same biome weights the ground is coloured
// with. A scatter layer names an asset, says how densely it occurs in each biome, and the placement
// pass turns that into a point cloud on the terrain surface.
//
// The reason this reads the biome weights rather than rules of its own is the whole point of the
// hierarchy: if the ground says marsh and the scatter says forest, no amount of tuning makes them
// agree. One source of truth, consulted twice.
//
// Placement is a jittered grid whose cell size comes from the layer's own peak density, so a sparse
// tree layer walks a coarse grid and a dense grass layer a fine one -- the cost is proportional to
// the number of instances, not to the area times the number of layers. Everything is a pure
// function of the map, the layer and its seed: the same world always grows the same forest.

#include "core/error.hpp"
#include "core/wind.hpp"

#include <glm/glm.hpp>
#include "spatial/point_cloud.hpp"
#include "world/world_map.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::world {

// How much of a layer occurs in one biome. Named rather than indexed so a scene reads as prose and
// so inserting a biome does not silently re-point every density in the file.
struct BiomeDensity {
    std::string biome;
    float density = 0.0f; // instances per square metre where that biome is at full weight
};

struct ScatterLayer {
    std::string name;
    std::string asset;                    // glTF path as written; resolved through the AssetRegistry
    std::vector<BiomeDensity> densities;
    // Filters beyond the biome. A biome says a fern belongs in the forest; these say it does not
    // grow on a cliff or under water, which is true in every biome.
    float minSlope = 0.0f;
    float maxSlope = 0.35f;
    float minAltitude = 0.0f;
    float maxAltitude = 1.0f;
    bool avoidWater = true;               // never below a water surface
    float shoreOffset = 0.0f;             // metres of clearance above the water line to also avoid

    // What this thing should be, in metres, rather than what the file happens to be authored at.
    // A library is authored to its own convention -- Quaternius grass is 1.8 units tall and its
    // trees are 7 -- so a scale factor is a number about the file, not about the world, and the
    // first pass put two-metre grass under seven-metre trees. 0 keeps the asset's own size. The
    // normalisation is applied by whoever resolves the asset, because only they know how big it is.
    float height = 0.0f;
    float minScale = 0.85f;   // per-instance variation around that height
    float maxScale = 1.25f;
    float sink = 0.0f;                    // metres pushed into the ground, so nothing floats
    float alignToGround = 0.0f;           // 0 upright, 1 fully along the surface normal
    float randomYaw = 1.0f;               // 0..1 of a full turn

    // Colour variation (ADR-054). A population is not one colour repeated: it is regions that
    // agree with themselves. `hueField` is how far the hue swings across the map in turns and
    // `hueFieldScale` how big a region is in metres; `hueRandom` adds the per-instance jitter on
    // top, and `emissiveRandom` varies how brightly each specimen burns.
    float hueField = 0.0f;
    float hueFieldScale = 40.0f;
    float hueRandom = 0.0f;
    float emissiveRandom = 0.0f;
    float emissiveSparsity = 0.0f;  // fraction of specimens that stay dark
    // Names a material program (ADR-030) for this layer. Emission is otherwise constant over a
    // mesh, which lights a tree evenly from root to crown; a program can put the glow in patches
    // so a tree reads as full of fireflies rather than as a lamp shaped like a tree.
    std::string materialProgram;

    // Clumping. Plants do not occur on a grid; they occur in patches with gaps between them. The
    // cluster field is a coarse noise, and `clustering` is how much of the layer's density it takes
    // away from the gaps and gives to the patches.
    float clusterScale = 24.0f;           // metres of the patch pattern
    float clustering = 0.0f;              // 0 even, 1 entirely in patches

    // Colour. The asset's textures stay; these multiply and add to them, which is the split that
    // lets one curated library become several worlds -- the mesh and its maps are the library's,
    // the palette is the world's. A library authored in daylight greens is otherwise a library
    // authored in daylight greens whatever the moon is doing.
    glm::vec3 tint{1.0f, 1.0f, 1.0f};      // multiplies the asset's base colour factor
    glm::vec3 emissiveColor{0.0f};         // linear
    float emissiveIntensity = 0.0f;        // 0 leaves the asset's own emission alone

    // How small on screen this thing gets before it is dropped, in pixels of projected radius.
    // Grass may vanish long before a tree does; both eventually should.
    float minScreenRadius = 1.5f;
    // How far this layer is drawn at all, in metres. 0 takes the terrain's view distance, which is
    // right for a tree and absurd for grass: nineteen thousand grass clumps reaching half a
    // kilometre are invisible past sixty metres and were being drawn into every shadow cascade.
    float viewDistance = 0.0f;
    // Whether this layer is drawn into the shadow maps. Ground cover is what this is for: grass and
    // pebbles cast shadows smaller than a shadow-map texel at the sizes they are drawn, so the cost
    // is real and the result is not on screen.
    bool castsShadow = true;

    // How this species answers the wind (ADR-055). It is per-layer because "soft and responsive"
    // versus "stiff and slow" is what separates grass from a mushroom, and one global setting
    // cannot say both. Deliberately absent from `structuralHash`: motion is a per-frame uniform,
    // so retuning how a fern moves does not replant the forest.
    wind::VegetationMotion motion;

    std::uint32_t seed = 1;
    int maxInstances = 60000;             // a hard ceiling per layer, whatever the density says
    int meshBudget = 0;                   // triangles, passed to the imported source (ADR-045)

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t structuralHash() const;
};

// Everything a world grows, in draw order.
struct Ecology {
    std::vector<ScatterLayer> layers;
    [[nodiscard]] Result<void> validate(const BiomeSet& biomes) const;
    [[nodiscard]] std::uint64_t structuralHash() const;
    [[nodiscard]] bool empty() const { return layers.empty(); }
};

// The placements for one layer over the whole map. Positions are world space and sit on the
// terrain; rotations carry the yaw and the ground alignment; scales carry the per-instance size.
[[nodiscard]] spatial::PointCloud scatter(const WorldMap& map, const ScatterLayer& layer);

// Ecology is authored in the scene file rather than shipped as a C++ default, unlike the geography
// and the biomes. Geography is design data with no dependencies; a scatter layer names an asset,
// and an asset path only means anything relative to the file that wrote it.

// ---- the ecology light field (ADR-053) --------------------------------------------------------

// What a patch of luminous ecology does to the air and the ground around it. A glowing layer can
// place tens of thousands of instances and none of them can afford to be a light, so the
// placements are binned into a coarse world grid and each occupied cell is reduced to one soft
// emitter: the emission-weighted centroid of the instances in it, how far they spread, their
// summed power. The count then follows the area the layer covers, not the number of things
// growing on it.
struct GlowCluster {
    glm::vec3 position{0.0f};
    float radius = 1.0f;   // spread of the contributing instances, never smaller than one of them
    glm::vec3 color{1.0f}; // the layer's emissive colour, normalised
    float power = 0.0f;    // summed emissive weight: intensity * per-instance area
};

// Bins `cloud` into cells of `cellSize` metres and reduces each to one GlowCluster. Returns
// nothing when the layer does not emit. `lift` raises each emitter off the ground by that
// fraction of the layer's height, so the light sits in the glowing organ rather than at the root.
// `hueSeed` must be the seed the instances were varied with, or the light a patch casts will be
// a different colour from the patch casting it.
[[nodiscard]] std::vector<GlowCluster> aggregateGlow(const spatial::PointCloud& cloud,
                                                     const ScatterLayer& layer, float cellSize,
                                                     std::uint32_t hueSeed = 12345u, float lift = 0.5f);

[[nodiscard]] Result<Ecology> ecologyFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json ecologyToJson(const Ecology& ecology);

} // namespace avgen::world
