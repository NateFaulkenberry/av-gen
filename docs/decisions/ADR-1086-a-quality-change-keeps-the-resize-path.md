# ADR-1086: A live quality change keeps the existing resize path; measured, it costs no more than ordinary frame jitter

**Status:** Accepted (live quality, `docs/live-quality/00-brief.md` §10-11).
**Date:** 2026-10-03

## Context

The projection investigation reported "a measured ~195 ms 1% low during a Sonic rung transition" and recommended
allocating the targets once at full size and rendering into a sub-rectangle. The brief asks for that to be
investigated before it is built.

**What the 195 ms was.** In `29-live-projection-perf/data/sonic-1080-on.cpu.csv` the rung change is at frame 36-37:
that frame took 17.07 ms against a 16.5 ms median, with `render.record` up 0.28 ms. The 1% low came from frame 204, a
single **1,032 ms main-window `gpu.acquire WAIT`**, 167 frames later. That is the main swapchain's drawable wait (on
macOS, `CAMetalLayer.nextDrawable` times out at one second), not an allocation. The other adaptive runs in that
dataset (changes at frames 38, 68 and 98) show nothing at the changes either.

**What a change touches** (read from the code):
- `renderScale`: `SceneRenderer::resize` recreates the HDR target and the auxiliary targets (normal, material,
  velocity, emission, ids, AO, shadow mask...), rebuilds the frame bind groups (rebuilt every frame anyway), drops the
  tone-map bind group, and calls `resetScreenHistory()`: the previous view-projection and per-entity model matrices
  (motion vectors, motion blur), AO's temporal history, and the temporal-media ring (ADR-410). Post targets come from
  the transient pool by size. Exposure is not reset: it meters luminance, which does not depend on resolution.
- `volumeResolutionScale`: the volume renderer reallocates its one half-resolution target; it has no history.
- `shadowResolution`: the shadow atlas is reallocated; shadows have no history.
- The motion blur and DoF gates, step counts, cascades and filtering: no allocation, no history (velocity is written
  every frame whatever the gate, and the previous view-projection is advanced every frame).

**Measured with the ladder** (`AVGEN_X_LQ_SWEEP`, a temporary probe that walks the ladder every 120 frames, through
the 1920x1080 projection, 2026-10-03): 27 transitions over Sonic, Glowmere and Liminal, every level up and down.
- The CPU cost of applying a level is 0.0-0.42 ms (`applied in` in the log).
- The worst frame among the three after each change exceeded the preceding 100 frames' own maximum by at most
  **+0.9 ms** (median -5.0 ms); 4 of 27 exceeded that segment's 95th percentile, by 1.5 ms at most.
- No transition produced a hitch of the size the brief describes.

## Decision

1. **Keep the resize path.** No full-size allocation, no sub-rectangle rendering: it would change the viewport and the
   UV range of every screen-space pass (post, volumes, AO, SDF, the tone-map's upscale), which is a renderer-wide
   change to fix a cost that does not measure.
2. **History is invalidated where it is today, intentionally:** a scale change goes through `resize`, which resets the
   screen-space history; an effect change resets nothing, because nothing it touches has history.
3. **The profile column `# live quality changes` counts the changes a frame rendered with**, so a hitch can always be
   looked for on the right frame.

## Alternatives considered

- **Full-size targets and a sub-rectangle.** Above. Revisit if a change ever measures as a hitch.
- **Keep a target per visited scale.** Memory for every rung visited, to save a cost that is not there.

## Consequences

- `tests/rendering/test_gpu.cpp` `[live-quality]`: after Ultra -> Low -> Ultra the frame is identical to a renderer
  that was reset at the same point and never left Ultra; a renderer that kept its history draws a different frame.

## Revisit triggers

- A transition measured above the steady-state frame-time maximum, or on a GPU whose allocations are not lazy.
