# ADR-1066: Feedback and slit-scan are FIR over the clean ring; the temporal passes get their own GPU timers

- **Status:** Accepted (2026-10-02), proto/sonic-garden (VFX expansion, brief §7-8, §18; research 4 §2-3)
- **Code:**
  - `TemporalEffectKind::{Feedback, Slit}` and their settings in `src/scene/temporal_settings.*`;
  - `fs_feedback` and `fs_slit` in `shaders/temporal.wgsl`;
  - the passes in `TemporalEffects::run`;
  - the timer marks `temporal/{echo, feedback, slit, mosh, capture}`;
  - `SceneRenderer` now calls `temporal_->setTimeline`.
- **Tests:** `tests/rendering/test_temporal_gpu.cpp` (`[gpu][temporal][adr1066]`) and the conformance table.

## Context

Video feedback (MilkDrop's zoom-and-turn tunnels and blooms) is the brief's "feedback loops". A feedback buffer is an
IIR filter, which ADR-410 forbids: no warm-up can rebuild it, and two renders of a range would disagree with a play.

## Decision

1. **Feedback, unrolled:**
   `out = C + amount (1 - decay) sum_{k=1..K} decay^(k-1) hue^k(R_k(T^k uv))`.
   - R_k is the clean ring's frame k back.
   - T zooms, turns and drifts per frame about the centre in aspect-correct space, so a past frame is read where its
     content would have been carried to.
   - hue^k rotates in OKLab, which holds lightness.
   - Samples from outside the frame are black.
   - It is the first K terms of the loop, as an exact FIR. Parameters:
     `temporal/feedback/{enabled, frames, amount, decay, zoom, rotate, driftX, driftY, hue}`.
2. **Slit-scan:** each pixel shows the frame `d(uv) x frames` back, blended between the two nearest layers (0 is the
   current frame). `d` is by rows, columns, radial distance, or luminance. Parameters:
   `temporal/slit/{enabled, frames, amount, mode, reverse}`.
3. **Order:** echo, feedback, slit, mosh, capture. The mosh corrupts what they show; the capture stays clean.
   Enabled at amount 0, the ring is kept warm and nothing is encoded.
4. **Timers.** The temporal passes were never on the frame timeline (`setTimeline` was not called), so their cost was
   charged to `post/meter`. Each pass now marks its own label under `temporal/`.

## Consequences

- **Tested:** a renderer that starts 12 frames before frame 23 draws the same frame 23 as one that played from 0, for
  feedback and two slit modes. That is the FIR guarantee: a seek or an offline range is right once the ring has
  refilled (at most `frames`).
- **Measured** (1080p, M2 Max): feedback 0.66 ms at 8 taps (+0.9 ms frame p50); slit 0.066 ms. The ring costs about
  2 MB a frame at half resolution.
- At the realtime tier the ring is half resolution, so the feedback's tail is softer than the frame. It reads as
  depth, and is kept.
- `bench-json` labels change: the temporal passes appear as `temporal/*`, and `post/meter` loses their cost.
