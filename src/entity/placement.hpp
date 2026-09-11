#pragma once

// Choosing where a hero element goes, from what the world and the frame already know (ADR-088).
//
// Placing something well is a search, and every term in it is already answerable:
//
//   * the frame -- is it on screen, how big does it read, does it crowd the subject -- is
//     arithmetic on the scene's own Camera, done with the Camera's own matrices so that "in frame"
//     here and "in frame" in the renderer are the same claim;
//   * the ground -- how high, how steep, under water -- is `WorldMap::sample`;
//   * what grows there, and whether a point is inside a hero, is `ClearanceField` (ADR-080).
//
// So there is no step in this that wants a person nudging coordinates and re-rendering, which is
// the process this exists to replace. Candidates are drawn in *screen* space and unprojected, not
// scattered over the world: a world grid spends almost all of its samples behind the camera, and
// the thing being chosen is a composition.
//
// Deterministic: the sampler is a seeded PCG32 and nothing reads a clock, so the same brief always
// returns the same point and a placement can be committed to a scene file.

#include "core/rng.hpp"
#include "scene/scene_types.hpp"
#include "world/camera_clearance.hpp"
#include "world/hero.hpp"
#include "world/world_map.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string>

namespace avgen::entity {

struct PlacementBrief {
    // ---- the frame ----
    const scene::Camera* camera = nullptr;
    float aspect = 16.0f / 9.0f;
    // Where in the frame it should sit, in normalised device coordinates: (0,0) is the centre,
    // (-1,-1) the bottom left. The default is the upper left third, which is where a second
    // subject goes when the first one owns the centre.
    glm::vec2 screenTarget{-0.42f, 0.40f};
    float screenSpread = 0.30f;     // how far from that it may land, in NDC
    float minScreenRadius = 0.035f; // it has to read as an object
    float maxScreenRadius = 0.170f; // and not take the frame off the subject
    float minDistance = 30.0f;
    float maxDistance = 220.0f;

    // ---- the thing ----
    float radius = 8.0f;          // horizontal half-extent, metres
    float halfHeight = 3.0f;
    float clearanceBelow = 10.0f; // metres of empty air under it: a beam, a shadow, somewhere to descend

    // ---- the world ----
    const world::WorldMap* map = nullptr;
    const world::ClearanceField* field = nullptr;
    // What it must not stand inside, and must not sit on top of in the frame. Usually the scene's
    // heroes: they are what the shot is already about.
    std::span<const world::HeroPoint> heroes;
    bool requireSkyline = true;  // the line of sight must clear the ground the whole way
    float maxGroundSlope = 0.5f; // where the beam lands has to be ground, not a cliff

    std::uint32_t seed = 1u;
    int samples = 40000;
};

// Why candidates were rejected, so a brief that finds nothing says which constraint was doing it
// rather than just failing.
struct PlacementRejects {
    int offScreen = 0;
    int tooSmall = 0;
    int tooLarge = 0;
    int noHeadroom = 0;
    int insideHero = 0;
    int crowdsHero = 0;
    int notSkylined = 0;
    int badGround = 0;
    int outOfWorld = 0;
    [[nodiscard]] std::string summary() const;
};

struct PlacementResult {
    bool found = false;
    glm::vec3 position{0.0f};
    float score = 0.0f;
    float screenRadius = 0.0f;   // as a fraction of the half-frame height
    glm::vec2 ndc{0.0f};
    float distance = 0.0f;       // from the eye
    float groundBelow = 0.0f;
    float canopyBelow = 0.0f;
    float headroom = 0.0f;       // metres between the underside and the top of what grows below
    PlacementRejects rejects;
};

[[nodiscard]] PlacementResult findPlacement(const PlacementBrief& brief);

// The ray through a normalised device coordinate, for callers that want to place by hand and check
// the result. Unit length.
[[nodiscard]] glm::vec3 rayThrough(const scene::Camera& camera, float aspect, glm::vec2 ndc);

} // namespace avgen::entity
