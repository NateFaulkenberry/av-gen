#pragma once

// Environmental contact queries (ADR-551, Phase B §17): what a procedural animation layer is
// allowed to know about the world.
//
// **The point of the abstraction is what it hides.** A foot placement layer needs to know where the
// ground under a point is. It must not know whether that answer came from generated terrain, a
// raycast, a collision system, a baked height field or a designer's plane -- because the day one of
// those replaces another, a layer that knew would have to change, and a layer that asked would not.
//
// **Why it exists at all**, rather than the seam carrying ground points directly. `entity::
// LocomotionState` carries ONE ground plane for the whole body, `GroundFollower` seats the body on
// that plane, and `plantOnPlane` drops each foot onto the plane its own body is standing on -- so
// the target is reachable by construction and foot IK can never need the body to move. Measured:
// three Glowmere aliens on ground with **0.3499 m of relief across their own 0.9 m footprint**, and
// not one foot clamped or compensated. The feet were being planted on an idealised surface, not on
// terrain.
//
// Widening `LocomotionState` to carry four ground points would have fixed that case and built this
// seam without its shape. This is the seam.

#include <cstdint>

#include <glm/glm.hpp>

namespace avgen::scene {

// What was found under a point. Deliberately not a height: a surface has an orientation, and a
// query that returned only a number would make every caller re-derive the normal it already had.
enum class GroundCategory : std::uint8_t {
    Unknown,   // there is a surface and nothing is known about what it is
    Terrain,
    Water,     // the surface is the water's, not the bed's
    Authored,  // a plane or a mesh somebody placed
};
[[nodiscard]] const char* groundCategoryName(GroundCategory category);

struct GroundSample {
    glm::vec3 point{0.0f};              // the surface point, world space
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; // world space, unit
    // Signed, along world -Y, from the queried point to the surface. Positive means the surface is
    // below the query -- a foot in the air -- and negative means it is above, which is a foot
    // through the floor. Both are ordinary and a caller must handle each.
    float distance = 0.0f;
    GroundCategory category = GroundCategory::Unknown;
    // **False means "no answer", not "no ground".** Off the edge of a height field, inside a hole,
    // or with no source installed at all. A caller that treated an invalid sample as flat ground at
    // y=0 would plant a foot at the world origin, which is the failure this flag exists to prevent.
    bool valid = false;
};

// The question a procedural layer is allowed to ask.
//
// Const and allocation-free by contract: this is called per foot per character per frame, and an
// implementation that allocated would put it on the frame's hot path. It is *not* required to be
// thread-safe -- the entity update is single-threaded by assumption in two other places already
// (`nav_grid.hpp`, `perception.hpp`) and this follows the same rule.
class IGroundQuery {
public:
    virtual ~IGroundQuery() = default;
    // The ground beneath `worldPoint`. Implementations answer along world -Y; a wall-walker would
    // want a direction argument, and that is a revisit rather than a guess.
    [[nodiscard]] virtual GroundSample sampleAt(const glm::vec3& worldPoint) const = 0;
};

// The ground is a plane. What a scene with no terrain gets, and what a test uses when the question
// is about the layer rather than about the world.
class PlaneGroundQuery final : public IGroundQuery {
public:
    PlaneGroundQuery() = default;
    PlaneGroundQuery(glm::vec3 point, glm::vec3 normal) : point_(point), normal_(normal) {}
    [[nodiscard]] GroundSample sampleAt(const glm::vec3& worldPoint) const override;

private:
    glm::vec3 point_{0.0f};
    glm::vec3 normal_{0.0f, 1.0f, 0.0f};
};

} // namespace avgen::scene
