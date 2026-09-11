#pragma once

// Placing assets by hand (ADR-069).
//
// Generate World composes a whole world from a recipe. This is the other half: putting one thing
// exactly where you want it. Both are needed and neither replaces the other -- a generated world is
// a starting point somebody then adjusts, and an adjustment you cannot make by pointing at the
// ground is an adjustment you make by typing coordinates.
//
// The layout maths lives here, apart from the UI and the engine, for the same reason the camera
// gestures do: "did the brush actually respect its spacing" and "is a cluster deterministic" are
// questions with answers, and they should not require a window to ask. What this file produces is a
// list of positions, yaws and scales; turning those into nodes is the caller's job, and it goes
// through `Engine::addNode` like everything else.

#include "assets/asset_library.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::app {

// How a click turns into objects.
enum class PlacementMode : std::uint8_t {
    Single,     // one, where you clicked
    Brush,      // a scatter filling the brush, spaced apart
    Cluster,    // a few, tight together, as one thing that grew there
    Landmark,   // one, deliberately enormous
    // ADR-092. These two place nothing on their own -- they are what a click *means* rather than
    // what it lays out -- so `planPlacements` returns an empty plan for Eraser and a Brush plan for
    // Replace, and the editor does the removing. They live in this enum anyway because to the person
    // holding the mouse they are brush modes, and a mode you have to leave the brush to reach is a
    // mode you stop using.
    Eraser,     // drag to remove what the brush covers
    Replace,    // remove what the brush covers, then paint over it
};
[[nodiscard]] const char* placementModeName(PlacementMode mode);

struct PlacementSettings {
    PlacementMode mode = PlacementMode::Single;
    float brushRadius = 4.0f;      // metres
    float spacing = 1.2f;          // minimum metres between two placements in a brush
    // 0..1 of what the spacing would allow. Spacing says how close two plants may be; density says
    // how much of that room to use, which is the control an artist actually reaches for -- "the
    // same meadow, thinner" is a density change and not a spacing change, and doing it with
    // spacing alone also changes how evenly the plants sit.
    float density = 1.0f;
    int clusterCount = 7;
    float clusterRadius = 1.4f;
    // 0 fills the disc evenly, 1 pulls everything toward a few centres. Evenly is the default
    // because a cluster that bunches reads as a target rather than as a patch of something growing.
    float clustering = 0.0f;
    float scaleJitter = 0.18f;     // +/- fraction: the scale range is 1-j .. 1+j
    float yawJitter = 1.0f;        // 0..1 of a full turn
    float landmarkScale = 4.0f;    // multiplies the asset's intended height in Landmark mode
    bool alignToNormal = false;    // lie along the surface rather than standing upright
    // Metres pushed into the ground. Negative lifts clear of it: one axis, both directions, rather
    // than a "sink" and a "height offset" that disagree about which way is which.
    float sink = 0.0f;

    // ---- where a placement is allowed to land (ADR-092, spec §21/§22) ------------------------
    // These do not lay anything out; they are the terms the ghost tests each planned instance
    // against and the reason it turns red. They are settings rather than constants because what
    // counts as too steep for a fern is not what counts as too steep for a boulder.
    float maxSlopeDegrees = 60.0f; // above this the ground is a cliff face
    bool avoidWater = true;        // do not plant in a lake
    bool avoidCollisions = true;   // do not plant inside something already there
    float collisionPadding = 0.0f; // extra metres of clearance demanded around each instance
    // 0 means "a new arrangement every stroke", which is what a brush should do. A fixed non-zero
    // seed makes a stroke reproducible, which is what a test and a bug report need.
    std::uint32_t seed = 0;
};

// One object to create. `scale` is a multiplier on whatever normalising scale the asset needs to
// reach its intended height -- kept separate so the caller can apply the library's own sizing and
// the placement's jitter without either having to know about the other.
struct Placement {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;      // radians about the surface normal
    float scale = 1.0f;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

// Where a click at `center` on a surface with `normal` puts things. Deterministic in `seed`: the
// same click with the same settings and seed produces the same objects, so a placement can be
// undone and redone, and so a test can assert about one.
//
// Brush mode uses Poisson-ish dart throwing rather than a uniform scatter: a uniform scatter over a
// disc clumps, and clumping is the one thing a brush must not do, because the user is already
// deciding where the density goes by moving the mouse.
[[nodiscard]] std::vector<Placement> planPlacements(const PlacementSettings& settings,
                                                    glm::vec3 center, glm::vec3 normal,
                                                    std::uint32_t seed);

// The uniform scale that makes `asset` stand `height` metres tall, or 1 when its bounds are unknown.
// `height` of 0 means "the height the library says this asset wants".
[[nodiscard]] float normalisingScale(const assets::AssetDescriptor& asset, float height = 0.0f);

// A name for a placed node: the asset's id, plus the smallest suffix that is not taken. Node names
// have to be unique and a person should not have to invent them.
[[nodiscard]] std::string uniquePlacementName(const std::string& assetId,
                                              const std::vector<std::string>& existing);

} // namespace avgen::app
