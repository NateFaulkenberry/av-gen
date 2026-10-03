# ADR-1085: The live controller measures each step on the way down, climbs only after a sustained fit, and reads min(GPU span, interval)

**Status:** Accepted (live quality, `docs/live-quality/00-brief.md` §13-14).
**Date:** 2026-10-03

## Context

The existing control law does not oscillate (measured: Liminal from 40 s over 2,400 frames held 0.5 with no hunting).
It decides on the median of the last 20 GPU samples, waits 30 frames between decisions, drops up to two rungs at once
on a pessimistic pixel-ratio prediction, and climbs one rung when the higher rung's predicted cost fits 80% of the
budget. It holds when the GPU is under 70% of the wall clock (a CPU-bound frame).

Its climb prediction is the pixel ratio. A level that only switches effects back on (Effects-first's High is Ultra's
scale) has a pixel ratio of 1, so the climb would be predicted free, taken, found over budget, dropped, and taken
again: a slow oscillation the old one-lever controller could not have.

The live sanity run on Liminal (2026-10-03) found a second problem: at Low the GPU timestamp span had a median of
11-12 ms while frames arrived every 9.0 ms. Consecutive frames overlap on the GPU, so the span over-states a frame's
cost (the investigation's C.6), and a 20-frame median above 14.67 ms walked the ladder to Emergency on a scene running
at 110 fps.

## Decision

All of the law above is kept. Three changes:

1. **Measured step ratios.** When the controller moves from rung a to rung b, it records the median at a, and once
   b's window has refilled (only b's frames) it stores `cost(higher) / cost(lower)` for each step crossed, clamped to
   [1, 4] (a 2-rung move is split evenly in log space). A climb from rung k is predicted as `median * ratio(k)`,
   measured if available. Unmeasured: the pixel ratio times 1.25 per effect lever the climb turns back on
   (conservative, as the pixel ratio already is). A drop uses a measured ratio when there is one, else the pixel ratio
   alone, and an unmeasured effects-only step is taken alone rather than skipped (it has no prediction at all).
2. **Sustained recovery.** The higher rung must be predicted to fit `budget * 0.8` for `raiseHoldFrames` (120)
   consecutive frames after the dwell. Any frame that does not resets the count. Degradation keeps the 30-frame dwell:
   quick down, slow up.
3. **The sample is `min(GPU span, frame interval)`.** A frame that arrives within its interval cannot be costing the
   GPU more than that. GPU-bound frames are unchanged (span and interval agree); only the overlap is removed. The
   Live panel's GPU figure is the same reading.

Learned ratios are forgotten on a strategy change (a new ladder) and on a floor change (new scales); a target change
keeps them (they are cost ratios, not budgets).

## Alternatives considered

- **A hysteresis band in milliseconds** (drop above budget, raise below 70%). Does not fix the effects-only climb: the
  question is what the higher rung costs, not how far under the budget this one is.
- **A raise backoff** (double the hold after a raise that bounced). Treats the symptom; the measured ratio removes the
  cause after one bounce at most, and in practice none, since every rung above was left on the way down.
- **Read the frame interval only.** Vsync-quantised (8.33 / 16.67 / 25 ms at 120 Hz): it cannot tell 9 ms of GPU from
  15 ms, so it could not choose a rung.

## Consequences

- Unit tests close the loop on cost models shaped like the three measured scenes: each settles inside its budget (or
  at Emergency when nothing fits) with at most six decisions in 4,000 frames, and an effects-only rung is held without
  a single raise (`tests/unit/test_interactive_resolution.cpp`).
- A scene that gets genuinely cheaper climbs back one level per ~2 s at 60 fps.

## Revisit triggers

- Content that changes cost fast enough that a measured ratio is stale by the time it is used.
