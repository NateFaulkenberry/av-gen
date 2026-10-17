# ADR-1363: Paced, a frame stretched past the period while the GPU is past it too is the GPU's miss

**Status:** Accepted (amends ADR-1362).
**Date:** 2026-10-16

## Context

ADR-1362 decides the paced live level on missed deadlines. A frame counts as late when its interval exceeds the
period by a quarter period, and at least half a refresh (`missedDeadline`). That threshold keeps the cap's wake-up
jitter out: on a 120 Hz screen at 30 fps, frames of 37.5-39 ms with nothing lost were read as misses by a
half-refresh rule.

The Cymatic World light pass found the rule's blind spot. The data was a live profile on Auto, 30 fps target, 1080p,
Feline Footwear from 95 to 135 s, with a `--trace` covering 1126 frames:

- every GPU span was at or above the 33.3 ms period (median 34.8 ms, p95 36.0 ms) at High;
- every interval was a steady 34-38.6 ms, with none at the 41.7 ms threshold.

With a refresh grid this fine against the period (30 on 120 Hz), a level a few milliseconds over the frame never
lands a quarter period late. So no frame counted, the backoff never engaged, and Auto held High at about 28 fps
with "0 misses". CPU work was about 4.4 ms; the main thread spent about 33.8 ms waiting on synchronization.

## Decision

A paced frame also counts as missed, and as the GPU's, when all three hold:

1. its interval exceeds the period by 2% (`kStretched`);
2. the window's median GPU span also exceeds the period by 2%;
3. its own span reached the period (ADR-1362's `gpuFull`, which already excludes lone stalls).

It is counted in `missedDeadlines`, and it moves the level exactly like ADR-1362's misses: 3 in the level's window
drop it, and probes revert on one.

Nothing else changes. The median requirement keeps the cap's wake-up jitter out, since those frames' spans read
0.80-0.92 of the period. So does the per-frame `gpuFull`, which also keeps out main-thread stretches with the GPU
inside the period. Unpaced, there are no deadlines, as before.

## Consequences

- A level that is persistently a few percent over the frame now descends, where before it held below the target
  frame rate with no miss reported.
- Levels at or under the period are unaffected, including the governor's fill (spans at 86-92% of the period), which
  never reaches the median requirement.
- **Tests:** `tests/unit/test_interactive_resolution.cpp` `[adr1363]` covers three cases:
  - a 34.8 ms level with intervals of about 35.5 ms descends (this fails without the rule);
  - frames on time with the span at the period do not;
  - stretched intervals with the GPU at 29 ms do not.

  The existing `[adr1362]` and `[resolution]` cases pass unchanged.
