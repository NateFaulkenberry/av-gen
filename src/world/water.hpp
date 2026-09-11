#pragma once

// Water bodies (ADR-091): the flowing description of the water a WorldMap already places.
//
// `WorldMap` answers *where* water is -- `waterSurface(p)` and `Sample::submerged` -- and that is
// all a flat sheet ever needed. A river that moves needs one more thing: which way is downstream,
// at every point of it, and how fast. That is not a rendering decision and it is not a second
// description of the geography; it is read off the geography that is already there. A water
// feature's path carries a level in y, and `WorldMap::prepare()` has already turned that path into
// a smoothed curve. Downstream is the direction along that curve in which the level falls.
//
// So a WaterBody is *derived*, never authored twice: `WaterBodySet::fromMap` walks the map's water
// features and builds one body per feature. What an artist authors is the same river they always
// authored, plus how fast it runs. A body with no fall in it -- a pond, a tarn, a lake -- reports
// no flow direction at all and a slow rotation instead, which is what still water does under wind.
//
// Everything here is a pure function of the map and the settings: the same map gives the same
// bodies, the same flow at a point and the same drift of the things floating on it, whatever the
// frame rate and whichever run. Nothing in this file knows about meshes, the GPU or a frame.

#include "core/error.hpp"
#include "world/world_map.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::world {

// What kind of body this is, which decides what "motion" means on it. Derived from the feature's
// own shape rather than declared: a course that descends is a river, one that does not is still.
enum class WaterBodyKind : std::uint8_t { River, Still };
[[nodiscard]] const char* waterBodyKindName(WaterBodyKind kind);

// How fast the water a world holds actually moves. One set of numbers for the whole map, because
// flow speed is a property of this *place* -- a mountain world runs fast, a fen barely at all --
// and a per-river override is a knob nobody has asked for yet.
struct WaterFlowSettings {
    // Metres per second at the centre of a channel of average fall. Real lowland rivers run
    // 0.3-1.0 m/s; the surface *pattern* reads faster than the water does, so this is the speed
    // the drifting things travel at and the shader's slow layer, not the ripple speed.
    float flowSpeed = 0.55f;
    // How much the fall of the bed changes the speed: 0 = every reach runs at `flowSpeed`,
    // 1 = a reach twice as steep as the average runs twice as fast. A river that speeds up through
    // its steep reaches and slows through its pools is the cue that it is water and not a conveyor.
    float gradientResponse = 0.6f;
    // How much slower the water is at the bank than at the centreline, 0..1. Shear against the
    // bank is why a leaf in midstream overtakes one at the edge.
    float bankShear = 0.7f;
    // Radians of wander the flow direction takes from the centreline tangent, as a slow function of
    // position. Small: this is the difference between a current and an arrow.
    float meander = 0.18f;
    // Still water: radians per second of the slow rotation a pond's surface carries, and the
    // direction the wind pushes it. Speed is `flowSpeed * stillFactor`.
    float stillFactor = 0.12f;
    glm::vec2 windDirection{0.7f, 0.7f}; // normalised on use

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t structuralHash() const;
};

// What the flow is at one point of one body.
struct FlowSample {
    glm::vec2 direction{0.0f};  // unit XZ, or (0,0) where there is no flow
    float speed = 0.0f;         // metres per second
    float distance = 0.0f;      // metres from the centreline
    float across = 0.0f;        // -1 at the left bank, 0 at the centreline, +1 at the right
    float along = 0.0f;         // 0 at the head, 1 at the mouth (arc length, normalised)
    float surface = 0.0f;       // metres; the body's surface level here
    bool inside = false;        // within the body's channel
};

// One body of water: a centreline, a width, and the motion along it.
//
// `centre` is the map feature's *smoothed* curve, so the body follows the same line the terrain was
// cut along -- not the authored polyline, which has mitred bends the channel does not.
struct WaterBody {
    std::string name;
    WaterBodyKind kind = WaterBodyKind::River;
    std::vector<glm::vec3> centre;  // (x, level, z) in world metres, head first
    std::vector<float> arc;         // cumulative arc length at each point; arc.size() == centre.size()
    float halfWidth = 7.0f;         // metres from the centreline to the nominal bank
    float length = 0.0f;            // metres of centreline
    float fall = 0.0f;              // metres the level drops from head to mouth
    float gradient = 0.0f;          // fall / length
    float speed = 0.0f;             // metres per second at the centreline, average over the course
    glm::vec2 stillDirection{0.0f}; // Still bodies: the wind direction their surface carries
    glm::vec3 centroid{0.0f};       // Still bodies: the point the slow rotation turns about

    // The flow at a world XZ point. Outside the channel this still reports the nearest point's
    // direction and arc position (a floating thing that has drifted wide is still in this river),
    // with `inside` false and the speed sheared toward zero.
    [[nodiscard]] FlowSample flowAt(glm::vec2 p) const;
    // The point and downstream tangent at a normalised arc position, which is how a thing that
    // drifts downstream is placed: it carries `along`, not a world position.
    [[nodiscard]] glm::vec3 pointAt(float along01) const;
    [[nodiscard]] glm::vec2 tangentAt(float along01) const;
    // Seconds to travel the whole course at `speed`; the natural loop period for anything drifting.
    [[nodiscard]] float transitSeconds() const;
    // How much of the centreline speed is lost at `r` = distance / halfWidth. Public because the
    // drifting-object system needs the same profile the surface uses, and two copies of it would
    // be two answers to "how fast is the water here".
    [[nodiscard]] float shearProfile(float r) const;

    float bankShear = 0.7f; // copied from the set's settings at derivation; see shearProfile
};

// Every body a world holds, derived from its water features.
struct WaterBodySet {
    std::vector<WaterBody> bodies;
    WaterFlowSettings settings;

    [[nodiscard]] bool empty() const { return bodies.empty(); }
    // The flow at a point, taken from whichever body's channel contains it (nearest centreline
    // when none does). Zero where the map has no water features at all.
    [[nodiscard]] FlowSample flowAt(glm::vec2 p) const;
    // Which body is nearest to p, or -1 when there are none.
    [[nodiscard]] int nearestBody(glm::vec2 p) const;
    [[nodiscard]] const WaterBody* find(std::string_view name) const;
    [[nodiscard]] std::uint64_t structuralHash() const;
};

// Derives the bodies from a *prepared* map (`WorldMap::prepare()` must have run; an unprepared map
// falls back to the authored polylines). Pure: same map + same settings = same bodies.
[[nodiscard]] WaterBodySet waterBodies(const WorldMap& map, const WaterFlowSettings& settings = {});

[[nodiscard]] Result<WaterFlowSettings> waterFlowFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json waterFlowToJson(const WaterFlowSettings& flow);

} // namespace avgen::world
