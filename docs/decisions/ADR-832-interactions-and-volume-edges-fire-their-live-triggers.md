# ADR-832: Interactions and volume edges fire their live triggers

**Status:** Accepted
**Date:** 2026-09-25
**Answers:** Phase D §26 ("none of the spec's event kinds has a producer"), and the Director's use of
the sequence's live trigger kinds
**Related:** ADR-828 (`ActionComplete`'s producer), ADR-098 (a scrub does not re-fire a live event)
**Implemented by:**
- `ActionEvent::interaction`, filled by `ActionQueue::emit` for an `Interact`;
- `Engine::postCharacterEvents`.

**Tests:** `tests/unit/test_character_goal.cpp`, "an interaction and a volume edge fire their live
triggers at the second they happen" (`[adr832]`). It fails without the producers: all three
notifications stay at -1. It passes with them.

## Decision

`seq::TriggerKind` has had four live kinds since the sequencer was written. ADR-828 gave
`ActionComplete` a producer. This ADR gives producers to the other three:

- **`InteractionComplete`**, named `"prop.verb"` (`"mushroom-2.inspect"`), with the entity as
  subject. Every completed `Interact` posts it, whether or not the author named an `onComplete`. The
  name is the interaction's own, so "when Rook has sat on the stump" is
  `{interactionComplete, "stump.sit", "rook"}` with nothing authored on the action.
- **`VolumeEnter` / `VolumeExit`**, named after the field, with the entity as subject. These come
  from the field pass's `TriggerEvent` edges. Those were recorded "for whoever wants edges" and,
  until now, nobody read them.

Both are posted from the step's own record, at the simulation second, before the dispatcher drains.
That is the same place and rule ADR-828 uses. A scrub does not re-fire them (ADR-098).

## Measured

On the autonomy demo, with a goal sending the warden to `mushroom-2` and a 3 m field around the
mushroom:
- the warden enters at 47.98 s, and the `volumeEnter` event fires at 47.98 s;
- she inspects at 53.35 s, and `interactionComplete` fires at 53.35 s;
- she leaves at 54.97 s, and `volumeExit` fires at 54.97 s.

All three match to the second.

## Not done

§26's other kinds have no producer yet: spawn and remove, world-effect start and end, and a world
event for "entered area" that *characters* perceive. Raising volume edges or interaction
completions as world events would make every character within earshot perceive them, which would
change every film that has a field or an interaction. That is its own decision.
