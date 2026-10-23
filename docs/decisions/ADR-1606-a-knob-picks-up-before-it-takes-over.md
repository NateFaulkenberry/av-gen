# ADR-1606: A knob picks up before it takes over (MIDI soft takeover)

- Status: Accepted (eng/living-prism-p1, The Living Prism P1-12)
- Extends ADR-021 (MIDI bindings). Opt-in: a project that does not ask behaves exactly as before.
- Code: `src/control/control_map.{hpp,cpp}` (`PickupState`, `pickupAccepts`, `MidiBinding::pickup`,
  `ControlMap::midiPickup`, the JSON keys), `src/app/control_hub.{hpp,cpp}` (`applyMidi`, `currentNormalised`, the
  `pickupHeld` count); `docs/control.md`.
- Tests: `tests/unit/test_control_map.cpp` (`[adr1606]`: the rule -- far, crossing, following, let go, tolerance, a
  first message at the value; the keys round-trip and are absent when off), `tests/integration/test_control_engine.cpp`
  (`[adr1606]`: through the engine, a knob is held while far from a parameter a recall moved, picks up passing through
  it, follows, lets go when the parameter is moved again, picks up within the tolerance; without pickup it jumps).

## Problem

The audit (02 §4) found MIDI bindings that write a parameter's base jump it to wherever the physical knob sits the
moment it is touched. After a preset recall, a scene state or a project load moves that parameter, the first touch of
the knob snaps the picture -- in a performance, a visible glitch the performer did not ask for.

## Decision

Soft takeover, the "pickup" mode of most hardware and of TouchDesigner/Resolume: a continuous binding (`cc`,
`pitchBend`, `pressure`) with `"pickup": true` (or every one, with `control.midi.pickup`) applies nothing until the knob
reaches the target's current value -- within 2% of the binding's range -- or passes through it between two messages (a
fast knob steps over it), and then follows. When the target moves under it again (its value is no longer what the
binding last wrote), it lets go until the knob comes back. The rule is one pure function, `pickupAccepts`.

Parameter targets only: a control signal (`control.<ch>`) is written by its knob alone, so there is nothing to take over
from, and a signal-only binding is never held.

## Consequences

- A performer can recall a look and keep playing without jumps; the Control tab's status counts held moves
  (`pickupHeld`), so "the knob does nothing" is diagnosable.
- Off by default: a project opts in. The key is written only when on.
