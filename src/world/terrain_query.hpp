#pragma once

// The world's spatial-query surface (§3 of the world-authoring brief, ADR-090).
//
// Four systems need to ask the same questions of the same ground: the walker needs somewhere to
// stand, the water placer needs to know where the low ground is, the editor needs to know where a
// click lands, and the composer needs to know what grows where it is putting things. Before this
// each of them reached for a different half of the answer -- `WorldMap::height` here,
// `WorldMap::sample` there, `ClearanceField::canopyHeight` in a third place -- and the questions
// that needed two of them together (is this walkable, is anything standing here) were re-derived
// per caller, differently, with different margins.
//
// So this is a *join*, not a new model. It owns no data. It holds a `WorldMap` (the analytic
// ground) and a `ClearanceField` (ADR-080: the statistical canopy and the exact heroes) and asks
// them the questions §3 names. Nothing here caches, bakes or copies the world: every answer is a
// pure function of the map and the arguments, which is what lets the same query object be used from
// a worker thread, from the editor and from a determinism test without any of them agreeing on a
// lifetime beyond the map's.
//
// **What is deliberately not here.** `isOccupied` cannot be answered by the canopy model. The
// canopy is statistical by design -- it says "trees about nine metres tall grow around here", never
// "there is a trunk at this spot" (ADR-080) -- so asking it whether a two-metre disc is clear
// returns an answer about the neighbourhood, not about the disc. A real per-object answer needs a
// per-object representation, which is §5's obstacle set and belongs to navigation. `ObstacleField`
// below is the seam: an interface with one required method, which `TerrainQuery::isOccupied`
// consults when one is attached and which reduces to heroes-and-bounds when it is not.

#include "world/camera_clearance.hpp"
#include "world/ecology.hpp"
#include "world/hero.hpp"
#include "world/world_map.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>

namespace avgen::world {

// What makes ground unstandable. The defaults describe a person-sized walker; a heavier or smaller
// thing changes the numbers, not the rules. These are deliberately the same names and the same
// defaults `entity::NavSettings` uses, because they are the same rules and two sets of them that
// drift apart is exactly the duplication §3 exists to stop.
struct WalkRules {
    float maxSlope = 0.55f;          // 0 flat .. 1 vertical (1 - normal.y); above this is a cliff
    float waterMargin = 0.35f;       // metres of dry land required above any water surface
    // The deepest water this body will walk into, in metres. Water is otherwise binary -- a point
    // is dry land or it is `Submerged` -- and to anything that walks, a river and a puddle are the
    // same wall. This is the band in between: water no deeper than this is standable, and anything
    // past it is still a refusal.
    //
    // **The default is 0, and that is a decision rather than an omission.** Zero reproduces the old
    // rule exactly, including the `waterMargin` freeboard, so every scene authored before this knob
    // existed has the walkable set it was authored against -- and a scene's water is not
    // automatically wadeable just because it is shallow, because whether a body wades is a property
    // of the body, not of the water. A duck, a person and a nine-metre elder disagree about the
    // same ford. The alternative, defaulting to something like 0.4 m, would have silently opened
    // every shoreline in every existing scene to a walker that was routed around it yesterday.
    //
    // Above 0, `waterMargin` stops applying and this replaces it: a body that can cross a
    // half-metre ford but refuses to stand on a bank 0.3 m above the water is not modelling
    // anything. See the rule in `TerrainQuery::at`.
    float wadeDepth = 0.0f;
    float headroom = 2.2f;           // metres needed under whatever grows here
    // Vegetation up to this height is walked *through*, not around -- see ADR-088. Without it the
    // statistical canopy rejects every square metre of a meadow, because grass grows there.
    float walkableVegetation = 1.5f;
    float heroMargin = 1.0f;         // metres to stay outside a hero's own radius
    float boundaryMargin = 12.0f;    // metres to stay inside the world's edge
};

// Why a point was rejected. The values match `entity::NavReject`'s vocabulary so a caller can carry
// one across without a translation table; `Obstructed` is the one that is new, and it is the one
// only an `ObstacleField` can produce.
enum class TerrainReject : std::uint8_t {
    None,
    OutOfBounds,
    TooSteep,
    Submerged,
    NoHeadroom,
    InsideHero,
    Obstructed,
};
[[nodiscard]] const char* terrainRejectName(TerrainReject reason);

// Everything about one place, from one set of evaluations. Asking for height, then slope, then
// water separately costs three independent sets of noise lookups over the same point; this costs
// one, and it is what every caller that needs more than a single scalar should use.
struct TerrainPoint {
    glm::vec2 position{0.0f};
    float height = 0.0f;              // world y of the ground
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float slope = 0.0f;               // 0 flat .. 1 vertical
    float waterSurface = 0.0f;        // world y of the water above, or -infinity where dry
    float waterDepth = 0.0f;          // metres of water over the bed; 0 on dry land
    bool water = false;
    float canopy = 0.0f;              // metres of growth above the ground (statistical)
    float moisture = 0.0f;            // 0 dry .. 1 at the water's edge
    float altitude = 0.0f;            // 0..1 over the map's measured range
    bool walkable = false;
    TerrainReject reject = TerrainReject::None;
};

// ---- the §5 seam --------------------------------------------------------------------------------
//
// The per-object obstacle representation belongs to navigation (§5): a lightweight spatial set of
// positions, radii, heights and types, not rigid bodies. This interface is all terrain needs to
// know about it, and it is deliberately one method wide so that implementing it is not a project.
//
// The contract: `occupied(p, radius)` is true when the disc of `radius` metres centred at the world
// XZ point `p` overlaps something a mover of that radius cannot pass through. It says nothing about
// the ground -- terrain answers that -- and nothing about vegetation the mover walks through.
// Implementations must be thread-safe for concurrent reads and must not allocate.
class ObstacleField {
public:
    virtual ~ObstacleField() = default;
    [[nodiscard]] virtual bool occupied(glm::vec2 p, float radius) const = 0;
    // How far inside the nearest obstacle the disc reaches, in metres; <= 0 when it is clear. The
    // default derives a usable-but-coarse answer from `occupied` so an implementation only has to
    // provide one method, but anything that can answer it properly should: a steering behaviour
    // that only knows "blocked" has nothing to steer *by*.
    [[nodiscard]] virtual float penetration(glm::vec2 p, float radius) const {
        return occupied(p, radius) ? radius : -radius;
    }
};

// The join. Copyable and small: it is pointers, a span and a handful of floats, so pass it by value.
struct TerrainQuery {
    const WorldMap* map = nullptr;
    // The canopy and the heroes (ADR-080). Held by value because it is itself only pointers and a
    // span, and because a query is often built with walker-shaped clearance settings rather than the
    // camera-shaped defaults.
    ClearanceField clearance{};
    // §5's obstacles, when navigation has installed them. Null is the honest default: without it
    // `isOccupied` reports heroes and the world edge and nothing else, and says so.
    const ObstacleField* obstacles = nullptr;
    WalkRules rules{};
    // The epsilon used for normals and slopes, in metres. About half the spacing of whatever mesh
    // the answer will be compared against: finer than that and the normals contradict the silhouette.
    float epsilon = 0.5f;

