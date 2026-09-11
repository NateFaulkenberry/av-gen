#pragma once

// Water bodies (ADR-099): a renderable, inhabitable body of water, built *from* the `WaterCourse`
// terrain hands over (ADR-090, world/terrain_water.hpp).
//
// The division of labour is terrain_water.hpp's and this file honours it. A `WaterCourse` is
// terrain's statement of fact: where the channel is, which way it runs, how wide and how deep, and
// it is derived from the same `WorldMap::features` the ground was cut from -- so moving a river
// moves the water, and nothing is authored twice. A `WaterBody` adds only what a *renderer* needs
// and terrain has no opinion about: how fast the surface travels, how much of that speed is lost
// against the bank, how far the current wanders off the centreline, and the arc-length table that
// lets a floating leaf be placed at "sixty per cent of the way down" rather than at a world point.
//
// Nothing here re-derives a course. `flowAt`'s direction, its surface and its position along all
// come from the course itself, so a change to how terrain traces a river arrives here with no edit.
//
// Everything is a pure function of the course and the settings: the same world gives the same
// bodies, the same flow at a point and the same drift of what floats on it, whatever the frame rate
// and whichever run. Nothing in this file knows about meshes, the GPU or a frame.

#include "core/error.hpp"
#include "world/terrain_query.hpp"
#include "world/terrain_water.hpp"
#include "world/world_map.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::world {

// How the water a world holds actually moves. One set of numbers for the whole map, because this is
// a property of the *place* -- a mountain world runs fast, a fen barely at all -- and because the
// per-body facts that would otherwise want overriding (which way, how steep, how wide) already come
// from the course.
struct WaterFlowSettings {
    // Multiplies the speed the course derives from its own gradient (`WaterCourse::flowSpeed`).
    // 1 means "as fast as the terrain says", which is the default because the terrain is what
    // knows: a steep short river runs and a long flat one drifts, and an author who has just moved
    // a river should not then have to remember to retune a speed to match it.
    float speedScale = 1.0f;
    // Metres per second, overriding the course entirely. 0 = use the course, which is the normal
    // case; this is here for the shot where the river has to read faster than its gradient says.
    float speedOverride = 0.0f;
    // How much slower the water is at the bank than at the centreline, 0..1. Shear against the bank
    // is why a leaf in midstream overtakes one at the edge.
    float bankShear = 0.7f;
    // Radians the flow direction wanders off the centreline tangent, as a slow function of
    // position. Small: this is the difference between a current and an arrow.
    float meander = 0.18f;
    // How much the *speed* varies from reach to reach, 0..1, from the same slow spatial field: at
    // 0.3 some stretches run a third faster than the course's average and some a third slower.
    // This is §11's "occasional local variation", and it is spatial rather than temporal on purpose
    // -- a river does not speed up and slow down in place, it has fast reaches and slow pools, and
    // the pattern moves through them.
    float turbulence = 0.25f;
    // Still water: the fraction of `stillSpeed` a pond's surface carries, and the direction the
    // wind pushes it. A pond that is a perfect mirror reads as glass rather than as water.
    float stillFactor = 0.12f;
    float stillSpeed = 0.55f;            // m/s that `stillFactor` is a fraction of
    glm::vec2 windDirection{0.7f, 0.7f}; // normalised on use

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t structuralHash() const;
};

// What the flow is at one point of one body.
struct FlowSample {
    glm::vec2 direction{0.0f};  // unit XZ, or (0,0) where there is no flow at all
    float speed = 0.0f;         // metres per second
    float distance = 0.0f;      // metres from the centreline
    // 0 at the centreline, +-1 at the banks. Positive is to the *right* of the downstream
    // direction, where right is downstream x up -- so a course running toward +Z has its positive
    // side at negative x. Only the consistency matters; this is where it is written down.
    float across = 0.0f;
    float along = 0.0f;         // 0 at the head, 1 at the mouth
    float surface = 0.0f;       // metres; the body's surface level here
    bool inside = false;        // within the course's banks
};

