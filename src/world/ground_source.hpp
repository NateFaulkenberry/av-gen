#pragma once

// ADR-1955: the ground seam. What a body that stands on a surface asks of it, behind an interface, so a behaviour can
// stand on ground that is not a WorldMap -- Musical Geography's lattice of tiles, which moves (ADR-1953) and springs
// under a landing (ADR-1954).
//
// The existing path is untouched: `Navigator` and `TerrainQuery` keep reading their WorldMap, and every behaviour
// that uses them behaves exactly as before. A behaviour that wants this seam reads `BehaviorContext::ground`, which is
// null unless the scene has a ground source (today: a tiles ground), and must stand still when it is null.

#include <glm/glm.hpp>

namespace avgen::world {

struct GroundSample {
    float height = 0.0f; // metres: the top of the surface under the point, at the time asked
    float water = 0.0f;  // 0..1
    float flat = 0.0f;   // 0..1: a gathering place (a tiles ground's crown)
    float path = 0.0f;   // 0..1: on the path
};

class GroundSource {
public:
    virtual ~GroundSource() = default;
    // The surface under (x, z) at timeline second t. Pure: the same question gives the same answer.
    [[nodiscard]] virtual GroundSample at(float x, float z, double t) const = 0;
    // The cell a point belongs to and its centre, for a surface made of cells; `pitch` 0 for a continuous one.
    [[nodiscard]] virtual float pitch() const = 0;
    [[nodiscard]] virtual glm::vec2 cellCentre(float x, float z) const = 0;
    // A body came down on the surface at (x, z) at time t with `strength` (0..). The surface may respond (a tiles
    // ground springs). Repeating the same landing (a seek replaying it) must not respond twice.
    virtual void landed(glm::vec2 xz, double t, float strength) = 0;
};

} // namespace avgen::world
