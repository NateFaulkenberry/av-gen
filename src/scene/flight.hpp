#pragma once

// ADR-1956: the flight -- a continuous drone camera over a "tiles" ground, built once into a seekable table.
//
// An art-authored route of waypoints (x, z in metres; an altitude above the ground; optionally a speed) is turned into
// a 30 Hz table of eye, aim and bank:
//   1. The route is a centripetal-free Catmull-Rom through the waypoints, flown by arc length at the waypoints' speeds
//      (interpolated), from `startTime` on the timeline. Past the last waypoint the line continues straight (or, with
//      `loop`, comes round to the first), so a long song never runs out of route. There is no cut anywhere.
//   2. Terrain following: the eye is the ground's smoothed height under the path plus the route's altitude, so the
//      flight hugs the land low and holds a reveal high with one rule.
//   3. Clearance: the highest ground within `clearanceRadius` of the eye AT THAT SAMPLE'S TIME -- the moving ground,
//      waves and authored rings included (ADR-1953), with `amplitudeHeadroom` over the authored amplitude for what a
//      route may add, and the live rings' amplitude as a margin -- plus `minClearance`. The height is lifted over it by
//      a cone envelope (it climbs at most `climbRate`, falls at most `fallRate`) and smoothed, twice, so it never
//      touches the ground and never jitters over metre-scale relief.
//   4. Aim: along the smoothed path `lookAhead` seconds ahead -- it looks where it travels, never at a subject -- with
//      a little `pitchBias` down, the pitch softly held between `pitchMin` and `pitchMax`, and smoothed.
//   5. Bank: toward the turn, proportional to the turn rate through a tanh limit of `bankLimit` degrees.
// Every distance is metres and the ground is asked through the tile query at the ground node's own pitch, so the
// flight is the same flight whatever the tile size. The algorithm is Strange Attractor's (ADR-1853/1854), made
// general over any tiles ground.

#include "core/error.hpp"
#include "scene/tile_dynamics.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <string>
#include <vector>

