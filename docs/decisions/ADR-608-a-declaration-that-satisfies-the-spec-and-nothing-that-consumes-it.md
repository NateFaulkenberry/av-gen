# ADR-608: A declaration that satisfies the specification, and nothing that consumes it

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-558 (a timer whose expiry has no consequence), ADR-600 (a gate with nothing
configured refuses nothing), ADR-606 (re-read the specification), ADR-607 (a requirement whose cost
lives in another section), Phase C §10
**Implemented by:** `src/scene/motion_database.{hpp,cpp}` (`motionFeatureLayout`,
`motionFeatureWeights`, `MotionCostBreakdown`)
**Tests:** `tests/unit/test_motion_cost.cpp`

---

## Context

Phase C §10: "cost = poseCost + trajectoryCost + velocityCost + facingCost + phaseCost +
contactCost + transitionCost. **Every term should have a configurable weight. Avoid an opaque
scoring function.**"

§10 was among the sixteen sections Phase C inherited as done, and `MotionFeatureConfig` carries
exactly the seven weights §10 asks for, with sensible defaults, serialised and round-tripped.
**The structure matched the specification perfectly. Only the reads did not exist.**

- `jointPositionWeight`, `jointVelocityWeight`, `trajectoryPositionWeight`,
  `trajectoryFacingWeight`, `rootVelocityWeight`: **zero uses** in any translation unit outside
  their own declaration.
- `phaseWeight` and `contactWeight`: read, but only as `> 0` presence tests deciding whether to
  *include* a dimension. Setting one to 2.0 rather than 0.5 changed nothing whatsoever.

The search summed every dimension at weight 1. The cost was a single undifferentiated squared
distance — **precisely the opaque scoring function §10 exists to forbid, with a tuning surface
bolted to the outside of it.**

## Decision

**Name this as a distinct and more severe failure than an absent control, and record its shape.**

### Why it is worse than a missing knob

An absent control is obviously absent. This one is present, documented, serialised and inert, so
**it actively misdirects the next person's diagnosis**: someone tuning the matcher moves five dials,
observes no change, and concludes *the features are wrong*. They then go and change the feature
selection, which was fine. A missing knob costs an afternoon of adding it; a dead knob costs a
misdiagnosis of the system it is attached to.

### The shape, which is a fourth distinct one

Three failure modes are already on the record: ADR-606's audit citing a true fact that answers a
neighbouring question; ADR-182's probe that cannot fail; ADR-607's requirement whose cost lives in
another section. This is a fourth: **the declaration satisfies the specification and nothing
consumes it.** It survives every review that checks structure against a spec, because the structure
is right.

It is the same family as the field with no producer, the publisher with no subscribers, and the tag
with no reader — which makes **four instances across two subsystems** in this programme. The common
test is cheap and nobody runs it: *grep for the field name outside its own declaration and count.*

## Consequences

- Weights are derived into a per-dimension vector and applied in the scan. **Deriving rather than
  baking is load-bearing**: a weight is tunable *without rebuilding the database*, because the
  features are unchanged and only the multiplier differs.
- `motionFeatureLayout` states the dimension layout once, so the weights and the breakdown cannot
  drift from what `buildMotionDatabase` writes — one statement of a layout, several readers.
- `MotionCostBreakdown` answers "why that sample" with numbers, computed **once for the winner**.
  The decisive reason is not cost: because of the early-out, **a losing candidate's partial sums
  would be false**, so a per-candidate breakdown would be a wrong number rather than an expensive
  one. It asserts `total() == cost` so it cannot drift into a decorative second opinion — which is
  the exact failure a breakdown exists to prevent.
- The probe flips the answer by raising one group's weight, in both directions, on two candidates
  made wrong in *different groups by the same amount* so they tie at equal weights. Nothing touches
  `db.features` between the three searches, so **the absence of a mutation is the evidence** for
  tunable-without-rebuild. Forcing the weights off fails four assertions — which is exactly the
  state the code was in before this ADR.
