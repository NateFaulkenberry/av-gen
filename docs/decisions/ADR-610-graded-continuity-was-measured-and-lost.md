# ADR-610: Graded continuity was implemented, measured, and lost, because a distance penalty makes standing still free

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-182 (a probe that cannot fail), ADR-559 (a monotone metric is a direction),
ADR-609 (recall is not a quality measure), `docs/testing.md` #33 and "this rig produces convincing
zeros", Phase C §11
**Implemented by:** `src/scene/motion_database.hpp` (`MotionCostWeights::continuityPerSecond`,
default 0)
**Tests:** `tests/unit/test_motion_database_scale.cpp`

---

## Context

Phase C §11: "do not allow the system to **constantly jump between unrelated clips** simply because
they happen to have similar poses … potential inputs: current sample, previous sample, source clip,
phase, root velocity, transition distance."

The continuity term in place used one of those six: a candidate is either `sampleNext[current]` or
it is not. So **a sample two frames later in the same clip is penalised exactly as hard as a sample
from an unrelated clip**, and once the loop is off by a single frame it has no reason to prefer the
clip it is already in. Grading by transition distance is the obvious reading.

§11 also names its own metric, which is the useful thing about the section's wording: *constantly
jump* is a **rate**, so it is measured by running the matching loop rather than by scoring a query.

## Decision

**Implement it, measure it against the binary version on real motion, and keep the binary default
because the graded one lost.**

Measured over 300 steps of a matching loop on the real Glowmere database:

| continuity | clip jumps/s | mean index step | **stalled** | worst excess |
|---|---|---|---|---|
| binary (0.0) | 0.50 | 2.47 | **0%** | 0.0612 |
| graded 0.5/s | 0.50 | 1.81 | **13%** | 0.0612 |
| graded 1.0/s | 0.50 | 1.81 | **13%** | 0.0612 |
| graded 2.0/s | 0.50 | 1.81 | **13%** | 0.0612 |
| graded 4.0/s | 0.50 | 1.81 | **13%** | 0.0612 |

**Identical jump rate at every rate tried, and the loop stands still on 13% of steps.**

The reason is structural and is the part worth keeping: a penalty proportional to the distance from
the current sample is **zero for the current sample itself**, so *not moving is free*. The matcher
does not continue coherently; it freezes. §11 asks for coherent continuation and a freeze is not
continuation — it is the worst available outcome wearing the appearance of the best, because a
frozen matcher also reports zero jumps.

The field is kept at its inert default rather than deleted. A future reader proposing
distance-graded continuity can see that it was tried, how it was measured, and exactly why it
fails. **Any working version must penalise *not advancing*, which means the term needs the previous
sample as well as the current one** — another of §11's six named inputs, and the direction a second
attempt should take.

## The fixture was vacuous first, and that is the other half of this record

The first version of the loop queried with the features of `sampleNext[current]` — the natural
continuation — so the answer was always trivially the continuation and **the continuity term was
never asked to decide anything.** It reported **0.00 jumps per second for every configuration**,
which reads as a perfect score.

It is ADR-182 in a fixture written an hour earlier by the author of ADR-606, and it was caught by
this repository's own rule that an exact zero is a reading to distrust before believing
(`docs/testing.md`). The rewritten loop drives the character toward a clip its current one cannot
reach, so continuity faces a real decision on every step: follow the request and cut, or stay and be
wrong.

**And the vacuous version hid the actual defect.** Its graded runs reported a mean index step of
0.15 against the binary's 1.82 — the freeze was visible in that number all along, in a test whose
headline metric said everything was fine. A probe that cannot fail does not merely prove nothing; it
can carry the evidence of a real fault and present it as a success.

## Consequences

- `continuityPerSecond` defaults to 0. The binary behaviour is unchanged, bit for bit.
- The test asserts `stalledFraction < 0.5` alongside the jump rate, so **a frozen matcher can no
  longer pass by reporting no jumps.** Frequency alone cannot distinguish a stable matcher from a
  dead one, which is the same shape as ADR-609's recall-without-severity one section earlier.
- §12 is unchanged and remains correct in its one explicit requirement — it keys on tag metadata,
  never on clip names — but is likewise binary, and any grading of it faces this same trap.
