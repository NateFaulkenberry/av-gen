# ADR-1782: the file grid follows a tempo path and sits on the low register

- Status: Accepted (eng/beat-clock)
- Code: `src/analysis/beat_tracker.cpp` (`trackBeatsOffline`, `localBpmAt`), `src/analysis/analysis_track.{hpp,cpp}`
  (`trackGrid` takes the low strength), `src/app/engine.cpp` (`advanceClock`: `beat.bpm` is the grid's local tempo).
  `tools/audio_lab/make_fixtures.py` (the dense fixture on its own grid).
- Tests: `tests/unit/test_beat_tracker.cpp` `[adr1782]` (120 -> 96 followed, local tempo read), the Rebuild tests
  (ADR-896's 30 ms), the lab's file grid gates (ADR-1785).

## Problem

The file grid had one tempo for the whole file (the dynamic fixture's 120 -> 96 step read 95.9 throughout), read the
syncopated fixture at 144.6 BPM, and scored F 0.0 on the dense fixture with the right tempo.

The dense fixture's F 0.0 was the fixture: its sections started at whole seconds, which are not on a 128 BPM grid, so
from 12 s its kicks sat 0.6 of a beat off the manifest's intended beats. The tracker followed the kicks.

## Decision

- **A tempo path.** ADR-1780's score over 8 s windows every 1 s; a Viterbi path through the windows (evidence: a
  window's normalised score times its confidence; a change costs 1.5 windows of evidence per octave). The DP (Ellis 2007)
  uses the path's local period at each hop.
- **The low register sets the phase** where it is confident of the path's period (confidence 0.3-0.5 fades it in), then
  each beat moves to the whole-band peak within an eighth of a period (the attack, not the bass body's later rise). Edge
  beats more than 8% off the local period, and leading or trailing beats under 0.4 of the mean local score (a reverb
  tail's small level-free rises), are trimmed.
- **Tempo.** `OfflineBeats::tempoBpm` is the median of the beat intervals each measured over eight beats (one interval
  is a whole number of hops). `beat.bpm` on a file is `localBpmAt`: the median of the eight intervals around the
  playhead. `audio.tempo` and `audio.tempoConfidence` stay the whole file's.
- **The dense fixture** starts each section's pattern on its own grid (generator version 2).

## Consequences

- File grid F: syncopated 0.28 -> 0.93, dense 0.0 -> 0.99, dynamic 0.80 -> 1.00, both tempo segments read.
- The grid is still one path through the file: `audio.tempoConfidence` is one number and has no lock state (ADR-1724).
