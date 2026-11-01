# ADR-1932: The render job pre-rolls the temporal history

- Status: Accepted (CHRONOS Gate 1, 2026-10-31), scene/chronos.
- Completes ADR-410 §3 ("Offline render (§47)"), which specified this pre-roll; it was never built.
- Code:
  - `RenderJob::temporalPreRoll` and `RenderJob::renderDiscarded` in `src/app/render_job.{hpp,cpp}`;
  - `start()` positions the clock through them;
  - the job sets `SceneRenderer::setTimelineFrameRate` (ADR-1931).
- Tests: `[render][temporal][adr1932]` in `tests/rendering/test_temporal_preroll_gpu.cpp`. It checks that a render of
  a range starting mid-piece draws its first frame the way a render from 0 draws that frame.

## Context

`RenderJob` walks frames strictly forward from `startSeconds` on a freshly constructed renderer. Its only warm-up is
the pipeline warm-up, on a throwaway renderer, for a 1-LSB Metal effect. So the first frame of `--range 60:90` was
drawn with an empty ring:

- an echo with no echo;
- a slit scan with no past.

A full render, at the same second, had the whole history. ADR-410 named the fix (a K-frame pre-roll on the real
renderer) and the gap was then forgotten. With CHRONOS's ring 128 frames or more deep, the difference is the picture.

## Decision

Before the range, the job:

1. **Reads the bound.** It updates the engine once at `startSeconds` and reads the temporal settings there: the
   `ringLayers() × captureStride()` that the settings at the range's start ask for.
2. **Clamps it** to the frames that exist before the start.
3. **Rewinds** the clock and the engine by that many frames.
4. **Renders those frames** on the real renderer, submitting and reading nothing back.

The clock then arrives at `startSeconds` one step later, exactly as a plain start would, with the ring full. A range
starting at 0, or a scene with no temporal effect, pre-rolls nothing and is unchanged.

## Consequences

- A render of a range costs K extra frames (128 at CHRONOS's settings: about 4 s at offline speed).
- The bound is the one at the range's start. A piece whose temporal settings grow later in the range (a timeline that
  enables a deeper ring at 90 s) refills that ring as a play-through does, frame by frame. The range render agrees
  with the full render once the ring has played through.
- **The pre-roll frames are real engine frames.** Particle warm-up, exposure metering and anything else stateful now
  begin K frames earlier in a range render. That is closer to a play-through, not further from it.
