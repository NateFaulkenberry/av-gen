# ADR-1702: NRPN and RPN are decoded from the CC stream, and Learn takes the decoded parameter

- Status: Accepted (eng/midi-mapping, 2026-10-25)
- Code:
  - `src/control/midi_nrpn.{hpp,cpp}`: `NrpnDecoder`.
  - `MidiKind::Nrpn/Rpn`, `kMidiHiRes`, `kMidiNrpnPart`.
  - `MidiBindKind::Nrpn/Rpn`.
  - `ControlHub::applyMidiWith` feeds the decoder, then applies the raw message and the decoded one.
  - `MidiLearn::learnable` skips sequence CCs.
- Tests:
  - `tests/unit/test_midi_mapping.cpp` `[adr1702]`: 14-bit, 7-bit, RPN, null RPN, data entry without a selection,
    interleaved channels and devices, a new parameter starting over, a device reset.
  - `tests/unit/test_midi_learn.cpp`: Learn takes the NRPN.
  - `tests/integration/test_midi_mapping_engine.cpp`: an NRPN knob drives a mapping through the engine.

## Problem

Many synths send their knobs as NRPN: CC 99/98 select a parameter, then 6/38 carry the value. Before this, AV Gen saw
four unrelated CCs per change. Learning one knob would have captured CC 99.

## Decision

Per device and channel:

- 99/98 select an NRPN and 101/100 an RPN. RPN 127/127 deselects. A select part not yet sent counts as 0.
- CC 6 emits `value14 = MSB << 7`. CC 38 emits it with the LSB and marks the parameter 14-bit. CC 96/97 step the value
  by one unit of its resolution.
- **7-bit until an LSB arrives**: normalised as `MSB / 127`, so a 7-bit synth's 127 is 1.0. Selecting a different
  parameter starts over.
- Data entry with nothing selected is an ordinary CC. Other CCs pass through and do not disturb a selection.
- The sequence CCs still reach plain CC mappings, which preserves existing behaviour. They are flagged
  `kMidiNrpnPart`: Learn never captures them, the monitor greys them, and a select CC that completes nothing counts as
  neither applied nor unmatched.

## Rationale

The MIDI 1.0 data-entry rules, with a resolution rule that does not invent bits a device never sent. It is a pure
per-channel state machine fed on the engine thread, so it needs no locking.

## Consequences

- A mapping can name `nrpn` or `rpn` with a 14-bit `number`.
- RPN 0/1/2 are labelled in the monitor because the MIDI specification defines them. NRPN numbers are not, because
  each manufacturer assigns its own.

## Rejected alternatives

- **Waiting for the LSB before emitting.** Rejected because a 7-bit device never sends one, and its knob would do
  nothing.
- **Hiding the sequence CCs from plain CC mappings.** Rejected because it would silently change existing bindings on
  CC 6.

## Revisit triggers

- A device that sends the value LSB before the MSB. The spec allows it; none is known to do so.
