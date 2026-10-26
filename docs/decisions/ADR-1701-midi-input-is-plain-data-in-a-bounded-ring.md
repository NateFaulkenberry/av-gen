# ADR-1701: MIDI input is plain data in a bounded ring, with a device table and removal in order

- Status: Accepted (eng/midi-mapping, 2026-10-25)
- Code:
  - `src/control/midi.hpp`: `MidiMessage` (POD, `device` slot, `param`, `flags`), `MidiDeviceSlot`,
    `MidiInput::devices/deviceGeneration/injectRemoval`, `MidiKind::DeviceRemoved`.
  - `src/control/midi_backend.hpp`, `src/control/midi_common.cpp`: `MidiInbox` (ring, overflow table, device table).
  - `src/control/midi_coremidi.cpp`: slots from `kMIDIPropertyUniqueID`; removal and close queue `DeviceRemoved`.
- Tests:
  - `tests/unit/test_midi.cpp`: the full policy, a lost note releasing every gate, the device table, unplugging and
    re-plugging a virtual source.
  - `tests/integration/test_midi_mapping_engine.cpp`: the flood tests.

## Problem

A message carried its device as `std::string source`, the endpoint name. That caused two problems:

- **Allocation.** The name could allocate per message.
- **Identity.** Two devices with one name were one device, and the CoreMIDI unique id was read and thrown away.

The inbox was a `std::deque` that evicted the *oldest* message when full. That could evict a note-off and leave a gate
stuck. And nothing downstream learned that a device was unplugged.

## Decision

- **A message is POD.** It carries a 16-bit slot into the input's device table: id, name, connected, virtual.
- **Slots.** A slot is never reused. The same id reconnecting gets its old slot back. Two connected sources with the
  same id get `id#2`, `id#3`.
- **Ids.** CoreMIDI's unique id is the id. An injected source is `inject:<name>`.
- **The inbox is a ring** allocated once (`setQueueLimit`, default 4096). A push copies under a short mutex on the
  CoreMIDI thread; the audio callback is not involved.
- **Full policy:**
  - Continuous messages (CC, bend, pressure) stop entering the ring at 3/4 full. They go to a 64-entry table keyed by
    device, channel, kind and number, where the latest value wins. A stall therefore keeps every control's final
    position.
  - Notes, program and transport may use the whole ring.
  - A note that finds it full is dropped and counted. The next drain then ends with a `DeviceRemoved` for
    `kAllMidiDevices`, so every gate is released.
- **Removal.** Unplugging a source, or closing the input, queues a `DeviceRemoved` for its slot behind its last
  messages, and marks the slot disconnected.

## Rationale

The brief requires bounded queues, no allocation per event, a defined full policy, no unbounded accumulation of
obsolete CCs, preserved note order, and no stuck notes.

At 30 fps a frame drains the ring, so the bound is reached only by a stalled engine. The policy is chosen for that
case: final positions and note balance matter, intermediate knob positions do not.

## Consequences

- `MidiInput::Stats::lastSource` is gone. Stats gains `coalesced` and `droppedNotes`.
- `parseMidiBytes` takes a device slot instead of a name.
- The pitch-bend centre change and the channel pressure fix are ADR-1703's.

## Rejected alternatives

- **A lock-free MPSC queue.** Rejected: CoreMIDI delivers on one thread, the critical section is a copy, and the
  measured worst inject under a four-thread flood was well under a millisecond (`test_midi_mapping_engine`). A
  lock-free queue would add complexity for no measured gain.
- **Coalescing every frame.** Rejected because pickup's crossing test and the monitor want each message, and the
  mapping cost per message is about 0.1 µs.

## Revisit triggers

- A device that sends faster than one ring per frame in normal play. None is known; DIN carries about 1,000 messages a
  second.
