#pragma once

// Turning a world into the solids a walker has to go round (ADR-093, §5).
//
// `spatial::ObstacleField` stores facts and answers questions; this file is the *policy* that
// decides which of a world's hundred thousand placed instances are facts worth storing. They are
// separate because the policy is opinionated and the container is not: a different world, or a
// different kind of walker, replaces this file and keeps the other.
//
// The judgement being made is the one §5 asks for -- "vegetation and small decorative objects
// should not necessarily block; large rocks, structures and cliffs should" -- and the reason it
// takes any code at all is that a scatter layer does not say. Glowmere grows bushes at 1.1 m,
// ferns at 1.4 and boulders at 1.6, so height alone cannot tell a rock from a shrub. What can:
//
//   * `ScatterLayer::category`, when the composer set it (it writes `assets::assetCategoryName`).
//   * the asset's own path, when it did not -- `Rock_Medium_1.gltf` is a rock in any language, and
//     Glowmere's scene file, authored before categories existed, carries none.
//   * the per-instance scale, which is what makes this per-instance rather than per-layer: a
//     fan-plant at 0.65x is waded through and the same species at 1.6x is walked around.
//
// And the height recorded is the height of the *solid*, not of the plant. A fourteen-metre tree
// has a half-metre trunk; recording its bounding radius would shut the forest.

#include "spatial/obstacle_field.hpp"
#include "spatial/point_cloud.hpp"
#include "world/ecology.hpp"
#include "world/hero.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <span>
#include <string_view>

namespace avgen::entity {

// The thresholds that separate scenery from an obstacle. Defaults describe a person-sized walker
// in a temperate valley; they are a struct rather than constants so a world with different
// proportions -- a miniature, a giant -- can say so without a rebuild.
struct ObstaclePolicy {
    // Above this height a solid is a trunk rather than a mass: only its stem is in the way.
    float trunkHeight = 4.0f;
    float trunkFraction = 0.06f;  // a trunk's radius as a fraction of the tree's height
    float minTrunkRadius = 0.22f;
    float maxTrunkRadius = 1.10f;

    // How tall each class has to be before it blocks at all.
    float rockMinHeight = 0.9f;        // a pebble is stepped on, a boulder is walked around
    float treeMinHeight = 2.5f;        // a sapling is pushed past
    float vegetationMinHeight = 4.0f;  // a shrub is waded through; a tree fern is not
    float structureMinHeight = 0.6f;

    // How much of an instance's footprint to actually claim. Vegetation is mostly air, and a
    // bounding cylinder that claims all of it makes a wide plant into a bollard.
    float footprintScale = 0.8f;
    // Never claim more than this, whatever the asset's bounds say. A single instance that swallows
    // a twenty-metre disc is a bug in an asset, not a feature of a world.
    float maxRadius = 6.0f;
};

// What a layer's instances are, by the best evidence the layer offers. `Vegetation` is the answer
// when nothing identifies it, because the cost of wrongly not blocking is a character clipping a
// shrub and the cost of wrongly blocking is a world it cannot cross.
[[nodiscard]] spatial::ObstacleType classifyScatterLayer(const world::ScatterLayer& layer);
// The same judgement from the two strings alone, so a test can pin it without an ecology.
[[nodiscard]] spatial::ObstacleType classifyAsset(std::string_view category, std::string_view assetPath);

// How tall an instance of `layer` at `instanceScale` stands, in metres. `assetHeight` is the
// asset's own height in its own units, used only when the layer did not normalise to a height.
[[nodiscard]] float instanceHeight(const world::ScatterLayer& layer, float instanceScale,
                                   float assetHeight);

// Appends one obstacle for every instance of `cloud` the policy considers solid, and returns how
// many it added.
//
// `footprintAspect` is the asset's horizontal radius divided by its own height -- unit-free, so the
// caller may pass the raw glTF bounds without knowing what the layer normalised them to. Pass 0
// and the trunk rule supplies a radius on its own, which is the right fallback for a tree and a
// poor one for a boulder.
std::size_t obstaclesFromScatter(const world::ScatterLayer& layer, const spatial::PointCloud& cloud,
                                 float footprintAspect, float assetHeight,
                                 const ObstaclePolicy& policy, spatial::ObstacleField& out);

// Heroes are the one thing in this world that already carries an authored obstacle volume, and
// they are the large structures a walker must not walk through: the elder, the monument, the arch.
// `radiusScale` trims the composition radius, which is sized for framing rather than for collision.
std::size_t obstaclesFromHeroes(std::span<const world::HeroPoint> heroes,
                                spatial::ObstacleField& out, float radiusScale = 0.72f);

} // namespace avgen::entity
