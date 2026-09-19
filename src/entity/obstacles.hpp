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
#include "world/terrain_query.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace avgen::entity {

// §3's seam, from this side (ADR-090, ADR-093).
//
// The terrain query surface names one interface -- `occupied(p, radius)`, plus an optional
// `penetration` -- and says the per-object answer belongs to navigation. This is that answer: a
// `spatial::ObstacleField` presented through it, so `TerrainQuery::isOccupied` stops reporting only
// heroes and the world's edge and starts reporting the trunks.
//
// The adapter lives here rather than on `spatial::ObstacleField` itself for a dependency reason.
// `world` already includes `spatial` (an ecology emits a point cloud), so making the container
// implement a `world` interface would close a cycle between the two directories. `entity` depends on
// both and is where the two halves of navigation already meet, so the join belongs here and the
// container stays a plain spatial structure that includes nothing but glm.
//
// Both methods are const, allocate nothing and touch no mutable state, which is the contract the
// interface asks for: a query object is used from a worker, from the editor and from a determinism
// test without any of them agreeing on a lifetime beyond the world's.
class NavigationObstacles final : public world::ObstacleField {
public:
    NavigationObstacles() = default;
    explicit NavigationObstacles(std::shared_ptr<const spatial::ObstacleField> field)
        : field_(std::move(field)) {}

    void setField(std::shared_ptr<const spatial::ObstacleField> field) { field_ = std::move(field); }
    [[nodiscard]] const spatial::ObstacleField* field() const { return field_.get(); }

    [[nodiscard]] bool occupied(glm::vec2 p, float radius) const override;
    [[nodiscard]] float penetration(glm::vec2 p, float radius) const override;

private:
    std::shared_ptr<const spatial::ObstacleField> field_;
};

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

    // Where the traversal classes fall, in metres of solid (ADR-196). These describe the *thing*,
    // which is the point of the class: a solid no taller than `stepOverHeight` is stepped over and
    // one no taller than `jumpOverHeight` is vaulted, and that is true of it whoever is asking.
    // What a *particular* body can do about the class is `spatial::ObstacleFilter`'s half of the
    // question and is asked at query time -- so a nominal `StepOver` still blocks a body whose own
    // step is lower than the thing is tall, and a `Jumpable` blocks everything that cannot jump.
    //
    // `stepOverHeight` matches `NavSettings::stepOver`'s default on purpose, so the class an
    // obstacle is born with and the answer the default walker gives about it agree. They are
    // allowed to disagree -- a cart and a deer both read the same field -- and nothing breaks when
    // they do; having them agree for the ordinary case just means a diagnostic and a query say the
    // same word.
    float stepOverHeight = 0.4f;
    float jumpOverHeight = 1.2f;  // about what a person vaults; above it, go round

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

// What getting past a solid of this identity and this height takes (ADR-196). Identity still only
// identifies; the single thing it decides here is that a `Creature` is never `Jumpable`, because a
// solid that walks away mid-vault is not one to commit to.
//
// This is the seam between the two halves of navigation policy. *This* file decides what a world's
// instances are, because it is the file that already knows a boulder from a bush; `spatial` stores
// the answer and honours it, and the body asking decides what it can do with it.
[[nodiscard]] spatial::Traversal traversalClass(spatial::ObstacleType type, float height,
                                                const ObstaclePolicy& policy);

// How tall an instance of `layer` at `instanceScale` stands, in metres. `assetHeight` is the
// asset's own height in its own units, used only when the layer did not normalise to a height.
[[nodiscard]] float instanceHeight(const world::ScatterLayer& layer, float instanceScale,
                                   float assetHeight);

// Appends one obstacle for every instance of `cloud` the policy considers solid, and returns how
// many it added.
//
// `layer.navigation` overrules the policy's height thresholds but not the physics: `Passable`
// contributes nothing however tall the layer is, and `Blocks` contributes every instance however
// short -- classed `Blocking`, which is what the author asserted. It still does not make a
// ten-centimetre kerb into a wall, because whether a *body* has to do anything about a solid is
// decided by that body's own step height at query time and not by the scene file.
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
//
// **`moving` is the names this rule does not apply to, and it is not an optimisation (ADR-349).**
// The sentence above -- "the elder, the monument, the arch" -- is an assumption, and starring a
// character in the Objects list breaks it: a hero is whatever somebody wants the camera to look at,
// and four of the five aliens in `glowmere-valley-2-multicam` were starred so the Auto-director
// would cut to them. Each then got a Blocking cylinder centred on its own standing position, so
// `TerrainQuery::at` answered `InsideHero` at the body's own feet and the router refused to plan a
// route out of it. Three of the five travelled **0.00 m in 226 seconds, at every seed tried**.
//
// A hero that is also a body is not scenery and must not be baked into the static field: a body
// moves, so a cylinder at where it was is wrong a second later, and bodies already avoid each
// other through `Ground`'s `bodyRadius` crowd field, which is dynamic and is the mechanism for
// exactly this. Pass the driven-node name of every entity; those heroes are skipped.
std::size_t obstaclesFromHeroes(std::span<const world::HeroPoint> heroes,
                                spatial::ObstacleField& out, float radiusScale = 0.72f,
                                std::span<const std::string_view> moving = {});

} // namespace avgen::entity
