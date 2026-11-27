# Volumetric lighting

Light you can see in the air: shafts through a colonnade, a spotlight's cone in haze, coloured beams. A froxel grid
(a 3D grid aligned to the camera) carries the scene's air from the eye out to a distance and lights it with the
scene's own lights and their shadows (ADR-2210).

## Turning it on

```json
"environment": {
  "volumeDensity": 0.02,
  "volumeAnisotropy": 0.5,
  "volumetricLight": { "enabled": true, "distance": 160, "intensity": 1.0 }
}
```

| Control | Parameter | Meaning |
|---|---|---|
| `enabled` | `scene/volumetric/enabled` | the grid carries the air (off by default) |
| `distance` | `scene/volumetric/distance` | metres along the ray the grid carries; the surface fog carries the air beyond, under the same law |
| `intensity` | `scene/volumetric/intensity` | multiplies the light the air scatters (not its density) |

The air itself is the environment's existing controls, unchanged (ADR-2211): `volumeDensity`, the height layer
(`fogHeight`, `fogHeightFalloff`, `fogUpperDensity`, `fogHeightCurve`, `fogGroundFollow`, `fogPooling`),
`horizonDensity`, `volumeScattering`, `volumeAbsorption`, `volumeAnisotropy` (forward scattering: positive values make
air toward a light glow), the noise and `volumeEmission` with `fogColor`. All are ordinary parameters, so routes,
the timeline, OSC and MIDI drive them; nothing is smoothed over time, so an audio flash is in the air on its own
frame (ADR-2212).

## Which lights, and which shadows

- Each light lights the air by its `volumetric` strength (0 = not at all).
- A light that `castsShadow` shadows the air through its own shadow views -- the sun's cascades, a spot's map, a point
  light's cube (ADR-2214). Set `volumetricShadow: false` on a light to keep its shadow on surfaces only.
- The clustered local lights (beyond the eight frame lights) light the air by `volumeLocalLights`, unshadowed for now.
- A shaft ends where the shadows end: past the shadowed range (`environment.shadowRange`, about 77 m when not set) the
  air is unshadowed.

## Quality

The tier sets the sampling, never the look (ADR-2213): 80x45x32 froxels at Preview, 160x90x64 at Realtime, 240x135x128
at High and Offline (with a 2x2 shadow tap, and four fixed samples a froxel offline). Measured at 1080p on an M2 Max,
the grid costs well under a millisecond at Realtime.

## Limits (B1)

- Placed media (fog banks, tornado, vortex) are still drawn by the march, and do not yet block the grid's light.
- A scene whose air names a density field (or a colour field with emission) keeps its air in the march; volumetric
  lighting says so in the log and in the render stats rather than drawing something different.
- Local box and sphere volumes, shadowed local lights, the live-ladder levers and the Draft view's treatment arrive in
  later phases.
- The lab: `examples/labs/volumetric/` (environments e01 light shafts, e02 dense atmosphere, e06 coloured scattering;
  cases 101, 102, 201, 601). Regenerate with `python3 examples/labs/volumetric/build.py`.

## Measuring the cost

Compute timestamps are unwritten on the Dawn/Metal path, so the grid's own passes read 0 ms in the GPU timeline.
`AVGEN_VOLUMETRIC_REPEAT=n` (diagnostic only; 1..64) encodes inject and integrate n times a frame with an identical
result, so their cost is `(GPU frame at n - GPU frame at 1) / (n - 1)`. The apply's cost is the
`volume.composite` render pass, which is timed.
