# ADR-1956: The flight, a continuous drone over a tiles ground

- Status: Accepted (Musical Geography Phase 5, 2026-11-09), scene/musical-geography.
- Code:
  - `scene::FlightSpec`, `buildFlightTable`, `flightPoseAt`, `tileFlightGround`, in `src/scene/flight.{hpp,cpp}`;
  - the scene's `"flight"` block and `Composition::applyFlightCamera`, in `src/scene/composition.*`.

## Context

The piece is one continuous flight (03- §4.7). The owner's camera rule applies: the camera looks where it travels, with
no hero shots, orbits or head turns; dives are fine. Strange Attractor's `buildFlight` (ADR-1853/1854) already has the
right motion, but it is tied to the attractor's gorge.

## Decision

The art agent authors a route of waypoints: x and z in metres, an altitude above the ground, and optionally a speed. A
pure builder turns it into a 30 Hz table of eye, aim and bank, and the camera reads the table at the timeline's time.
Seeking therefore equals playing, and the route has no notion of a cut.

The builder:
1. A Catmull-Rom curve through the waypoints, flown by arc length at the interpolated speeds. Past the last waypoint it
   continues straight on, or loops back to the first.
2. **Terrain following:** the eye sits at the smoothed ground under the path plus the route's altitude. The same rule
   gives low hugging flight and high reveals.
3. **Clearance:** the highest ground within `clearanceRadius` at that sample's own time, on the moving ground
   (ADR-1953). It includes the waves and the authored rings, at `amplitudeHeadroom` × the authored amplitude for what
   a route may add, with the live rings' amplitude as a margin, plus `minClearance`. The eye is lifted over it by a
   cone envelope, which limits climb and fall rates, and then smoothed. This repeats in rounds until the smooth path
   clears everywhere; it is never clamped afterwards, because a clamp is a jolt.
4. **Aim:** the smoothed path `lookAhead` seconds ahead, with a small `pitchBias` down and the pitch softly held
   between `pitchMin` and `pitchMax`.
5. **Bank:** toward the turn, from the turn rate, through a tanh limit of `bankLimit`.

Every quantity is in metres. The ground is read through the ground node's own tile pitch, so changing the tiles from
1 m to 4 m leaves the route unchanged and the ground still cleared, as a test shows.

The table is rebuilt when the flight, the ground's tiles spec, its pitch or offset, or the moving ground's authored
block changes. It is never rebuilt per frame.

## Consequences

- **Tests** (`[flight]`):
  - clearance on the moving ground at every sample;
  - continuity of position, velocity (no step, acceleration under 30 m/s²) and bank;
  - the aim within 45° of the travel direction everywhere, which is the turn's own lead, never a head turn;
  - the pitch within its limits;
  - terrain following low (10 m) and high (80 m);
  - independence from tile scale (1 m against 4 m);
  - determinism, and seek equals play;
  - the scene camera following the table;
  - the JSON format.
- **Not yet built:** named targets (request 1979) and the top-down leg, both CP-B material. Strange Attractor keeps its
  own builder, so the refactor onto this module is deferred and its camera is untouched.

## Amendment (2026-11-09): named features and the top-down leg (request 1979)

`resolveFlightRoute` expands a route against the ground's tiles before the build:
- **`follow`**: `"follow": "path" | "river"` makes the waypoint a leg along that polyline, from `from` to `to`
  (fractions of its length, either direction), `offset` metres to the right of the direction flown.
- **`snap`**: `"snap": "shore" | "crown"` moves the waypoint's `position` (a hint) to the strongest shoreline or crown
  top within `snapRadius`. The search maximises the tile's shore channel, or its flat channel then height.

**The top-down leg:** `"nadir": seconds` on a waypoint. As the flight passes it, the pitch eases to −90° and back:
30% easing in, 40% held, 30% easing out. Screen-up follows the heading and turns to the travel direction itself on the
leg, so there is no flip at the nadir. The bank fades out on the leg. The flight's pose now carries its up vector, and
the scene camera uses it.

Tests (`[flight]`):
- follow points lie at the offset from the path;
- the snapped point sits on the shoreline;
- following a missing river is refused;
- the leg reaches straight down, lasts its span, and keeps screen-up continuous (consecutive dot product > 0.99) and
  perpendicular to the aim;
- screen-up at the nadir is the travel direction;
- there is no bank at the nadir;
- the look-where-it-travels test excludes the leg.
