# ADR-1055: The world wave: a band of coloured light travelling through the world

- Status: Accepted (2026-10-01), proto/liminal-space (All You Got art pass 3, §30)
- Code:
  - `PostSettings::wave*` and `post/wave/*` in `src/scene/post_settings.*`.
  - `SdfObjectUniforms::wave0-4` in `src/rendering/sdf_renderer.*`.
  - `sdfWaveAt` / `sdfWaveRecolor` and the wave term in `fs_sdf` in `shaders/sdf_raymarch.wgsl`.
- Tests:
  - `tests/unit/test_post_wave.cpp` (`[adr1055]`).
  - `tests/rendering/test_sdf_gpu.cpp` (`[adr1055]`): the band lands where the front is and moves with the
    progress, the trail recolours the lines behind it, and off is byte-identical.

## Context

The owner (§30, about 3:28): not "a rainbow rectangle wiping across the screen" (ADR-1050's sweep is
screen-space), but "a wave of coloured light moves through the room, surfaces change colour as it passes ... the
environment transitions from one palette to another", like GV3's camera beam.

The existing world effect (`travelBeam`, ADR-207/702) does reach ray-marched SDF surfaces, and it is a working
stand-in. But it only adds light. In this world the fills are near-black and what reads is emission and the edge
lines (ADR-1047), so a palette change has to recolour the lines and the emission, not just light the surfaces.

## Decision

The wave is a set of scene-wide parameters, `post/wave/*`. They are copied into every SDF object's uniforms, so one
wave sweeps every object at once.

- **The front:**
  - It is a plane at `progress` metres from `origin` along `direction`, with a band `width` metres half-wide.
  - Key `progress` to move it, for example from -2 to the far wall over the two beats.
- **What it does in the band:**
  - It adds `intensity` of light in its colour, into the colour and the bloom target, fogged.
  - It recolours edge lines and surface emission toward its colour by `edgeTint`.
  - The colour is `color`, or a cosine rainbow across the band when `hueSpan` > 0 (from hue `hue`).
- **What it does behind the front:**
  - It pulls the lines and emission toward `trailColor` by `trail`. That leaves the new palette behind the front.
  - Key `palette/position` to arrive as the front leaves, then key `trail` back to 0.
- **The recolouring keeps each line's luminance and changes only its hue,** so a line neither brightens nor dims
  as the band passes.
- **Off while `intensity`, `edgeTint` and `trail` are all 0** (the defaults): the term is skipped and the image is
  byte-identical (tested).
- Every setting is a parameter, so it can be keyed and routed. A scene's `post` block takes `waveOrigin`,
  `waveDirection`, `waveProgress`, `waveWidth`, `waveIntensity`, `waveColor`, `waveHue`, `waveHueSpan`,
  `waveEdgeTint`, `waveTrail` and `waveTrailColor`.

## Consequences

- The SDF object uniform block grows to 352 bytes, within the 512-byte stride. The wave itself costs one dot
  product and an exp per shaded pixel.
- The wave touches SDF ray-marched objects only. Mesh nodes (the lyric text, glTF figures) are lit by it only
  through bloom. A `travelBeam` alongside it adds light to meshes too.
- One wave at a time. A second one means a second set of parameters, or the `travelBeam` effect.
