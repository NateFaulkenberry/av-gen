# ADR-1613: The upscaler's sharpening is a post parameter

- Status: Accepted (eng/latency-levers; asked for by The Living Prism's scene work)
- Amends ADR-1607: `upscaleSharpness` moves from `QualitySettings` to `PostSettings` (no alias, ADR-441). Same
  default (0.2), so every scene renders as before.
- Code: `src/scene/post_settings.{hpp,cpp}` (`PostSettings::upscaleSharpness`, the parameter
  `post/output/upscale-sharpness` 0..1, the `post` block key `upscaleSharpness`), `src/rendering/render_quality.hpp`
  (the field removed), `src/rendering/scene_renderer.cpp` (the tonemap reads `scene.post.upscaleSharpness`).
- Tests: `tests/unit/test_post_settings.cpp` (`[adr1613]`: the parameter exists at 0.2, a scene's post block and a
  project's parameter both reach the settings the renderer reads); `tests/rendering/test_upscaler_gpu.cpp`
  (`[adr1607]`, unchanged, passes).

## Problem

ADR-1607 put the sharpening of the spatial upscaler in `QualitySettings`, which no project, scene or live override
reaches: a scene whose reduced rungs looked over- or under-sharpened could not say so.

## Decision

It is the scene's look, so it is a post parameter beside `post/output/sharpen`: `post/output/upscale-sharpness`
(0..1, 0.2), settable from a project's `parameters`, a scene's `post` block (`upscaleSharpness`), a preset, a route or
a MIDI binding like any other. It is inert at `renderScale` 1 (the fetch is bilinear, byte-identical) and acts only on
the live ladder's reduced rungs. Whether the upscaler runs at all stays a quality lever (`spatialUpscaler`, the
`bilinearupscale` arm). A `live.overrides` key was not needed: a parameter already reaches live and offline alike.
