# ADR-131: The cheap half of the fragment curve is not measurable, and the target stays where it is

**Status:** Accepted
**Date:** 2026-09-13
**Amends:** ADR-124 (which remains as written — this records what the missing measurement found)

## What was asked

ADR-124 set `RepresentationPolicy::targetPixelsPerTriangle = 500`, calling it "the measured minimum
of the sweep", and flagged its own weakest point: between 500 and 512,000 px/triangle the sweep was
nearly empty, so the rise to the right of the minimum rested on almost nothing. It named the
measurement that would settle it and could not run it, because the GPU lock was held.

It has now been run: arms at 8, 32 and 512 triangles were added, filling the region.

## Two corrections to ADR-124 before the result

- It says the region has **"no samples at all"**. It had one: side 8 is 128 triangles at 8,000
  px/triangle, inside the interval. The gap was one point wide, not empty.
- Its quoted px/triangle for the proposed arms (64,000 / 16,000 / 4,000 / 2,000) are computed from
  half the real pixel count. At 1280x800 the arms land at 128,000 / 32,000 / 8,000 / 2,000.

Neither changes its conclusion. Both are the kind of thing that propagates if nobody re-derives it.

## The result

**The first run looked decisive and was wrong.** It put the minimum at 8,000 px/triangle (1.376 ms)
against 2.097 ms at 500 — a 52% gap, which would have meant the shipped target was calibrated more
than an order of magnitude off. Three repeats say otherwise:

| px/triangle | scene ms, three runs | spread |
|---|---|---|
| 512,000 | 2.163 / 2.818 / 2.097 | 34% |
| 128,000 | 1.311 / 2.163 / 2.884 | **120%** |
| 32,000 | 2.163 / 1.835 / 1.442 | 50% |
| 8,000 | 1.442 / 2.884 / 1.638 | 100% |
| 2,000 | 1.442 / 1.966 / 1.901 | 36% |
| 500 | 1.507 / 1.507 / 2.032 | 35% |

Every interval overlaps every other. **There is no measurable minimum in the cheap half of the
curve**, and the 52% gap was one run's noise.

The expensive half is a different instrument entirely:

| px/triangle | three runs |
|---|---|
| 8 | 3.015 / 3.015 / 3.211 |
| 4 | 3.867 / 3.867 / 4.981 |
| 2 | 5.702 / 5.702 / 5.702 |
| 1 | 6.816 / 6.816 / 6.816 |
| 0.5 | 7.340 / 7.340 / 7.406 |

Three of those rows are identical to the microsecond across runs. So the sweep is not a noisy
instrument — it is a precise instrument whose *subject* stops varying once quad overdraw dominates,
and whose subject is dominated by everything else when triangles are large and few.

## Decision

**The target stays at 500 px/triangle.** Not because 500 is the minimum — nothing is — but because
every candidate value from 500 to 512,000 is indistinguishable at this instrument's resolution, and
changing a shipped constant on the strength of a difference that has been measured *not* to exist is
the same error in the opposite direction.

What the data does support, and what the selector actually relies on:

- **below ~8 px/triangle cost climbs steeply and monotonically**, and that half of the curve is
  reproducible to the microsecond. The floor at 8 is real, and it is the only threshold the
  selector compares against.
- **above ~500 px/triangle, triangle size stops being the thing that costs.** "Get above the knee
  and stop optimising" is the whole actionable content of the right-hand side.

## Consequences

- ADR-124's ceiling of 50,000 stays **advisory**, and is now advisory on stated grounds: not
  "barely evidenced" but *measured and found unmeasurable*. That is a stronger statement and a
  weaker number.
- ADR-124 rejected a "minimise triangles" objective using the 2-triangle row. Across four runs that
  row is 3.932 / 2.163 / 2.818 / 2.097 against 2,048 triangles at 2.097 / 1.507 / 1.507 / 2.032 —
  separated on the mean, overlapping at the edges. The rejection stands on the *shape* of the whole
  curve, not on that row, which is weaker evidence than ADR-124 presents it as.
- **Do not calibrate anything else in the cheap region with this instrument.** A single run there
  will produce a confident number roughly half the time.

## Verified vs assumed

**Verified:** every figure above, four runs of the same binary under `tools/gpu-lock.sh`, nothing
else on the GPU. The added arms are committed, so the sweep is re-runnable rather than described.

**Assumed:** that the cheap region's variance is measurement noise rather than a real bimodal cost —
the values cluster near multiples of the 65.5 us timestamp period, but the spread is far wider than
quantisation, and no cause was isolated. Untested: whether more frames per arm would collapse it.
