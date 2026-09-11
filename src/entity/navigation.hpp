#pragma once

// Where a thing may stand and where it may walk (ADR-088).
//
// This is deliberately not a navmesh. The world already answers every question a walker has, and
// it answers them analytically:
//
//   * `WorldMap::sample` returns height, normal, slope, the water surface above a point and
//     whether that point is submerged, from one set of noise evaluations.
//   * `ClearanceField` (ADR-080) already knows the tallest thing that grows at a point and how far
//     inside a hero a point is -- the two facts a camera needed, and the same two a walker needs.
//
// A navmesh would be a second description of the same ground, baked at a different moment, and the
// first time someone moved a hill it would be wrong in a way nothing checked. Sampling the real
// ground costs a few noise evaluations per query, which is affordable because a walker asks for a
// destination every few seconds, not every frame.
//
// Everything here is a pure function of the world and the arguments, except `pickDestination`,
// which consumes a caller-supplied seeded Rng. There is no wall clock and no global randomness:
// the same world, the same generator state and the same request always produce the same answer.

#include "core/rng.hpp"
#include "spatial/obstacle_field.hpp"
#include "world/camera_clearance.hpp"
#include "world/terrain_query.hpp"
#include "world/world_map.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <span>
#include <vector>

namespace avgen::entity {

// What makes ground unwalkable, plus what a *walker* needs that a point does not have.
//
// The first six are `world::WalkRules` under other ownership, and they are deliberately the same
// names with the same defaults: §3 says there is one set of rules, and this struct now hands them
// to `world::TerrainQuery` rather than re-implementing them. The rest -- the step height, the body
// radius, the step-over -- are properties of a thing that *moves*, which a query about a point has
// no business knowing.
struct NavSettings {
    float maxSlope = 0.55f;         // 0 flat .. 1 vertical (1 - normal.y); above this it is a cliff
    float waterMargin = 0.35f;      // metres of dry land required above any water surface
    float headroom = 2.2f;          // metres a walker needs under whatever grows here
    // Vegetation up to this height is walked *through*, not around. Without it, the undergrowth
    // test below rejects anywhere anything short grows -- and because the canopy model is
    // statistical (ADR-080: "trees about nine metres tall grow around here", not "there is a trunk
    // at this spot"), that is every square metre of a meadow. Glowmere grows grass at 0.7 m, ferns
    // at 1.4 and bushes at 1.1, so its entire open ground read as impassable bramble while the
    // forest floor under fourteen-metre trees read as fine. A walker picked a destination, took one
    // step, found the way blocked, dropped it and repeated -- walk animation, no travel.
    float walkableVegetation = 1.5f;
    float heroMargin = 1.0f;        // metres to stay outside a hero's sphere
    float boundaryMargin = 12.0f;   // metres to stay inside the world's edge
    float stepHeight = 1.4f;        // metres of rise tolerated between two samples of a step
    // The walker's own width. The statistical canopy above cannot say "there is a trunk here", so
    // this is what the per-instance obstacle field (ADR-093, §5) is tested against: a body radius
    // and a step-over height turn a set of cylinders into "may I stand here".
    float bodyRadius = 0.45f;
    float stepOver = 0.4f;          // solids shorter than this are stepped over, not avoided

