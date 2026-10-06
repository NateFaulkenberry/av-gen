# ADR-1151: Reflection-only light bands are an environment term

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, iteration 3, step 3)
**Date:** 2026-10-06
**Resolves:** "reflection-only bands as an environment term" (`docs/prototypes/astral-forge/06-iteration-2.md`,
"Honest state of the production path", next step 4; research `01-research.md` §E, the visual language's
"light only in reflections" in `02-visual-language.md`).
**Implemented by:** `scene::ReflectionBand`, `ReflectionSoftbox`, `ReflectionBands` and `Environment::bands` in
`src/scene/scene_types.hpp`; the file block, the parameters, the packing and the CPU twin in
`src/scene/reflection_bands.{hpp,cpp}`; `src/scene/composition.{hpp,cpp}` (`environment.bands`, read, saved,
registered, applied per frame); `FrameUniforms::bands*` in `src/rendering/scene_renderer.{hpp,cpp}` and
`shaders/common.wgsl`; `shaders/reflection_bands.wgsl` (`reflectionBands`, `reflectionBandDirection`,
`bandEnvBrdf`); the band term in `shaders/pbr_shade.wgsl`; the particle path's copy (ADR-1153) in
`ParticleUniforms` / `shaders/particles.wgsl`.
**Tests:** `tests/unit/test_astral_production_look.cpp` (`[adr1151]`: round trip and refusals; zero lanes when
absent; the CPU radiance's peak, roughness widening, rig rotation and dash phase; the parameters reach the
environment and the save writes them back; no parameters and no key without the block);
`tests/rendering/test_astral_production_look_gpu.cpp` ("bands: a mirror reflects them, the background stays
black, and at gain 0 nothing is lit"), `[gpu][bands][adr1151]`.

## Context

The prototype has no lights. Its only light is four "bands" in direction space (`prototypes/astral-forge/
shaders/lighting.wgsl` `envF`, `surface.wgsl` `env`): strips and rings, some cut into dashes that turn with a rig
phase, plus a broad, dim soft-box sweep. They are seen only in reflections; the background stays black. That is
what makes black chrome read as metal and is a large part of the look. Production could approximate them with
tube lights (iteration 2's example did), but a tube light is a punctual light: it lights the diffuse term, it
has a position (so its reflection moves with the object, not with the view), and its highlight is a GGX lobe,
not a band seen across a whole rounded surface.

## Decision

**A scene's environment may carry `bands`:**

```json
"environment": { "bands": {
    "phase": 0.0, "rotation": 0.0, "gain": 1.0,
    "softbox": {"intensity": 0.55, "azimuth": 0.8, "elevation": 0.5, "falloff": 2.6, "skyFill": 0.08},
    "strips": [ {"axis": [0, 1, 0], "offset": 0.62, "width": 0.02, "intensity": 9,
                 "segments": 0, "warmth": 0.4, "rate": 0.12}, ... at most 4 ] } }
```

- A strip is the directions with `dot(dir, axis) = offset` (a great circle at 0, a ring otherwise), a Gaussian
  of angular width `width` across that line, convolved with the reflecting lobe: `sigma^2 = width^2 + alpha^2`
  with the peak scaled by `width / sigma`, so a rough surface sees a wider, dimmer band of the same energy.
- `segments` > 0 cuts it into dashes (72% duty, soft edges `0.04 + alpha`) that turn by `rate` turns per unit of
  `phase`. `rotation` turns the whole rig about world +Y. `warmth` mixes the prototype's cool (0.80, 0.90, 1.08)
  and warm (1.08, 0.93, 0.78).
- The soft box is `intensity * exp((dot(dir, key) - 1) * falloff) + skyFill * smoothstep(-0.3, 1, dir.y)`, the
  key at (`azimuth`, `elevation`).
- All of it times `gain`.

These are the prototype's formulas with its constants; its four bands, phases and soft box can be authored
exactly (the comparison scenes in `examples/astral-forge/compare-*.scene.json` do). The prototype's band axes
that wobble with the phase are fixed axes here (a route can sweep `rotation` and `phase`).

**Where it is evaluated.** In `shadeSurface`, after the IBL, gated on `frame.bandsSoft2.w` (a uniform): the
reflection (bent toward the anisotropic stretch exactly as ADR-1143 bends the IBL's) is looked up with the
lobe width `alpha = roughness^2`, multiplied by Karis's analytic split-sum scale and bias on f0 (no LUT: the
bands are analytic) and by the IBL's specular occlusion, and added to the ambient specular. So it reaches
every lit surface -- entities, procedural instances, SDF objects -- with ADR-1143's thin film in f0. It is never
drawn behind the world and lights no diffuse term. The tier-2 (flat) path skips it, as it skips the IBL.

**No map, bands present: the hemisphere goes.** A scene with no environment map gets a constant hemisphere
(sky 0.10, 0.12, 0.20) in the ambient. That is what turned iteration 2's black chrome purple. With bands
authored and no map, the ambient is zero: the bands and the scene's own lights are the light. Scenes without
bands are unchanged.

**Parameters**, registered only when the block is authored (so a scene without it lists exactly what it did):
`scene/bands/{phase, rotation, gain}`, `scene/bands/softbox/{intensity, azimuth, elevation}` and
`scene/bands/<k>/{intensity, offset, width, warmth}`. All are uniforms (no rebuild), so a route, MIDI or the
timeline can rotate the rig and sweep the dashes. The save writes the block back with the parameters' base
values; a scene without bands writes no key.

**The frame block** grows by twelve vec4s, appended last (`bandsInfo`, `bandsSoft`, `bandsSoft2`, `bandsRate`,
`bands[8]`), mirrored in `common.wgsl`; the static_assert's sum and the layout guard check both sides. All zero
without the block. The particle pipelines bind no frame block, so a flake system (ADR-1153) gets the same lanes
copied into its own uniforms, as the wind is (ADR-370).

**The shader function** (`reflection_bands.wgsl`) reads the lanes through five accessor functions each includer
defines (pbr_shade from the frame block, particles from its uniforms). `scene::reflectionBandRadiance` is its CPU
twin, line for line.

## Consequences

- Without the block the lanes are zero and the gate is not taken: the seven reference scenes render with
  **0 differing channels** against the pre-change binary (ADR-1150's list), including the entity path
  (`hero.json`, IBL) and the procedural path (`fungi.json`, `organic.json`).
- A black-background scene can be lit by reflections alone. The GPU test's mirror sphere shows the strips and
  dashes, the frame's corners are exactly 0, at `gain` 0 not one pixel is lit, and a phase change moves the
  dashes.
- Cost: an evaluation of up to four Gaussians and the soft box per fragment (five with an engraving's smear,
  ADR-1152); see ADR-1153's table.
- The CPU path tracer does not trace bands. `buildSnapshot` reports a `reflection bands` capability note
  (Unsupported, with the strip count), so a trace says why a band-lit surface is dark.

## Rejected alternatives

- **Tube or rect lights** (iteration 2's approximation). They light the diffuse term, have a position, and give
  a GGX highlight, not a band across the form.
- **Baking the bands into a cube map and using the IBL.** It would follow the existing path exactly, but a
  route that turns the rig would re-prefilter the cube every frame, and a 0.012-wide strip needs a cube far
  larger than the IBL's to stay crisp.
- **A new light type.** A light is evaluated per light per fragment with shadows and a cluster entry; the
  bands are a property of the surroundings, like the IBL.

## Revisit triggers

- The path tracer should match: add a band term to its environment and remove the note.
- More than four strips are wanted (the lanes are sized for the prototype's four).
- A band should be seen directly (a visible light panel): today it never is.
