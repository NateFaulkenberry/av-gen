#pragma once

// ADR-1957: TileNav -- A* over a lattice of tiles, one node per tile, for a body that walks small steps
// and hops the rest. A neighbour is
//   * WALK when it is one of the four edge neighbours, the step |dh| is within `stepHeight`, and it is not water
//     (sea slabs are hopped onto, never waded);
//   * JUMP when it lies 1..`jumpTiles` tiles away along one of the eight lattice directions and the rise is within
//     `jumpRise` and the drop within `jumpDrop` (the jump envelope);
//   * blocked otherwise.
// Heights come from the caller's query (the ground at the planning time); the arc is re-checked at take-off against
// the moving ground, which is the behaviour's business. Distances are in tiles, so the rules are the same whatever the
// tile size; `stepHeight` and the envelope are metres.

#include "world/ground_source.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace avgen::entity {

struct TileNavRules {
    float stepHeight = 0.6f;  // metres a walk may rise or drop
    int jumpTiles = 3;        // the farthest hop, in tiles
    float jumpRise = 3.0f;    // metres a hop may rise
    float jumpDrop = 8.0f;    // metres a hop may drop
    int window = 48;          // tiles from the start the search may go
    float waterThreshold = 0.5f;
    float jumpCost = 1.5f;    // added per hop, so a walk is preferred where one exists
};

enum class TileEdge : std::uint8_t { Walk, Jump };

struct TileStep {
    glm::ivec2 tile{0};
    TileEdge edge = TileEdge::Walk;
};

struct TilePath {
    bool found = false;
    std::vector<TileStep> steps; // from the first tile after the start to the goal
    int expanded = 0;
};

using TileQuery = std::function<world::GroundSample(glm::ivec2 tile)>;

[[nodiscard]] bool tileWalkAllowed(const world::GroundSample& from, const world::GroundSample& to, const TileNavRules& r);
[[nodiscard]] bool tileJumpAllowed(const world::GroundSample& from, const world::GroundSample& to, int tiles,
                                   const TileNavRules& r);
[[nodiscard]] TilePath planTiles(const TileQuery& query, glm::ivec2 from, glm::ivec2 to, const TileNavRules& rules);

} // namespace avgen::entity
