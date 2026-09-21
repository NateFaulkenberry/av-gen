# ADR-603: Two thirds of the motion stack is a hierarchy walk, and it cannot simply be hoisted

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-385 (a stated reason is not evidence), ADR-543 (a limb is three named joints),
Phase B §47 (performance targets), §53 (profile before optimizing)
**Implemented by:** nothing — this records a baseline and forecloses one wrong optimization
**Tests:** `tests/unit/test_motion_performance.cpp`

---

## Context

§47 asks for measured baselines at 1, 10, 50 and 100 characters, with per-character, per-layer and
shared costs separated, and says explicitly not to promise a frame time. Measured on the Glowmere
alien (90 joints, 26 clips), full stack of seven layers, minima over five repeats:

| | clip only | +stride (2) | +lean (1) | +feet (2 IK) | +look (1 aim) | +reach (1 IK) |
|---|---|---|---|---|---|---|
| n=1 | 4.74 | 8.87 | 8.94 | 14.37 | 18.88 | 21.43 |
| n=100 | 4.92 | 9.07 | 9.26 | 15.03 | 19.20 | 21.91 |

microseconds per character per frame. Marginals at n=1: stride +4.25 for two layers, lean +0.09 for
one, feet +5.51 for two IK chains, look +4.68 for one aim, reach +2.64 for one IK chain.

**Shared cost stays shared:** +0.9% from one character to a hundred, which is within cache noise
and far below what any per-instance copy of a 90-joint skeleton or 26 clips could hide in. **The
steady-state path allocates nothing:** net live blocks over a whole repeat is zero at every count.
The baseline is **2.16 ms per frame for a hundred characters**, stated as a measurement on this
machine today and not as a promise.

The number that does not fit the intuition is the aim layer. A look at 4.68 µs costs more than a
two-bone IK solve at 2.75 µs, which is backwards if you think the solve is the work.

## Decision

**Record that the dominant cost is a repeated hierarchy walk, and record why hoisting it is wrong.**

Every layer that works in model space calls `poseToModel` over all 90 joints in order to read one
to three of them. There are six such calls in a full stack plus the body compensation pre-pass:
seven walks per character per frame. Measured directly rather than inferred (ADR-385): one walk is
**2.048 µs**, so seven are **14.3 µs of the 21.6 µs full-stack cost — 66%.** The solves are the
small part.

**And it cannot be hoisted.** A layer *writes* the pose, and the next layer needs model space as the
previous layers left it — that is the whole content of §5's pipeline order. One rebuild at the top,
shared by every layer, would give the reach layer a hand position from before the lean moved the
spine, and give the body compensation a foot from before it was planted. It would be fast and
wrong, and it would be wrong silently, in a way that reads as a slightly-off pose rather than as a
failure. This ADR exists mainly to say that out loud, because 66% in a profile is exactly the shape
that invites the one-line change.

## Consequences

- The optimization, when §53 takes it, is **incremental** rather than hoisted: rebuild only the
  joints beneath what the previous layer actually wrote. The stack already knows what each layer
  touched — `masks_`, `chain_` and `stride_` are resolved per layer at bind time — so the
  information needed is present and the change is bounded.
- The expected win is large and should be stated before the work rather than after, so it can be
  wrong: a layer typically writes one to three joints out of ninety, so an incremental rebuild
  should turn most of those 14.3 µs into the cost of a subtree, and the stack's per-character cost
  should approach the 4.9 µs the clip sampling alone costs.
- Until then, **2.16 ms for a hundred characters is the number**, and the aim layer's cost is not a
  defect in the aim layer.
- The allocation assertion is the one most likely to catch a future regression, and it is the
  cheapest: it fails the day someone returns a vector by value from inside the per-frame path,
  which is a change that would otherwise show up only as a frame-time drift nobody bisects.
