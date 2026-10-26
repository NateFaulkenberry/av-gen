# ADR-1703: A channel pressure binding reads its pressure; pitch bend's centre is exactly 0.5

- Status: Accepted (eng/midi-mapping, 2026-10-25)
- Code: `src/control/control_map.cpp` (`midiSourceValue`), `src/control/midi_common.cpp` (`MidiMessage::normalized`).
- Tests:
  - `tests/unit/test_midi_mapping.cpp` `[adr1703]` and the decoding case;
  - `tests/unit/test_midi.cpp`: normalised values.

## Problem

**Channel pressure.** The parser stores channel pressure in `data2` (there is no number), but `matchMidi` read
`data1 / 127`. Every `pressure` binding produced 0, whatever the player did. This is the silent no-op family. No
tracked project used the kind, so nothing showed it.

**Pitch bend.** Bend normalised as `value14 / 16383`, so the wheel at rest (8192) read 0.50003. A symmetric parameter
range then rested slightly off its middle.

## Decision

- Channel pressure is `data2 / 127`.
- Pitch bend scales each half on its own: `8192 → 0.5`, `0 → 0`, `16383 → 1`.

## Consequences

- A `pressure` binding now works.
- Pitch-bend bindings move by at most 0.00003 at the centre.

## Revisit triggers

None.
