# ADR-835: The crowd's push reaches the motion tier as steering

**Status:** Accepted
**Date:** 2026-09-25
**Answers:** Phase B §39 ("allow the motion system to consume an externally supplied steering
direction: desired velocity + avoidance correction = motion velocity"). The progress doc's gap was
that the seam was "built and tested, and fed nothing".
**Related:** ADR-831 (separation's backward component), ADR-240 (separation as a speed)
**Implemented by:** `separateFromCrowd` (`src/entity/behaviors.cpp`) and `Entity::advanceMotion`.
**Tests:** `tests/unit/test_navigation_system.cpp`, "the crowd's push is published as steering the
motion tier can execute" (`[adr835]`). It fails with the producer line removed: no frame publishes
any steering.

## Decision

`MotionRequest::steering` and `CharacterIntent::steering` existed, and the controller and the
matcher already add steering to `desiredVelocity`. Nothing wrote to it.

- **The producer.** Crowd separation is the one correction in the behaviour tier that moves a body
  in a direction it did not ask for: the push lands in `travel`, not in `speed` or `yaw`.
  `separateFromCrowd` now also adds that push, as a velocity (push / dt, horizontal), to
  `state.intent.steering`. The intent is cleared at the top of every step on both paths, so the
  correction describes this step only, and a replay reproduces it.
- **The consumer.** `advanceMotion` passed steering only when a behaviour had published a
  vector intent. Separation acts on bodies whose behaviours write only the polar pair, so the
  request now carries `intent.steering` in both branches.

Obstacle avoidance is not included. It already rewrites the desired heading before `speed` and
`yaw` are set, so it is part of `desiredVelocity` already, and publishing it again would count it
twice. The penetration resolve is not included either: it is a guarantee, not a steer.

## Measured

On two walkers that start 3.5 m inside each other, every frame on which both are pushed
publishes steering that points each body away from the other, horizontal and bounded by the push.
On the multicam film over 60 s, the entity positions and the motion chain's per-frame clip choices
hash identically before and after this change (`5f6071349066d4eb`, `bb181b87fd9f5813`).
Separation there is rare and does not change a clip. The seam is now fed. It is not yet visible
in that film.
