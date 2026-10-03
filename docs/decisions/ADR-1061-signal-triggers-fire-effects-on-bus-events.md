# ADR-1061: Signal triggers fire the Effect Library on bus events, derived from the piece or recorded live

- **Status:** Accepted (2026-10-02), proto/sonic-garden (VFX expansion, brief §5-6)
- **Code:** `TriggerSource::Signal` in `src/world/effects/effect_timing.hpp`, `TriggerClock` in `effect_trigger.*`,
  `sonic::SignalEventDeriver` in `src/sonic/signal_events.*`, and `Engine::serviceSignalTriggers`.
- **Tests:** `tests/unit/test_signal_trigger.cpp` and the engine case in `tests/unit/test_sonic.cpp` (`[adr1061]`).

## Context

The Effect Library (ADR-702/703/716/719: Shockwave, Ripple, Lightning, Discharge, Bounce, Shake, Dissolve and the
rest) is the brief's §5 "entity effects". Its EVENT activation reads only the offline analysis track: beats, onsets,
musical events, markers, repeats and proximity. Live, nothing fired, and no trigger could name `notes.noteOn` or a
kick. The art agent was blocked on it.

## Decision

1. A seventh trigger source, **`signal`**: `{"source": "signal", "name": "response.kick", "threshold": 0.3}` fires
   on any bus EVENT signal whose strength reaches `threshold`. The threshold defaults to 0 for this source (any event
   fires); the panel field is `trigger/threshold`.
2. **The contract is unchanged:** `lastTriggers(t)` searches a per-signal event list backward and keeps no
   consumer state.
3. **Derived where the signal is a function of the piece** (offline renders and file playback):
   - `audio.onset`, `audio.beat` and `audio.onsetLow/Mid/High` come from the analysis frames, with the strengths
     `AudioSignals::publish` gives them;
   - every `sonic.*`, `response.*`, `notes.*` and `timbre.*` event comes from a scratch `SonicRuntime` stepped over
     the timbre track and published at each analysis frame (with no audio, at 100 Hz over the note track). That is
     the engine's own runtime and publish, so the list is the bus's events at the analysis frames' instants. One walk
     serves every name, cached on the inputs.
   
   A seek therefore finds the fronts a play reaches (tested: a fresh engine seeking to 3 s answers exactly what a
   play to 3 s did).
4. **Recorded otherwise:** live input (live Sonic or an input device), and any signal not a function of the piece
   (an LFO, a control, a beat-grid channel). The clock appends the bus's events once per frame at the frame's
   transport second. A backward jump forgets the events after it. The list is bounded at 4096 (halved when full).
   `silence()` tells the panel whether a list is derived ("never fires in this piece") or recorded ("no event yet").
5. A name is wanted from the moment a trigger asks about it. The host resolves it (derives or records) on the next
   frame. Changing the analysed track, the Sonic setup, or live on/off resets every list.

## Consequences

- Live, a Shockwave released by `response.kick` or `notes.noteOn` fires on the beat it hears.
- Offline, it is reproducible and seek-exact. Derived times are analysis-frame instants: up to one 10.7 ms hop after
  a note's start, which is when the bus itself publishes it.
- A recorded list starts empty after a seek to a time never played. That is "what has happened", the only thing live
  has, and the panel says so.
- The route auditor does not judge Signal triggers (their events come from the host).

## Rejected

- **A trigger that reads `bus.event(id)` at the frame** (an edge with no history): it is not a function of t, and a
  scrub could never rebuild a front's age.
- **Recording everything always:** a render that seeks would lose fronts that the derivation gives exactly.
