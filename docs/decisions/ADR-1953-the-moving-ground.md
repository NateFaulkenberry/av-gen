# ADR-1953: The moving ground, as a pure function of time

- Status: Accepted (Musical Geography Phase 3, 2026-11-09), scene/musical-geography.
- Code:
  - `scene::TileDynamics`, in `src/scene/tile_dynamics.hpp`;
  - the rule (`tileSample` with t, `tileMotion`, `tileSampleMoving`), its validation and JSON, in
    `src/scene/tile_terrain.{hpp,cpp}`;
  - the generator mirror (`generatorTile*` with dyn and t), in `src/scene/generator.cpp`;
  - the GPU twin (`tileSample`, `tileMotion`, `cs_generate_tiles`), in `shaders/generator.wgsl`;
  - the packing (`GeneratorTileUniforms`, 944 B), in `src/rendering/procedural_renderer.*`;
  - the parameters and the live rings (`env/tiles/...`, `updateTileDynamics`), in `src/scene/composition.*`.

## Context

The brief's landscape moves to the music: wave trains that interfere, radial disturbances, breathing, rolling
formations, and amplitudes the audio drives. The plan (03- §4.3) asks for each behaviour as a term in `H = base + dyn`,
evaluated where the tiles are generated, and as a function of `t` so that seeking equals playing (ADR-091).

## Decision

**One block, `environment.tileDynamics`, followed by every "tiles" generator in the scene.** Each ring and the underlay
move with the same ground, so the rings never disagree.

The terms:
- **Waves:** up to 4 trains, planar (by heading) or circular (from a centre). Each has a wavelength, a speed, an
  amplitude, a crest sharpness and land and water weights. The trains interfere by summing.
- **Breathing:** the hills' relief × (1 + A · sin 2π(t/period + n)), where n is a slow regional noise.
- **Rolling:** the hills' domain advected at a velocity.
- **Rings:** a front from an event's point at `speed`, carrying a ripple of `wavelength` inside a Gaussian of `width`
  and fading over `decay`. Events are authored with a time, or triggered live.
- **Crest:** the normalised crest of the trains and rings, written into the record's emissive alpha.

Every fine sample is the base plus the waves and rings at that point, faded for the fine pitch. A coarse ring's cell
is the mean of its fine samples, as at rest, and so is the seam band's block. The rings therefore agree in motion
exactly as they do in height. An earlier take added the motion once per coarse cell, and it opened cracks at the ring
seam. A term shorter than two to four fine pitches fades out.

**An underlay ring (`"childrenMin": true` on its generator)** stands at its lowest fine sample instead of the mean.
Under a moving fine ring an averaged underlay rose into the troughs and showed through as dark specks. A canaried
green underlay went from 3,934 pixels showing to 449, and those 449 are the end-on tile gaps by the camera, which are
static.

**Audio** goes through the existing modulation: every knob is a parameter under `env/tiles/`, so a route, a stem or a
timeline key drives it. A rising edge of `env/tiles/rings/trigger` through 0.5 drops a ring, either at `rings/x, z` or
at `rings/ahead` metres ahead of the camera, stamped with the time.

**Seeking:** a seek backwards drops the live rings that had not yet happened. Authored events are part of the scene.

**A moving sea gets a skirt.** It opens steps between its slabs that a static sea never has, so it gets as much skirt
as its motion can step, never more than the land's.

**Inactive (the default), nothing changes:** the static ground is bit-identical, which a CPU test asserts.

## Consequences

- **Tests:**
  - CPU: bit-identity when inactive; continuity in t; additivity; ring decay; seek equals play; rolling; the alias
    fade; the crest range; no NaN at the extremes; JSON; parameters and live rings.
  - GPU: parity of the moving ground, fine and coarse, within 2e-3.
- A ring triggered live is reproducible offline, because the render plays forward. A scrub reproduces it only if the
  route that fired it is itself a function of time, which an audio onset is.