    [[nodiscard]] bool valid() const { return map != nullptr; }
    // True when an obstacle set is attached, so a caller can tell "nothing is there" from "nobody
    // asked". The difference matters: a placement tool that treats the second as the first drops
    // objects inside trees and reports success.
    [[nodiscard]] bool hasObstacles() const { return obstacles != nullptr; }

    // ---- the §3 surface ------------------------------------------------------------------------
    [[nodiscard]] float heightAt(glm::vec2 p) const;
    [[nodiscard]] glm::vec3 normalAt(glm::vec2 p) const;
    [[nodiscard]] float slopeAt(glm::vec2 p) const;
    // Is there standing water over this point? True strictly above the bed, so a shoreline is where
    // the two surfaces cross rather than where a mesh boundary fell.
    [[nodiscard]] bool isWater(glm::vec2 p) const;
    // Metres of water over the bed at p; 0 on dry land. The one number a water renderer, a floating
    // object and a wading walker all need, and the continuous half of the water seam (see
    // `waterCourses` in terrain_water.hpp for the described half).
    [[nodiscard]] float waterDepthAt(glm::vec2 p) const;
    // The tallest thing that grows here, in metres above the ground. Statistical (ADR-080).
    [[nodiscard]] float canopyHeightAt(glm::vec2 p) const;
    // The same, from a sample this caller already took at `p`. See the note on
    // `ClearanceField::canopyHeight`: the canopy is a function of the sample, and taking a second
    // one at the same point was half the cost of every walkability query in the engine.
    [[nodiscard]] float canopyHeightAt(glm::vec2 p, const Sample& sample) const;
    [[nodiscard]] bool isWalkable(glm::vec2 p) const;
    // Does anything *solid* overlap the disc of `radius` at p: a hero, a registered obstacle, or the
    // world's edge? Deliberately not the canopy -- see the note at the top of this file.
    [[nodiscard]] bool isOccupied(glm::vec2 p, float radius) const;
    [[nodiscard]] bool inBounds(glm::vec2 p, float margin = 0.0f) const;
    // The nearest point to p that is both walkable and unoccupied, searched outwards on a spiral to
    // `searchRadius` metres. Returns p itself when p already qualifies, and nothing when the search
    // found nowhere -- a caller that needs an answer regardless should say what it wants done then,
    // because silently returning an invalid point is how an entity ends up inside a cliff.
    //
    // Deterministic: the spiral is a fixed sequence, not a sample. Two callers asking the same
    // question of the same world get the same answer, which is what makes it usable from a
    // generator whose output has to be reproducible.
    [[nodiscard]] std::optional<glm::vec2> nearestValidPoint(glm::vec2 p, float radius = 0.0f,
                                                             float searchRadius = 48.0f) const;

    // ---- composites ----------------------------------------------------------------------------
    // Everything at once, for the callers that need more than one scalar.
    [[nodiscard]] TerrainPoint at(glm::vec2 p) const;
    // The ground point as a world position: the single most common thing a caller does with
    // `heightAt`, spelled once here rather than reassembled at fifteen call sites.
    [[nodiscard]] glm::vec3 groundPoint(glm::vec2 p) const;
    // The surface a thing rests on: the ground, or the water above it when there is any. What a
    // floating object and a shoreline prop want, and what `heightAt` deliberately is not.
    [[nodiscard]] float surfaceAt(glm::vec2 p) const;
    // Why this point is not walkable, or `None`.
    [[nodiscard]] TerrainReject rejectAt(glm::vec2 p) const;
};

// A query over a map, with the canopy wired up when there is an ecology to read it from and the
// heroes wired up when there are any. This is the constructor callers should use: building a
// `ClearanceField` by hand is three lines that are easy to get subtly wrong, and the walker-shaped
// clearance settings (no headroom above the ground, a body's radius rather than a near plane) are
// the ones almost every caller of this file wants.
[[nodiscard]] TerrainQuery terrainQuery(const WorldMap& map, const Ecology* ecology = nullptr,
                                        std::span<const HeroPoint> heroes = {});

} // namespace avgen::world
