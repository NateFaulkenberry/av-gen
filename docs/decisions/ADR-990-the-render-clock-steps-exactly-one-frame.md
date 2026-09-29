# ADR-990: The render's clock steps exactly one frame

**Status:** Accepted. The owner chose this directly on 2026-09-29 ("just go with option A"), over keeping the film as
rendered and changing the tools to match it.
**Date:** 2026-09-29
**Found by:** the GV3 art pass, revision round 1. Shot s50 framed an empty field, because Ember was 80 m from where
every trace put her.
**Implemented by:** `FixedStepClock::tick` in `src/core/time.cpp`
**Tests:** `[adr990]`, in two places:
- `tests/unit/test_fixed_step_clock.cpp` checks that every tick's delta is exactly `1.0 / fps` for 20,000 frames at
  five frame rates and three start times. Its control confirms that the old formula differs from that.
- `tests/integration/test_engine_seek_parity.cpp` ("an Engine played by the render's clock lands where a scrub
  does") plays GV2 multicam through the render's own clock and compares a scrub at 1, 10 and 30 s.

## Context

`FixedStepClock` drives every offline path: `--render`, `RenderJob`, `FrameRange` and the headless benchmark. It
handed out `deltaTime = next - renderTime`, the difference of two instants `start + f / fps`. In floating point that
is `1/fps` give or take the last bits, and it differs from frame to frame.

Everything that is meant to reproduce a render steps exactly `1/fps`:
- ADR-700's seek replay;
- `avgen_cast_trace` and `avgen_foot_probe`;
- the test harness (`frameAt` in the parity tests).

The autonomous cast is chaotic, so it grows that difference. On Glowmere Valley 3, a body moved 0.7 mm at 6.28 s
with only the delta changed, then up to 114 m by 168 s. So a full render from zero was a different film from every
seek, range render and trace of it. Shots framed from traces (framing.py, the generator's placements) aimed at
where a character was not; s50 looked at an empty dark field.

The parity tests never saw this, because their "play" was the harness's hand-built `1/60` step rather than the
render's clock. That is one more case of a test sharing the product's blind spot.

## Decision

`FixedStepClock::tick` hands out `deltaTime = 1.0 / fps`, exactly. `renderTime` is still `start + frameIndex / fps`
(ADR-012), so a frame's time on the timeline is unchanged. Only the step changes.

## Alternatives

- **B. Make the replay and the tools compute the clock's delta.** This keeps the rendered film as it was, but
  enshrines a step that wobbles by construction, and every constant-delta test would have to change. The owner
  chose A.
- **C. Change nothing, and frame shots against the film as rendered.** This was the art pass's interim plan. It
  leaves renders, seeks and traces disagreeing, which is the trap this ADR closes.

## Consequences

- **A render from zero now matches a seek and a trace of the same instant.** Measured on GV2 multicam: with the old
  clock the parity case fails, with Ember 0.053 m off at 10 s and Vane 9.10 m off at 30 s. With the new clock,
  every body is exactly equal at 1, 10 and 30 s.
- **The rendered film changes.** Every autonomous path from about 6 s on becomes the trace's path, which is the one
  the cut was designed on. The owner waived before-and-after comparisons.
- **What is not changed:**
  - The live transport still integrates wall-clock or audio-position time. It is not reproducible by design
    (ADR-186).
  - `MusicalEventDetector` derives its step from analysis-frame instants; the replay feeds it the same frames, so it
    doesn't cause a render/seek difference.
  - `signals/source.cpp` reads its window start as `renderTime - deltaTime`. The particle spawn count and the
    procedural `prevInfo` take differences of instants; both are render-only.
- **GV2's behaviour fingerprint is unchanged.** It is traced with a constant `1/60`.
- **Open, not explained by this ADR:** on 2026-09-29 one full GV3 render diverged from a 0-8.25 s render of the same
  project and binary from 8.017 s. Two 0-12 s renders were identical to each other. The art pass is re-checking
  this with the new clock.
