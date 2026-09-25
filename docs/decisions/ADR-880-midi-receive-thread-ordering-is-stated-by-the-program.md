# ADR-880: MIDI receive-thread ordering is stated by the program, not inherited from CoreMIDI

**Status:** Accepted
**Date:** 2026-09-25
**Related:** ADR-021 (MIDI input)
**Implemented by:** `gReceivePublications`, `publishForReceive`, `ReceiveGate` and
`CoreMidiInput` (`src/control/midi_coremidi.cpp`)
**Tests:** `tests/unit/test_midi.cpp`, the cases tagged `[control][midi][device]`, and
`[adr880]` ("MidiInput survives open, drain and destroy while a source is streaming")

---

## Context

A ThreadSanitizer run of `avgen_tests` (2026-09-23) reported data races in the MIDI input:
`parseMidiBytes` on CoreMIDI's receive thread writing a `MidiParserState` that another thread had
just constructed in `CoreMidiInput::Connection`, and the input's `bytes_` scratch vector and the
inbox's `std::deque` being destroyed on the main thread after the receive thread last wrote them.
The inbox itself was already mutex-guarded; every report was about the *edges* of the callback's
life: publication before the first callback, and retirement before destruction.

Both edges were ordered only inside CoreMIDI: a callback for a source runs after
`MIDIPortConnectSource`, and none runs after `MIDIPortDispose`. That ordering goes through the MIDI
server and CoreMIDI's own threads. The C++ memory model gives no happens-before from it, and
ThreadSanitizer cannot see it. Reproduced on main at `e995e3b9` with TSan: 19 warnings from the
two existing device tests, 23 from the new `[adr880]` stress case.

## Decision

**The program states both edges itself, with atomics, and keeps the callback non-blocking.**

- **Publication.** A process-wide, constant-initialised `std::atomic<uint64_t>` is incremented
  with release after a `Connection` (or a port's gate) is fully built and before it is handed to
  CoreMIDI. Every receive callback begins with an acquire load of it. Because the callback really
  does run after the connect, the load reads that increment or a later one, so construction
  happens-before the callback.
- **Retirement.** Each port gets a fresh `ReceiveGate` (an `accepting` flag and an `inside`
  count), owned jointly by the input and the port's receive block. A callback enters (seq_cst
  increment, then checks `accepting`) and leaves with a release decrement. `close()` disposes of
  the port, clears `accepting`, and waits (yielding) for `inside == 0` with an acquire load. A
  callback that CoreMIDI delivered late touches only the gate the block keeps alive, never the
  destroyed input.
- **Scratch per connection.** The byte and parsed-message scratch moved from the input onto each
  `Connection`, so it is never shared between sources and keeps its capacity: in steady state the
  callback allocates only for the inbox's deque growth and source names longer than the SSO.
- **The inbox stays a short mutex section.** Producers are the receive thread and `inject` from
  any thread, so it is multi-producer, and `MidiMessage` owns a `std::string`. The existing
  `SpscRingBuffer` (audio) is float-only and single-producer and does not fit. The critical
  section is bounded by the queue limit.

## Consequences

- TSan: zero warnings on `[midi]`, `[control]` and `[ai]`.
- `close()` can wait for at most the callbacks already inside, none of which block.
- The device tests name their virtual sources per process (`<name>-<pid>`). Concurrent suites in
  other worktrees no longer connect to each other's sources.

## Rejected alternatives

- **Trust CoreMIDI and suppress the reports.** The ordering is real, but it is invisible to the
  tools that check this code. A suppression would also hide a genuine lifetime bug introduced later.
- **Lock a mutex in the callback around the whole receive.** This works, but it makes the real-time
  thread wait on whatever the main thread holds.
- **A lock-free MPSC queue for the inbox.** `MidiMessage` would first need a fixed-size source
  identifier. That is a larger change than these races require.

## Revisit triggers

- The callback shows up in a real-time profile: moving the inbox to a fixed-size ring needs an
  interned source id first.
