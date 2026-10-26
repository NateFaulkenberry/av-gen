# Audio Response Lab 01: audit and plan

The owner's brief is `00-brief.md`. This is checkpoint 1: what exists, what can be tested, what needed instrumentation,
the clock conventions, the fixture set and the threshold reasoning. The Gate 1 results are in `02-gate1-report.md`.

Branch `eng/audio-response-lab`, cut from main f8d15dad. ADRs 1720-1722.

## 1. What exists (verified against the code on f8d15dad, not taken from the audits)

The prior audits are the Living Prism's `02-audio-modulation-audit.md`, its `03-integrated-report.md` and
`17-stem-analysis-milestone.md`. Most of their §1-§2 still holds. The rows below either re-check them in the code or
say plainly that a claim was not re-checked.

| Area | Code | State on f8d15dad |
|---|---|---|
| Mix analysis | `analysis/analyzer.*`: STFT with a 2048-sample Hann window and a 512-sample hop | Each `AnalysisFrame` is stamped at its **window centre** (`frameIndex`, `timeSeconds`). Five auto-gained bands (`audio.bass…treble`); fixed-scale band levels (`audio.<band>Level`: 0 = -60 dB, power smoothed over `levelSmoothingSeconds` = 1 s); a linear-flux `audio.onset`; the onset register (ADR-1604). |
| File path | `analysis/analysis_track.*`; `sonic/sonic_runtime.*`; `SonicSetup::analyse` | The whole file is analysed at load. Each render frame consumes every analysis frame whose **centre is at or before t**. The timbre pass (which holds ADR-1060's causal onsets) runs once at load. The response model then steps on the hop clock. |
| Live path | `analysis/analysis_runner.*` (thread, `BeatTracker`, `CausalOnsetDetector`); live Sonic tap | Uses the same `Analyzer` and the same causal onset code. ADR-1025 measured the threaded live timbre path equal to the file path to 1e-6. **The beat is different:** the file path uses an offline DP grid over the whole file (`trackGrid`), while live uses the causal `BeatTracker`. |
| Response model | `sonic/response.*` (ADR-1062/1510) | `response.low`, `response.onset` and `response.note` are events, each with an `*Env`. The levels `bass`, `level`, `sustain`, `levelSlow` and `intensity` are dB followers against an adaptive reference, gated at -60 dBFS. The onset register is `response.onsetLow/Mid/High`. **No hit names an instrument** (`kHitNames` = low, onset, note; ADR-1620). |
| Sources | `audio/stem_source.*`, `analysis/stem_analysis.*`, `signals/stem_signals.*`, `analysis/channel_analysis.*` | `stem.<key>.*` and `role.<role>.*`. **kick/snare/hat are declared only on a `drums`-role source** (`isClass` / `classifiesHits`, ADR-1630). A source outside its span, absent or stalled reads `active` 0 and all values 0. |
| Beat clock | `app/engine.cpp` `advanceClock`; `signals/beat_grid.*` | `beat.*` comes from MIDI clock, else the analysed grid (mix or `feeds.beat`), else the live tracker. **On the file path the grid has one tempo and one confidence for the whole file** (`analysis_track.cpp` l. 246-248: every frame gets `beats.tempoBpm` and `beats.confidence`). So `audio.tempoConfidence` cannot show acquisition, loss or reacquisition there. The live `BeatTracker` now drops confidence to 0 when its window holds too few onsets (`beat_tracker.cpp` l. 389-391), which the audit's "holds 0.81 through silence" predates. |
| Conditioning and mapping | `params/processor.*`, `params/modulation.*` | One route chain. The ops are Replace, Multiply, Add, Min and Max, applied in that order. `route.lastOutput` is the mapping output; the parameter final is what the scene reads. `EnvelopeSource` was fixed by ADR-1601, and the `signal.*` conditioned signals were added by ADR-1602 (project-defined only). |
| MIDI | `control/*`, ADR-1700-1705 | `control.midi.bindings` publishes `control.<name>` on the frame it is drained. The lab binds CC 1 to `control.labCC1`. The rig layer (ADR-1705) sits outside the project. |
| Instrumentation | `--sonic-trace` / `--trace-finals` (`app/sonic_trace_cli.cpp`) | An offline `Engine::update` per row, then the bus and every route target's final. **Defect found here:** every event column read 0 on every row, because `Engine::update` clears events before it returns. Fixed by ADR-1721. |
| | `--sonic-live-log` + `tools/sound_to_light.*` (ADR-1605) | Live: one row per frame with `frameNs`, `presentNs` and `gpuMs` on the host clock, and the events kept by `keepFrameEvents_`. This is the sound-to-light rig on BlackHole. |
| | `--live-profile` (ADR-1090/1600/1608), `tools/live_perf_baseline.py` | Frame, GPU and CPU percentiles, deadline misses and the critical path, at a target fps. |
| | `--audit-routes` | Dead and hazardous routes. It flags the lab's `control.labCC1` routes as live-only, which is correct. |
| Fixtures | `tools/make_stem_fixture.py` (generated into the build tree by CMake), `tools/make_test_audio.py`, `tests/support/groove.hpp` | They provide stems with event times, but none has the categories the brief asks for, sample-indexed ground truth or flags for ambiguous events. The lab's generator follows the stem fixture's pattern: generated at build time into the build tree, never committed. |

**Not re-verified at Gate 1 (taken from the audits, still open):**
- the auto-gained `audio.<band>` rising to full on a long noise floor (02 §2.5);
- `audio.*` in GUI file playback coming from the causal runner while `response.*` comes from the track (02 §0.4).

## 2. What is testable, and how

| Stage | Testable offline (deterministic) | Testable live |
|---|---|---|
| 1. Ground-truth event | The manifest: sample index, source and measured register | The same manifest, played through BlackHole by the probe (next phase) |
| 2. Input audio time | The file sample = transport second (stems `offsetSeconds` 0) | The capture callback's host time (`--sonic-live-log`) |
| 3. Analysis event | `hop.csv`: a trace at 93.75 fps, one row per hop | ADR-1605's rig: bus time (`frameNs`) |
| 4. Conditioned signal | `response.*`, `stem.*`, `beat.*` columns | The same columns in the live log |
| 5. Mapping output | The route's final (`--trace-finals`; replace routes, so output = final) | The same (`--trace-finals` with `--sonic-live-log`) |
| 6. Scene parameter | `final:<param>[c]` in `frame.csv` (30 fps) | The same |
| 7. Rendered frame | The offline render's frame N against trace row N, compared by pixel (`compose.py`, which samples each indicator's rectangle) | `presentNs` + GPU end (ADR-1605) |

