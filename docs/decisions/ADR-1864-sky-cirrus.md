# ADR-1864: The analytic sky has a cirrus layer

- Status: Accepted by the owner (2026-10-28), scene/strange-attractor. Extended by ADR-1865 (bands, water).
- Code:
  - `scene::SkyCirrus` (`src/scene/scene_types.hpp`) and `SkySettings::cirrus`.
  - In `src/scene/sky.{hpp,cpp}`:
    - `skyCirrus`, the CPU twin;
    - `packSkyCirrus`;
    - `skyCirrusFromJson` / `skyCirrusToJson`;
    - `SkyRuntime::cirrus`, the hash and the rebuild tolerance;
    - `skyRadiance`, which now applies the layer.
  - `shaders/sky_cirrus.wgsl` is included by `environment.wgsl` (the sky cube) and `sky_background.wgsl` (the visible
    sky).
  - Uniforms: `EnvUniforms` gains `cirrus0..3` and `FrameUniforms` gains `skyCirrus0..3`, both appended.
  - `SceneRenderer` draws the analytic background whenever the sky has cirrus.
  - The Composition handles `environment.sky.cirrus`.
- Tests:
  - `[adr1864]` in `tests/unit/test_sky.cpp`:
    - amount 0 is bit-identical and leaves the hash unchanged;
    - the layer never goes below the horizon;
    - coverage thins it;
    - the block round-trips and refuses unknown keys.
  - `[cirrus][adr1864]` in `tests/rendering/test_sky_gpu.cpp`:
    - amount 0 draws the plain sky's bytes;
    - per pixel, the GPU streaks fall where the CPU twin says. Disagreement must stay under 1% of the decided pixels.
- Evidence: `docs/prototypes/strange-attractor/08-canyon-cirrus.md`.

## Context

The Strange Attractor's dusk has thin streak clouds. Since Phase 1b they were 18 lobes of ADR-1289's volumetric
clouds: long, flat ellipsoids at 3–4.5 km. That pass marches every pixel from the first lobe it enters to the last
one it leaves. A streak seen at a grazing angle is kilometres of nearly empty steps, each evaluating every lobe the
ray hits.

It cost 18–26 ms of a 1080p ultra frame (20.7 ms in this measurement). At 24 steps it costs 5.4 ms but breaks up into
dither speckle.

## Decision

Cirrus becomes part of the analytic sky, with no volume. Each direction is projected onto a plane `altitude` metres
up. The coordinates are rotated into the streaks' axis, scaled by `length` along it and `width` across, warped once
across the streaks, and turned into cover by four octaves of value noise thresholded between `coverage` and
`coverage + softness`.
- **Fading.** The cover fades in over the first 0.04 of elevation, which is also where the projection would alias.
- **Colour.** It mixes `shadeColor` to `litColor` with the sun's side of the sky, plus a forward-scattering lift
  toward the sun.
- **Compositing.** The layer is laid over the sky gradient, and the sun disc and aureole shine through at 40%.
- **One sky.** The cube, the visible sky and the CPU twin evaluate the same function.
  - The noise lattice uses an integer hash, so CPU and GPU agree on it bit for bit.
  - The cube carries `reflection` × the layer, because a cube face is too coarse for a streak. At full strength a
    mirror-still river showed the layer as soft blotches.
  - The visible sky always carries the whole layer, drawn analytically (ADR-345/1070's path) whenever the sky has
    cirrus, offline as well as live. The cube still provides the lighting.
- **Off by default.** `amount` 0 changes nothing, bit for bit: the uniforms are zero, the hash is unchanged and the
  background path is the old one.
- **Clouds remain.** ADR-1289's volumetric clouds are untouched, for scenes that want sculptural masses.

## Consequences

- The Strange Attractor at 1080p ultra on an M2 Max goes from 20.7 ms of clouds and 82.7 ms total GPU to 0 ms of
  clouds and 61.3 ms total. The cirrus costs a few ALU per sky pixel in the background pass, which measures 0.00 ms.
- In the canyon the streaks match the 1c look and are finer. `canyon-cirrus/` has the comparisons.
- The frame block and the environment pass's uniform each grew by 64 bytes. The layout guards check both.
