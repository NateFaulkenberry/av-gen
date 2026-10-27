# The Audio Response Lab

The lab checks AV Gen's audio analysis and response against music whose answers are known. It is a diagnostic scene,
a fixture generator and a scorer. The ADRs are 1720-1726, the plan and its thresholds are in
`docs/prototypes/audio-response-lab/01-plan.md`, and the measured state is in `03-final-report.md`.

## The scene

File > Examples > Live > **Diagnostics** > Audio Response Lab (`examples/audio-response-lab/`). It is written by
`tools/audio_lab/make_lab_scene.py`; edit the script, not the JSON. It is not in the live scene switcher (ADR-1722).

Every indicator is a route from a production signal, so it shows what the bus published, on that frame.

**A Energy:**
- `response.level`, `response.bass`, `response.mid` and `response.high` (ADR-1723);
- the slow `audio.midLevel` and `audio.trebleLevel`, as thin grey bars;
- `response.sustain` and `response.onsetEnv`.

**B Events:** lamps for `response.onset`, `response.low`, `beat.pulse` and `response.note`, plus the onset register.

**D Beat:**
- the phase marker (`beat.phase`) and the bar marker;
- CONF (`audio.tempoConfidence`) and BPM;
- three lamps for the clock source (`beat.source`, ADR-1724). **FILE GRID: NO LOCK STATE** means the dial is showing
  a whole-file estimate, and its confidence cannot say whether it is locked. The manual clock (`beat.source` 4,
  ADR-1783) has no lamp: the lab never sets a tempo override.

**C Sources:** one column per key: `drums`, `bass`, `synth` and `pad`. Map live input sources or stems with those
keys.
- A red lamp means the source is not configured or not sending.
- A green lamp means it is active.

**E Probes:** real route chains: the bass sphere, the 120 ms onset flash, the brightness colour, the bass-stem cube
and MIDI CC 1 (`control.labCC1`).

**An absent signal reads as absent:** empty meters, the red lamp, no substitution.

## Running the suite

```
cmake --build --preset release                 # also generates the fixtures into build/release/test-fixtures/audio-lab
python3 tools/audio_lab/lab.py run             # all categories: traces, the causal replay, scores
                                               #   -> build/audio-lab-run/{summary.md,results.json,<fixture>/...}
python3 tools/audio_lab/transfer.py --generated-root ../av-gen    # cross-scene transfer (Event Horizon, Rainbow World)
python3 tools/audio_lab/compose.py build/audio-lab-run/<fixture> --render --out clip.mp4   # synchronised clip
tools/audio_lab/loopback.sh build/release/test-fixtures/audio-lab/<fixture> out/   # the live path on BlackHole
```

**Every `avgen` goes through `tools/gpu-lock.sh`.** The scripts do this themselves, even for a GPU-free trace.

In the test suite, `tests/integration/test_audio_lab_e2e.cpp` (`[audiolab][e2e]`) runs each category in-process, plus
the real-drum fixture when the samples are present. Every gate must pass.

**Paths a result can come from:**
- **offline (file):** `avgen --sonic-trace` at the hop clock (93.75 fps, `hop.csv`) and at 30 fps with every route
  final (`frame.csv`). This is the engine's own update sequence, so render frame N is row N.
- **live analysis:** `avgen_audio_lab_replay` (ADR-1725) runs the same `Analyzer`, `BeatTracker` and causal onsets that
  the live runner does, hop by hop, then the runner's carry at 30 fps (`live.frame.csv`). It also writes the file grid's
  beats (`live.grid.csv`: `AnalysisTrack::analyze`, one row per beat with its local tempo), for scoring a tracker change
  without an engine trace.
- **live device:** `loopback.sh`. The probe plays the fixture into BlackHole, AV Gen captures it at 1080p30, and the
  two logs are joined on the host clock.

## Adding a fixture

1. In `tools/audio_lab/make_fixtures.py`, write a `make_<name>()` that builds a `Fixture`:
   - add each sound with `fx.add(source, time, segment, kind, register, ...)`;
   - set `onset=False` for something that is not an attack;
   - set `ambiguous=True`, with a `note`, for something no detector owes you;
   - pass `attackSeconds` for anything with a slow attack: 100 ms or more is a swell, and is flagged automatically.

   Add beats with `fx.grid([...])` and labelled sections with `fx.sections`. Register it in `CATEGORIES`.
2. The manifest is written for you. Event samples, registers and silences are **measured from the audio written**,
   never taken from what you meant.
3. If the category needs its own checks, add them in `lab.py`'s `checks()`:
   - `gate=True` for a property the system promises;
   - `gate=False` for a measured finding.

   Give every check its bound and the reason for it, in the implementation's own terms (the window, the hop, a
   documented release).
4. Add a `TEST_CASE` to `test_audio_lab_e2e.cpp`. **Never commit audio.** Generated audio goes in the build tree.

## Reading a result

**`summary.md`** has a table per fixture:
- each detector's precision, recall, timing median and p95 (negative means published before the onset sample, which
  the centre-stamped file path can do by up to half a window), missed, duplicate and false;
- false events per minute in measured silence and in held notes;
- the band levels per section;
- the beat clock and the live tracker: F-measure, tempo per segment, lock time and losses, confidence in silence;
- per source: attribution, leakage and the drum-class confusion;
- every check, as PASS, FAIL or FAIL (finding).

**`results.json`** has every raw list: errors in ms, the missed and false times, the beat-by-beat lock table.

**What a failure usually means:**

| Failure | First suspicion |
|---|---|
| Onset recall drops | A detector or threshold change. Check `missed[].kinds` |
| A false event in silence | A detector firing on nothing: look at its time; a file start is a reset (finding) |
| A mapping error over 0.001 | The scene's route changed, or a chain stage was added to an identity indicator |
| A stale value after silence | A follower's release grew, or a signal stopped being gated |
| A band-selectivity failure | A band's edges moved (`causal_onsets.cpp` `layout`) or the reference rules changed |
| A pixel check (`<clip>.pixels.json`) where lag 0 is not best | The render and the engine's update sequence disagree: frame N is not row N |
| A beat gate (ADR-1785) | `beat_tracker.cpp`: the tempo score or path (a 4:3 or octave tempo), the low-register phase (half a beat off), the silence gate (confidence or pulses in a silence). Check the fixture's own grid first: the dense fixture once started its sections off its beat |

**Timing:**
- On the hop clock, an onset is about 11 ms early (the window centre).
- At 30 fps it lands 0 to +21 ms after the onset.
- The live replay adds half a window: about 30-60 ms.
- The BlackHole loopback measured 32-53 ms (median) from sound to bus, and 36-56 ms to the present call.
