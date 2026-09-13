# ADR-124: The target is a band of a few hundred pixels per triangle, not the fewest triangles

**Status:** Accepted
**Date:** 2026-09-13

## Problem

Task C8: calibrate the representation thresholds against the gate measurement. The gate has two
findings and the second is the one that is easy to optimise straight past.

Audit §4.5, at 1280×800 and constant full-screen coverage, depth control subtracted:

| triangles | px/triangle | scene pass | minus depth | ÷ minimum |
|---|---|---|---|---|
| 2 | 512,000 | 3.02 | 2.89 | **2.01** |
| 2,048 | 500 | **1.57** | **1.44** | **1.00** |
| 16,200 | 63 | 2.69 | 2.56 | 1.78 |
| 131,072 | 7.8 | 3.21 | 3.14 | 2.18 |
| 259,200 | 3.95 | 4.13 | 4.06 | 2.82 |
| 524,288 | 1.95 | 6.03 | 5.83 | 4.05 |
| 1,036,800 | 0.99 | 7.14 | 6.81 | 4.73 |
| 2,097,152 | 0.49 | 7.73 | 7.27 | 5.05 |

The curve has a **minimum**, not an asymptote. Two screen-filling triangles cost twice what 2,048
cost. So "fewer triangles" is not monotonically better, and a representation system written as
"reach the fewest triangles you can get away with" is wrong **by measurement**, not by taste.

## Decision

Three numbers, and each is labelled with how well it is evidenced.

### Target: 500 px/triangle — measured

The measured minimum of the sweep. `RepresentationPolicy::targetPixelsPerTriangle` is set to it.

The rule is: **the coarsest rung still at or under the target**. Not "the fewest triangles". This
structurally cannot overshoot past the cheap band into the two-triangle regime, because a rung whose
px/triangle exceeds the target is never accepted. Where every rung is already coarser than the
target, the finest rung is chosen — it is the one closest to the target from above, and there is
nothing else a selector can do: you cannot add triangles that were never authored.

### Floor: 8 px/triangle — measured, conservatively placed

The knee sits between 3.95 and 7.8 px/triangle, exactly where a 2×2 quad predicts. Eight is the safe
side of that bracket. It is not a threshold anything crosses — the "coarsest under target" rule
already prefers the largest triangles available — it is the number a diagnostic compares against to
say "this rung is in quad-overdraw territory and the mesh ladder has run out".

### Ceiling: 50,000 px/triangle — **advisory, and barely evidenced**

This is the honest part. Between 500 and 512,000 px/triangle **the sweep has no samples at all**. The
rise on the right of the minimum is established by exactly one point. So:

- the ceiling is set where cost is *known* to be bad rather than where the rise is *known* to begin;
- nothing in the selector compares against it. It is documented as a property of the hardware and a
  constraint on *authoring* and on any future HLOD merge — the two levers that can actually make
  triangles larger — not as a runtime threshold.

**The measurement that would settle it** is a re-run of `[.perf][fragment]` with arms at 8, 32, 128
and 512 triangles (64,000 / 16,000 / 4,000 / 2,000 px/triangle), which fills the gap with four points
and costs one afternoon. It is not done here because it is a GPU measurement and the lock was held.

### The tiers do not move the target

`RepresentationPolicy::forTier` moves the radius bands between preview, realtime and high, and moves
nothing else. The px/triangle target is a measured property of this GPU, not a taste setting; a tier
that moved it would be claiming a different machine. Offline forces the top representation and is
governed by ADR-125 and §5.9.

## Consequences

- The calibration is asserted, not just written down: `tests/unit/test_representation.cpp` sweeps a
  ladder across six distances and checks that the chosen rung's px/triangle never exceeds the target
  while any rung is under it — the anti-overshoot property, which is the one the caveat is about.
- Applying this to Glowmere (ADR-126) says the ecology's estimated fragment-cost excess is 2.02×
  while the whole frame's is 1.52×. Both are estimates on a curve whose absolute milliseconds do not
  transfer, and both are quoted that way.

## Rejected alternatives

- **A single "minimise triangles" objective**, which is how every LOD system is usually described.
  Rejected by the 2-triangle row, which is a measurement and not an opinion.
- **Deriving the target from the 2×2 quad geometry alone** (i.e. "anything above ~16 px is fine").
  That explains the left of the curve and says nothing about the right. The measured minimum is
  thirty times higher than the quad argument predicts, so the quad argument alone would have set the
  target thirty times too low.
- **Extrapolating the curve past its measured ends.** Both ends are clamped in the analysis code
  instead. A confident extrapolation is exactly the kind of number this exercise exists to avoid.

## Revisit when

The four-point sweep between 500 and 512,000 px/triangle has been run, or the sweep is repeated on
different hardware — the target is a property of the GPU and is not portable.
