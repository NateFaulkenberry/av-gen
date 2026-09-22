# ADR-559: A monotone metric is a direction, not a quality measure

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-182 (a probe that cannot fail proves nothing), ADR-170 (minima over repeats),
ADR-385 (a stated reason is not evidence), `docs/testing.md` #20 (a yes/no question cannot receive
a magnitude), #24 (a fixture at a boundary measures the boundary)
**Implemented by:** `src/entity/trajectory.{hpp,cpp}`, `MotionLimits::anticipation`
**Tests:** `tests/unit/test_trajectory.cpp` — "lookahead starts the turn earlier", "over-eager
lookahead shows in path deviation, which onset cannot see"

---

## Context

Phase B §38 asks that a turn begin before the corner rather than at it. The obvious measure of
success is **turn onset**: how far before the corner the body starts to come round. Measured on an
L-shaped path with a right-angle corner:

| anticipation | turn onset | worst path deviation |
|---|---|---|
| 0.0 (no lookahead) | 0.10 m | — |
| **0.35 (chosen)** | **0.87 m** | **0.39 m** |
| 1.0 | 2.33 m | **3.19 m** |

Read on onset alone, the setting of 1.0 is **strictly better** — it turns two and a half times
earlier. It is also the worst setting available: the body cuts the corner by 3.19 m, which reads as
a character anticipating a corner the viewer cannot see yet.

## The decision, stated as a principle because it generalises

**A metric that improves monotonically with a dial will always recommend the extreme of that dial.**

Onset can only rise as anticipation rises. It contains no term for what anticipation costs, so it
cannot have an interior optimum — and optimising it is therefore not tuning, it is a proof that the
dial should be set to its maximum. The number is correct, the measurement is honest, and following
it ships the worst configuration as the best one.

So: **a monotone metric is a direction, not a quality measure.** Before it can choose a *value* it
needs a second measurement that moves the other way. Here that is **path deviation** — how far off
the intended line the body travels while anticipating — and the pair has an interior optimum where
the single metric had none.

This is distinct from the traps already recorded:

* `testing.md` #20 is a question that cannot receive its answer (a yes/no asked of a magnitude).
* `testing.md` #21 is a window too small for the quantity to exist in.
* This is a measurement that is **correct, complete and unopposed** — and therefore a ratchet.

**The test for it is cheap: ask whether the metric has a maximum in the interior of the dial's
range.** If it does not, it is half a measurement.

## Consequences

* `anticipation` is a dial with a default of 0.35 chosen from the pair, not a constant chosen from
  onset.
* Where a Phase B layer has a strength, its tests carry an opposing measurement: stride shortening
  against foot slide, lean angle against balance margin, lookahead against corner cutting.
* **Revisit if** a quantity appears whose cost is genuinely zero. That is the case this rule does
  not cover, and the honest response is to say what the cost is and why it does not apply rather
  than to declare the metric sufficient.
