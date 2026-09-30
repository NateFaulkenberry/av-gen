# ADR-1022: The procedural sky rebuilds when it moves past a tolerance, not on every bit

**Status:** Accepted (Sonic Garden POC, engineering follow-up to art pass 1).
**Date:** 2026-09-30

## Context

ADR-036 rebuilds the procedural sky's lighting cube (source cube, irradiance, GGX prefilter; at the offline tier
a 1024 face, ADR-919) whenever `SkyRuntime::hash()` changes. The hash is over the float bits. The Sonic Garden's
world palettes drive `env/sky/zenithColor`, `horizonColor` and `haze` through slow route chains that approach
their targets asymptotically, so the sky moved by parts per million on nearly every frame and rebuilt on nearly
every frame: 301 builds in 300 frames of the pad, at about 47 ms each as logged (including the chain's queue
waits). The art agent's A/B put it at about 20 ms of a 128 ms 1080p frame.

ADR-233 already defers rebuilds during an editor drag; offline and in playback it does nothing.

## Decision

`scene::skyWithinRebuildTolerance(built, current)` compares the sky the cube was built from with this frame's:
each colour within 1/128 of its own largest channel, each scalar within 1/128 relative, the sun direction within
0.25 mrad. `SceneRenderer::updateEnvironment` keeps the cube when the sizes are unchanged and the sky is within
tolerance. The comparison is against the sky that was **built**, so a slow drift still rebuilds once it has added
up, and nothing lags by more than the tolerance.

The alternative, amortising the prefilter over frames, would make the reflections lag by whole frames on every
change and needs the cube kept across builds; the tolerance is a few lines and bounded in error.

## Consequences

Measured against a rebuild on every frame, head shaders, `--headless` at 30 fps:

| clip | frames | builds before | after | render time before | after | frames differing | worst pixel |
|---|---|---|---|---|---|---|---|
| Sonic Garden pad, 0-10 s, 960x540 | 300 | 301 | 76 | 17.8 s | 14.3 s | 254 | 1 (8-bit) |
| Sonic Garden morph, 8-24 s, 640x360 | 480 | 481 | 170 | 20.3 s | 15.6 s | 478 | 1 |
| night-shift (sky on a sequence), 0-20 s | 600 | 2 | 2 | 23.0 s | 24.9 s | 0 | 0 |

Mean absolute difference 0.02 and 0.01 per channel. 1/512 halved the pad's builds only; 1/64 was also within one
level but changed twice as many pixels; 1/32 reached two levels.

- The cube is now a function of the sky's history within the tolerance: a seek that lands on a frame builds the
  exact sky, while playback may arrive with one built up to 1/128 earlier. The difference is at most one level in
  8 bits on the material measured.
- `tests/unit/test_sky.cpp` (`[adr1022]`) covers the tolerance; `tests/rendering/test_sky_gpu.cpp`, "a sky drifting
  inside the rebuild tolerance is not rebuilt", covers the renderer through `environmentBuildCount()`.
