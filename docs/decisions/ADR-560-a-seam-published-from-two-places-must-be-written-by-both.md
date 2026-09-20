# ADR-560: A seam published from two places must be written by both, field by field

**Status:** Accepted
**Date:** 2026-09-20
**Related:** ADR-091 (two-tier simulation authority), ADR-267 (defect 3: the action tier a scrub
could not reproduce), ADR-300 (a pose layer cannot reach the world), ADR-360 (the determinism
contract), ADR-545 (velocity is measured once), ADR-553 (a correct number that reached nothing)
**Implemented by:** `src/entity/entity.cpp` — `EntityWorld::update`, `EntityWorld::seek`;
`src/entity/locomotion.hpp`
**Tests:** `tests/unit/test_entity_velocity.cpp` — "the velocity the SEAM carries", "play and scrub
publish the same seam, field by field"; `tests/unit/test_entity_action.cpp` — "a scrubbed frame
publishes the action it is performing"

---

## These are product defects, not architecture findings

Recorded plainly because the distinction decides who cares. Two of the three had a **user-visible
symptom on the shipping cast**:

| defect | what a person sees |
|---|---|
| `action` not published by `seek` | **a character performing an action stands up and walks the moment you scrub to it.** `sit`, `sleep`, `pickUp` — anything an action names — plays correctly while the timeline runs and reverts to the gait's walk on a scrubbed frame. This is an editor-facing defect, and the editor is where this project's owner works. |
| `velocity`/`facing` not published by `update` | nothing today, because the only consumer arrived in Phase B. It would have been a strafing character whose animation layers believed it was standing still — and it would have looked *correct while scrubbing*, which is the hardest kind of report to act on. |
| `grounded` published by neither | nothing; a dead field. |

**Where the fix belongs:** they are fixed in their own commit, separate from any stage
(`1d0f3ab9`), with their own guard, because folding a user-visible determinism bug into a feature
stage is how it becomes invisible in a changelog. The work that *found* them was Phase B.B; the
fix is not Phase B.B.

## Context

`entity::LocomotionState` is the whole of what the animation tier receives from the simulation. It
is published from **two** functions: `EntityWorld::update` while the timeline runs, and
`EntityWorld::seek` when somebody scrubs. ADR-360 requires that those agree.

Phase B needed `MotionContext` to read the seam, so the question "which of the two paths writes
each field" got asked for the first time. **Two of the three answers were wrong**, and a third
field was written by neither.

| field | `update` | `seek` | symptom |
|---|---|---|---|
| `velocity`, `facing` | **no** | yes | zero velocity for the whole of live play; correct while scrubbing |
| `action` | yes | **no** | a sitting body stands up and walks when scrubbed to |
| `grounded` | **no** | **no** | dead field, read by nobody |

Measured: a body travelling north-east at 3 m/s reports `velocity` **2.12, 2.12** at
`Entity::state()` and **0, 0** at `Entity::locomotion()`. An action publishing activity `"Sit"`
during play publishes `""` after a seek to the same instant — and because
`Composition::AnimationSink::setLocomotion` picks the clip from `action` first and the gait second,
that is a visibly different character on a scrubbed frame.

`seek` replays the action tier correctly — ADR-267 defect 3 had already taught it to — and then
**discards the result with a literal `(void)`**. The simulation was right; only the publication was
missing.

## Why it survived

Every velocity test asserted on `Entity::state()`, which is the tier that **computes** the value.
None asserted on `Entity::locomotion()`, which is the struct that **carries** it. The measurement
was tested thoroughly and the delivery was tested nowhere.

This is ADR-553's shape exactly: impeccable numbers written to somewhere nothing reads. In both
cases the gate that would have caught it is the same one — *assert on what the consumer receives*.

## Decision

1. **Every field of a published seam is written by every path that publishes it.** Not "kept in
   sync by convention": a field added to `LocomotionState` must be assigned in both `update` and
   `seek`, and `grounded` is now published (from `EntityState::airborne`) rather than left dead,
   because a foot placement layer will need it.
2. **The guard compares the struct field by field**, not the three fields that happened to be
   wrong. `"play and scrub publish the same seam, field by field"` names each one, so the next
   field added is covered on the day it is added rather than on the day it breaks a render.
3. **A test for a seam asserts on the seam.** A test that asserts on the tier upstream of it is
   testing a different thing, and this ADR is what that costs.

## Consequences

* `LocomotionState` gains `dt`, which `MotionContext` needs and only the entity tier knows.
  Deriving it downstream by differencing `time` would be memory in the composition, which is
  exactly what ADR-359 moved the ground smoothing upstream to avoid.
* One wrong assertion is recorded in the test rather than deleted: the first draft required
  `speed > 0`, and `speed` is *intent* while `velocity` is *measurement* — ADR-545's distinction —
  so a director-driven body correctly has zero intent beside a real velocity.
* **Revisit if** a third publish path appears. The right answer then is one function both call, not
  a third copy of the list.
