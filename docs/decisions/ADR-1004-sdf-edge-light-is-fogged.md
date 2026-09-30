# ADR-1004: The SDF edge light is fogged like the surface it sits on

- Status: Accepted (2026-09-30), proto/procedural-space
- Amends ADR-1002. Implemented in `shaders/sdf_raymarch.wgsl`.

## Context

ADR-1002's edge emission was added after `shadeSurface`, which had already applied the distance fog.
So a repeated structure's edges stayed at full strength all the way to the march's end. On the
procedural-space lattice this read as a flat, depthless wireframe (the "generic cyberpunk" look the
POC brief rules out), and the distant edges, far thinner than a pixel, aliased into moire (the brief's
"noisy fractal sludge").

## Decision

The edge light is multiplied by the fog's transmittance at the hit before it is added to the colour
and the emission target. The transmittance is not recomputed: `applyFog(c, p)` is `mix(fog, c, f)`,
so `applyFog(1, p) - applyFog(0, p)` is exactly `f` for whichever fog model is active (height fog,
the fog start that hands over to the volumetric march, aerial perspective). The edge colour itself is
not tinted towards the fog colour: the surface under it already carries the in-scattered light, and
adding the fog colour twice would lift every edge pixel.

## Consequences

- Edges recede with the architecture: near edges are unchanged, and at the fog's e-folding
  distance an edge keeps 37 % of its light.
- With no fog (`volumeDensity` 0), `f` is 1 and the output is unchanged to the bit.
- Two extra `applyFog` calls per hit pixel, on the edge branch only (off when `look/edge/intensity`
  is 0).
- It is a look change with no new parameter, so there is no test beyond the render suite.
