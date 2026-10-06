# ADR-1124: Scene states read the bus after the sources

- Status: Accepted (gpu/productionization, the flagship LIVE scene)
- Amends ADR-031 (scene states). Found by: PHONOTAXIS, whose MIDI pads move between scene states.
- PHONOTAXIS was dropped by the owner on 2026-10-05 and its scene deleted; this capability stays.

## Problem

`docs/scene-states-and-macros.md` and the control docs say a MIDI pad bound as `noteEvent` (a one-frame pulse
on `control.<channel>`) can trigger a scene state through a `signal` trigger. It never could. The frame order
was:

1. `controlHub_.update` queues the pad's pulse in the control source;
2. `states_.update` reads the bus;
3. `sources_.update` writes the pulse to the bus (`ControlSource::update` -> `setEvent`);
4. `bus_.clearEvents()` at the end of the frame zeroes the pulse.

The state machine ran at step 2, before the pulse existed. By the next frame's step 2 the pulse was gone. Only a
value held across frames, such as a CC, a held `note`, or an OSC `/signal`, could change a state. The integration
test used a held value, so the event path was never exercised.

Found on the live path. A scripted controller's eleven pad hits all reached the engine (`notesReceived` 11), and
none of them moved a state. The states that did change were moved by the music's energy trigger.

## Decision

The scene state machine updates after `sources_.update`, and before `params_.resetFinals()`. Two things
follow:

- It reads this frame's events: control pulses, envelope triggers, and the interpreter's and macros' outputs.
- A transition's preset morph still lands on the parameter bases, which the routes then modulate, as before.

`state.progress` and `state.index` are written right after, so a source that reads them sees the previous
frame's values. That one-frame lag already applied to everything the sources publish.

## Consequences

- MIDI pads (`noteEvent`) and every other one-frame event can trigger a state. Test: `A MIDI pad bound as
  noteEvent changes the scene state` (`tests/integration/test_states_engine.cpp`) injects a pad hit and its
  release in one MIDI packet. It fails on the old order (the transition never becomes pending) and passes on
  the new one.
- Macro and interpreter signals reach a state trigger in the same frame that produced them, one frame earlier
  than before.

## Rejected alternatives

- **Latch events on the bus until the next frame.** This changes the meaning of an event for every reader
  (envelopes, routes, the Critic's traces) to fix one reader.
- **Ask performers to bind pads as `note`** (held values). A drum pad sends note-on and note-off within
  milliseconds, so a held value can still fall between frames.
