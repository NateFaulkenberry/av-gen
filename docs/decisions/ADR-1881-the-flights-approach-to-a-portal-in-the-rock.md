# ADR-1881: The flight's approach to a portal in the rock

- Status: Proposed (Gate A, dimensional portals), scene/strange-attractor.
- Code:
  - `AttractorGen::Approach` (`site`, `departArc`, `speed`, `slowFrom`, `slowFor`), in the `"approach"` object of
    `attractorWorld`. It is structural: it is in the hash and regenerates the world.
  - `buildFlight`.
  - `buildTerrainMesh`: the forced refinement around the site.
- Tests: `tests/unit/test_attractor_world.cpp` (`[adr1881]`):
  - the river run before the slowing is the plain flight, exactly;
  - the flight reaches the site at the approach speed;
  - round-trip and refusals.

## Context

The owner, after Gate R: the portal must be part of the terrain, a canyon or river wall that the river could run
into, chosen from the real camera path. The camera must meet it at 20–30 m/s, against today's 80 m/s, and cross on a
bar line.

## Decision

- Enabled, the flight follows the gorge until `departArc` metres along its centreline. Then it flies straight at
  `site`, easing its height to the site's.
- From `slowFrom` it eases from the river speed to `speed` over `slowFor` seconds.
- The climb and the reveal are not flown.
- On the approach, the clearance lift is off (it would carry the camera over the wall), and the view is level at the
  aperture.
- Past the site the line continues: Gate B crosses there.
- Around the site the terrain mesh is refined whatever its flatness:
  - no hypotenuse longer than 1.5 m within 140 m;
  - none longer than 4.5 m within 320 m.
  - The corruption (ADR-1873) deforms it at metre scale, and a flat wall would otherwise be two triangles.
  - A forced split is an error above the threshold, so it propagates and the mesh stays watertight (ADR-1867).

The Strange Attractor's site is the river's own east bank wall below the escarpment, at (188.5, 30, 3314.2), facing
WNW. The camera departs the river at gorge arc 1572 m, slows from 21.25 s over 6.5 s to 22 m/s, and crosses the
wall's plane at about 32.0 s. That is the downbeat of bar 17, the start of the track's second section.

## Consequences

- Disabled, the flight and the mesh are ADR-1853's and ADR-1867's.
- Enabled, 0–17.9 s is identical to 1e-6 m. The mesh changes away from the old path's later legs, which are no
  longer flown: 2.92M triangles become 2.84M.