namespace avgen::scene {

struct TileTerrainSpec;

struct FlightWaypoint {
    glm::vec2 position{0.0f}; // x, z (metres, the ground's generator space)
    float altitude = 20.0f;   // metres above the (smoothed) ground
    float speed = -1.0f;      // metres per second from here; < 0 = the flight's speed
    // Named features (request 1979), resolved against the ground's tiles before the build (resolveFlightRoute):
    //   follow "path" | a river ("river", "river1", an index or its name; ADR-1965): this waypoint becomes a leg along that polyline, from `from` to `to` (fractions of
    //     its length, 0..1, either way round), `offset` metres to the right of it, at this altitude and speed;
    //   snap "shore" | "crown": `position` is a hint, moved to the nearest shoreline / crown top within `snapRadius`.
    std::string follow;
    float from = 0.0f;
    float to = 1.0f;
    float offset = 0.0f;
    std::string snap;
    float snapRadius = 300.0f;
    // The top-down leg (the owner's one exception to "look where it travels"): as the flight passes this waypoint the
    // aim eases to straight down and back over `nadir` seconds, screen-up the direction of travel, so there is no
    // flip at -90 degrees. 0 = none.
    float nadir = 0.0f;
};

struct FlightSpec {
    bool enabled = false;
    std::string ground;               // the "tiles" procedural node the flight follows (its spec and pitch)
    std::vector<FlightWaypoint> route; // at least two
    // ADR-2010: a looped route comes round to its first waypoint and the flight never ends: the table covers three
    // laps, and from the second lap on the pose repeats the second (eased into its own start over the lap's last
    // seconds), so the playhead can run on forever (a live scene's endless transport, ADR-1342). `duration` is not
    // read on a loop.
    bool loop = false;
    float startTime = 0.0f;           // timeline seconds at the first waypoint
    float duration = 600.0f;          // seconds the table covers (past it, the last pose holds); not read on a loop
    float speed = 22.0f;              // metres per second
    float minClearance = 4.0f;        // metres above the highest ground near the eye
    float clearanceRadius = 10.0f;    // metres
    float amplitudeHeadroom = 2.0f;   // the moving ground's amplitude assumed at up to this many times the authored
    float climbRate = 18.0f;          // metres per second
    float fallRate = 30.0f;           // metres per second
    float smoothing = 0.6f;           // seconds (the path's Gaussian sigma)
    float groundSmoothing = 1.2f;     // seconds (the followed ground's Gaussian sigma)
    float lookAhead = 2.5f;           // seconds
    float pitchBias = 0.12f;          // the aim's drop per metre ahead (looking a little down)
    float pitchMin = -28.0f;          // degrees
    float pitchMax = 8.0f;            // degrees
    float bankLimit = 11.5f;          // degrees
    float bankGain = 1.6f;            // degrees of bank per (degree per second of turn), before the limit
    float fov = 0.0f;                 // degrees; 0 = the scene's own
};

inline constexpr float kFlightRate = 30.0f;

// The ground the flight reads: height of the tile under (x, z) at time t, on the moving surface.
using FlightGround = std::function<float(float x, float z, float t)>;

struct FlightTable {
    std::vector<glm::vec4> eye; // xyz, w = bank (radians)
    std::vector<glm::vec3> aim; // unit view direction
    std::vector<glm::vec2> heading; // unit, the horizontal direction of travel (screen-up on the top-down leg)
    std::vector<float> nadir;       // 0..1, how far into a top-down leg each sample is
    std::vector<float> clearance; // the ground bound the eye was lifted over (metres), per sample
    float lap = 0.0f; // ADR-2010: seconds per lap of a looped route (0 = not looped); the table covers three
    [[nodiscard]] bool valid() const { return eye.size() >= 2 && aim.size() == eye.size(); }
};

struct FlightPose {
    glm::vec3 eye{0.0f};
    glm::vec3 target{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f}; // perpendicular to the aim, the travel direction at the nadir (no roll applied)
    float rollDegrees = 0.0f;
};

[[nodiscard]] Result<void> validateFlight(const FlightSpec& spec);
// The route with its named features resolved against the ground (generator space): every `follow` leg expanded into
// waypoints along the polyline, every `snap` moved onto its feature. Pure; plain waypoints pass through.
[[nodiscard]] Result<FlightSpec> resolveFlightRoute(const FlightSpec& spec, const TileTerrainSpec& tiles);
// The table. `ground` is the moving surface; `liveMargin` is added to the clearance for what cannot be known ahead
// (live-triggered rings). Pure: the same spec and ground give the same table.
[[nodiscard]] FlightTable buildFlightTable(const FlightSpec& spec, const FlightGround& ground, float liveMargin);
// The pose at timeline second `t` (linear between samples; each signal is smooth already). Seekable. On a looped
// table (ADR-2010) any `t` past the second lap is the second lap's pose again: endless, and still a function of `t`.
inline constexpr float kFlightLoopBlend = 10.0f; // ADR-2010: seconds over which a lap's end eases into its start
[[nodiscard]] FlightPose flightPoseAt(const FlightSpec& spec, const FlightTable& table, double t);
// The ground a tiles spec gives at `pitch` on the moving ground `dyn` (authored events only), amplitude scaled.
[[nodiscard]] FlightGround tileFlightGround(const TileTerrainSpec& tiles, const TileDynamics& dyn, float pitch,
                                           float amplitudeScale);
// A conservative bound on what live rings can add (their amplitude x the scene's amplitude x headroom).
[[nodiscard]] float flightLiveMargin(const TileDynamics& dyn, float amplitudeHeadroom);

[[nodiscard]] nlohmann::json flightToJson(const FlightSpec& spec);
[[nodiscard]] Result<FlightSpec> flightFromJson(const nlohmann::json& j);

} // namespace avgen::scene
