# ADR-2120: a vortex in the cloud sea

- Status: Accepted (2026-11-21), avagent/neon-dreams (Neon Dreams, iteration 12; owner msg 149: "turn the ocean
  break into a CLOUD VORTEX" at 62 s -- owner-requested, overriding the old no-vortex rule for this shot)
- Code: `scene::CloudVortex` (`src/scene/clouds.{hpp,cpp}`); `vortexLocal`, `vortexSwirl`, `vortexSurface`,
  `vortexGlow`, `vortexColumn`, `columnSpan` and the swirl in `animTap` and `seaSurface` (`shaders/clouds.wgsl`).
- Tests: `tests/unit/test_clouds.cpp` (`[adr2120]`), `tests/rendering/test_clouds_it12_gpu.cpp` (`[adr2120]`).

## Decision

A whirlpool round a vertical axis, part of the relief sea (ADR-2101), in the plan frame of an ellipse (centre,
elongation, yaw, as the breaks):

- **Swirl**: inside 1.4 `radius` every sea tap (the surface's octaves, the billow, the detail) and every animated tap
  is read where its noise was `tau` seconds ago, turned back about the axis by `spin / (1 + (r / 1.5 eye)^2) x tau`,
  plus a fixed `twist` with depth and a fixed `wind` along log spirals (0 at the rim, `wind` at the eye: the shear that
  draws a still frame as a whirl). `tau` runs on the churn's two clocks (ADR-2119), cross-faded, so the winding is
  bounded however long the song.
- **Funnel**: `arms` log-spiral troughs in the sea (a pattern turning at 0.4 of the spin), then the surface falls
  `depth` metres in `shelves` terraces (wound in and out with the arms by `pitch` shelves) to an `eye` where the sea
  is gone, its rim torn by the surface noise. The walls keep a third of the sea's relief so the shelves read.
- **Light**: `glowColor x glow` from the eye, falling over `glowHeight` above the rim (four times higher over the
  eye) and with the distance outside it; summed with the glow emitters, so ADR-2104's breakup and shadow-toward shape
  it.
- **Column**: optional mist spiralling up out of the eye (`column` metres over the mean top, `columnDensity`,
  `columnPitch`), widening as it rises, banded by the arms, churned.
- Routes: `clouds/vortex/glow`, `clouds/vortex/pulse` (-1..1: depth x (1 + 0.35 pulse), eye x (1 + 0.15 pulse)),
  `clouds/vortex/spin`.

## Consequences

- Inside the vortex every sea surface evaluation is two (the clocks): the shadow taps included. Only there.
- `spin` routed abruptly turns the noise by `delta spin x tau` at once; Neon Dreams routes glow (pad) and pulse (bass).
- Neon Dreams: the break is replaced (no `sea.breaks`), the cosmic ocean moves down under the eye (-228 m).
