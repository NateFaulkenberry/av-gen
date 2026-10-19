# Remove live kick / snare / hat detection: blast radius and removal plan

**Decision (owner, 2026-10-19): C1 with C4 = drop the whole row.** Implemented as ADR-1530.

2026-10-19. Branch `eng/remove-ksh-detect` (from main `35a6883a`). Brief: [00-brief.md](00-brief.md). This report covers
the brief's Phases 1 and 2. No production code is changed until the owner decides the Category C question in §8.

The stems branch `eng/stems-multichannel` (head `a65e251c`, about to merge) was audited too. Every statement below holds
before and after that merge (§6).

## 1. The answer in one paragraph

The Live panel's "kick / snare / hat" display is a thin view over a detector that is NOT exclusive to it.
- **The detector.** `CausalOnsetDetector` (ADR-1060/1067/1068) runs on every hop of every analysis, live and offline.
- **What it feeds besides hits.** It is also the only source of the dB levels behind ADR-1510's response levels
  (`response.bass/level/levelSlow/intensity/transient/sustain`), of `response.flux`, and of the live input's
  `audio.onsetLow/Mid/High`. Those in turn feed the GPU onset fields' live onsets and Astral Forge Live.
- **Who uses its signals.** Its hit signals are routed 394 times in 47 example projects, 38 of them in the live set.
- **The stems branch.** Its per-input "hits" toggle reuses the same class.

The one thing that is exclusive to the "feature" is **the Live panel's row of kick / snare / hat meters** (and the
wording around it). Removing the detection *processing* would break most audio-reactive live scenes and ADR-1510's
levels, so it cannot be done as "cleanup". It is a design decision for the owner (§8).

## 2. The feature as it runs (data flow)

```
audio (device callback, or file playback)
  -> audio::AnalysisStream -> AnalysisRunner thread (src/analysis/analysis_runner.cpp)
       Analyzer (FFT 2048/512, flux, onset, bands)          [shared]
       BeatTracker                                          [shared]
       CausalOnsetDetector::process(frame)                  <- the detector, EVERY hop, unconditionally
         fills frame.causal: hit/strength/ratio/drumness per class {Kick, Low, Snare, Hat, Onset},
                             levelDb, bassDb, snareDb, hatDb, ...
       frame.lowOnset/midOnset/highOnset := causal Kick/Snare/Hat   (live only; overwrites the analyzer's none)
       FrameTap (live Sonic): sonic::TimbreFeatures.causal = frame.causal
       carryEvents (ADR-1060 stamps) -> TripleBuffer
  render thread: Engine::publishFrame -> LiveEventLatch -> AudioSignals::publish
       audio.onsetLow/Mid/High  (live input: the causal kick/snare/hat; live FILE playback: overlaid with ADR-898 offline)
       LiveAudioHistoryFeed -> spatial::AudioHistory live onsets Low/Mid/High (GPU onset fields, Astral Forge Live)
  SonicRuntime::step -> ResponseModel::step(TimbreFeatures)  (src/sonic/response.cpp)
       hits: causal.hit/strength x Transients/Sensitivity -> response.kick/low/snare/hat/onset (+Env), response.hatRate
       ADR-1068: a snare/hat on a MIDI note-on with low drumness is demoted
       levels: causal.levelDb/bassDb -> response.bass/level/levelSlow/intensity/transient/sustain (ADR-1510)
       flux: causal.ratio[Onset] -> response.flux
  Live panel (src/ui/live_panel.cpp:544-555): four ProgressBars reading response.kickEnv/snareEnv/hatEnv/noteEnv
```

**Offline (file load and render).** `AnalysisTrack::analyze` runs `detectCausalOnsets` over the whole file
(`analysis_track.cpp:251`). `SonicRuntime` steps the stored `frame.causal`, so `response.*` hits and levels are
identical in a render and in live playback ("file == live", ADR-1060). File `audio.onsetLow/Mid/High` come from
ADR-898's *offline* band onsets, not from this detector.

## 3. Answers to the brief's eight questions

