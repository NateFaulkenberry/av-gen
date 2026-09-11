#pragma once

// Where a thing may stand and where it may walk (ADR-087).
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
#include "world/camera_clearance.hpp"
#include "world/world_map.hpp"

#include <glm/glm.hpp>

namespace avgen::entity {

// What makes ground unwalkable. Defaults describe a person-sized walker on Glowmere's valley
// floor; a heavier or smaller thing changes the numbers, not the rules.
struct NavSettings {
    float maxSlope = 0.55f;         // 0 flat .. 1 vertical (1 - normal.y); above this it is a cliff
    float waterMargin = 0.35f;      // metres of dry land required above any water surface
    float headroom = 2.2f;          // metres a walker needs under whatever grows here
    float heroMargin = 1.0f;        // metres to stay outside a hero's sphere
    float boundaryMargin = 12.0f;   // metres to stay inside the world's edge
    float stepHeight = 1.4f;        // metres of rise tolerated between two samples of a step
};

// Why a point was rejected. A string rather than an enum because its only consumer is a diagnostic
// and a reason nobody can read is a reason nobody acts on.
enum class NavReject : std::uint8_t { None, OutOfBounds, TooSteep, Submerged, NoHeadroom, InsideHero, Step };
[[nodiscard]] const char* navRejectName(NavReject reason);

struct NavSample {
    bool navigable = false;
    float ground = 0.0f;   // world y of the surface
    float slope = 0.0f;
    float canopy = 0.0f;   // metres of growth above the ground here
    NavReject reject = NavReject::None;
};

class Navigator {
public:
    Navigator() = default;
    Navigator(const world::WorldMap* map, world::ClearanceField field, NavSettings settings = {});

    [[nodiscard]] bool valid() const { return map_ != nullptr; }
    [[nodiscard]] const NavSettings& settings() const { return settings_; }
    [[nodiscard]] const world::ClearanceField& clearance() const { return field_; }

    // The surface height at p. Zero when there is no map, so a scene with no terrain still runs
    // its entities on the y = 0 plane rather than refusing to run them at all.
    [[nodiscard]] float groundHeight(glm::vec2 p) const;
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
    // deviations either side. Local steering rather than a planner, because this world is open
    // ground with scattered obstacles and A* over it would be a great deal of machinery to walk
    // around a tree. Returns a unit vector, or (0,0) when every direction is blocked.
    [[nodiscard]] glm::vec2 steer(glm::vec2 from, glm::vec2 to, float lookahead) const;

private:
    const world::WorldMap* map_ = nullptr;
    world::ClearanceField field_{};
    NavSettings settings_{};
};

} // namespace avgen::entity
