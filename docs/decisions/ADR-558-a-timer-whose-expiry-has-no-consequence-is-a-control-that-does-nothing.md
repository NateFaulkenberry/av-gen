# ADR-558: A timer whose expiry has no consequence is a control that does nothing

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-086 (a state stores when it was entered), ADR-096 (gait hysteresis), ADR-182 (a
probe that cannot fail proves nothing), ADR-300 (a pose layer cannot reach the world), ADR-554
**Implemented by:** `src/entity/attention.{hpp,cpp}`
**Tests:** `tests/unit/test_attention.cpp`

---

## Context

Phase B §22 asks for an attention target with a **duration**, and is explicit that look-at must not
choose its own targets: Phase D decides a mushroom is interesting, Phase B turns that into body
motion. So the selector scores nothing, knows nothing about the world, and answers one question —
which candidate the body is attending to, and for how long.

## The defect the tests found, and its general shape

The first implementation dropped the target when its hold expired and then re-acquired a target on
the same frame. With a lone candidate, the target it re-acquired was **the one it had just
released** — because it was still the most salient thing in the world.

The consequences were invisible from outside. The character *was* looking at the most salient
thing, which is what a working selector does. But:

* the dwell never reset, so `maxHoldSeconds` never elapsed again;
* the ease-out ran to zero and stayed there;
* the character never looked away from anything, ever.

**A hold that expires, drops its target and immediately re-acquires it is not a hold.** The timer
ran, the branch fired, and nothing downstream could tell. It would have been reported as *"attention
feels dead"* rather than as a bug, and the search would have started at the look-at layer.

The general form is worth stating on its own: **a timer whose expiry has no consequence is a
control that does nothing.** It is the sibling of ADR-182's probe that cannot fail — there, a test
that cannot come out the other way; here, a mechanism that cannot change an outcome. Both pass
every check you would think to run.

## Decision

**A target that has been examined enters a refractory period during which it cannot be re-acquired.**

That is what makes *"I have looked at that"* **a fact with consequences** rather than a value that
resets. Attention is not only a question of what is loudest: a character that has just examined
something should look somewhere else before looking back, and the difference between those is the
difference between attending and staring.

Two further rules, each measured rather than argued:

1. **The margin and the dwell are both required, because they catch different failures.** The
   margin stops a *near-equal* rival; the dwell stops a *briefly stronger* one. Measured: 600
   frames of two candidates swapping the lead every frame give **at most 4 switches** with a
   shortest dwell above 0.3 s, against **over 100** with both disabled — and a two-frame spike at
   **8×** the incumbent's salience does not steal attention, while the same rival sustained does.
   A single switch count would have hidden that; it is the answer to "is one of these redundant"
   as a measurement.
2. **Ties break on identity, never on list order.** The candidate list is rebuilt every frame by
   whoever perceives the world, so its order is an artefact of the rebuild and not a fact about the
   scene. Two equally salient things swapping because a vector was reordered is a thrash with no
   cause in the world at all. **State the property the code depends on, not the incidental one it
   can observe.**

## Consequences

* Attention reports **dwell** as well as a target, because "how often does it switch" and "how long
  does it commit" are different questions and only the second is what a viewer reads.
* `refractorySeconds` is a setting rather than a constant, because how long a character ignores
  something it has just examined is a character trait and Phase D §29 will want it.
* **Revisit if** a scene needs a target that genuinely should be stared at indefinitely. The
  honest answer then is a candidate whose `duration` is large, not a selector with no hold.