1. **Code that exists only for kick/snare/hat detection.** Inside the analysis there is none: the classifier is one
   pass whose kick/snare/hat decisions share state with the onset ratio and the level fields that other features
   read. Exclusive to the *panel feature*:
   - the meter row `live_panel.cpp:544-555`, with the comment "What the detector hears, as four meters";
   - the meters' wording in the Transients tooltip (`:537`);
   - the input-gain comment and tooltip that cite "the hit detector" / "the hits" (`:495-507`).
2. **Shared utilities.**
   - `Analyzer` (the FFT, magnitudes, flux and onset), `BeatTracker`, `AnalysisFrame`, `LiveEventLatch` / `carryEvents`
     and `HitPicker`.
   - `CausalOnsetDetector` itself: shared by the response levels, `response.flux`, the live band onsets and the stems
     hits tier.
   - ADR-898's `band_onsets` (offline file onsets) does not depend on the detector.
3. **Scene and modulator consumers.** Yes, many (§4).
4. **Configuration and saved data.**
   - There are no detector settings: `CausalOnsetConfig` is compiled-in, and "the detector itself has no parameters a
     live control could change" (`causal_onsets.hpp`).
   - The settings that exist belong to the *response model*:
     - `sonic.response.transient`, in 38 projects (21 of them not at 0.5);
     - the parameter `sonic/response/transient`, carried across a live scene switch (`live_scenes.hpp:48`);
     - optional `sonic.response.hitRelease{kick,low,snare,hat,onset,note}`, `hatRateSeconds` and `hatRateFull`
       (none in examples).
   - `ResponseSettings::applyJson` reads keys by name with `j.value(...)`, so an unknown key is ignored silently.
     Retired keys get one warning by the ADR-1510 pattern.
5. **Does it run when the UI is hidden?** Yes. It runs in `AnalysisRunner::threadMain` on every hop of every live input
   or file playback, and in every `AnalysisTrack::analyze` (each file load, offline render and review). No flag, panel
   state or project setting gates it.
6. **Performance.** See §7. The detector is the most expensive single stage on the analysis thread, which is not the
   audio callback thread. Removing it would not change render-frame cost.
7. **Signals usable by scenes and external workflows.** Yes, all labelled in the route-source picker:
   - `response.kick`, `low`, `snare`, `hat` and `onset` (events);
   - their `...Env` envelopes and `response.hatRate`;
   - `audio.onsetLow`, `Mid` and `High` (live input).

   External users:
   - the AI tools: `ai/engine_tools.cpp:3038` frame onsets, and the Director's reactivity proposer templates
     `kick/clap/hats -> audio.onsetLow/Mid/High`;
   - the Effect Library's event timing (`effect_timing.hpp:92`);
   - `SignalEventDeriver` (seek fronts for `response.*` and `audio.onset*`).
8. **Other Live panel controls coupled to it.**
   - **Transients** (`sonic/response/transient`) scales the hits. It also scales `response.transient`, `response.note`
     (MIDI) and the GPU onset fields' hit sensitivity (`spatial::AudioResponse::hitSensitivity`).
   - **Sensitivity** feeds both kinds. **Release** sets the hit envelopes. **Reset response** resets all five.
   - The ADR-1068 MIDI demotion uses the detector's drumness.
   - None of these is exclusive to kick/snare/hat.

## 4. Consumers in example scenes (measured by `build/ksh/inventory.py`: every JSON under examples/, every string value)

The hit-family signals are routed **394 times in 47 project / scene files**:

| Signal | Refs |
| --- | --- |
| `response.kick` | 135 |
| `response.snare` | 103 |
| `response.hat` | 77 |
| `response.kickEnv` | 20 |
| `audio.onsetHigh` | 14 |
| `audio.onsetLow` | 10 |
| `audio.onsetMid` | 9 |
| `response.hatRate` | 8 |
| `response.snareEnv` | 6 |
| `response.hatEnv` | 6 |
| `response.onset` | 4 |
| `response.onsetEnv` | 2 |