// One body of water: the course terrain traced, plus how its surface moves.
struct WaterBody {
    WaterCourse course;             // ADR-090; terrain's, never edited here
    std::vector<float> arc;         // cumulative arc length at each centreline node
    bool flowing = false;           // a river with a descent, as opposed to a pond, lake or sea
    float speed = 0.0f;             // metres per second at the centreline
    float bankShear = 0.7f;         // how much of that is lost at the bank
    glm::vec2 stillDirection{0.0f}; // the direction a still body's surface carries its pattern
    // How far across the channel there is actually water, as a fraction of `halfWidth`, sampled at
    // `kWettedSamples` points along the course. A course's banks are a planar test and a bed is
    // not planar: the channel narrows, shoals and runs out at the head, and the nominal half-width
    // says none of that. This does, and it is a property of the world rather than of the frame, so
    // it is measured once here instead of by every floating leaf on every frame -- which is the
    // difference between 0.02 ms and 0.43 ms for a layer of three hundred.
    //
    // Empty when `waterBodies` was called without a terrain query, in which case the nominal
    // half-width is all anyone has.
    std::vector<float> wetted;

    [[nodiscard]] const std::string& name() const { return course.name; }
    [[nodiscard]] float halfWidth() const { return course.halfWidth; }
    [[nodiscard]] float length() const { return course.length; }
    [[nodiscard]] const std::vector<glm::vec3>& centre() const { return course.centreline; }

    // The flow at a world XZ point. Outside the banks this still reports the nearest point's
    // direction and position along (a leaf that has drifted wide is still in this river), with
    // `inside` false and the speed sheared toward zero.
    [[nodiscard]] FlowSample flowAt(glm::vec2 p) const;
    // The point and downstream tangent at a normalised arc position, which is how a drifting thing
    // is placed: it carries `along`, not a world position.
    [[nodiscard]] glm::vec3 pointAt(float along01) const;
    [[nodiscard]] glm::vec2 tangentAt(float along01) const;
    // Seconds to travel the whole course at `speed`; the natural loop period for a floating thing.
    [[nodiscard]] float transitSeconds() const;
    // How much of the centreline speed is lost at `r` = distance / halfWidth. Public because the
    // drifting-object system needs the same profile the surface uses, and two copies of it would be
    // two answers to "how fast is the water here".
    [[nodiscard]] float shearProfile(float r) const;
    // The usable half-width at a normalised arc position, in metres: `halfWidth()` where the table
    // is empty, and the measured wetted width where it is not. 0 means there is no water here.
    [[nodiscard]] float wettedHalfWidth(float along01) const;
};

// How finely `WaterBody::wetted` samples a course. 128 over Glowmere's 516 m river is a probe every
// four metres, which is finer than the channel's own width and far finer than anything that floats.
inline constexpr int kWettedSamples = 128;

// Every body a world holds, in the order `waterCourses` reports them.
struct WaterBodySet {
    std::vector<WaterBody> bodies;
    WaterFlowSettings settings;

    [[nodiscard]] bool empty() const { return bodies.empty(); }
    // The flow at a point, taken from whichever body's banks contain it (nearest otherwise).
    [[nodiscard]] FlowSample flowAt(glm::vec2 p) const;
    [[nodiscard]] const WaterBody* find(std::string_view name) const;
    // The fastest body in the set, which is what a per-vertex speed lane is a fraction of.
    [[nodiscard]] float fastest() const;
    [[nodiscard]] std::uint64_t structuralHash() const;
};

// Builds a body for every course terrain reports on this map. Pure: same map + same settings = same
// bodies. The map must be prepared (`WorldMap::prepare()`), which every parser and `defaultWorld`
// already do.
// `terrain` (optional) is used once, here, to measure how far across each reach there is actually
// water; see `WaterBody::wetted`. Pass one whenever there is one.
[[nodiscard]] WaterBodySet waterBodies(const WorldMap& map, const WaterFlowSettings& settings = {},
                                       const TerrainQuery* terrain = nullptr);
// The same, from courses already in hand -- the form to use when a caller has them, because
// `waterCourses` is a walk over the features and there is no reason to do it twice.
[[nodiscard]] WaterBodySet waterBodies(const std::vector<WaterCourse>& courses,
                                       const WaterFlowSettings& settings = {},
                                       const TerrainQuery* terrain = nullptr);

[[nodiscard]] Result<WaterFlowSettings> waterFlowFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json waterFlowToJson(const WaterFlowSettings& flow);

} // namespace avgen::world
