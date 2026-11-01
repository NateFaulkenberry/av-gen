# ADR-1931: The temporal ring has a capacity, a stride and per-layer metadata; the slit scan becomes the sampler

- Status: Accepted (CHRONOS Gate 1, 2026-10-31), scene/chronos.
- Amends:
  - ADR-410: the 32-frame ceiling on the ring;
  - ADR-1066: the slit scan's parameters.
- Code:
  - `src/scene/temporal_settings.{hpp,cpp}`:
    - `TemporalHistorySettings` (`temporal/history/{capacity, stride}`);
    - `SlitSettings`, rewritten (`temporal/slit/{span, angle, strips, edge, blend, scan, scanSpeed}` beside `enabled`,
      `amount`, `mode`, `reverse`);
    - `ringLayers()`, `captureStride()`;
    - the constants `kMaxTemporalFrames` (256 layers), `kMaxTemporalTaps` (32), `kMaxTemporalStride` (8) and
      `kMaxTemporalHistoryFrames`.
  - `src/rendering/temporal_history.{hpp,cpp}`:
    - `TemporalHistoryConfig::stride`;
    - `beginFrame(frameIndex, renderTime, captureFrame)`;
    - `capturesThisFrame`, `newestAge`, `stride`;
    - `LayerInfo` per layer;
    - `allocations()`.
  - `src/rendering/temporal_effects.{hpp,cpp}`: the ring sized by `ringLayers()`, `fs_sample` in place of `fs_slit`,
    `TemporalFrameInputs::captureFrame`, and new `TemporalStats` fields.
  - `shaders/temporal.wgsl`: the uniforms gain `more`; `fs_sample`, `sampleAge`, `sampleLayer`.
  - `src/rendering/scene_renderer.{hpp,cpp}`:
    - `setTimelineFrameRate`;
    - the debug-geometry pass moves after the temporal stage.
- Tests:
  - `tests/rendering/test_temporal_sampler_gpu.cpp` (`[gpu][temporal][adr1931]`).
  - `[temporal][adr1931]` in `tests/unit/test_temporal_conformance.cpp`.
  - The ADR-1066 GPU case, updated to the new slit parameters.

## Context

CHRONOS (`docs/prototypes/chronos/02-recon.md` §3) found three problems in the ring ADR-410 built.

1. **The ring is too short.** It is capped at 32 frames, about a second at 30 fps. Slit scan needs seconds: with 32
   ages across 1,920 px, each frame step is 60 px wide and cross-faded, which reads as a soft lean, not as temporal
   structure. The Gate 0 proof clip A shows it.
2. **The depth reallocates under modulation.** The ring's depth was the largest of the live effects' `frames`. A
   route on `frames` reallocated the texture array whenever the value crossed an integer, and every reallocation
   dropped the history. That happens at exactly the musical moment the route exists for.
3. **Debug geometry entered the history.** The debug pass drew into the HDR target before the capture, so
   `--debug-draw` overlays were written into the ring and smeared through every temporal effect.

## Decision

1. **Capacity is not span.**
   - `temporal/history/capacity`, when above 0, is the ring's depth in layers (up to 256). It is fixed, and changing it
     resets the ring.
   - At 0, the ring sizes itself from the effects' bounds, as before.
   - The slit's `span` is a float in frames that may be routed every frame. A fractional age blends the two nearest
     layers.
   - The capacity is not a tier setting. ADR-410's rule holds: how far back an effect reaches is artistic. The tier
     scales only the ring's resolution.
2. **A capture stride.**
   - `temporal/history/stride` (1..8) captures every Nth frame, so 128 layers span 256 frames at stride 2.
   - The stride is anchored to the frame's number on the timeline (`round(t × fps)`, from
     `SceneRenderer::setTimelineFrameRate`, which the render job sets). It is not anchored to how many frames this
     renderer has drawn: a render of a range starts its count at 0, and would otherwise capture different frames from
     a full render of the same second.
   - Live, where nothing is reproducible anyway, the frame index is used.
   - `newestAge` (1..stride) says how old the newest stored layer is. The sampler blends the current frame into it
     over that interval.
3. **Per-layer metadata.**
   - Each layer records its frame number and render time (`LayerInfo`).
   - The sampler's ages need only `newestAge` and the stride. The record exists for depth-aware and reprojecting
     readers (CHRONOS brief §11). It is not bound to the GPU until one of them needs it.
4. **The ceilings separate.**
   - The ring holds 256 layers.
   - The effects that loop over taps (echo, mosh, feedback) keep ADR-410's 32, because their cost is one fetch per
     tap.
   - A declared bound may reach 256 × 8 frames. The conformance check uses that.
5. **The slit scan is a sampler.** Each pixel's age is a map `d(uv)` in 0..1:
   - along a direction (`angle`: 0 is a vertical slit scan with now at the left, 90 is horizontal with now at the
     top), radial, or luminance;
   - wrapped by `scan` + `scanSpeed × t` when scanning, so the seam between now and the oldest moment moves (the
     moving slit);
   - reversed by `reverse`;
   - quantised into `strips` with soft `edge`s.

   Age = q × span. The ring is read by age, and `blend` chooses linear or nearest between the stored frames. A span of
   any length costs two fetches. The pass is `temporal/sample` on the frame timeline. Displacement and fracture are
   to join this pass as further address terms (Gate 2), rather than becoming passes of their own.
6. **The debug pass draws after the capture,** into whatever image the temporal stage returned, against the scene's
   depth.

## Consequences

- **Memory.** 128 layers at half resolution (960×540, RG11B10Ufloat) is 265 MB. At full resolution (the Offline tier)
  it is 1.06 GB. The owner approved both at Gate 0. `bytes()` and `TemporalStats` report what is allocated.
- **Seek.** A 128-layer ring cannot be warmed up on seek settle (about 4 s of scene renders). It settles as it plays,
  and the existing badge says so. The offline render's pre-roll (ADR-1932) makes a render of a range exact.
- **No scene used the slit's old parameters** (`frames`, mode 0 rows / 1 columns / 2 radial / 3 luminance), so
  nothing migrates. The ADR-1066 GPU case uses the new ones.
- **Taps under a stride.** The echo, mosh and feedback taps read layers, so under a stride above 1 each tap is
  `stride` frames apart. That is documented, not hidden.
- **Debug overlays** are no longer bent by the screen-space distortion or post shader layers, which run before the
  temporal stage. They are diagnostics, so this is correct.
