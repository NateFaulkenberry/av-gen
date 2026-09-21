# ADR-545: Velocity is measured once, not authored in twenty places

**Status:** Accepted
**Date:** 2026-09-20
**Related:** ADR-088 (the behaviour/animation seam), ADR-091 (two-tier determinism), ADR-240 (the
rotation representation), ADR-260 (three positions), ADR-267 (seek is re-simulation), ADR-337 (root
motion is a transfer of authority), ADR-521 (the first render tick reports `deltaTime = 0`),
ADR-541 (a provider that remembers lives in the entity tier)
**Implemented by:** `EntityState::velocity` / `facing()` / `groundSpeed()` / `strafeAngle()`,
measured in `EntityWorld::update`, published through `entity::LocomotionState`.
**Tests:** `tests/unit/test_entity_velocity.cpp` — 6 cases, 101 assertions.

---

## Context

`EntityState` has described velocity in polar form since it existed: `speed`, a scalar, along
`yaw`, a heading. Everything downstream reads that pair — the gait machine, the animation seam, the
foot-slip diagnostic.

**A scalar along a heading can only describe a body travelling the way it faces.** It cannot say
"moving north-east while facing north". It cannot distinguish a walk from a backpedal — both are a
positive `speed` along `yaw`. It cannot express a strafe, a body circling a target while watching
it, or the future-trajectory features a motion matcher queries on. Phase 0 recorded this
(`docs/design/autonomous-character-animation.md` §1.11); the Phase A brief makes it §3.

## The decision, and the part that is not obvious

**Velocity is measured, not authored.** It is the backward difference of `EntityState::position()`,
taken **once**, at the end of the per-entity update, after `applyOffsets` and after the field arc
has had its say.

The obvious implementation is to add a `velocity` field and have every mover keep it up to date.
There are **more than twenty such sites** — `state.speed = …` appears 15 times in `behaviors.cpp`
and 7 in `action.cpp`, and `travel.x/z +=` appears in as many more, across behaviours, the action
tier, root motion, the director, crowd separation and grounding. Twenty writers of a derived
quantity is twenty chances for it to disagree with the position it is supposed to describe, and the
disagreement would be silent.

One backward difference at one site is correct for **all** of them at once, including the movers
that do not think of themselves as movers: a crowd push, a penetration resolve, a grounding
assignment, a director placement. It also measures the thing the polar form cannot — *which way the
body actually went* — rather than re-asserting the intent it was given.

So the two are now different things and both cross the seam:

| | what it is | who writes it |
|---|---|---|
| `speed`, `yaw` | **intent** — how fast the mover means to go, and where it means to face | the movers |
| `velocity`, `facing()` | **measurement** — what the body did over the step that just ran | one site |

They differ whenever something downstream had an opinion, which is most frames on a crowded scene,
and the difference is information rather than error.

## Determinism

A backward difference is state, so `EntityWorld::reset` clears it — the same rule, and the same
two lines, as the root-motion sample beside it (ADR-267 D4). A seek rebuilds it by replaying the
same fixed steps. Carrying it across a reset would make the first replayed step a difference
between two unrelated places, which is exactly the teleport `reset` exists to remove, and the test
asserts it by walking a body 50 m out, resetting, and requiring zero.

**`dt <= 0` leaves the velocity at zero rather than dividing.** That is not defensive coding for a
case that cannot happen: ADR-521 records that `FixedStepClock::tick()` hands out `deltaTime = 0` on
the **first tick of every render**, so a naive division would publish an infinity into the pose
layer on frame one of every offline job. There is an arm for it.

## Evidence

Driven by the **director tier**, which is the one production path that writes position and yaw
independently ("A director says where a body *is*", `entity.cpp`) — deliberately, rather than a
synthetic test behaviour, which would have proved something about the test.

| arm | result |
|---|---|
| a body that has not moved | velocity is **exactly** zero; `strafeAngle()` does not divide by it |
| first step of a body's life | zero — a backward difference has nothing to difference against |
| `dt == 0` after a 5 m move | finite, and zero |
| travelling north-east, facing north | speed 3.0 m/s, direction north-east, facing +Z, strafe angle **π/4** |
| travelling +X, facing +Z | strafe angle **π/2** |
| backing up | strafe angle **π**, at the *same* magnitude as walking forward — which is precisely what `speed` alone cannot tell apart |
| circling a target while facing it, 38 steps | tangential speed `r·ω` and a right angle at **every** step |
| reset after walking 50 m | zero, and the next step measures 1.0 m/s from the reset position |

## Rejected alternatives

* **A `velocity` field each mover maintains.** Twenty writers of a derived quantity; see above.
* **Deriving `speed` from `velocity` and deleting it.** `speed` is *intent*, and the gait machine
  is right to select on intent rather than on what the crowd field allowed — a body pushed to a
  stop should not flicker into an idle clip. They are different quantities and both are wanted.
* **A forward difference, or an integrated velocity.** A forward difference needs the next step;
  an integrated one is an accumulator, which ADR-091 D4 forbids and which would drift against
  `position()`.
* **Publishing only the direction.** The magnitude is what stride warping and rate matching need,
  and the pair is what a trajectory feature is.

## Consequences

* Strafing, backpedalling and circling are expressible for the first time, which Phase B §11
  depends on and which Phase C's trajectory features are built from.
* `LocomotionState` carries both halves, so a pose layer can tell a strafe from a walk.
* The measurement is free: one subtraction and one divide per entity per step.

## Revisit triggers

* Angular velocity is wanted as a measured quantity too — `turnRate` is intent and has the same
  problem, and the same one-site fix would work.
* A mover appears that writes `position()` *after* the measurement site.
