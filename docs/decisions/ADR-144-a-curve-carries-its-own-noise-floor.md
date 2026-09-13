# ADR-144: A curve carries its own noise floor, or it is not a curve

**Status:** Accepted
**Date:** 2026-09-13
**Extends:** ADR-113 (a measurement carries its conditions) from a pair to a sweep
**Answers:** ADR-131's closing instruction — "do not calibrate anything else in the cheap region
with this instrument"

## Problem

ADR-113 fixed the *pair* comparison: two arms, interleaved in one process, differenced against a
floor that rises to the session's own spread. Phase G's headline deliverable is not a pair. It is a
**curve** — cost against object count, cost against instance count, cost against the visible
fraction — and nothing in the repository knew how to refuse to certify one.

The consequence is on the record twice.

**ADR-131.** A sweep of fragment cost against triangle size, one run per arm, put the minimum at
8,000 px/triangle against a competitor 52% slower. It read as decisive. Three repeats dissolved
every interval into every other: the cheap half of that curve has no measurable minimum at all, and
the 52% was one run's noise. ADR-131 closes by warning that a single run in that region "will
produce a confident number roughly half the time" — a warning addressed to future authors, which is
the weakest place to put one.

**§1.1 of the audit.** The Constellation discrepancy was justified with Glowmere's 1% spread.
Constellation's own is 37%. The number was withdrawn.

Both failures have the same shape: a spread measured on one subject was used to judge another, and
nothing in the code objected. A curve is where this is most dangerous, because a curve *looks like*
its own evidence — five points in a row that rise are persuasive whether or not any adjacent pair is
separated.

## Decision

`rendering::SweepPoint` and `rendering::SweepSummary` (render_stats.hpp) represent a curve, and they
are built so that a curve without a floor cannot be expressed.

**1. A point holds every repeat, never a summary of them.** `SweepPoint::repeats` is the raw vector.
There is no constructor that takes a mean. `summariseSweep` derives `spreadPercent` — this arm's own
peak-to-peak over its median — for each one.

**2. The curve's floor is the worst arm's spread, floored at `kGpuNoiseFloorPercent`.** Never
lowered: an arm that happened to repeat well is not licence to certify below what this machine has
been measured to produce. Always raised: a session with one 40%-spread arm cannot certify a 20%
endpoint difference, which is exactly the judgement ADR-131 had to make by hand and after the fact.

**3. Adjacent steps are reported as well as the endpoints.** An endpoint ratio cannot see a knee,
and ADR-131's own curve is knee-shaped: flat and unresolvable on the cheap side, monotonic and
microsecond-reproducible on the expensive side. `largestStepPercent` names the arm the largest
resolvable step lands on, or says that no adjacent pair clears the floor.

**4. Every sweep runs a null arm.** The first arm is measured a second time under a second name, so
the last row of every table is the harness measuring itself. ADR-113 §5 made this the rule for A/B;
it is a stronger rule for a sweep, because a sweep has more arms for a drift to hide in. A null that
clears the floor invalidates the table it is printed under, and the perf tests `CHECK` it.

**5. A second reading, on the fastest repeats, reported alongside and never instead.**
`docs/performance.md` concluded in September that under contention the median of identical runs
varied by 3x while the minima agreed to a tenth of a millisecond, because contention is never
negative. ADR-113 kept the minimum in every record and declined to make it the A/B statistic, on two
stated grounds: it discards the tail, and pairing already removes the drift a minimum would remove.

Neither ground transfers to a sweep on **this** machine. A sweep is not paired, and this repository's
machine is shared between agents — the first full run of these curves was taken at a load average of
62 and reported per-arm spreads of 85%, 130% and 172%, correctly refusing to certify anything. So
`SweepSummary` carries `endpointChangeMinPercent` with its own floor: the largest gap any arm showed
between its *fastest* and *second-fastest* repeat. An arm whose two best repeats disagree has no
reliable minimum either, and that is the thing the floor is measuring.

It is labelled in the printed table as "fastest repeats", and the header says what it is blind to:
a change that leaves the fast frames alone and doubles the slow ones is invisible to it, and that is
precisely what a viewer notices. It answers "what does the hardware do when nothing is in the way",
not "what does the frame cost".

## Consequences

- The three Phase G curves (`tests/rendering/test_scalability_perf.cpp`) print their floor, their
  null and both readings. On a busy machine they say "NOT A RESULT" and that is the instrument
  working, not the instrument failing.
- The arithmetic is unit-tested without a device (`[perf][scalability][stats]`), including the exact
  ADR-131 shape: a 20% endpoint difference with one 40%-spread arm, which must not be certified.
- **The sweep does not fail on a millisecond.** Its assertions are about the fixture and the
  harness: that the visible count really is constant across an existence sweep, that the null does
  not report a result, that the device logged no errors. A threshold on a time would be a
  cross-session comparison with the other session hidden inside a constant.

## Alternatives considered

**Fit a line and report R².** Rejected. A high R² over five points that are each individually
unresolvable is the ADR-131 failure with a statistic on top. The question a sweep is asked is
whether *these two arms* differ, and a fit answers a different one.

**Bootstrap a confidence interval over the repeats.** Rejected for now, for ADR-113's reason: three
to five repeats is not a sample a resampling method has anything to work with, and the observed
peak-to-peak is a direct measurement of the quantity the interval would be estimating.

**Raise the repeat count until the floor comes down.** Attempted, and it is why `kRepeats` is five
rather than three. It does not fix contention — the contended repeats are not outliers to be
averaged out, they are a different machine — which is what the fastest-repeats reading is for.

## Verified vs assumed

**Verified:** the statistics, by unit test. The null arms, the per-arm spreads and the refusals, on
this machine under `tools/gpu-lock.sh` — including a run whose every curve was correctly declared
not a result.

**Assumed:** that the fastest-repeat reading is measuring an uncontended machine rather than a
systematically cheaper first frame. Untested: whether the minima agree across *sessions*, which is
the property `docs/performance.md` claims for them and which nothing here has re-checked.
