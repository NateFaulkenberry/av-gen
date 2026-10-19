# ADR-1490: The live profile measures the brief's matrix arms: projection off, MIDI counted, a scripted controller

- Status: Accepted (perf/resonator-live)
- Extends: ADR-1090 (the live profile), ADR-1025 (the live Sonic probe), ADR-1026 (the projection).
- Found by: the Resonator live-performance brief (`docs/prototypes/resonator-quality/07-brief-art-pass-2.md` §7.1),
  whose matrix compares each scene with audio and MIDI off and on, and with the projector off and on.
- Report: `docs/prototypes/resonator-quality/10-report-live-performance.md`.

## Problem

`--live-profile --mode live` always pressed Start projection, so the "internal only" arms (A, B, C) could not be run
in the same loop as the "projector enabled" arms (D, E). Running the editor without the flag instead renders at the
editor canvas's size (2880x1800 px on this display in the first frames, then whatever the layout gives), which is a
different number of pixels from the 1920x1080 projection, so the two arms would differ by more than the projection.

Nothing in the record said whether MIDI had reached the engine during the measured frames: `conditions.midi` is the
`--midi` flag, and a live scene's own `control.midi` filter (`*` on all three Resonator scenes) opens every source
anyway. A run "with MIDI" was indistinguishable from one without.

And there was no deterministic MIDI input. The engine has no MIDI-file player. `MidiInput::inject` is in-process
only. The live Sonic probe (`tools/sonic_live_probe.cpp`) creates a CoreMIDI virtual source that the editor connects
to like a keyboard, but its scenarios were Digital Ocean's pads and two CCs.

## Decision

1. **`--projection on|off`** (live mode, default on). Off: Start projection is not pressed, and the editor canvas
   is put in Output Frame mode at Native preview quality, so it renders exactly the project's output size (1920x1080
   on these scenes), the same pixels the projection arm renders. The record's `conditions.window` says
   `windowed (editor canvas, no projection)`. The profile does not wait for a projection window it did not ask for.
2. **The record counts MIDI.** The control hub's message and applied counters are read when measurement begins and
   ends. A note gives both counts and the number of connected sources:
   `midi: 1476 message(s) received and 1025 control message(s) applied while measuring, from 5 source(s)` (Cymatic World, 30 s; a message no binding consumes, such as a CC the scene does not map, is received but not applied).
3. **A `resonator` probe scenario**, `avgen_sonic_probe resonator --device BlackHole --record-seconds <s>`. It repeats
   a 30 s pattern. Two of CC 1-8 and 11 sweep at a time on 6 s triangle waves, at 25 messages a second each, like an
   encoder. CC 9 steps the palette every 10 s, CC 10 toggles the lock every 30 s, and note 36 (Cymatic World's pad
   strike) plays bursts of four at 4 Hz every 2 s. Every event is a function of its time on the probe's clock, so
   two runs send the same sequence. The only variable is where the profile's measured window falls in it.
   The synth's audio goes to BlackHole and is not heard by these runs, because a `--audio` file wins over the live
   input (ADR-1408).

## Consequences

- One command per arm, and all five arms come from one build. For example:
  `avgen --live-profile --mode live --projection off --project <scene> --audio <wav> ...`, with the probe started
  first for the MIDI arms.
- **Audio off on a live scene is a silent live input, not "no audio".** A `sonic.live` project opens the machine's
  capture device when no file is given. On this machine that is BlackHole, with nothing playing. The analysis thread
  runs, but every band is silent. The record says `audio: off (the audio file was not played)`.
- **A projector cannot be measured here unless one is connected.** The projection window then opens on whatever
  display the projection settings choose. On this machine that is the one LG UltraFine at 60 Hz, which also shows
  the editor. The report says so for every number.
- Tests: `live profile: --projection off measures the editor alone (ADR-1490)` (`tests/unit/test_live_profile.cpp`).
