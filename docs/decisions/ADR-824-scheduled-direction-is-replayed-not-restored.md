# ADR-824: Scheduled direction is replayed, not restored; a body can be released and given a goal

**Status:** Accepted
**Date:** 2026-09-25
**Supersedes, in part:** the tier-2 note in `seq/events.hpp` ("not scrub-safe ... restore the standing
intent", ADR-093), for `EntityAction` events that this engine can apply itself
**Related:** ADR-091 (tiers), ADR-093 (restored intent), ADR-700 (checkpoints), ADR-671 (a scrub
replays the director), ADR-758 (performers use the same hook); Phase D §35
**Implemented by:**
- `Composition::Directive`, `setDirectives` and `applyDirectives`, applied beside `applyPerformers`
  on a play and in both replay paths, with each directive's signature in `replayInputKey`;
- `Engine::installDirectives`, and `directedEvents_`, which keeps the host from giving the same
  order twice;
- `EntityWorld::release` and `setGoal`, and `entity::DirectorGoal` (entity state);
- `GoalConsiderer` reading a runtime goal;
- the section verbs `release` and `goal`.

**Tests:** `tests/unit/test_scheduled_direction.cpp` (`[motion][direction]`)

## Context

A section action, or any timed `direct` a Director compiles, gives an entity an order at a known
second. The engine used to deliver these after the frame, from the event dispatcher.

A seek could only **restore the standing intent**: it re-delivered the latest order per target and
dropped the rest. Before checkpoints that was honest, because rebuilding the orders a play had given
meant re-simulating history nobody kept.

The Phase D recount found the consequence for a Director. Orders issued over time are not
scrub-exact:
- a scrub after a `move` then a release lands on a different body;
- a render started mid-film differs from a full one.

There was also no world-level way to hand a body back, and no runtime way to give a character a goal
(§35 "partial").

## Decision

- **Scheduled `EntityAction` events become composition inputs.**
  - Every tier-2 `EntityAction` with a verb this engine knows and a subject that exists becomes a
    `Directive`.
  - Directives are applied at their second on the step whose `(now − dt, now]` contains it. A
    zero-length step owns only its own instant, and re-giving an order at the same second leaves
    the queue as it was.
  - They are applied at the performers' point of the step, on play and replay alike. Their
    signatures are in the replay key, so a changed schedule drops stale checkpoints.
  - The engine no longer `direct()`s those events. Verbs a host owns, subjects nothing here is
    called, and live-tier events stay with `firedEvents()` exactly as before.
- **`release`** drops the Director tier, so the body resumes the tiers below it, and clears any
  runtime goal.
- **`goal <subject>[.<affordance>]`** (§35) gives the character a `DirectorGoal`, stored as entity
  state.
  - It fills the character's `goal` considerer, which can be authored with no subject as an empty
    slot, in place of the authored subject, open from the second it is given.
  - The character still decides the how (path, approach, affordance) and still weighs the goal
    against everything else it wants. It is a bias, not an order.

## Consequences

- **Timing.** A directive now acts on the step at its second. It used to act one frame later, after
  the host saw the fired event. That one-frame change applies to play and scrub alike.
- **Scrub-exact on the composition harness.** A body mid-order at 3.5 s and after the release at
  8 s lands where the play did, to 1e-5 m. A warden sent on a runtime goal at 20 s matches at 60 s.
- **Live-tier `EntityAction` events stay as they were.** These are triggered by a volume or an
  action's completion, so their time is not knowable from the playhead, and they are not
  scrub-exact.
- **A scene with directives replays every body deep,** as a scene with staging or performers
  already does.
