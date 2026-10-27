# ADR-1805: In the live editor, pipelines first needed mid-performance compile in the background

- Status: Accepted (eng/qa-pass-2, 2026-10-27)
- Code: `src/gpu/pipeline_compile.{hpp,cpp}` (`startBackgroundRenderPipeline`), `src/rendering/procedural_renderer.cpp`
  (`ensureUnlitPipelines`, `ensureRefractPipelines`, `refractionReady`), `src/rendering/scene_renderer.cpp`
  (`setInteractivePipelineDeferral`, the refraction phase), `src/app/application.cpp` and `live_profile_run.cpp`.

## Problem

Two procedural pipelines are built the first time a frame needs them: the unlit fast path (ADR-1673) and the
refraction pair plus its depth lay-down (ADR-1603). They were created synchronously, on the main thread, inside that
frame. On a cold Metal shader cache that is the whole Metal compile of the procedural uber-shader: in an
`--open-sequence` run (Hyperspace, then Neon Vector World) the first switch held the main thread for **17.0 s**
(`pipeline-timing render 'procedural-refract' 17023.8 ms`; no frame was drawn in between). The label was wrong too:
every pipeline with a custom fragment entry was called `procedural-refract`, so the stall was the unlit fast path's,
not refraction's. A live set that switches to a scene with an unlit surface or glass for the first time on a machine
froze for a quarter of a minute.

## Decision

- `gpu::startBackgroundRenderPipeline` compiles a pipeline on Dawn's workers with nobody waiting; its result lands in
  shared state the frame loop's event processing fills in.
- `SceneRenderer::setInteractivePipelineDeferral(true)` (the live editor and the live profiler, beside ADR-233's
  interactive environment budget) makes the procedural renderer start those pipelines in the background the first
  time a frame wants them. Until they land, the surfaces draw their defined fallbacks: unlit surfaces take the full
  lit path (ADR-1673: the same colour within one level), and the refraction phase stays off, so refractive surfaces
  draw as their opaque material (the ladder's Low look, ADR-1603).
- Off, which is the default and every offline render, test and headless capture, they are created where first
  needed, exactly as before.
- The pipelines are labelled by what they are (`procedural-unlit`, `procedural-unlit-twosided`).

## Consequences

- Measured after the change (warm cache): the unlit pair lands 170 ms after the switch, in the background
  (`pipeline-timing render (background) 'procedural-unlit' 170.2 ms`), and the switch's 90 frames take 3.2 s as the
  other switches do. A cold-cache run was not repeated: it would have cleared avgen's shader cache on the owner's
  machine (ADR-1560), making their next launch cold.
- On a cold cache the first seconds after a switch show the fallback look; nothing freezes.
- Material-program variants (ADR-1493) already compiled in the background and are unchanged.
