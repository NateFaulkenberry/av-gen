#pragma once

// What is under the cursor, asked on the CPU, every frame (ADR-092, world-authoring-spec §18/§22).
//
// The editor already has a picker: `app::viewport_pick` reads the identifier and linear-depth
// targets the scene pass writes. It is exact and it costs nothing per frame -- but it is a blocking
// GPU round trip, up to five of them per query, and the brief's headline requirement is a **live**
// ghost under a moving cursor. Blocking on the GPU sixty times a second to find out where the mouse
// is pointing is not a thing that can be made fast; it is a thing that has to be asked a different
// way.
//
// So the ghost asks the world instead of the picture, through **`world::TerrainQuery`** (ADR-090) --
// the one spatial-query surface §3 asks for. Every terrain fact in `GroundSample` below is one of
// its queries: the height, the normal, the slope, the water, the canopy, whether the point is
// walkable and, when it is not, *why*. This file decides nothing about terrain. It owns the ray
// march and nothing else, and even that is a loop over `heightAt`.
//
// That matters more than it looks. §3 forbids separate terrain logic per consumer, and the way a
// project acquires it is not by deciding to: it is by one consumer needing a slightly different
// answer and writing three lines rather than asking for a shared one. The brush's "is this too
// steep" and the walker's "can I stand here" are the same question, and when they are the same call
// they cannot drift.
//
// The GPU picker is still the authority for a *click*, because it is exact and can see procedural
// instances and terrain chunks that no node owns. This is for the frame-by-frame question.
//
// ImGui-free; see tests/unit/test_world_probe.cpp.

#include "scene/camera.hpp"
#include "scene/composition.hpp"
#include "world/terrain_query.hpp"

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <vector>

namespace avgen::ui {

struct ViewRay {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f}; // unit length
};

// The ray through a normalised device coordinate, (-1,-1) bottom left. Uses the camera's own
// matrices by way of `entity::rayThrough`, so "where the cursor points" here and "where the cursor
// points" in the renderer are the same claim rather than two that agree most of the time.
[[nodiscard]] ViewRay rayThroughNdc(const scene::Camera& camera, float aspect, glm::vec2 ndc);

// Everything the world says about one point of ground. Every field but `valid`, `distance` and
// `hasTerrain` comes straight from `world::TerrainPoint`.
struct GroundSample {
    bool valid = false;            // the ray met the ground at all
    glm::vec3 position{0.0f};      // on the surface
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    // In degrees, from the normal. `TerrainPoint::slope` is 1 - normal.y, which is a convenient
    // 0..1 and not an angle; every slope limit an artist sets is written in degrees, so the
    // conversion happens once, here, rather than in each place that would have to remember.
    float slopeDegrees = 0.0f;
    float distance = 0.0f;         // along the ray
    bool insideWorld = true;       // within the terrain's extent
    bool submerged = false;        // the ground here is under water
    float waterDepth = 0.0f;       // metres of water over it, 0 when dry
    float canopyHeight = 0.0f;     // statistical (ADR-080): what grows *around* here, not at it
    bool hasTerrain = false;       // false when the scene has no terrain node and y=0 stood in
    // What the world itself says about this point, and why it would refuse a walker. The brush has
    // its own thresholds -- a fern and a boulder disagree about what is too steep -- but when the
    // world has already rejected a point, its reason is the one to show.
    world::TerrainReject reject = world::TerrainReject::None;
    bool walkable = true;
};

// Marches `ray` against the scene's terrain (the first Terrain node), falling back to the y = 0
// plane when there is none -- an empty scene still has to be paintable, and a brush that refused to
// work until somebody generated a world would be a brush nobody found.
[[nodiscard]] GroundSample sampleGroundAlong(scene::Composition& composition, const ViewRay& ray,
                                             float maxDistance = 4000.0f);
// The same answer for a point already known, e.g. every instance of a scatter brush around the
// cursor. Does not march: it reads the column at (x, z).
[[nodiscard]] GroundSample sampleGroundAt(scene::Composition& composition, glm::vec2 xz);
// The same, against a query the caller already has. The brush takes one for a whole stroke rather
// than rebuilding it per instance: a `TerrainQuery` is pointers and floats, but finding the terrain
// node is a linear scan of the node list and a stroke has dozens of instances.
[[nodiscard]] GroundSample sampleGroundAt(const world::TerrainQuery& query, float lift, glm::vec2 xz);

// The nearest place a thing of `radius` could actually stand, searched outwards from `xz`.
// `world::TerrainQuery::nearestValidPoint` does the searching; this wraps it back into a
// `GroundSample` so a caller that has one can keep having one. Empty when the search found nowhere.
[[nodiscard]] std::optional<GroundSample> nearestPlaceable(scene::Composition& composition, glm::vec2 xz,
                                                            float radius, float searchRadius = 48.0f);

// The nearest node whose world-space box the ray enters. Used for box selection and for the
// collision term of placement validity, not for click selection -- a box is not a silhouette, and
// for a click the GPU picker is exact and already there.
struct ObjectHit {
    bool hit = false;
    std::string node;
    float distance = 0.0f;
    glm::vec3 position{0.0f};
};
[[nodiscard]] ObjectHit pickNodeAlong(scene::Composition& composition, const ViewRay& ray,
                                      float maxDistance = 4000.0f);

// Node names whose world box, projected to the screen, overlaps the rectangle `min`..`max` in NDC.
// The drag-box of §25. Projection rather than geometry: an artist dragging a box over the viewport
// means "the things I can see in here", which is a screen-space question.
[[nodiscard]] std::vector<std::string> nodesInScreenRect(scene::Composition& composition,
                                                          const scene::Camera& camera, float aspect,
                                                          glm::vec2 ndcMin, glm::vec2 ndcMax);

// Where a world point lands in NDC, and whether it is in front of the camera. Behind-camera points
// project to a mirrored position that looks perfectly plausible, which is how a gizmo ends up drawn
// on the wrong side of the screen.
struct Projected {
    glm::vec2 ndc{0.0f};
    bool inFront = false;
    float depth = 0.0f; // view-space distance along the forward axis
};
[[nodiscard]] Projected projectPoint(const scene::Camera& camera, float aspect, glm::vec3 world);

} // namespace avgen::ui
