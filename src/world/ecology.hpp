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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::world {

// How much of a layer occurs in one biome. Named rather than indexed so a scene reads as prose and
// so inserting a biome does not silently re-point every density in the file.
struct BiomeDensity {
    std::string biome;
    float density = 0.0f; // instances per square metre where that biome is at full weight
};

struct ScatterProximity {
    std::string layer;
    float minDistance = 0.0f;
    float maxDistance = 12.0f;
    float fade = 2.0f;
    float strength = 1.0f;
};

// A region the ecology leaves alone, in world XZ. Negative space is a positive instruction: a
// composer that only ever adds material produces a uniform scatter, and a uniform scatter is the
// thing every note about composition in this project is written against.
//
// This exists because for a while it did not. The composer emitted void regions, `installWorld`
// translated them into the scene's exclusion regions, the exclusion regions became reserved
// composition fields -- and nothing on the ecology side read any of it, so a world's negative space
// was a value carried faithfully from end to end of a pipeline and then dropped. The corridor the
// brief calls mandatory was, in practice, absent.
struct ScatterClearance {
    glm::vec2 center{0.0f};
    float radius = 0.0f;      // metres cleared outright
    float softness = 0.0f;    // metres over which density returns to normal
    float strength = 1.0f;    // 1 removes everything inside; 0 is a no-op
    // Only layers at least this tall are cleared. A lane through a forest is a lane in the canopy:
    // the trees are gone and the ground is still growing. Clearing everything instead produced a
    // bald hillside with one tree on it -- technically a corridor, and the exact opposite of a
    // world composed to be dense at the viewer's feet. 0 clears every layer.
    float minHeight = 0.0f;
    // What density becomes *inside* the region, before `strength` blends toward it. 0 is a
    // clearing; values above 1 are the same mechanism used to make a region denser than the world
    // around it, which is what an ecological zone is. Defaulting to 0 keeps every existing
    // clearance meaning exactly what it did.
    float densityScale = 0.0f;
    // Which layers this applies to, by the category the composer recorded on them. Empty means all.
    // A zone that thickens the fungi without also thickening the trees is the difference between a
    // glowing hollow and simply more of everything.
    std::string category;
};

// The density multiplier `clearances` impose at `p`: 1 where nothing applies, 0 inside a region at
// full strength. Exposed so a test can assert a corridor is clear without scattering anything.
[[nodiscard]] float clearanceWeight(std::span<const ScatterClearance> clearances, glm::vec2 p,
                                    float layerHeight = 0.0f,
                                    std::string_view layerCategory = {});

// What a layer is for navigation, when the layer knows something the heuristic cannot (ADR-196).
//
// `entity::classifyScatterLayer` reads the composer's category, then the asset's *filename*, then
// the layer's own name. That is honest and inspectable and it is still keywords: an author whose
// tree asset is called `Plant_7` has no way to say "this one blocks", and one whose monument is
// scenery a character walks through has no way to say so either. This is that way.
//
// `Auto` is the heuristic and is the default, so a scene that says nothing classifies exactly as it
// did and is written back out without the key.
enum class ScatterNavigation : std::uint8_t {
    Auto,     // the heuristic decides: category, then asset path, then layer name, then height
    Blocks,   // every instance is a solid, whatever its height and whatever the asset is called
    Passable, // no instance is a solid, however tall
};
[[nodiscard]] const char* scatterNavigationName(ScatterNavigation navigation);
[[nodiscard]] std::optional<ScatterNavigation> scatterNavigationFromName(std::string_view name);

struct ScatterLayer {
    std::string name;
    std::string asset;
    // What kind of thing this is ("flora", "fungi", "rock", ...), as the composer's asset library
    // classified it. Carried on the layer so a region can act on a category without the placer
    // having to consult a library it does not have.
    std::string category;                    // glTF path as written; resolved through the AssetRegistry
    std::vector<BiomeDensity> densities;
    // Filters beyond the biome. A biome says a fern belongs in the forest; these say it does not
    // grow on a cliff or under water, which is true in every biome.
    float minSlope = 0.0f;
    float maxSlope = 0.35f;
    float minAltitude = 0.0f;
    float maxAltitude = 1.0f;
    bool avoidWater = true;               // never below a water surface
    float shoreOffset = 0.0f;             // metres of clearance above the water line to also avoid

    // Height above the water table, in metres, as a habitat band (ADR-174).
    //
    // Slope and altitude are properties of a point on its own; this is the first one that is a
    // property of a point *relative to the geography*, and it is the one a valley's vegetation
    // actually organises itself along. `WorldMap::heightAboveWater` is the ground minus the surface
    // of the nearest water course extrapolated past its bank -- what the riparian literature calls
    // height above river and uses as a groundwater proxy -- so a bench two metres above the water
    // and forty metres from it reads as wet, and a shelf two metres above it and ten metres away
    // reads the same way. Distance to the channel cannot say that.
    //
    // Unlike `minSlope`/`maxSlope` the edges are **feathered**, because a hard edge on this axis is
    // a contour line drawn across the hillside in plants. The defaults admit everything, so a layer
    // that says nothing about water is unaffected and costs nothing -- the field is only evaluated
    // when a layer constrains it.
    float minHeightAboveWater = -1.0e6f;
    float maxHeightAboveWater = 1.0e6f;
    float heightAboveWaterFeather = 1.5f; // metres of soft shoulder at each edge

    [[nodiscard]] bool constrainsHeightAboveWater() const {
        return minHeightAboveWater > -1.0e5f || maxHeightAboveWater < 1.0e5f;
    }

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
    // ADR-057: the hue drifts in world space and time rather than being fixed when the cloud is
    // projected. 0 leaves the colour where `hueField` put it.
    float chromaDrift = 0.0f;
    float chromaDriftScale = 55.0f;
    float chromaDriftSpeed = 0.05f;
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
    std::optional<ScatterProximity> proximity;

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

    // Whether this layer's instances are solids a character has to go round (ADR-196). Deliberately
    // an override rather than a replacement: `Auto` leaves the height thresholds in
    // `entity::ObstaclePolicy` in charge, and the other two settle only the question the heuristic
    // cannot answer -- *whether* -- while the per-instance height still decides the traversal class.
    ScatterNavigation navigation = ScatterNavigation::Auto;

    std::uint32_t seed = 1;
    int maxInstances = 60000;             // a hard ceiling per layer, whatever the density says
    int meshBudget = 0;                   // triangles, passed to the imported source (ADR-045)

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t structuralHash() const;
};

// Everything a world grows, in draw order.
struct Ecology {
    std::vector<ScatterLayer> layers;
    // Applies to every layer. Negative space is a property of the world, not of a species: a
    // clearing with the grass removed and the trees still standing in it is not a clearing.
    std::vector<ScatterClearance> clearances;
    [[nodiscard]] Result<void> validate(const BiomeSet& biomes) const;
    [[nodiscard]] std::uint64_t structuralHash() const;
    [[nodiscard]] bool empty() const { return layers.empty(); }
};

// The placements for one layer over the whole map. Positions are world space and sit on the
// terrain; rotations carry the yaw and the ground alignment; scales carry the per-instance size.
[[nodiscard]] spatial::PointCloud scatter(const WorldMap& map, const ScatterLayer& layer,
                                         std::span<const glm::vec3> anchors = {},
                                         std::span<const ScatterClearance> clearances = {});

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

// Clearances round-trip separately from the layers because `scatter` is a bare JSON array -- the
// ecology *is* the list of layers in the file format -- so there is nowhere inside it for a
// property of the whole world to live. They are written as the terrain node's "clearings".
[[nodiscard]] Result<std::vector<ScatterClearance>> clearancesFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json clearancesToJson(const std::vector<ScatterClearance>& clearances);

[[nodiscard]] Result<Ecology> ecologyFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json ecologyToJson(const Ecology& ecology);

} // namespace avgen::world
