# ADR-1089: A slow output is paced, not waited for

**Status:** Accepted (live quality, `docs/live-quality/00-brief.md` §17).
**Date:** 2026-10-03

## Context

`OutputManager::presentAll` gives every open output (the Live projection, a project's outputs) its own Fifo swapchain
acquire, mapper draw, submit and present, on the main thread, after the main window's. WebGPU (Dawn) has no
non-blocking acquire: `Surface::GetCurrentTexture` waits for a drawable, and on macOS `CAMetalLayer.nextDrawable` can
wait up to a second. Measured by the projection investigation: a covered 960x540 projection window's acquire blocked
**14.8 ms per frame** and capped the editor and the performance at 60 fps. Whatever the cause -- a projector that is
slow, mis-clocked or asleep, a window macOS throttles -- the whole show waits for the slowest output.

## Decision

`app::PresentPacer`, one per `Output` (`src/app/output_manager.hpp`), consulted in `presentAll`:
1. **Every acquire is timed.** One slower than `kSlowAcquireMs` (4 ms; a healthy second surface acquires in well under
   0.1 ms when the loop is GPU-bound, and about 1.3 ms when the loop runs past 100 fps) doubles the output's
   presentation interval: every 2nd, 4th, then **at most every 8th** frame. On the frames in between the output is
   not acquired, drawn or presented; its window keeps showing its last frame.
2. **`kRecoverAfter` (8) fast acquires in a row halve the interval again**, back down to every frame.
3. A healthy output never leaves interval 1, so this changes nothing on a working projector.
4. **Plain words when it happens:** the Live panel's projection status says the window is slow to take frames, how
   long the last acquire took, and that it shows every Nth frame so the show keeps its pace.
5. The main window's acquire is unchanged: it is the loop's own vsync pacing.

## Verified in the real loop (2026-10-03)

The investigation's stall did not reproduce on 2026-10-03 (the same small covered window's acquire took about 1.3 ms
with or without this change), so a temporary probe (commit 2f28e016, reverted in 989fe61f) made the projection's
acquire block 15 ms, inside the timed region. Sonic Live projecting, live quality automatic at a 120 fps target, two
runs per arm, 480 analysed frames each:

| arm | fps | median interval | 1% low | output wait mean | frames paying a block |
|---|---|---|---|---|---|
| healthy projection | 87.8 / 88.6 | 8.7 / 8.9 ms | 28 / 25 ms | 1.1 / 1.0 ms | 0 |
| 15 ms block, no pacing (the old behaviour) | 51.6 / 54.5 | 19.1 / 18.5 ms | 23 / 22 ms | 16.8 / 16.6 ms | 480 of 480 |
| 15 ms block, paced | 85.1 / 85.2 | 8.2 / 8.9 ms | 33 / 36 ms | 2.1 / 2.1 ms | 60 of 480 |

The cost of pacing is visible in the 1% low (one frame in eight still pays the block) and in the projection showing
every 8th frame while its display is not keeping up.

## Alternatives considered

- **Immediate present mode for outputs.** No acquire wait, but tearing on a projector, which is worse than a held frame.
- **Present outputs on another thread.** The device and queue are used from the main thread throughout; a second
  thread is a redesign of the output subsystem, which the brief rules out.
- **Skip occluded windows.** Covers the covered-window case only, and a real projector is never occluded.

## Consequences

- `tests/unit/test_projection.cpp`: a healthy output is never paced; a 14.8 ms output pays at most one blocked acquire
  in eight frames; it recovers; one slow acquire costs one step.
- A one-second `nextDrawable` timeout on an output still stalls the frame it happens on; after that the output is
  paced, so it can happen at most once every 8 frames rather than every frame.

## Revisit triggers

- A projector that is slow by less than 4 ms per frame but enough to matter at 120 fps.
