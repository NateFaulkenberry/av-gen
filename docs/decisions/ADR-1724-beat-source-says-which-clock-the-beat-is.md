# ADR-1724: `beat.source` says which clock the beat is, and a file grid says it has no lock state

- Status: Accepted (eng/audio-response-lab; the owner approved it at Gate 1: "show the state honestly; do not fake
  acquire, lose or reacquire")
- Code: `src/app/engine.{hpp,cpp}`: `TimeSignals::clockSource`, declared last in `declareFrameSignals`, and set in
  `advanceClock`. The Audio Response Lab's dial lamps (`tools/audio_lab/make_lab_scene.py`).
- Tests: `tests/integration/test_sonic_trace_stems.cpp` `[adr1720]` (a file with an analysed grid reads 2);
  `tests/integration/test_audio_response_lab.cpp`.

## Problem

`beat.*` comes from one of three clocks: MIDI clock, the analysed file grid, or the live tracker (ADR-1581). Nothing on
the bus said which one. The file grid has one tempo and one confidence for the whole file, so `audio.tempoConfidence`
on a file cannot show acquisition, loss or reacquisition. A dial showing a high CONF bar on a file was presenting a
whole-file estimate as if it were lock.

## Decision

`beat.source` is 0 with no clock, 1 for MIDI clock, 2 for the analysed file grid and 3 for the live tracker. It is a
value, not an event.

**The tracker is not changed.** The owner takes the beat clock as separate work. The lab measures it:
- the file grid and the live tracker on the syncopated and tempo-change fixtures, through the causal replay
  (ADR-1725) and the BlackHole loopback;
- the numbers are in `docs/prototypes/audio-response-lab/03-final-report.md`.

## Consequences

- The lab's dial lights FILE GRID: NO LOCK STATE, MIDI CLOCK or LIVE TRACKER. A scene can gate anything that means
  "locked" on `beat.source == 3`.
- The signal is declared after every older id, so seek replays and saved buses keep their ids.