By category:
- **Sonic Abstract:** all 10 live scenes, 6-27 refs each. Neon Vector has 27.
- **Sonic VFX:** all 16 live scenes, 5-16 refs each.
- **Resonator:**
  - Cymatic World routes `hat`, `hatEnv`, `kickEnv` and `snareEnv` (6).
  - Waveform Dimension routes `hatEnv` and `snareEnv` (2).
  - Resonant Silk routes no hit signal; it uses a low onset field.
- **Generative:**
  - RD Pins: `hat` x2, `hatRate` x2, `snare`.
  - Phase Hall (`experiments/phase-wall`): `hat` x2, `hatRate`.
- **Experiments:**
  - Phase Membrane, Phase Wall, RD Relief, RD Bloom and RD Pins (R&D) each route `hat`.
  - The Current Meadow / Storm / Lines scenes route `kick`, `snare` and `hat`.
- **Other live scenes:**
  - Digital Mosh Live: `kick` x4, `snare` x3, `hat` x3.
  - THE RIFT LIVE: `snare`.
  - Sonic Live: `onset`.
- **File showcases (`audio.onset*`, offline ADR-898 on files):** Digital Mosh and Digital Mosh - Trench, THE RIFT, Chorus
  Field p5/p8 and Glowmere Valley 3.

**GPU onset fields** (`"onsetSource": "low"|"mid"`, 50 in 40 files) read the audio history's onsets. On live input
those are the detector's kick/snare. Live scenes with one:
- Cymatic World, Resonant Silk, Waveform Dimension;
- RD Pins, Phase Hall, and every experiment;
- Digital Mosh, Sonic Abstract (one).

**Astral Forge Live** takes its kicks and snares from the same live history (`scene/astral_forge.cpp:282-302`):
kick-opened collapses and snare face-flashes.

**Generators** that write these routes:
- `tools/sonic_vfx/{abstract,signals}.py`;
- `tools/rd_pins/build.py`, `tools/phase_wall/build.py`;
- `tools/gv3/look.py`, `examples/chorus-field/build.py`, `examples/bioluminescent/cp*.py`.

So on live input (BlackHole 2ch or an interface), the kick/snare/hat decisions currently drive:
- every Sonic Abstract, Sonic VFX, Resonator and Generative scene;
- Digital Mosh Live, THE RIFT LIVE and Astral Forge Live.

## 5. Classification

### A. Remove (exclusive to the Live panel's kick/snare/hat feature)
- The meter row in the Response group (`live_panel.cpp:544-555`: `response.kickEnv/snareEnv/hatEnv` bars). The `note`
  bar in the same row is MIDI, not detection. See C4 for whether it stays as a lone "note" meter.
- The detector wording in the panel:
  - the Transients tooltip "kicks, snares, hats, notes";
  - the input-gain comment and tooltip "the hit detector" / "the hits".

  These are reworded to what the controls actually do; they are not removed.
- In the docs and comments that describe the panel as showing kick/snare/hat:
  - ADR-1062's Code list (the panel's hit lights);
  - `ResponseModel::envelope` / `hits` comments ("the panel's hit lights").

  The read-outs themselves stay, because tests use them.
- **No processing, signal, setting or project key is in A.** There is nothing to strip from saved files under option
  C1.

### B. Preserve
- `Analyzer`, `BeatTracker`, `AnalysisFrame`, the ADR-1060 event carry and `LiveEventLatch`, and ADR-898
  `band_onsets` (offline).
- `CausalOnsetDetector` / `CausalOnsets` / `HitPicker` / `HitClass`, which serve:
  - ADR-1510 levels (`levelDb`, `bassDb`);
  - `response.flux` (the Onset ratio);
  - live `audio.onset*` and the GPU onset fields;
  - the stems `hits` tier (`StemAnalyzer::causal_`, `stem.<key>.kick/snare/hat`).
- `ResponseModel` and all `response.*` signals, names and semantics. That includes the hits and `hatRate`, which have
  394 route refs.
- The five response parameters, their project keys and the live-switch carry: `sonic/response/*` and
  `sonic.response.*`.