**New instrumentation, kept small:**
- **ADR-1721:** the trace writes the frame's events, plus `audio.*` and `control.*`.
- **ADR-1722:** the index flag that keeps a diagnostic entry out of the scene switcher.
- **`tools/audio_lab/`** (Python, no new dependency: numpy and Pillow are already the tools' own):
  - `make_fixtures.py`, the generator;
  - `make_lab_scene.py`, the scene;
  - `lab.py`, which runs and scores;
  - `compose.py`, which renders the clip and does the pixel check.

There is no new analysis API and no new signal.

**Controlled injection** is kept apart from end-to-end tests:
- the mapping checks (an indicator equals the declared function of its signal) read the production signal off the
  bus, not an injected one;
- unit tests of a mapping in isolation, if any are added, will be reported in their own section.

## 3. The scene (brief §2)

`examples/audio-response-lab/` is generated by `tools/audio_lab/make_lab_scene.py`.
- **Structure:** 149 procedural nodes and 58 routes.
- **Materials:** all unlit (ADR-1673's slim path) and shadowless (ADR-1545).
- **Look:** clamp tonemap, so a pixel is linear in its emissive value. No bloom, no AO.
- **Labels:** text.

**Panels:**
- **A Energy:** `response.level`, `response.bass`, `audio.midLevel`, `audio.trebleLevel`, `response.sustain` and
  `response.onsetEnv`. Fill height = value, with an identity chain.
- **B Events:** lamps for `response.onset`, `response.low`, `beat.pulse` and `response.note`, plus register bars.
- **D Beat:** a phase marker orbiting a dial (`beat.phase`), a bar marker (`beat.bar`), and bars for
  `audio.tempoConfidence` and `beat.bpm`.
- **C Sources:** per key (drums, bass, synth, pad), an ACTIVE lamp over a red no-data lamp, a level bar and an onset
  lamp. The drums column adds K/S/H lamps (`stem.drums.kick/snare/hat`, a drums role only).
- **E Probes:** real chains:
  - a sphere on smoothed `response.bass`;
  - a 120 ms onset flash;
  - a colour panel on `sonic.brightness`;
  - a cube on smoothed `stem.bass.level`;
  - MIDI CC 1.

**An absent signal reads as absent:**
- every per-source route is `optional`;
- every meter is authored empty;
- the ACTIVE lamp is authored hidden, so the red lamp shows;
- nothing substitutes another signal.

The project's `audioLab` block (ignored by the engine) gives each indicator's signal, parameter and screen rectangle
for the tools.

**Registration:**
- File > Examples > Live, under a **Diagnostics** heading (`"live": true`, `sonic.live`);
- `"diagnostic": true` keeps it out of the live scene switcher (ADR-1722), so a performer stepping through a set never
  lands on it.

**Ground truth is never in the project.** `lab.py` writes a project from the fixture's file names only. The manifest
is read by the scorer and the composer's strip, nowhere else.

## 4. Clock and timestamp conventions

- **Canonical: sample index.** A ground-truth onset is the first non-zero sample the event wrote into the mix's
  timeline. Time = sample / 48000.
- **Analysis frame:** its time is the **window centre**, 1024 samples (21.3 ms) after the window's first sample.
  - Offline, a render frame at t consumes frames with centre ≤ t. A centre-stamped frame can already hold an attack
    up to 21.3 ms after its stamp, so **the file path can publish an onset up to half a window before its sample.**
    Measured median: -11 ms on the hop clock.
  - Live, the frame exists only once its last sample is captured: centre + 21.3 ms, plus the device and capture
    buffers, plus the wait for the next render frame.
- **Hop clock trace (`hop.csv`):** 93.75 fps, so row k is at k·512/48000 s and consumes exactly one new hop. This is
  the analysis time at hop resolution.
- **Render clock trace (`frame.csv`):** 30 fps, so row N is at N/30 s. It is the same `Engine::update` sequence as the
  offline render, so render frame N shows row N's finals. This was checked by pixel: the correlation is 0.997-1.0 at
  lag 0 and about 0 at ±1 frame for events.
- **Errors** are detection time minus ground-truth time, so negative means published before the onset sample.
- **Live** (later phases, ADR-1605's rig): `frameNs`, `presentNs` and the GPU end on mach_absolute_time, and the probe
  logs its first sample on the same clock.

## 5. Fixtures (brief §3-4)

`tools/audio_lab/make_fixtures.py`:
- **Output:** seven categories, 48 kHz stereo float32, written into the build tree.
- **Determinism:** a PCG64 seed per fixture; two runs are byte-identical (checked with `diff -r`).
- **Size:** about 100 MB for all seven, never committed.

| Fixture | Seconds | What is in it |
|---|---|---|
| `sparse` | 30 | Leading silence; isolated kicks; isolated bass notes; separated snare/hat/pluck; a 4 s held 440 Hz note; trailing silence |
| `dense` | 24 | 128 BPM; low-only, mid-only and high-only sections, then everything (kick, 8th bass, backbeat, 16th hats, pad, 3/16 plucks); snare pairs 60 and 40 ms apart |
| `noisy` | 30 | A continuous broadband floor at -40 dBFS, then hits at SNR 30, 20, 10 and 0 dB; a 2 s noise swell; band-noise percussion |
| `syncopated` | 32 | 110 BPM from 0.5 s; son clave; displaced accents; 5:2 and 3:2 tuplets; two rest bars; bass off the downbeat |
| `dynamic` | 60 | A -30 dB intro; a build; a sudden 0 dB section; a drop to 6 s of silence; recovery; a tempo step from 120 to 96 BPM at 44 s |
| `isolation` | 24 | Stems: drums (drums role), bass, synth, and pad (silent throughout). Drums alone with the snare stacked on kicks, bass alone, synth alone, then all overlapping with snares alone, then silence. Mix = Σ gain·pan·stem; the stems are kept |
| `adversarial` | 30 | A pure held tone; a bass drone; 25 Hz thumps and 14 kHz ticks; click pairs 15/30/50/80 ms apart; a 30 Hz snare roll; -60 dBFS noise |

**The manifest records:**
- the generator version, seed, rate, layout, duration and mix peak;
- each event's sample, time, source, kind, duration, attack, and its **measured** register shares (the FFT of its
  first 50 ms over ADR-1604's bands);
- flags for ambiguous events (a swell with an attack of 100 ms or more, a masked hit, a near-coincident pair);
- the intended beats, downbeats and tempo segments, and the sections;
- the silent intervals, **measured from the produced mix** (below -80 dBFS RMS for at least 250 ms) and per source;
- the sha256 of each file.

**Committed:** the generator, the scene builder, the runner, the composer and the docs. No audio.

## 6. Threshold reasoning (`lab.py`, each check carries its "why")

| Quantity | Bound | Derived from |
|---|---|---|
| Onset match window | -23.3 / +34.0 ms | Earliest: half a window (21.3 ms) before the attack, which the centre-stamped file path can do, plus 2 ms. Latest: three hops (one to land in a centre, one for ADR-1060's decision, one spare) plus 2 ms |
| Ground-truth attack clustering | 30 ms | Below the detectors' 60 ms refractory: two attacks closer than that are one event by design (reported, not failed) |
| Onset precision / recall | ≥ 0.90 on firm attacks | Ambiguous attacks (swells, masked hits, pairs under 50 ms) are excluded from recall and never counted false |
| Timing p95 (hop clock) | ≤ 34 ms | The match window's late edge |
| False events in silence | 0 per minute | Silence is the measured interval, from 250 ms in to 30 ms before the next sound |
| False events in sustained notes | ≤ 6 per minute | One retrigger per 10 s at most; more reads as flicker |
| Stale value after silence | < 0.02 once the signal's **documented** release has run out | From the code: `response.sustain` 0.9 s × ln 50; `onsetEnv` 0.09 s × ln 50; `lowEnv` 0.18 s × ln 50; the levels' dB follower from the window top to its bottom at the -40 dB reference floor (level 0.60 s, bass 1.00 s); `audio.*Level` 13.8 s (4.34 dB/s over 60 dB) |
| Mapping | within 0.001 × the meter scale | The trace writes 4 decimals of the signal |
| Onset flash | ≤ 120 + 33.3 ms | The declared fall plus one render frame |
| Beat | F ≥ 0.80 at ±70 ms; tempo within ±3% of intended | MIREX tolerance; an octave or other ratio fails, reported as a ratio |
| Live 1080p30 | ADR-1608's T1-T4 (the live baseline's own) | Not invented here |

## 7. Order of work (the brief's) and what Gate 1 covers

1. Audit and plan: this document.
2. Diagnostic infrastructure: the trace events (ADR-1721), the generator, the runner and scorer, and the composer
   with its pixel check.
3. The scene, registered (ADR-1720, ADR-1722), with tests.
4. **Gate 1:** three categories end to end (sparse, syncopated, isolation), two synchronised clips, the first
   metrics report.
5. **After owner notes:**
   - all seven categories scored;
   - a causal (live-path) replay so the live `BeatTracker` and the runner's carry are measured, not only the file
     grid;
   - the live loopback run through ADR-1605's rig with fixtures played by the probe;
   - cross-scene transfer (Event Horizon and Rainbow World);
   - the perf report, the docs and the Catch2 E2E per category (the generator run by CMake as the stem fixture is).
