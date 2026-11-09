# ADR-1962: The horizon fill, so a finite world meets the horizon

- Status: Accepted (Musical Geography Phase 5, 2026-11-09), scene/musical-geography.
- Code: `Environment::horizonFill` (scene JSON `environment.horizonFill`, 0..1, default 0), carried in
  `FrameUniforms::aoFade.z`; the fill itself is in `fs_sky`, `shaders/skybox.wgsl`.

## Context

The owner's standing rule is that the world fills every frame with no visible edge. A tiles world ends where its
coarsest ring ends. From altitude, that edge is some degrees below the horizon: about 10° from 120 m with the CP-A
rings. Between the edge and the horizon the visible sky drew its below-horizon colour, a flat fill that read as a band.

A canary showed it: with the sky's ground colour painted magenta, 309,446 pixels showed at 120 m and 467,277 at 180 m
(4 m tiles, sunset revealC).

## Decision

Below the horizon, the visible sky eases to exactly what `applyFog` fades a surface at infinite distance toward: the
fog colour, or the sky's radiance where the fog takes the sky's colour. It eases over the sky's own ground band. A
world that went on would look exactly like that, so the fill is the ground continued to infinity under the fog.

At 0, nothing changes. The fill applies to the visible sky only; the fog's own map of the sky is untouched, so there
is no feedback.

## Consequences

Measured at 120 m and 180 m, 1080p ultra, 4 m tiles:

| Arm | Magenta pixels, 120 m / 180 m | GPU median, 120 m / 180 m |
|---|---|---|
| Neither | 309,446 / 467,277 | 19.1 / 18.0 ms |
| Fill only | 0 / 0 | 19.1 / 18.1 ms |
| A 64 m ring to 4.5 km, no fill | 80,962 / 121,813 | 22.4 / 22.1 ms |
| A 64 m ring to 4.5 km, plus the fill | 0 / 0 | 22.8 / 22.1 ms |
| The same ring with a plain material, plus the fill | 0 / 0 | 21.9 / 21.2 ms |
| Three far rings (64 / 256 / 1024 m, to 72 km), no fill | 43,910 / 61,978 | 23.3 / 23.8 ms |

- **The fill alone** costs nothing and removes the band, but the last ring's edge (16 m tiles at 1.1 km, only partly
  fogged) still reads as a line against it.
- **The fill with one fogged far ring** (64 m to 4.5 km, where the fog is opaque) leaves no edge and no band. Its
  plain-material version costs +2.8 ms, and the program version +3.7 ms.
- **Far rings alone** never close the gap: the ground only reaches the horizon asymptotically.
