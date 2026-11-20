# ADR-2103: The visible sky's intensity and tint as parameters, a cycle on the random source, traced track targets

- Status: Accepted (2026-11-19), avagent/neon-dreams (Neon Dreams, iteration 5: audio reactivity and the palette arc)
- Code: `src/scene/scene_types.hpp` (`Environment::skyTint`), `src/scene/composition.{hpp,cpp}` (`env/skyIntensity`,
  `env/skyTint`, the `skyTint` scene key), `src/rendering/scene_renderer.{hpp,cpp}` and `shaders/common.wgsl`
  (`FrameUniforms::skyTint`, before `portalClip`), `shaders/sky_background.wgsl` (`skyBackgroundAt` multiplies the sky
  by it), `src/signals/source.{hpp,cpp}` (`RandomSource` `cycle`), `src/app/sonic_trace_cli.cpp` (`--trace-finals`).
- Tests: `tests/unit/test_environment_sky.cpp` and `tests/unit/test_sources.cpp` (`[adr2103]`).

## Context

Neon Dreams' palette arc steps four colour states on the song's section crashes, each system at its own rate, and the
far field (the nebula) slowest. The nebula is an equirect map baked in one state; the visible sky's brightness
(`Environment::skyIntensity`) was a scene-file setting only, with no parameter, so neither a route (the pad breathing
the nebula, a crash lifting the horizon) nor a track (the hue drifting over 30 s) could reach the sky. Second, the
brief asks the snare to light two sign "businesses" in turn: a route can scale its depth by a signal, but no source
alternates per trigger. Third, the reactivity analysis samples every routed parameter with `--sonic-trace
--trace-finals`, which listed route targets only, so the palette arc's tracks could not be charted from the engine.

## Decision

1. `env/skyIntensity` (the visible sky's brightness, default the scene's `skyIntensity`) and `env/skyTint` (rgb,
   default white, scene key `environment.skyTint`, written only when not white) are registered parameters. The tint
   multiplies the visible sky in `skyBackgroundAt` after its intensity, so the background pass and the fog's view of
   the sky agree. Shading (`env/intensity`, the IBL) is unchanged. A per-channel tint of the Ultraviolet nebula
   reproduces the other states' nebula colours to within the art's tolerance (ratios of the palette's nebula colours).
2. `RandomSource` gains `cycle` (settings key, 2..64; 0 or absent = random as before): the k-th trigger outputs
   `(k mod cycle) / (cycle - 1)`. A cycle of 2 alternates 0, 1, 0, 1; two routes with opposite depth ranges on it make
   two groups answer an event in turn.
3. `--trace-finals` also writes every parameter a timeline track keys, except node transforms (walkers and vehicles
   key hundreds).

## Consequences

- A palette arc can move the nebula's hue and the audio can breathe the sky with ordinary routes and tracks.
- The cycle source is state counted from load like the random source (a seek restarts the count); offline renders
  play linearly, so the alternation is exact there.
- `FrameUniforms` grows by one vec4; the white default keeps every other scene's sky unchanged.