- `audio.onsetLow/Mid/High`, `SignalEventDeriver`, the Effect Library event timing, the reactivity proposer and the AI
  tools.
- The BlackHole 2ch path, MIDI and the ADR-1068 demotion, offline analysis and the stems work.

### C. Investigate before deciding: the owner's call
- **C1 (the scope I recommend).** Remove the panel's detection UI only, which is Category A. All processing and signals
  stay, and audio-reactive scenes are untouched. This prepares the panel for source assignment without breaking
  anything.
- **C2.** Stop live input from publishing the detector's kick/snare/hat as `audio.onsetLow/Mid/High` (the runner
  override, `analysis_runner.cpp:131-138`). It would make the stale help text "live input has none" true again.
  - **Breaks** live-input onset fields in Cymatic World, Resonant Silk, Waveform Dimension, RD Pins, Phase Hall and
    every experiment.
  - **Breaks** Astral Forge Live's collapses and flashes, and Director-proposed kick/clap/hat routes on live input.
  - **Saves** no CPU, because the detector still runs for the levels.
  - Not recommended.
- **C3.** Remove the kick/snare/hat classes from the detector, keeping its levels and onset ratio. That retires
  `response.kick/snare/hat(+Env)` and `response.hatRate`, which ADR-441 would require stripping from 47 files.
  - **Breaks:**
    - the core reactivity of 26 Sonic Abstract / VFX scenes, the Resonator and Generative hits, and Digital Mosh Live;
    - ADR-1068;
    - the stems hits tier, which reuses the classes (it would need its own copy, which is a replacement detector and is
      forbidden).
  - **Saves** part of the cost in §7. Not recommended. It contradicts "do not change shared signal semantics" and
    "do not add a replacement detector".
- **C4.** Inside C1, keep a lone **note** meter (the MIDI note envelope) or drop the whole row.
  - The MIDI section already prints the last note, its velocity and a running count.
  - My default is to **drop the whole row**: one "note" bar under Response reads as a leftover.

## 6. The stems branch (`eng/stems-multichannel`)

- It adds `StemAnalyzer` with an opt-in **hits** tier that owns its own `CausalOnsetDetector`
  (`stem_analysis.hpp:100`, `.cpp:138-145`). It publishes `stem.<key>.kick/snare/hat` and the Live panel's Inputs
  table has a per-row **Hits** checkbox (`live_panel.cpp`, "Also find kicks, snares and hats on this input").
  This is the new source-assignment feature and is preserved untouched.
- It does not modify `causal_onsets.*`, `response.*` or `analysis_runner.*`. The main mix still goes through
  `AnalysisRunner` (with the causal detector), as today.
- On that branch the Response group's meter row is unchanged; it moved to about line 750. My edit is a small hunk in
  that block, so a `git merge main` after stems lands is a clean or trivial merge. I will merge main first, as
  instructed.
- None of C1's changes touch a file that stems' tests assert on.

## 7. Performance (measured)

**How it was measured.**
- A scratch benchmark, `build/ksh/bench_detector.cpp` (not committed), runs the analysis thread's per-hop work over the
  whole file: `Analyzer` push/pop, `BeatTracker::push` and `CausalOnsetDetector::process`. The settings are the live
  ones: 48 kHz, 2048/512, 93.75 hops/s.
- Each figure is the median of 3 runs on the M2 Max, in a release build, with another agent sharing the machine.

| track | hops | Analyzer | BeatTracker | causal detector | detector share | detector, % of one core |
| --- | --- | --- | --- | --- | --- | --- |
| feline-footwear.wav | 19,731 | 16.8 us | 0.7 us | **77.8 us** | 82% | 0.73% |
| trench.wav | 19,458 | 17.5 us | 0.7 us | **83.9 us** | 82% | 0.79% |
| hyperborea.wav | 19,094 | 17.9 us | 0.7 us | **83.7 us** | 82% | 0.78% |

This agrees with the stems agent's 77-90 us per hop for the classifier.

**What it means:**
- **Live analysis.** The detector is 82% of the analysis thread's work per hop, but under 1% of one core in absolute
  terms. It runs on the analysis thread, not the audio callback, so the callback is unaffected either way. Render-frame
  cost is unaffected too: the render thread only reads the frame.
