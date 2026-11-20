# ADR-2108: The cloud resolve blends a tent-filtered frame; surfaces beyond `fogSkyBeyond` are sky

- Status: Accepted (2026-11-19), avagent/neon-dreams (Neon Dreams, iteration 7)
- Extends: ADR-2100 (cloud temporal resolve), ADR-705 / ADR-918 (surface fog, fog from the sky).
- Code: `shaders/clouds_resolve.wgsl` (`fs_cloud_resolve`), `shaders/common.wgsl` (`applyFog`),
  `src/scene/scene_types.hpp` (`Environment::fogSkyBeyond`), `src/scene/composition.cpp` (read, write, apply),
  `src/rendering/scene_renderer.cpp` (`frame.fogSky.w`).
- Tests: `[adr2108]` in `avgen_render_tests` (`test_fog_sky_gpu.cpp`); `test_composition.cpp` reads
  `fogSkyBeyond`; `[clouds]` and `[fog]` in both suites.

## Context

1. **Stipple.** Neon Dreams showed a dotted screen-door pattern wherever soft cloud lies over lit cloud. The
   art agent saw it at 25 s, 44.5 s and 62 to 70 s. The march jitters each pixel's first step by interleaved gradient
   noise, which is a fixed spatial pattern. The resolve blends 15 % of the raw frame into the history every frame, so
   15 % of that pattern stays on screen wherever the jittered samples disagree.
2. **Air on the meshes.** The Condensery (1.4 km), the Highwater settlement (2.2 km) and the Long Arc (22 km) drew
   at full contrast beside cloud that fades with its own `hazeDistance`. The scene's own air (ADR-705
   `volumeDensity`) gives aerial perspective. But the Neon Dreams moon is a mesh 60 km out, and air thin enough to put
   the Condensery a fifth of the way into the haze fogged the moon out completely.

## Decision

1. The resolve blends a 3x3 tent average of this frame (centre 4, edges 2, corners 1) instead of the raw pixel. Only
   neighbours cut against the same scene depth (within 8 %) are averaged, so a tower's edge is not mixed with the sky
   beside it. Interleaved gradient noise is evenly spread over any 3x3 window, so the pattern averages out. The clamp
   box and the history are unchanged.
2. `environment.fogSkyBeyond` (metres, 0 = off): a surface farther away than this takes no surface fog, as the sky
   takes none. A moon or planet drawn as a mesh beyond the world reads as part of the sky.

## Consequences

- (1) applies to every cloud scene that has history. It takes out the stipple's high frequencies: the mean absolute
  difference from a 3x3 box blur, over whole Neon Dreams 1080p frames, falls 0.856 -> 0.744 at 64 s, 0.937 -> 0.921
  at 44.5 s and 2.80 -> 2.76 at 25 s. A crop at 44.5 s shows the grid gone. Spatially, the frame's contribution is a
  little softer, which a half-resolution cloud does not show.
- (2) is off by default and changes nothing until a scene sets it.
