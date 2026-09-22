# ADR-600: A gate with nothing configured refuses nothing, and knowing the hazard does not prevent it

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-182 (a probe that cannot fail proves nothing), ADR-551 (no answer is not no ground),
ADR-558 (a timer whose expiry has no consequence), ADR-559 (a monotone metric is a direction),
`docs/testing.md` #24
**Implemented by:** `src/scene/motion_quality.{hpp,cpp}`, `src/scene/motion_variants.{hpp,cpp}`
**Tests:** `tests/unit/test_motion_variants.cpp` — "a generated variant that fails the gate never
enters the set"

---

## Context

Phase B §44 requires that offline generation reject bad variants. §41's metrics are the evidence,
and one of their properties is load-bearing: **a metric reports whether it was measurable,
separately from its value.** Reach error and velocity error need a live target and a controller,
and zero is the *good* answer for both — so filling them in would read as a pass.

From that follows a rule I defended explicitly: **an unmeasured metric never fails a gate.**
Without it, every hand-authored clip in this repository would be rejected for lacking a contact
track nobody gave it. Unmeasured is a third state, not a bad score.

## The defect, which is that rule's failure mode

`generateVariants` constructed its own default `MotionQualityOptions`. A default names **no
contacts and no limbs**. So every metric came back unmeasured, every unmeasured metric declined to
fail, and the gate **refused nothing**. Three deliberately broken variants — a clip whose every
pose asks for twice the limb the skeleton has — passed into the set.

The validator ran. It produced a report. It was incapable of rejecting anything.

**This is not an argument against the rule.** The rule is right and the alternative is worse. It is
an argument that **who configures the gate is part of the gate**: a caller supplying no options
disables it silently, and nothing in the type system says so.

## Decision

1. **The metrics a gate may measure come from the caller**, not from a default the generator
   invents. `VariantOptions` carries `quality` and `limits`, and a caller that wants a gate must
   say what it gates on.
2. **The test that catches it is the one that keeps it fixed.** A gate test whose input is only
   *good* content cannot distinguish "refused the bad ones" from "refused nothing", so every gate
   test here carries deliberately broken input and asserts the refusal.

## The larger finding, which is worth more than the fix

**Knowing a hazard by name does not prevent it.**

I wrote the sentence *"a gate that only logs is the dead-knob family in a new place"* and committed
it. **Within the hour I shipped a gate that could not reject**, in the code written to prevent that
exact thing, and it was caught by a test rather than by the knowledge.

That is the third independent arrival at this conclusion in one session — the tornado agent reached
it from six wasted renders, the fog agent from writing five entries about a family and then
producing a fresh instance within a day, and this one from an hour. Three agents, three routes, one
conclusion:

> **Prefer the structural fix to the remembered one.** A tripwire switch that fails the build beats
> a note saying "remember to update both sites". A gate whose options are a required argument beats
> a gate whose options default to nothing. Awareness decays within the hour; a compile error does
> not.

The corollary is how to spend review effort: when a hazard is identified, the next question is not
"will we remember" but **"what change would make this impossible"** — and if there is not one,
"what test fails when it recurs".

## Consequences

* Every gate in this phase takes its metrics from its caller.
* The `-Wswitch` tripwire over `PoseLayerKind` (four catch-alls) and this are the same move, and
  both were adopted after the awareness had already failed.
* **Revisit if** a gate appears whose configuration genuinely has a safe default. The honest
  response is then to say what makes it safe, not to rely on callers supplying one.