    // The six shared rules, as the terrain query surface wants them.
    [[nodiscard]] world::WalkRules walkRules() const {
        return world::WalkRules{.maxSlope = maxSlope,
                                .waterMargin = waterMargin,
                                .headroom = headroom,
                                .walkableVegetation = walkableVegetation,
                                .heroMargin = heroMargin,
                                .boundaryMargin = boundaryMargin};
    }
};

// Why a point was rejected. A string rather than an enum because its only consumer is a diagnostic
// and a reason nobody can read is a reason nobody acts on.
enum class NavReject : std::uint8_t { None, OutOfBounds, TooSteep, Submerged, NoHeadroom, InsideHero, Step, Obstructed };
[[nodiscard]] const char* navRejectName(NavReject reason);

struct NavSample {
    bool navigable = false;
    float ground = 0.0f;   // world y of the surface
    float slope = 0.0f;
    float canopy = 0.0f;   // metres of growth above the ground here
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    // The water surface above this point, or -infinity where the ground is dry. Carried rather
    // than reduced to a boolean because "how far above the water am I" is what tells a shoreline
    // from a hilltop, and the navigation grid uses it to find the places worth walking to.
    float waterSurface = 0.0f;
    NavReject reject = NavReject::None;
};

class NavGrid;
struct PathRequest;
struct PathResult;

class Navigator {
public:
    Navigator() = default;
    Navigator(const world::WorldMap* map, world::ClearanceField field, NavSettings settings = {});

    [[nodiscard]] bool valid() const { return map_ != nullptr; }
    // The walkable extent: the map's own bounds pulled in by the boundary margin, which is the
    // rectangle `sample` will actually accept. Falls back to a small square when there is no map,
    // so a caller sizing a structure to the world never has to special-case not having one.
    [[nodiscard]] glm::vec2 worldMin() const;
    [[nodiscard]] glm::vec2 worldMax() const;
    [[nodiscard]] const NavSettings& settings() const { return settings_; }
    void setSettings(const NavSettings& settings) {
        settings_ = settings;
        query_.rules = settings_.walkRules();
    }
    [[nodiscard]] const world::ClearanceField& clearance() const { return field_; }
    // The world's own query surface (ADR-090), carrying this walker's rules. `sample` is a
    // translation of `TerrainQuery::at` rather than a second implementation of it.
    [[nodiscard]] const world::TerrainQuery& terrain() const { return query_; }

    // The per-instance solids this world contains (ADR-093, §5). Shared rather than owned: the
    // host builds one set for the world and every walker in it reads the same one, and a copy of a
    // Navigator -- which is how it reaches EntityWorld -- keeps pointing at it.
    // `bridge` is the adapter that presents the same set through §3's interface, so a caller
    // holding only a `TerrainQuery` gets the same answers. Optional: without it the navigator still
    // avoids obstacles and `TerrainQuery::isOccupied` still reports only heroes and the world edge,
    // which is exactly the "nobody asked" state `hasObstacles()` exists to distinguish.
    void setObstacles(std::shared_ptr<const spatial::ObstacleField> obstacles,
                      const world::ObstacleField* bridge = nullptr) {
        obstacles_ = std::move(obstacles);
        query_.obstacles = bridge;
    }
    [[nodiscard]] const spatial::ObstacleField* obstacles() const { return obstacles_.get(); }
    // The filter this walker queries the obstacle field with, standing on ground at `footY`.
    [[nodiscard]] spatial::ObstacleFilter filter(float footY = 0.0f) const;

    // The navigation graph (ADR-093, §2). Built explicitly by the host, once, because it costs one
    // world sample per cell; absent is a legal state and every query below degrades to the
    // straight-line behaviour that was here before rather than failing.
    void buildGrid(float cellSize = 4.0f);
    void setGrid(std::shared_ptr<const NavGrid> grid) { grid_ = std::move(grid); }
    [[nodiscard]] const NavGrid* grid() const { return grid_.get(); }

    // The surface height at p. Zero when there is no map, so a scene with no terrain still runs
    // its entities on the y = 0 plane rather than refusing to run them at all.
    [[nodiscard]] float groundHeight(glm::vec2 p) const;
    // The surface normal at p, from central differences `epsilon` metres apart. Sampling wider than
    // a body is wide is what turns a noise function into a slope a body can lean on.
    [[nodiscard]] glm::vec3 groundNormal(glm::vec2 p, float epsilon = 0.5f) const;
    // The tallest thing that grows at p, in metres above the ground.
    [[nodiscard]] float canopyHeight(glm::vec2 p) const;
    [[nodiscard]] NavSample sample(glm::vec2 p) const;
    [[nodiscard]] bool navigable(glm::vec2 p) const { return sample(p).navigable; }

