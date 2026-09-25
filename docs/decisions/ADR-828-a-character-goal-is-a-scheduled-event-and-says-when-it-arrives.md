# ADR-828: A character goal is a scheduled event, and the character says when it arrives

**Status:** Accepted
**Date:** 2026-09-25
**Answers:** the Director's interface request in its ADR-763 (Slice 4), items 1–5
**Related:** ADR-824 (scheduled direction replayed), ADR-700 (checkpoints), ADR-800 (seek parity),
ADR-098 (restored firings); Phase D §26, §35; Phase F7
**Implemented by:**
- `seq::EventActionKind::CharacterGoal` and `EventAction::GoalSpec`;
- `entity::DirectorGoal` (subject, affordance, since, until, intent, activity, approach, dwell);
- `EntityWorld::setGoal`;
- `GoalConsiderer` reading the full runtime goal and naming `goal.arrived` / `goal.done`;
- `Engine::installDirectives` for `CharacterGoal`, and `Engine::postCharacterEvents`;
- `Composition::clipReadout` and `AnimationPlayer::currentSpeed`.

**Tests:**
- `tests/unit/test_character_goal.cpp` (`[motion][goal]`);
- `tests/unit/test_scheduled_direction.cpp` (the Engine-seek arm).

## Decision, item by item

1. **`CharacterGoal` event (F7).**
   - Its fields: target is the entity, value the subject, argument the affordance, and `seconds` how
     long the goal stands (0 = until replaced or released). A `goal` block carries intent,
     activity, approach and dwell; an unset field falls back to the considerer's own value.
   - It is **scheduled**: it becomes an ADR-824 directive, given at its second on a play and in the
     replay, with its signature in the replay key.
   - It lives in the sequence, owned by the plan. It is **not** written into the entity's
     behaviours.
   - It fills the character's `goal` considerer. A character needs one in its decider; authored with
     no subject, it is an empty slot.
   - **`weight` stays the considerer's registered parameter,** so it can be keyframed. The event says
     *what* the character wants, and the weight says *how much* it weighs against everything else.
     Putting it in the event would make two answers to one question.
2. **Semantic character events.**
   - The goal's own actions carry `onComplete` names: `goal.arrived` when the walk ends, and
     `goal.done` when the errand is over.
   - These, like every named action completion, are raised as world events on a play and in the
     replay alike. That machinery already existed, and a scrub reproduces the record exactly.
   - They are also posted to the sequence's `EventDispatcher` as `ActionComplete`, with the event
     name as the trigger's name and the entity as its subject, at the simulation second. The
     Director's `rook.goal.arrived` is `{name: "goal.arrived", subject: "rook"}`.
   - `ActionComplete` had no producer before this. `InteractionComplete`, `VolumeEnter` and
     `VolumeExit` still have none (not done).
3. **Clip readout.** `Composition::clipReadout(node, now)` returns:
   - the state;
   - clip seconds from the clip's first frame;
   - the speed;
   - the phase origin a cue would reproduce;
   - whether it loops or has finished;
   - the state a cross-fade is leaving.

   It is read-only.
4. **The goal in the checkpoint.**
   - `DirectorGoal` is entity state, and the considerer's memory (`investigatedAt`) is inside the
     decider. A checkpoint copies both whole (ADR-700).
   - Proved by a scrub that restores a checkpoint from inside the goal's window.
5. **`restored`.**
   - For the orders this engine applies itself, the replay gives them at their seconds (ADR-824), so
     the host no longer delivers the restored firing at all.
   - Proved through `Engine`: a seek into a move does not give it again. With the skip removed, the
     body lands elsewhere.
   - Orders the engine cannot apply stay with the host as before: a verb a host owns, or a subject
     nothing is called.

## Measured

- In the autonomy demo, the warden is given `mushroom-2.inspect` at 20 s. It arrives at 49.33 s,
  and the sequence's arrival trigger fires at 49.33 s.
- Scrubs through `Engine` at 30 s, 45 s (including from a checkpoint) and 90 s land where the play
  did, with the same world-event record.
- Each mechanism was broken and seen red: the bridge, the goal directive in the replay, and the goal
  in the checkpoint.
