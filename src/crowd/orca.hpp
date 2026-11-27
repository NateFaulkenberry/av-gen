#pragma once

// ADR-2223: optimal reciprocal collision avoidance (van den Berg, Guy, Lin, Manocha, ORCA, 2011),
// self-written from the paper's construction. The 115 Stage A spike measured it in a 2 m
// bottleneck at 0.3-1.0 us per agent-step with overlap <= 0.014 m and essentially no oscillation,
// against 8-25x the cost for sampled avoidance (Detour-crowd style) and 0.54 m of interpenetration
// for a positional push (`EntityWorld::crowdSeparation`'s shape).
//
// Pure functions of their arguments. The caller fixes the order of neighbours and obstacles; with
// a Jacobi step (every agent reads the previous step's state) the result is bit-identical however
// the agents are divided between threads.
#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

namespace avgen::crowd {

struct OrcaLine {
    glm::vec2 point{0.0f};
    glm::vec2 direction{0.0f};
};

struct OrcaNeighbour {
    glm::vec2 position{0.0f};
    glm::vec2 velocity{0.0f};
    float radius = 0.0f;
    // 0.5 for another agent (each takes half the avoidance), 1 for something that will not move
    // out of the way: a static disc or a body that is waiting.
    float responsibility = 0.5f;
};

struct OrcaSegment {
    glm::vec2 a{0.0f};
    glm::vec2 b{0.0f};
    float radius = 0.0f; // the wall's own half-thickness
};

struct OrcaParams {
    float radius = 0.3f;
    float maxSpeed = 1.6f;
    float timeHorizon = 2.0f;         // seconds ahead another agent is avoided
    float timeHorizonObstacle = 1.0f; // seconds ahead a wall is avoided
    float dt = 1.0f / 60.0f;
};

// The new velocity for an agent at `position` moving at `velocity` that would like `preferred`.
// `lines` is scratch the caller keeps to avoid an allocation per agent.
[[nodiscard]] glm::vec2 orcaVelocity(glm::vec2 position, glm::vec2 velocity, glm::vec2 preferred,
                                     const std::vector<OrcaNeighbour>& neighbours,
                                     const std::vector<OrcaSegment>& walls, const OrcaParams& params,
                                     std::vector<OrcaLine>& lines, std::vector<OrcaLine>& scratch);

} // namespace avgen::crowd