    // A navigable point in the annulus [minRadius, maxRadius] around `from`. Rejection sampling
    // with a bounded attempt count: bounded because an entity standing in a place with no valid
    // ground around it must give a wrong answer quickly rather than a right answer never.
    // Returns false and leaves `out` untouched when no attempt succeeded.
    [[nodiscard]] bool pickDestination(Rng& rng, glm::vec2 from, float minRadius, float maxRadius,
                                       glm::vec2& out, int attempts = 24) const;

    // Whether a straight walk from a to b stays navigable, sampled every `spacing` metres and
    // rejecting a rise greater than `stepHeight` between consecutive samples.
    [[nodiscard]] bool pathClear(glm::vec2 a, glm::vec2 b, float spacing = 2.5f) const;

    // The direction to move in to get from `from` towards `to` without walking into anything: the
    // straight line when `lookahead` metres of it are clear, otherwise the clearest of a fan of
    // deviations either side. Local steering, and only local: it gets a walker round a trunk and a
    // boulder, and it cannot get one round a lake. That is what `findPath` is for, and the two are
    // meant to be used together -- a planner for the route, a steerer for the metre in front.
    // Returns a unit vector, or (0,0) when every direction is blocked.
    [[nodiscard]] glm::vec2 steer(glm::vec2 from, glm::vec2 to, float lookahead) const;

    // A route from `from` to `to` as a polyline of world XZ waypoints, excluding `from`. Uses the
    // grid when there is one; without a grid it answers with the single point `to` when the
    // straight line is clear, which keeps every caller written against this working in a scene
    // that never built a graph.
    [[nodiscard]] bool findPath(glm::vec2 from, glm::vec2 to, std::vector<glm::vec2>& out) const;

    // The path request seam (ADR-093). This is the one an action layer should use: it gets a route
    // or a *reason*, and the reasons are different things to do about. §6 asks that a character not
    // freeze forever when its destination becomes unavailable, and a caller told only "false"
    // cannot tell "try a nearer goal" from "try a different kind of goal" from "try again".
    //
    // Deterministic in the sense ADR-091 needs: the same world, the same start and the same goal
    // give the same waypoints, every time, with no seed and no clock involved. A baked actor's
    // route survives a re-bake unchanged.
    [[nodiscard]] PathResult requestPath(const PathRequest& request) const;

    // Whether a route already in hand is still walkable from where the mover now is, checking from
    // `nextLeg` onwards. This is the replanning trigger: cheap enough to run every second or two,
    // and it re-checks the legs rather than repeating the search. False means ask for a new path.
    [[nodiscard]] bool pathValid(glm::vec2 from, std::span<const glm::vec2> waypoints,
                                 std::size_t nextLeg = 0) const;

    // Whether a walker of this size may stand at `p` without being inside a solid. Separate from
    // `sample` so a caller that already has the ground height does not pay for it twice.
    [[nodiscard]] bool obstructed(glm::vec2 p, float ground) const;
    // Metres of open ground around `p`, clamped to `maxRange`; `maxRange` when there is no
    // obstacle field at all, because a world with no recorded solids is a world with no solids.
    [[nodiscard]] float clearanceAt(glm::vec2 p, float ground, float maxRange = 12.0f) const;
    // The push that takes a walker at `p` out of anything it has ended up inside. Zero normally;
    // non-zero after terrain moved under it, after a seek, or after an author dropped a rock on it.
    [[nodiscard]] glm::vec2 resolvePenetration(glm::vec2 p, float ground) const;

private:
    const world::WorldMap* map_ = nullptr;
    world::ClearanceField field_{};
    NavSettings settings_{};
    world::TerrainQuery query_{};
    std::shared_ptr<const spatial::ObstacleField> obstacles_;
    std::shared_ptr<const NavGrid> grid_;
};

} // namespace avgen::entity