- **Offline.** Every file load, render and review runs it once over the file: about 1.5-1.6 s of CPU per 3.5-minute
  track. That is part of `AnalysisTrack::analyze`'s time.
- **C1 (UI only) changes none of this.** It removes four ImGui progress bars, which is unmeasurable.
- **C3 would save less than the full 78-84 us.** The levels and the onset ratio would still need the compressed
  spectrum and the band sums. The kick/snare/hat-specific part is the percussive frequency median, the noise-floor
  median, the kick period comb and the snare confirmation. I have not measured that split, and would do so only if C3
  were chosen.
- **The stems hits tier** pays this cost per source on which it is enabled (stems' 05-benchmarks.md: 130-155 us per
  source-hop with hits on all 8). That is opt-in and stays.

## 8. The decision needed

The detector's outputs are load-bearing. Removing the "feature" therefore means one of:
- **C1:** remove the Live panel's kick/snare/hat meters and their wording only. No scene, signal, setting or CPU change.
- **C2 or C3:** remove processing, which breaks the live scenes listed in §4 and does not meet the brief's Phase 4.

I recommend **C1** (with C4 = drop the whole meter row). The per-source "Hits" toggle from the stems work then becomes
the panel's only kick/snare/hat surface, which is the direction the brief names.

## 9. Removal plan (for C1)

Each step is one commit, after `git merge main` (stems).

1. **Panel.** Delete the meter row and its comment. Reword the Transients tooltip to "How strongly attacks fire:
   every hit signal and the MIDI notes; 0 turns them off" (no kick/snare/hat list). Reword the input-gain comment and
   tooltip to "the Sonic Character and the hit signals" (they are not a panel feature). Keep Reset response.
2. **Regression test.** Pin the Live panel's Response group. There is no ImGui test harness for the Live panel, so the
   test checks the source: `live_panel.cpp` must not read `response.kickEnv`, `snareEnv` or `hatEnv`. In the same
   test, check that the five response parameters are still registered and still carried across a live switch, and
   that the response hit signals are still declared on the bus.
3. **Docs.**
   - ADR-1530, "The Live panel drops its kick/snare/hat meters; the detection stays (source assignment replaces it)",
     with a row in the ADR index.
   - The range row 1530-1539 `eng/remove-ksh-detect`.
   - An addendum on ADR-1062's Live panel line.
   - In `docs/help/audio-signals.md`, fix the stale "live input has none" for `audio.onsetLow/Mid/High`: live input has
     the causal kick/snare/hat (ADR-1060).
4. **Checks.** A clean reconfigure and full build, then:
   - `run-suite.sh cpu-assets` and `run-suite.sh gpu`;
   - `review_examples.py`, `check_project_integrity.py` and `check_response_range.py`;
   - a final grep audit.

   The panel change is UI-only. I will capture one avgen-window-only screenshot of the Live panel for
   `~/Desktop/av-gen-review/73-remove-ksh/`.

Configuration and migration (C1):
- No key is removed and no project file changes.
- ADR-441 stripping does not apply.
- Unknown `sonic.response` keys are already ignored (`j.value`), so a C3-style removal later would need only the
  existing one-warning pattern.

Tests (C1):
- **Unchanged and still required:**
  - `test_causal_onsets` and `test_drum_recall`;
  - `test_sonic_response` and `test_response_live`;
  - `test_audio_history`, `test_signal_trigger` and `test_stem_*`.
- **New:** the panel test from step 2.
- No obsolete test exists under C1.

**Remaining risk (C1).** The performer loses at-a-glance confirmation that the hits fire on their input. The route
picker and the Signals view still show `response.kickEnv` and the others. The stems Inputs table (Hits) is the
intended replacement.

Under C2 or C3 the plan would differ. It would strip routes from 47 projects and 7 generators, rewrite ADR-1060, 1062,
1067 and 1068, and need art review of 30+ live scenes. I would write that plan only if the owner chooses it.
