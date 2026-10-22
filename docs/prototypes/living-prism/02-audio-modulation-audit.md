# The Living Prism 02: audio, MIDI, modulation and live-scene integration audit (Sub-Agent B)

- **Author:** Sub-Agent B, for the lead agent's integrated report.
- **Brief:** `00-brief.md`. This file covers:
  - §3 "Sub-Agent B";
  - the audio and modulation half of §4;
  - all of §7;
  - the signal side of §8 and §8.2;
  - §9 D, and §9 E for mapping and diagnostics;
  - the audio and modulation half of §10;
  - the owner's follow-up on phasing out the mix kick/snare/hat (§15).
- **Worktree:** `av-gen-prism`, branch `audit/living-prism`, from main `5e0f5b02`. Release build in `build/audio`.
- **What this is not:** no scene was built, no production code was changed, and nothing was committed. The probes are
  listed in §14. Every avgen run went through `tools/gpu-lock.sh`.
- **Status labels** are exactly §2.2's:
  - Verified existing;
  - Existing but limited;
  - Experimental;
  - Duplicated or fragmented;
  - Missing;
  - Unknown.
- **Evidence tags:**
  - **[code]** means read in the source at the reference given.
  - **[B1]**, **[B1b]**, **[B2]**, **[B3]** mean measured by my probes (§14).
  - **[ADR-n]** or **[doc]** means a measurement recorded by an earlier agent and not re-run by me.
  - **[lead]** means the coordinator's own probe.
  - **[est]** means an estimate, never a measurement.
- **Feeds the final report:**
  - §2 (architecture map): §1 here;
  - the four traces: §10;
  - §4 (audio/MIDI/modulation audit): §2-§6;
  - §6 (reusable VFX, reactive side): §7;
  - §7 (research D/E): §9;
  - §8 (refactor/removal): §11;
  - §10 (plan): §12;
  - §12 (risks/open questions): §13.


## 0. Summary

**What is already strong:**
- **One modulation model.** One signal bus, one route model and one fixed-order per-route chain serve every scene, every
  input and both engine modes. The chain is: delay, normalise, trend, gain/offset/curve, clamp, threshold, smoothing,
  envelope (with latch), spring, remap and integrate.
- **The vocabulary is musically sensible.** `response.*` (hits, levels, presence) and `stem.<key>.*` are close to what
  Synesthesia and Notch expose (§9.6). The loudness-reference levels (ADR-1510) gate silence to 0. 43 of 44 live
  examples route `response.*`.
- **Seek equals play** is engineered in depth (ADR-870/901/1168/1573), with tests.
- **Stems and live channel sources** are wired and merged:
  - analysed stems (ADR-1520);
  - live per-channel sources over AUHAL (ADR-1521/1522);
  - feeds for the beat clock, hits and levels (ADR-1580/1581);
  - per-source trims (ADR-1582).
- **Signal-side spatial propagation** exists:
  - audio spectrum and onset fields with delay by distance (ADR-1116);
  - fields timed from the latest musical event (ADR-906);
  - effect triggers on any bus event, recorded live (ADR-1061).

**What is genuinely missing or wrong, in priority order:**
1. **There is no same-frame, named, stateful conditioned signal.** A conditioned "bass breath" or "drum hit" can live in
   only two places:
   - copied into every route that uses it;
   - in a macro knob, which publishes **one frame late**: exactly one frame, measured [B1].

   Rainbow World shows the cost. Of its 405 routes, 179 read macros, the identical bass latch chain is copied onto three
   targets, and every effect is written twice: a stem route plus a `response.*` fallback gated on `stem.X.active` (§3.3).
2. **Two envelope primitives disagree.** The `EnvelopeSource` (`env.*`) publishes its trigger **one frame late** and
   **never shows its peak**. It reads 1 - dt/release on its first visible frame: **0.72 at 60 fps, 0.44 at 30 fps** for a
   60 ms release [B1].

   The route-chain envelope fires on the event's own frame at full strength (ADR-900's rule), so the same
   "envelope" means different things in two places. It is a correctness defect in a shared primitive.
3. **Transient detectors are fragmented and slow where it matters live.** Five onset detectors exist (§2.3). Measured
   from the true onset to the frame the event is on the bus [B1], offline:
   - broadband onsets: 0 to +1 frame;
   - `response.kick` and `stem.drums.kick`: **+28-32 ms**;
   - the snare classes: **+41-44 ms**, and **50-67 ms after the broadband onset of the same hit on Feline Footwear**
     [B3].

   Live, add about 21 ms of window plus the pipeline (§6). The kick and snare classes then reach the screen roughly
   **75-130 ms after the sound [est]**. That is past the +45 ms detectability threshold (ITU-R BT.1359, §9.3).
4. **The time base differs by family in GUI file playback.**
   - `audio.*` comes from the causal runner fed by the player callback, so it lags the play-head.
   - `response.*`, `stem.*`, beats and band onsets are read from the whole-file analysis at the play-head.
   - The same name can mean a different detector live and on file: `audio.onsetLow` is ADR-898's look-ahead detector on
     file but ADR-1060's causal one on live input.
5. **Diagnostics stop at the bus.**
   - There is no live plot of a source → chain → parameter final.
   - `--sonic-trace` has no `audio.*`, `control.*`, route-output or parameter-final columns.
   - Its `env.*` columns, which ADR-1571 uses to trace events, carry finding 2's one-frame lag and peak loss.
6. **The GPU audio history (spectrum and onset fields) hears only the mix.** Feeds (ADR-1580) never reach it, so a
   travelling front cannot be "the drums" (handoff to A, §7).

**Most important recommendation (P0):**
- **Add a "conditioned signal" (§12 P0-1).** This is a source kind that runs the existing route chain over one or more
  inputs, with a "first active input" selection, and publishes a named signal in the rack's second pass. Any route,
  state trigger or effect trigger can then read it on its input's frame. This is the smallest change that removes the macro lag, the chain copying and the fallback doubling
  together, without a new framework.
- **Fix the `EnvelopeSource` publish order and peak loss (P0-2).**

Everything else is P1 or below. The modulation architecture does not need replacing.

## 1. Integration map (feeds final report §2 and §4)

### 1.1 Producers, per render frame, in `Engine::update` order (`src/app/engine.cpp` l. 7100-7395)

| # | Step | Code | Publishes | Notes |
|---|---|---|---|---|
| 1 | Live analysis acquire (Live mode) | `runner_->acquire()`, `Engine::publishFrame` l. 5992 | `audio.*` (mix) | The newest causal frame only. Events between frames are carried (`AnalysisRunner::carryEvents`) and latched once (ADR-1060). With a file loaded, beats, tempo and band onsets are overlaid from the whole track at the frame's centre time (`overlayTrackFields`, `analysis_track.cpp` l. 281). |
| 1' | Offline/track consume | `Engine::consumeAnalysis` l. 6019 | `audio.*` | Every analysis frame with a centre ≤ renderTime. Events are OR-ed across the batch (ADR-898). |
| 2 | Stems / live input sources | `publishStems` l. 4520 / `publishInputSources` l. 5908 | `stem.<key>.*` | Offline stems: newest frame ≤ t, events in (prev, t]. Live: triple buffer per source; inactive after 250 ms without a frame. |
| 3 | Control hub | `ControlHub::update` (`control_hub.cpp` l. 113) | `control.*` (via `ControlSource`), parameter **bases** directly | MIDI and OSC are drained once per frame. MIDI clock goes to `MidiClockTracker`. Live Sonic takes notes into its note model (ADR-1025). |
| 4 | Clock and music | `updateTimeSignals` l. 6215 → `advanceClock` l. 6093 | `time.*`, `beat.*`, `section.*`, `music.*`, `sonic.*`, `timbre.*`, `notes.*`, `response.*` | The beat clock comes from MIDI clock, else the analysed grid (mix or beat feed, ADR-1581), else the live tracker's phase. On file, the sonic runtime walks the track to t. Live Sonic publishes from its own session. |
| 5 | Source rack | `sources_.update` (`source.cpp`) | `lfo.*`, `noise.*`, `timeline.*`, `control.*`, `macro.*` (pass 1); `env.*`, `random.*`, `visual.*` (pass 2, readers of triggers) | **`macro.<k>` publishes the parameter final from the PREVIOUS frame's routes.** `env.*` publishes before applying this frame's trigger (§2.4). |
| 6 | Scene states | `states_.update` l. 7221 | `state.index`, `state.progress`; writes parameter bases (preset morph) | After the sources (ADR-1124), before the finals reset. |
| 7 | Automation | `timeline_.apply` | parameter finals | The first modulation layer. |
| 8 | Fields (CPU) | `controller_->updateFields` | `field.<n>.*` and route `spatialGain` | ADR-097. A field's output is a gain on its routes' depth. |
| 9 | Entity / staging | `publishEntitySignals`, `publishStagingSignals` | `entity.*`, staging events | One step late by design (ADR-703). |
| 10 | **Routes** | `modulator_.applyRoutes` (`modulation.cpp`) | parameter finals | In op order: Replace, Multiply, Add, Min, Max. Each route runs its own `ProcessorChain::process` (`processor.cpp` l. 158). |
| 11 | Palette | `palette_.step/apply` | colour parameters | ADR-1043/1420. The selector can advance on the music. |
| 12 | Behaviours, sequence, effects | `updateBehaviour`, `updateEffects(BeforeScene)`, `serviceSignalTriggers`, `TriggerClock::bind` | Effect Library inputs | Signal triggers are derived (file) or recorded (live), ADR-1061. |
| 13 | Flatten / scene | `controller_->update`, `updateEffects(AfterScene)` | scene, materials, post | **The parameter-to-pixel boundary (Sub-Agent A).** |
| 14 | Shader layers | `shaderLayers_.update` | `StdUniforms.audio/beat` (rms, 5 bands, onset, beat phase, bpm, bar) | A second, hard-wired audio path into user shaders, bypassing routes. |
| 15 | End of frame | `bus_.clearEvents()` | | Every event lives exactly one frame. |

### 1.2 The path from sound to signal

Each path below runs from input, through analysis, to publication.

- **File playback, GUI (Live mode):**
  - Input: `AudioPlayer` callback → `AnalysisStream`.
  - Analysis: `AnalysisRunner` thread (STFT 2048/512 Hann, `BeatTracker`, `CausalOnsetDetector`).
  - Also loaded: the whole-file `AnalysisTrack` (offline beats, ADR-898 band onsets), the `SonicSetup` timbre track,
    and `StemTrack`s.
  - Publication: `audio.*` from the runner. `response.*`, `sonic.*` and `stem.*` from the tracks at the transport
    second.
- **Offline render, seek replay, `--sonic-trace`:** the whole-file tracks only, indexed by the frame's time.
- **Live input:**
  - Mix: `AudioInput` (miniaudio, or `CoreAudioCapture` AUHAL when sources are mapped) → `AnalysisRunner`.
  - Per-channel sources: `ChannelAnalysis` (one thread, a `StemAnalyzer` per source).
  - Live Sonic (ADR-1025) taps the runner and publishes `sonic.*`, `timbre.*`, `notes.*` and `response.*`.
  - Feeds (ADR-1580) substitute a source's hits and levels into the response model and its beat into the clock.
- **MIDI:**
  - CoreMIDI → `MidiInput` queue → drained at the top of the frame → bindings.
  - Bindings go either to a `control.<ch>` signal (published the same frame) or straight to a parameter **base**
    (unsmoothed).
  - Live Sonic additionally builds a live note track → `notes.*`, `response.note`.

### 1.3 Consumers of the bus (what reads signals)

| Consumer | Reads | Same frame as publish? |
|---|---|---|
| Routes (`applyRoutes`) | any signal; depth signal | yes, except `macro.*` (a frame late), and `env.*` events (a frame late) |
| Scene-state triggers | events, values, `macro.*` | yes, for anything published before step 6 |
| Source triggers (`env`, `random`) | events | yes, for events published before step 5 pass 2. **Events published after step 5 (`field.*.enter/exit`, `entity.*`, staging, cinematic) are cleared at step 15 before any source or state sees them [code; Unknown in practice, not probed].** |
| Interpret sources (`visual.*`) | values | yes (pass 2) |
| Effect Library `signal` triggers | events | derived per analysis frame (file), recorded per frame (live) |
| GPU fields `spectrum`/`onset` | `AudioHistory` (mix only) | its own clock (ADR-1116) |
| Astral Forge conductor | its own `SongAnalysis` from the track (`astral_song.*`) | bespoke |
| User shader layers | `StdUniforms` | hard-wired mix values |

### 1.4 Discrepancies between docs and code (the code is authoritative)

- **`docs/audio.md` §Testing:** says device tests "skip themselves without an output or capture device". Since ADR-1550,
  real-device tests are opt-in (`AVGEN_AUDIO_DEVICE_TESTS=1`). The doc predates that.
- **`docs/audio.md` §Signals:** lists `audio.*` but not the band levels, `audio.energy`, `onsetRate`, `width` or
  `onsetLow/Mid/High` that `AudioSignals::declare` (`audio_signals.cpp` l. 32-53) publishes.
- **`docs/audio.md`:** "No smoothing is applied at this level; each modulation route smooths independently" is true of
  `audio.*`, but not of `response.*` (followers in `ResponseModel`), `stem.<k>.level` or `sonic.*` (tiers).
- **ADR-1571 addendum:** "an event is traced through an envelope source" is true, but the traced value is one frame late
  and attenuated (§2.4).
- **`docs/control.md`:** describes MIDI bindings to parameters as "mapped to min..max". It does not say these writes are
  to the base, unsmoothed, and not seek-replayed.


## 2. Signal semantics (feeds final report §4; brief §7.1)

### 2.1 The table

**Abbreviations:**
- **Rate:** "hop" means once per analysis hop, 512 samples, 10.7 ms at 48 kHz. On the bus every value changes at most once
  per render frame; for hop-rate families each frame publishes the newest hop.
- **Kind:** I = instantaneous, S = smoothed, E = event (one frame, strength 0..1), A = accumulated or stateful.
- **Use:** C = continuous modulation, T = trigger.
- **Latency:** offline, the true onset to the frame that publishes, measured [B1] at 60 fps; "live" adds §6's pipeline.

| Family | Meaning, units, range | Rate | Kind | Silence / missing input | Latency | Use | Status |
|---|---|---|---|---|---|---|---|
| `audio.rms`, `peak` | mix window RMS and peak, linear 0..1 | hop | I | 0 | file/offline: the frame holding the window centre | C (needs a chain) | Verified existing |
| `audio.bass…treble` | 5 bands over their own running peak (4 s decay, floor 1e-4): auto-gained 0..1 | hop | I, adaptive | 0 in digital silence; **0.88-0.96 on a -70 dBFS noise floor after about 30 s** [B1b] | as rms | C. Not comparable across bands or songs | **Existing but limited** (auto-gain normalises a noise floor up to full scale) |
| `audio.<band>Level` | long-term band level, 0 = -60 dB, 1 = 0 dB, not auto-gained | hop | I | 0 | as rms | C | Verified existing |
| `audio.spectralCentroid`, `spectralFlux`, `energy`, `onsetRate`, `width` | descriptors 0..1 (onsetRate 0..20/s, width 0..2) | hop | I or S | 0 | | C | Verified existing |
| `audio.onset` | linear-flux onset; strength = min(1, onsetStrength/2) | hop | E | none in digital silence; see §2.5 for noise | ~0 to +1 frame [B1] | T | **Existing but limited:** on a held bass note it fires about every 75-100 ms (13 false triggers in 6 s [B1], as documented in `stem_analysis.hpp`) |
| `audio.onsetLow/Mid/High` | file: ADR-898 offline, look-ahead band onsets. Live: ADR-1060 causal kick/snare/hat | hop | E | none | file: Mid ~0 to +1 frame [B1] | T | **Duplicated or fragmented:** one name, two detectors (§2.3). ADR-898's Low found **2 of 36** kicks on a 45-85 Hz synthetic kick (band 100-300 Hz) [B1; synthetic caveat] |
| `audio.tempo`, `tempoConfidence`, `beat`, `beatPhase`, `beatCount` | tracker (live) or offline DP (file) | hop | E / I | **confidence holds its last value through silence (0.81 [B1])**; the file grid extrapolates | | T / C | Existing but limited: octave error on [B1]'s 120 BPM kick + 60 BPM backbeat (locked 60.1 BPM, confidence 0.81) |
| `beat.phase/pulse/count/bpm/bar/phrase/phrasePulse/section/sectionCount` | THE beat clock: MIDI clock > analysed grid (mix or beat feed) > live tracker | frame | I / E | grid keeps running in silence (pulses every beat after the music stops [B1]); MIDI Stop or 2 s without a tick hands back | pulse on the frame the clock passes the beat | T / C | Verified existing |
| `music.*` (beat, downbeat, bar, phrase, section, energyRise/Drop, build, break, drop, impact) | ADR-063 classifier events | hop | E | none | multi-bar by design | T (rare) | Verified existing |
| `section.index/progress/energy/change` | the authored or analysed section timeline (ADR-899) | frame | I / E | holds | pure in time | C / T | Verified existing |
| `response.kick/low/snare/hat/onset/note` + `*Env` | ADR-1060 causal hits, shaped by the performer's sensitivity; Env = exponential release (0.06-0.22 s × Release) | hop | E + S | none, env 0 | **kick +31 ms, snare +41 ms, low ~0, onset −3 ms** median at 60 fps [B1]. On Feline, snare 50-67 ms after the same hit's `response.onset` [B3] | T (events), C (Env) | Verified existing. The **mix** kick also fires on bass-note and snare attacks (12 + 12 of its 61 triggers [B1]) |
| `response.bass/level/sustain/levelSlow/intensity` | levels in dB against an adaptive loudness reference (ADR-1510), 0..1 | hop | S (followers 15 ms-12 s) | **0 below -60 dBFS; the reference does not move** [B1: 0.000 in silence and noise] | follower attack 15-250 ms | C | Verified existing |
| `response.transient, flux, hatRate, melodic, pitch` | derived rates 0..1 | hop | S | 0 | | C | Verified existing |
| `stem.<key>.active` | 0/1 data present | hop | I | 0 outside the span, absent, stalled > 250 ms | | gate / depth | Verified existing |
| `stem.<key>.rms/peak/level/bands/centroid/width` | as `audio.*` / `response.level`, per source | hop | I / S | 0 when inactive, never a held stale value | as audio | C | Verified existing |
| `stem.<key>.onset` | log-spectral flux onset (not `audio.onset`'s linear flux) | hop | E | none | **−3 ms median, 0 false, 0 misses** on the drum stem; bass-note onsets on the frame [B1] | T | Verified existing. **The best transient in the system** |
| `stem.<key>.kick/snare/hat` | ADR-1060 classifier per source (`hits: true`) | hop | E | none | kick +28 ms, snare +41 ms [B1] | T | Verified existing; 0 confusions on a drum stem [B1] |
| `sonic.<dim>`, `.slow`, `sonic.transient`, `timbre.*` | Sonic Character (15 dimensions, medium and slow tiers), raw timbre | hop | S (tiers) | gated per dimension, but `sonic.energy` reads **0.13 on a -70 dBFS noise floor** [B1] | tiers 0.3-several s | C | Verified existing; noise-gating Existing but limited |
| `notes.*` (32 + voices + pitch classes) | musical context from a note file or live MIDI (ADR-1025/1062), scaled 0..1 | frame | I / E / S | 0 | MIDI → bus p50 13 ms [ADR-1025] | C / T | Verified existing (needs live Sonic or a notes file) |
| `visual.<m>` | interpret mappings: weighted combine → bias/gain/clamp/curve, group competition | frame | I (stateless) | follows inputs | same frame | C | Verified existing |
| `control.<ch>` | MIDI/OSC/UI channel, 0..1 hold or one-frame pulse | frame | I / E | holds the last value | MIDI → bus p50 13 ms [ADR-1025] | C / T | Verified existing |
| `macro.<k>` | the knob parameter `macros/<k>`'s **final from the previous frame** | frame | I (+1 frame) | holds | **exactly one frame late** (max diff 0.0000 against the previous frame's source, at 30 and 60 fps [B1]) | C | **Existing but limited** (ADR-1573 replays it, but the lag remains) |
| `env.<n>` | ADSR+hold envelope source on an event | frame | A | 0 | **+1 frame, and the first visible value is 1 − dt/release (0.72 at 60 fps, 0.44 at 30 fps for 60 ms)** [B1] | C | **Existing but limited (defect, §2.4)** |
| `lfo.*`, `noise.*`, `timeline.*`, `random.*` | time and beat functions; S&H on a trigger | frame | I / A | n/a | value timelines on the frame; event timelines ceil-quantised, 0-1 frame [lead] | C / T | Verified existing |
| `time.*`, `state.*`, `camera.speed`, `field.*`, `entity.*` | transport, the state machine, scene-derived | frame | I / E | | entity: +1 step (ADR-703) | C / T | Verified existing |

### 2.2 Overlaps with demonstrated semantic difference (do not merge blindly)

| Concept | Signals | Same thing? | Evidence |
|---|---|---|---|
| "a beat" | `audio.beat`, `beat.pulse`, `music.beat`, `music.downbeat` | No. `audio.beat` is the mix tracker's own beat (file: the offline DP, live: the causal tracker). `beat.pulse` is THE clock, which can be MIDI or a stem's grid. `music.beat` is the classifier's copy of it. | `advanceClock` l. 6093; [B1]: `audio.beat` and `beat.pulse` both followed the 60 BPM octave error. Recommend authoring on `beat.*` only and hiding `audio.beat` from the route picker (P2). |
| "a kick" | `audio.onsetLow`, `response.kick`, `response.low`, `stem.<k>.kick` | No (§2.3) | [B1] confusion table |
| "loudness" | `audio.rms`, `audio.<band>Level`, `response.level`, `stem.<k>.level`, `sonic.energy`, `timbre.loudness` | No. Linear, dB-fixed, dB-adaptive, character tier and raw dB are deliberately different. | ADR-1510 is the one to author on |
| "phrase" | `beat.phrasePulse`, `music.phrase`, `notes.phraseStart` | No. Metre, classifier and note gaps respectively. | |

### 2.3 Transient detectors: five, and where each one wins

| Detector | Code | Signals | Live? | On the drum stem [B1] | On the mix [B1] | Decision latency [B1, 60 fps] |
|---|---|---|---|---|---|---|
| Linear-flux onset, median×1.5+0.02, 60 ms refractory | `analyzer.cpp` | `audio.onset` | yes | | 136 triggers: 43 kicks, 20 snares, 18 bass, **55 elsewhere** (a held tone beats) | 0 to +1 frame |
| Log-spectral-flux onset | `stem_analysis.cpp` `detectOnset` | `stem.<k>.onset` | yes | 53/53, 0 false | (not run on the mix) | −3 ms median |
| ADR-1060 causal classes (SuperFlux + HPSS-median, kick rise + click + grid, snare deferred noise) | `causal_onsets.*` | `response.*` hits, `stem.<k>.kick/snare/hat` (hits tier), live `audio.onsetLow/Mid/High` | yes | kick 36/36, snare 17/17, 0 confusions | kick: 36 kicks + 12 snares + 12 bass notes + 1 elsewhere | **kick +28-32 ms, snare +41-44 ms**; on Feline the snare is 50-67 ms behind the broadband onset [B3] |
| ADR-898 offline band onsets (look-ahead HPSS, grid evidence) | `band_onsets.*` | file `audio.onsetLow/Mid/High`, `AudioHistory` onsets, `TriggerClock` | **no** | | Low: **2 of 36** (low-tuned kick); Mid: 17/17 snares + 21 on kicks | file only |
| Character transient | `character.cpp` | `sonic.transient` | yes | | | not measured |

**Reading:**
- On a separated drum source, the log-flux onset is fast and clean.
- The classifier is clean but costs about 30 ms (kick) or 43 ms (snare) by construction. The kick waits for its low-band
  rise to peak; the snare waits 4 hops for noise confirmation.
- On a full mix, every class confuses bass attacks with kicks. That is the measured case for instrument-aware input.

The fixture is synthetic (a 45-85 Hz kick, a noise snare, a 55-73 Hz bass). The ADR-898 Low miss rate in particular may
not transfer to real kicks with energy in 100-300 Hz, so treat it as "Unknown on real material".

### 2.4 The envelope-source defect (a shared primitive)

`EnvelopeSource::update` (`src/signals/source.cpp` l. 320-401):
1. advances the stages by `dt`;
2. only then sees this frame's trigger (`stage_ = Attack`);
3. publishes `level_`, the value from before the trigger.

The next frame's advance runs the attack, which is instant at 0 ms, and then **a full frame of decay or release** before
publishing. So:
- **The trigger reads one frame late.** Confirmed: the rise frames of `env.dKick` and of a route-chain envelope into a
  macro knob (itself +1) coincide exactly, 36 of 36 [B1].
- **The peak is never shown.** The first visible value is 1 − dt/release, so the amplitude depends on the frame rate:
  0.72 at 60 fps, 0.44 at 30 fps for a 60 ms release [B1].

ADR-900 fixed exactly this for the route chain ("the frame the rise completes in shows the level in full"); the source was
never given the same rule.

**Source order** (the coordinator's question) adds no further frame for any event that reaches a source. `SourceRack`
runs publishers first and trigger-readers second, and every event family a trigger can name is published before
step 5. The exception is §1.3's set of late events (`field.*.enter/exit`, `entity.*`, staging, cinematic): they are
published after the rack and cleared at step 15. A source or state trigger therefore never sees them [code; not probed].

**Who uses `EnvelopeSource`:**
- Digital Ocean (2) [examples];
- the ADR-1571 trace convention;
- anyone following `docs/control.md`.

Route chains are the common path, which is why this survived.

### 2.5 Silence, missing input and abrupt changes

| Situation | Behaviour | Evidence | Verdict |
|---|---|---|---|
| Digital silence | `response.*`, `stem.*.level`, `audio.*` read 0; envelopes finish their release (`response.sustain` tail 0.14 → 0 over about 1 s) | [B1] | good |
| -70 dBFS noise floor, mix only | `response.level`, `response.bass` and `stem.*` stay 0 (gate -60 dBFS). `audio.treble` 0.004 and `audio.onsetStrength` low within 3 s. `sonic.energy` **0.13** | [B1] | response good; `sonic.*` not noise-gated |
| Long noise floor (80 s at -70 dBFS, after 18.8 s of music) | **`audio.mid` and `audio.treble` climb to 0.88-0.96 (and `audio.bass` to 0.40) once the running peak has decayed, 25-30 s in.** `audio.rms` 0, `audio.onset` and `response.onset` never fire; `response.level/sustain` 0; `sonic.energy` 0.126 throughout | [B1b] | **defect for live use**: any route on `audio.<band>` lights to full in a long quiet passage on a noisy input. `response.*` is the safe vocabulary |
| Music stops | beat grid keeps pulsing (by design, ADR-1581); `audio.tempoConfidence` holds 0.81 indefinitely | [B1] | **confidence is not a liveness signal: nothing says "the beat is gone"** |
| Stem outside its span or absent | `stem.<k>.active` = 0, every value 0, no events; feeds read 0 levels and no hits | [code], ADR-1580 | good |
| Live channel stalls or disconnects | inactive after 250 ms; the beat clock free-runs at the last tempo | `publishInputSources` l. 5908 | good |
| MIDI controller unplugged | `control.*` holds the last value; no "stale" flag | [code] | Existing but limited |
| MIDI clock stops | Stop, or 2 s without ticks, hands back to analysis | `docs/control.md` | good |
| Seek / loop (file) | full replay of the signal and control layers (ADR-870/1168/1573) | tests `[adr1168]`, `[adr1573]` | good; cost grows with song position |
| Live input "seek" | control layer reset, not replayed (ADR-1168) | | expected |


## 3. Modulation architecture (brief §7.2)

### 3.1 Mechanism by mechanism

| Need | Mechanism | Where | Shared / scene / duplicated | Status |
|---|---|---|---|---|
| Source selection | route `source` by name, bound once; ADR-1570 `optional` and `depthDefault` | `Modulator::bind` | shared | Verified existing |
| Normalise / remap | chain `normalizeSeconds` (ADR-1182), gain/offset, `remap*`, polarity | `ProcessorChain` | shared | Verified existing |
| Curves, thresholds | Linear/Power/Log/Exp/SCurve; Gate/Binary/Subtract | same | shared | Verified existing |
| Smoothing / envelopes | asymmetric one-pole (frame-rate independent); event ramps (ADR-900); PeakHold/LinearFall + latch (ADR-1574); spring (ADR-1041) | same | shared; **plus** `EnvelopeSource` (defect §2.4), `ResponseModel` envelopes, Astral `SongAnalysis` envelopes | Duplicated or fragmented |
| Accumulation, decay, reset | `integrate` with bounds (ADR-1041/1161); trend STA/LTA (ADR-1575); reset on seek, replayed | same | shared | Verified existing |
| Output range, clamping | `clamp*`, parameter hard ranges, op order | same | shared | Verified existing |
| Depth / gating | `depthSource`, `depthMin/Max/Default` (ADR-900/1570); field `spatialGain` (ADR-097) | `ModRoute` | shared | Verified existing |
| MIDI control | bindings → `control.<ch>` (routable) or parameter base (direct) | `ControlHub::applyTarget` | shared | Existing but limited (§4) |
| Event → parameter | route on an event + envelope; `TriggerClock` for effects; state triggers | | shared | Verified existing |
| Targets: world, entity, material, renderer | any modulatable parameter path: `nodes/<n>/…`, `material/<m>/op/<i>/…`, `field/<f>/…`, `post/…`, `camera/…`, `palette/…`, `macros/…`, `sources/…` | `ParameterSet` | shared | Verified existing |
| Many-to-one blending | `interpret` → `visual.*` (stateless) | `interpret_source.*` | shared | Verified existing |
| One-to-many fan-out | macros (+1 frame), world macros (ADR-031/522) | `MacroSource`, `scene_states.*` | shared | Existing but limited |
| Named conditioned signal (stateful, same frame) | **none** | | each scene copies chains | **Missing** |
| Inspection | `lastOutput` per route row; `--audit-routes` liveness (ADR-902); `--sonic-trace` | `control_panel.cpp` l. 1774/2110 | shared | Existing but limited (§8) |

### 3.2 Many simultaneous routes: CPU cost [B2]

Probe B2 builds the orb scene with the B1 fixture's mix and stems, plus N routes. The routes rotate through four of Rainbow
World's real chain shapes:
- a smoothed level;
- a latched onset envelope;
- the trend/threshold/peak-hold/spring scan chain;
- a plain remap.

`Engine::update` per frame, as `--sonic-trace` reports it at 60 fps, 3 runs each:

| Routes | Engine update per frame | Per route |
|---|---|---|
| 0 | 0.01 ms | |
| 250 | 0.01 ms | |
| 1,000 | 0.04 ms | about 30 ns |
| 4,000 | 0.13 ms | about 30 ns |

**Conclusions:**
- **Route evaluation is not a CPU concern at Living Prism scale.** 1,000 routes cost 0.03 ms of a 16.7 ms frame. The
  orb scene isolates the chains, and Rainbow World's 405 routes cost about 0.01 ms of chain work.
- **The cost that does scale is the seek replay.** ADR-1168/1573 replay the control layer from 0 to the target on the
  play's frame grid whenever a route writes `macros/` or `sources/`. At about 30 ns per route-frame, 1,000 routes and a
  seek to 3:00 at 60 fps (10,800 frames) is about 0.33 s of routes alone [est]. ADR-1168's measured 184 ms at 200 s was
  for Digital Mosh. A conditioned-signal pass (P0-1) would not change this, because the replay is required for any
  stateful chain. It is a reason to keep it in mind for scrubbing.
- **A side finding: a liveness false positive.** Every trend-detector route (ADR-1575, the scan chain) was reported dead
  by the `flat-chain` rule. The rule probes constant inputs, and a trend of a constant is 1, so it never crosses a
  binary threshold above 1. 1,000 warnings at load for 4,000 routes. The rule does not model the trend stage (§11, R7).

### 3.3 Duplication, measured on Rainbow World (`examples/rainbow-world/rainbow-world.json`)

- **405 routes.**
  - By source: 179 `macro.*`, 75 `stem.*`, 70 `response.*`, 40 `timeline.*`, 22 `control.*`, 18 `beat.*`.
  - 10 routes write `macros/*`; the rest fan out from them.
- **Chain copies.** `stem.bass.onset` with the same latch chain (linearfall 2.4/s, refractory 130 ms, rearm 0.05) and the
  same depth (`stem.bass.level`, -0.3..1.9) appears on `macros/swell`, `field/skyBreath/strength` and
  `material/rw_cloud/op/26`. That is three chain states computing one signal. The project's own history (commit
  `e03f9107`) took the clouds off the macro precisely to avoid its one-frame lag.
  Likewise, one MIDI fader (`control.glow`) drives **16 routes with identical 150 ms chains**: 16 states for one knob.
- **Fallback doubling.** Every stem effect has a twin on `response.*` with `depthSource: stem.X.active`, depth 1 → 0 and
  default 0 (ADR-1570's fallback idiom). For example, `stem.drums.highMid` and `response.snareEnv` are both on
  `material/rw_rainbow/op/9`.
- **Origin.** The routes are generated by `build_world.py` (116 kB), so scene-specific Python is doing the work of a missing
  shared abstraction.
- **The offline Feline video** (`~/Desktop/Feline Footwear/feline-footwear.json`, 600 routes) went further. It replaced
  the live-derived scans and bass notes with **baked timeline sources** (`timeline.scanFront`, `timeline.bassNotes`):
  the best-looking reactivity in the repository was authored offline, not derived.
- **Live collection.** Most live scenes are 10-100 routes on `response.*`, `control.*` and `visual.*`. Digital Ocean (777
  routes; 320 `control.*`, 304 `visual.*`) and Sonic Live (452; 417 `visual.*`) are the outliers. Astral Forge Live has
  **0 routes**: its reactivity is a bespoke conductor over its own `SongAnalysis` (`src/scene/astral_song.*`), a
  scene-specific re-implementation of band envelopes and hit envelopes. ADR-1361 had to retrofit the Response controls
  into it.

**Verdict:**
- The per-route chain is the right shared primitive, and its feature set already matches or exceeds Notch, Resolume and
  TouchDesigner (§9.6).
- What is missing is the ability to **name a chain's output once and reuse it the same frame**. Scenes work around that
  with macros (lag), copies (cost and drift), Python generators (opacity) or bespoke C++ (Astral).
- The division between `response.*` as the shared hit/level semantics and scene routes as the art is deliberate and
  beneficial. The duplication is not deliberate.


## 4. MIDI (brief §3B.3)

| Capability | Status | Evidence |
|---|---|---|
| Note on/off, velocity, channel, source filter | Verified existing | `matchMidi` (`control_map.cpp` l. 244); note → 0..1 velocity, noteEvent → pulse |
| Pitch, register, chord, voices | Verified existing with live Sonic: `notes.*` (pitch classes, chord, tension, low/high split, 8 voices); without it, only per-note bindings | ADR-1025/1062 |
| CC | Existing but limited: **7-bit only**; no 14-bit CC or NRPN, no relative encoders, **no soft takeover or pickup**, **no MIDI output** (LED or motor feedback) | no symbols found (`grep` takeover/NRPN/MIDIOutput) |
| Pitch bend, pressure, program | Verified existing; program change switches scenes under live Sonic (ADR-1063) | |
| MIDI clock | Verified existing: median-gated tempo, extrapolated phase, Start/Stop/Continue; wins the beat clock | `midi_clock.cpp`, `docs/control.md` |
| Timing | messages are drained **once per render frame**; values land on the next frame boundary. MIDI → bus p50 13 ms, p90 23; → present p50 18 ms | [ADR-1025] |
| Smoothing | none on direct parameter bindings (127 zipper steps). Through `control.*` + a route chain, any smoothing; Rainbow World uses 150 ms | [code] |
| Learn | the Control tab's learn binds the last message to a channel or parameter | `docs/control.md` |
| Seek | MIDI is live state, not replayed; a direct parameter binding writes the base, so a saved project keeps whatever the knob last set | ADR-1168 |

## 5. Instrument-aware input and stems (brief §7.3)

| Question | Answer | Status |
|---|---|---|
| Input routing and channel selection | Live: `Engine::setInputSources` maps channels or linked pairs to keys. AUHAL captures every channel of every input stream (ADR-1522). The main mix is selectable (`setInputMainChannels`). Mapping is saved per device in settings. Offline: `assets.stems` with `offset`, `hits`, `gainDb`. | Verified existing (hardware-tested on a 32-channel Volt 876, `stems-multichannel/06-report.md`) |
| Timestamps and sync | Offline: each stem frame carries its own clock + `offset`, published at the transport second (`publishTrack`), so it is sample-accurate to the stem file. Live: one device, one HAL sample clock; every source's ring is written whole-block-or-none in the same callback, with discontinuity markers after a drop or a sample-time gap. | Verified existing |
| Can stems align with playback? | Yes, by construction (same transport second). Misalignment is an authoring error (`offset`). `audio::checkStem` / `mixDiagnostics` warn on a stem that does not match the mix. | Verified existing |
| How per-input signals enter the bus | One vocabulary, `stem.<key>.*`, for both a file and a channel (`StemSignals`), so a route written offline plays live on a channel keyed the same. Feeds route the beat clock, hits and levels. | Verified existing |
| Multiple sources without duplicating the pipeline | `StemAnalyzer` is a reduced `Analyzer`: levels tier about 16 µs per hop, hits tier about +80-100 µs. No beat tracker or timbre per source unless a feed asks. Eight sources with hits on all of them: 10.4-12.1% of a core. | Verified existing ([doc] `05-benchmarks.md`) |
| Input disappears, changes or goes silent | `active` 0, values 0, no events, feeds fall back; the beat clock free-runs. A routed scene needs the ADR-1570 fallback idiom to stay alive, which is the doubling of §3.3. | Verified existing (behaviour); authoring cost Existing but limited |
| Same semantics live and offline | `stem.*`: yes (same analyzer). `response.*` via feeds: yes (ADR-1580 "pure functions of the second"). `audio.onsetLow/Mid/High`: **no** (ADR-898 on file vs ADR-1060 live). GPU `AudioHistory`: mix only in both. | Duplicated or fragmented |
| What feeds do not reach | `audio.*` (always the mix), `response.melodic/pitch/note`, the GPU spectrum/onset fields (ADR-1116), `sonic.*`/`timbre.*`, the Effect Library's derived `audio.onset*` triggers | Existing but limited |
| Adoption | stems: Rainbow World only. `feeds`: **no live example**; only the offline Feline project (`{"beat":"stem.drums"}`). | |

**Sequencing:**
- **This milestone:** nothing new in input routing. The Living Prism needs:
  - the conditioned signal (P0-1), so "drums hit / else mix kick" is authored once;
  - a decision on which transient to author percussive impacts on (P1-1);
  - letting `AudioHistory` take a feed (P1-4, a signal-side change with a GPU-side cost owned by A).
- **A separate audio-input milestone:** live source separation. Rejected for now (§9.5): it costs too much latency for
  live use.


## 6. Timing: the audio → visual latency budget

### 6.1 What happens to a drum hit, live input (the performance case)

| Stage | Cost | Kind | Source |
|---|---|---|---|
| Device input latency + safety offset | 74 + 74 frames = 3.1 ms on the Volt 876 at 48 kHz; 0 on BlackHole | fixed | [doc] `06-report.md` |
| Capture period | 128 frames = 2.7 ms (ADR-1025), or the device's current buffer when another app holds it (ADR-1550) | 0-1 period | ADR-1025/1550 |
| Analysis window | the attack must reach the window; the window is stamped at its centre and complete 1024 samples later: **21.3 ms** group delay | fixed | `docs/audio.md`; ADR-1025 §8 |
| Hop quantisation | 0-10.7 ms (mean 5.3) | uniform | analyzer |
| Runner poll + ring + render-frame wait | p50 5.8-7.0 ms, p99 11.7-15.7 ms (at 30 fps) | measured | [doc] `05-benchmarks.md` (onset latency) |
| Decision latency of the detector | broadband onset 0; **kick +28-32 ms; snare +41-44 ms** (synthetic), **+50-67 ms behind the broadband onset on Feline** | detector | [B1], [B3] |
| Chain | 0 (an event ramps on its own frame); an attack of N ms adds N ms to full | authored | ADR-900 |
| Macro or `env.*` hop | **+1 frame** each if used | architecture | [B1] |
| CPU update + record, GPU, Fifo present, scanout | 1-2 frames + scanout | renderer | **handoff: Sub-Agent A.** Swapchain Fifo (`gpu/surface.cpp` l. 70); ADR-1025 measured analysis-to-present p50 29 ms "and the frame's GPU work and one display refresh follow present" |

**Totals, live [est]:** built from the measured parts above, at 60 Hz, assuming one frame of GPU and queue and half a
refresh of scanout. The measured end-to-end anchor is ADR-1025: audio → bus p50 24 ms, → present p50 29 ms, for a
broadband/timbre response.

- Broadband (`stem.<k>.onset`, `response.onset`) on screen: **≈ 45-55 ms**.
- `response.kick` / `stem.<k>.kick`: **≈ 75-85 ms**.
- Snare classes: **≈ 90-95 ms** on the synthetic kit; **≈ 110-130 ms** on Feline, where the class lands 50-67 ms
  after the broadband onset [B3].
- Each macro or `env.*` hop adds 16.7 ms.

Against ITU-R BT.1359's detectability threshold for sound leading picture (+45 ms, §9.3), broadband is at the edge and the
classifier hits are past it. **No end-to-end photon measurement exists** (§8; gap G9).

### 6.2 File playback in the GUI (rehearsal, and what the owner usually watches)

The visual frame time is the play-head (`AudioPlayer::positionSeconds`: frames consumed by the callback).
- **Output latency is not compensated**, so what is heard trails the play-head by the output buffer plus the device latency
  (≈ 10-30 ms [est]).
- **Track-read families** (`response.*`, `stem.*`, beats, ADR-898 onsets) are read at the play-head from windows centred
  at or before it.
  - Their broadband onsets land −3 to +1 frame of the true onset [B1].
  - So on file the picture leads or matches the sound, and the classifier hits are +28-44 ms.
- **`audio.*` alone** comes from the causal runner and trails the play-head by the window half, the hop and the ring
  (≈ 25-35 ms [est]).

### 6.3 Frame quantisation and the one-frame hops

| Item | Effect | Source |
|---|---|---|
| Value-mode timeline | on the frame | [lead] |
| Event-mode timeline, route envelope | fires on the first frame whose (prev, this] interval holds the hit: ceil, 0-1 frame, mean ½ | [lead]; ADR-900 |
| Bus events from analysis | the frame at or after the analysis hop (OR-ed, never lost at 30 fps) | ADR-898; [B1] |
| `macro.*` | +1 frame exactly | [B1]; ADR-1573 |
| `env.*` source | +1 frame and peak loss | [B1] |
| Entity signals | +1 step | ADR-703 |

**A candidate the coordinator asked me to assess: nearest-frame firing for pure-in-time event timelines.**
- It would halve the mean error, from ½ frame to ¼.
- It would make a hit able to fire *before* its time by up to ½ frame, which ADR-900 rejected so that a hit cannot
  precede its cause.
- For authored hits that align with audio, early by ≤ 8 ms is perceptually better than late (BT.1359 is asymmetric: the
  picture may lead by far more than it may lag).
- **Recommendation: P2.** It is cheap and correct only for timeline sources; the audio events are decided at hop
  instants, and those are already the frame at or after.

## 7. Musical structure, temporal memory and spatial propagation (brief §7.4, §8, §8.2 signal side)

| Need | Existing support | Demonstrated in | Gap / where it belongs | Status |
|---|---|---|---|---|
| Short transient response | events + chain envelopes (latch), `response.*Env` | every live scene | detector latency (§2.3) → shared analysis | Verified existing |
| Sustained modulation | `response.*` levels, `stem.*.level`, `sonic.*` tiers, followers, spring | Rainbow World synth light, Sonic scenes | none | Verified existing |
| Beat-synchronised events | `beat.*` from MIDI, grid or tracker; beat-synced LFOs; quantised state transitions; Effect triggers on beats | Digital Mosh, Rift | octave errors; no "beat lost" signal → shared analysis | Existing but limited |
| Phrase-scale change | `beat.phrase*`, `section.*`, `music.build/drop/break`, palette selector (ADR-1420), trend stage (ADR-1575), scene states with Phrase/Section/Elapsed triggers | Rainbow World looks and scans; Digital Mosh states | phrase = metre (4 bars) not detected structure, live | Existing but limited |
| Long-lived scene state | state machine (preset morphs, ADR-031/1164); bounded integrals | Digital Mosh (21 states), Rift (12) | none | Verified existing |
| Accumulated activity with decay | `integrate` with bounds, peak-hold, trend | Digital Mosh camera integral | an accumulator shared by many targets is again a macro (+1 frame) → P0-1 | Existing but limited |
| Spatial propagation | **signal side exists:** (1) `onset`/`spectrum` field kinds with `delay = audioDelay + d/audioSpeed` (ADR-1116); (2) field clocks from the latest event (ADR-906); (3) Effect Library Shockwave/Ripple on `signal` triggers (ADR-1061); (4) resonance readers and onset ripples (ADR-1400-1409) | Echo Field, Resonator / RD scenes, GV3 waves | `AudioHistory` hears **only the mix**, not a feed or a stem (`Engine::updateAudioHistory` l. 7717; `buildAudioHistory`), so a "drum front" or a "bass wave" cannot be keyed to its instrument. **Handoff to A:** the GPU cost of extra history rows | Existing but limited |
| Graph propagation (an event travelling across connected elements) | not found as a signal primitive. `delay` per route (≤ 4 s) can stagger N targets by hand. | | scene-specific until the spec asks; a reusable "ordered stagger" is a P2 authoring helper, not engine work | Missing (low priority) |
| Controlled manual intervention | MIDI/OSC `control.*`, macros, `goToState`, preset morph, Response controls, palette lock | Rainbow World CC 1-10; RD scenes | soft takeover, MIDI feedback | Existing but limited |

**Live versus file for memory:**
- Everything above is seek-exact on file.
- Live, trigger lists are recorded (ADR-1061) and the control layer resets on a seek, which is correct since live input has
  no seek.

## 8. Diagnostics: can a developer prove a signal reaches a parameter? (brief §3B.8, §13)

| Question | Tool | Status |
|---|---|---|
| Is audio arriving? | Live panel lights and meter; `--input-meter`; the 10 s `live input sources:` log with onset latency | Verified existing |
| Is MIDI arriving? | Control tab: last message, matched/unmatched counters; Live panel MIDI → frame ms | Verified existing |
| Does a route bind, and can it reach the picture? | `--audit-routes` liveness verdicts (ADR-902), `firstReport` log-once, route row `lastOutput` | Verified existing |
| What did a signal do over a song? | `--sonic-trace` CSV (sonic, timbre, notes, response, visual, beat, music, stem, env, macro) | Existing but limited: no `audio.*`, `control.*`, `lfo.*`, `field.*`; no route outputs; no parameter finals; `env.*` columns lag and attenuate |
| What is a route doing **now**, over time? | numeric `lastOutput` only | **Missing:** no scope (a source, its chain stages and the final as time plots) |
| Which signal moved this pixel? | none | Missing (a reverse lookup, parameter → routes → sources, exists only as the route list filtered by hand) |
| End-to-end latency, sound → light | none (partial probes `tools/sonic_live_latency.py`, ADR-1025) | Missing (gap G9) |
| Reactivity strength ("does it read?") | Rainbow World 23's manual method (fire counts from the trace + visibility judgement); `tools/check_response_range.py` for level ranges | Experimental (no shared tool) |


## 9. Research (brief §9 D, E) — only what changes a decision

Each item gives:
- the problem;
- the evidence, with sources and dates;
- the decision it drives;
- its compatibility, cost and alternatives.

"[inference]" marks my reasoning, as opposed to what a source states.

### 9.1 Stable, fast transient detection

**Problem.**
- The classifier hits are 28-44 ms late by construction [B1].
- Broadband linear flux false-fires on held tones [B1].
- The Living Prism's "percussive impacts" need both stability and speed.

**Evidence.**
- **Bello et al. (2005)**, "A Tutorial on Onset Detection in Music Signals", *IEEE Trans. Speech and Audio Processing*
  13(5). It compares energy, spectral flux, phase and complex-domain detection functions. Pitched and soft onsets defeat
  energy and linear-magnitude flux, while percussive onsets are easy for any of them.
- **Dixon (2006)**, "Onset Detection Revisited", DAFx-06. Spectral flux with a logarithmic magnitude, peak-picked against
  an adaptive (median) threshold, is among the best simple detectors.
- **Böck & Widmer (2013)**, "Maximum Filter Vibrato Suppression for Onset Detection" (SuperFlux), DAFx-13. Max-filtering
  the previous frame across frequency removes most false positives from vibrato and held tones, with no added latency.
- **Böck, Krebs & Schedl (2012)**, "Evaluating the Online Capabilities of Onset Detection Methods", ISMIR 2012. Causal
  detectors lose accuracy mainly through how much future context peak-picking needs. Low-latency online detection is
  feasible, but every frame of look-ahead or confirmation is latency.

**What the code already matches.**
- `stem.<k>.onset` is Dixon's log flux plus a median threshold.
- ADR-1060 uses SuperFlux and HPSS-median.
- So the detectors are textbook. The latency comes from **classification by confirmation**: waiting for the kick rise to
  peak and for the snare's noise floor 43 ms later.

**Decision (P1-1).**
- For Living Prism impacts, author on the fast broadband onset of a separated source (`stem.drums.onset`). Without a
  source, author on `response.onset`.
- Treat class (kick, snare, hat) as an **attribute that may arrive later**, not as the trigger.
- The small shared addition is a per-onset **band share at the onset frame**: low/mid/high energy of the attack, which the
  analyzer already has. A scene can then colour or size the impact by register immediately. When a later classifier
  disagrees, it can only adjust the effect's tail [inference].

**Alternatives considered.**
- A neural onset detector (madmom's RNN/CNN; Böck et al., "madmom", ACM MM 2016). It is more accurate on hard material but
  heavier, and it is not needed on separated sources, where log flux already scores 53/53 [B1].
- Shortening the 2048 window. Rejected by ADR-1025: it breaks live/file equivalence and moves only about 10 ms.

**Cost.** About 0 CPU; S effort. It touches `StemAnalyzer`/`ResponseModel` and adds an event strength channel.

### 9.2 Beat confidence and tempo stability

**Problem.**
- [B1] locked a clear 120 BPM kick pattern at 60 BPM, with confidence 0.81.
- The confidence also holds through silence.
- Nothing tells a scene "the beat is wrong" or "the beat is gone".

**Evidence.**
- **Ellis (2007)**, "Beat Tracking by Dynamic Programming", *J. New Music Research* 36(1). The offline DP AV Gen uses; the
  tempo prior is the only octave defence.
- **Davies & Plumbley (2007)**, "Context-Dependent Beat Tracking of Musical Audio", *IEEE TASLP* 15(3).
- **Stark, Davies & Plumbley (2009)**, "Real-Time Beat-Synchronous Analysis of Musical Audio", DAFx-09 (BTrack). The
  established causal design, which AV Gen's tracker resembles.
- **Zapata, Davies & Gómez (2014)**, "Multi-Feature Beat Tracking", *IEEE/ACM TASLP* 22(4). The mean mutual agreement
  between several trackers predicts tracking quality without ground truth. Disagreement flags octave and phase errors that
  a single tracker's salience cannot.

**Decision (P2-2).**
- Do not add a new tracker.
- AV Gen already runs several trackers that could agree or disagree:
  - offline DP versus causal;
  - mix versus beat feed (ADR-1581);
  - analysis versus MIDI clock.
- Publish a `beat.confidence` derived from their agreement, plus a `beat.present` that decays when onsets stop. This is
  cheap: a comparison of beat times [inference].
- For the performance itself, **MIDI clock is the robust answer and already wins the clock**. That is a workflow
  recommendation, not engine work.

**Alternative: Ableton Link.** The common live-sync protocol, but its SDK is dual-licensed (GPLv2+ or a proprietary
licence from Ableton), and AV Gen is MIT. **Rejected by the owner (2026-10-22: "no Ableton Link support").**

### 9.3 How late is too late: the latency budget's target

**Problem.** §6 estimates that classifier hits reach the screen about 75-95 ms after the sound. Is that acceptable?

**Evidence.**
- **ITU-R BT.1359 (1998)**, "Relative timing of sound and vision for broadcasting". Detectability thresholds are about
  +45 ms (sound before picture) to −125 ms (sound after picture). Acceptability is about +90 / −185 ms. These come from
  speech material.
- **ATSC IS-191 (2003)**: sound should not lead picture by more than 15 ms or lag it by more than 45 ms. Stricter, and it
  states that BT.1359 was found inadequate for DTV.
- **Wessel & Wright (2002)**, "Problems and Prospects for Intimate Musical Control of Computers", *Computer Music
  Journal* 26(3). It argues for about 10 ms latency with about 1 ms jitter for gesture-to-sound control.

**Decision.** A live audio-reactive picture is always "sound before picture". So:
- **Proposed target, for owner approval:** sound → light ≤ 45 ms p50 for the impact class.
- This is reachable only with broadband onsets, no extra frame hops (P0-1 and P0-2 remove two) and the renderer's
  present queue (A's domain).
- Classifier hits at about 80 ms are detectably late on screen. Reserve them for slower responses: tails, colour, counts.
- The asymmetry also says **picture slightly early is better than late**. That supports nearest-frame firing for authored
  timeline hits (§6.3; P2-3) and supports compensating output latency in file playback (P1-5) so the picture does not lead
  by 10-30 ms more than it should.

### 9.4 Latency-aware smoothing

**Problem.** Smoothing trades jitter against lag. 7-bit MIDI CC zippers; envelopes of audio levels lag.

**Evidence.** **Casiez, Roussel & Vogel (2012)**, "1€ Filter: A Simple Speed-Based Low-Pass Filter for Noisy Input in
Interactive Systems", CHI 2012. Its cutoff rises with the signal's speed, giving low jitter at rest and low lag in fast
motion. Two parameters.

**What AV Gen has.**
- An asymmetric one-pole (separate attack and decay, frame-rate independent).
- Event ramps that never eat an event (ADR-900).
- A critically damped spring (ADR-1041).

These cover audio levels well.

**Decision (P2-4).** A "1€" smoothing mode in the chain, meant for **MIDI CC and OSC faders**, removes zipper without the
150 ms lag Rainbow World pays. It is a few lines in `ProcessorChain` [inference]. Not a P0: nothing breaks without it.

### 9.5 Instrument-aware analysis and stems

**Problem.** The brief's direction is: designate drums, bass and synth; analyse stems; play the mix.

**Evidence.**
- **Rouard, Massa & Défossez (2023)**, "Hybrid Transformers for Music Source Separation", ICASSP 2023 (HT Demucs).
  State-of-the-art separation uses seconds of context and a GPU-class model.
- **Stöter et al. (2019)**, "Open-Unmix", *JOSS* 4(41). A lighter LSTM model, still offline in practice.
- Neither offers a causal, low-latency mode suitable for a ≤ 45 ms budget [inference from their architectures].

**Decision.** Keep AV Gen's existing architecture:
- **Live:** mapped live channels from a mixer or DAW.
- **Offline or rehearsal:** pre-rendered stems.

It is the only one compatible with the latency target. Live separation is rejected for this milestone and belongs to a
separate audio-input milestone if ever. The engine work that remains is **reach**: feeds into `AudioHistory` (P1-4) and the
conditioned signal (P0-1) to stop the fallback doubling.

### 9.6 How Notch, TouchDesigner, Resolume and Synesthesia do mapping, diagnostics and live workflow (§9 E)

| Tool (source, date) | Mapping model | Reusable derived signal | Diagnostics | What it means for AV Gen |
|---|---|---|---|---|
| **Synesthesia**: "Audio uniforms" (synesthesia.live/docs/ssf/audio_uniforms.html, undated, read 2026-10-22) | Fixed semantic tiers per band: **Level** (smoothed, normalised), **Hits** (isolated transients), **Presence** (rising or falling activity), **Time** (a clock that runs faster when loud), plus BPM, **BPMConfidence**, beat toggles and random-on-beat, Intensity, FadeInOut | uniforms are global and shared by every shader | the uniforms' monitor | AV Gen already has every tier: `response.*` levels and hits, the trend stage (Presence), `integrate` (Time), `beat.*`, `response.intensity`. **No new signal names are needed.** Synesthesia's `BPMConfidence` meaning "tempo stability" supports §9.2. |
| **TouchDesigner**: Palette:audioAnalysis (docs.derivative.ca, last edited 2022-05-23); CHOP networks | analysis CHOP channels (Low/Mid/High with threshold, smooth, gain; Kick, Snare, Rhythm with threshold), then any CHOP chain, then many parameter references | **yes: a CHOP channel is a named, conditioned, stateful signal that any number of parameters reference the same frame.** "Active" toggles skip unused analysis | every operator has a live viewer of its channels over time | The pattern AV Gen lacks is the named derived signal (P0-1) and the per-node live viewer (P1-2). AV Gen's `sought` (ADR-951) already plays the "Active" role. |
| **Resolume Arena**: "Parameter Animation" (resolume.com/support, v7.x, undated) | per parameter: FFT source = composition, clip, layer, group or **external device + channels**; band L/M/H or in/out points; Gain; **Fall** | Dashboard dials control several parameters (one-to-many) | the spectrum display in the parameter | Per-parameter source selection is what AV Gen's stems and inputs plus routes do. No derived-signal layer. AV Gen's chain is richer. |
| **Notch**: "Sound Modifier" and "Sound FFT Modifier" (manual.notch.one 2026.1/2026.2) | a modifier node per use: band, **Attack, Decay, Change Threshold, Min/Max, Spikiness/Smoothness**; the FFT modifier triggers on a drawn frequency/amplitude region with its own envelope | modifier nodes are graph nodes, so one modifier's output can drive several parameters | node previews | Same lesson as TouchDesigner: conditioning lives in a reusable node, not per parameter. A "trigger on a drawn spectral region" is close to AV Gen's band onsets; not needed. |

**Conclusion of §9.6.**
- AV Gen's per-route chain is at least as capable as any of these products' per-parameter conditioning.
- What the node-based tools all have that AV Gen lacks:
  1. a conditioned signal computed **once** and referenced by many targets in the same frame;
  2. a **live view of any signal over time**.

  Both are §12's P0-1 and P1-2.
- None of these tools derives class-specific hits faster than AV Gen does. Their "Hits" are broadband per band, which
  matches §9.1's recommendation to trigger on fast broadband onsets.



## 10. The four end-to-end traces

**Common to all four traces:**
- They use **Rainbow World's live project** (`examples/rainbow-world/rainbow-world.json`, `sonic.live: true`, a live
  scene in the Examples > Live menu), played from Feline Footwear through probe B3.
- Two variants:
  - **mix:** no stems, so every route takes its `response.*` fallback, which is what the live scene does on a plain
    stereo input;
  - **stems:** the three Feline stems.
- `--sonic-trace` at 60 fps; 12,632 frames, 210.5 s.
- `Engine::update` cost 2.45 ms per frame for this scene (CPU, offline, whole update).
- Route finals were exposed through probe knobs (a copy of the production route into `macros/p_*`), which read exactly one
  frame late (B1).
- Live input runs the same code from the response model onwards (ADR-1025, tested equal); live adds §6.1's pipeline in
  front.

### Trace 1: a simple transient. The snare/clap bump on the rainbow arches

| Step | Code | Value / timing |
|---|---|---|
| Sound → analysis | live: `AudioInput` → `AnalysisStream` → `AnalysisRunner::threadMain` → `CausalOnsetDetector::process` (`causal_onsets.cpp`); file: the timbre pass at load (`SonicSetup::analyse`) runs the same detector | the snare is decided `kDeferFrames` = 4 hops after its attack (noise-floor confirmation) |
| → response | live: `LiveTimbre` tap → `LiveSonic::frame` → `SonicRuntime::step` → `ResponseModel::step`; file: `SonicRuntime::advance` in `advanceClock` (l. 6208) | `response.snare` (event) and `response.snareEnv` (exponential release 0.12 s × Release), published in `ResponseModel::publish` |
| → route | `rainbow-world.json`: `response.snareEnv` → `material/rw_rainbow/op/9/constant/constant`, add 2.5, chain attack 4 ms / decay 220 ms, depth `stem.drums.active` 1 → 0, default 0 (the ADR-1570 fallback) | `Modulator::applyRoutes`, the same frame |
| → parameter → pixel | the material program's constant op 9: the flow of the arch bands. **Handoff to A** (material program evaluation) | |

**Measured [B3]:**
- **Volume:** 152 snare events in 210 s.
- **Route timing:** the route's final rises on **the event's own frame, in all 152** (knob +1 for all 152).
- **Peak level:** the bump reaches p90 0.74 of its depth.
- **Class lateness on real material:** the same hits' broadband `response.onset` came **50-67 ms earlier (median
  66.7 ms)**. That is larger than the synthetic kit's 41-44 ms.
- **Live total:** the arches flash about 110-130 ms after the clap [est, §6.1].
- **With stems:** the fallback stands aside (depth 0, as designed) and `stem.drums.highMid` drives it instead. That is a
  thresholded *band level* (offset -1.55, gain 2.5), not an event: 193 rises against 152 snare events. So the "clap"
  has a different definition per input.

**Verdict:** the route plumbing is exact. The **detector choice** costs about 60 ms and makes this read late. P1-1 is the
fix.

### Trace 2: a continuous response. The bass swell

| Step | Code | Value / timing |
|---|---|---|
| Sound → level | `ResponseModel::step`: the 30-150 Hz level in dB, followed (attack 15 ms, release 1.2 s), read against the adaptive loudness reference (ADR-1510), window 6.5 dB over the reference and 15 dB span | `response.bass` p10/p50/p90 **0.38 / 0.68 / 0.83** over the song |
| → macro knob | route `response.bass` → `macros/swell`: gain 2.4, offset -1.45, clamp, attack 10 ms / decay 180 ms, depth `stem.bass.active` inverted (fallback) | `macro.swell` p10/p50/p90 **0.00 / 0.28 / 0.62**; above 0.3 for 46% of the song |
| → fan-out | `MacroSource::update` publishes the knob's **previous** final; 5 routes: `palette/value` +0.07, `palette/saturation` +0.18, `lights/cloudsea/intensity` +1.2, `material/rw_cloud/op/26` +0.3, `field/skyBreath/strength` +1 | +1 frame |
| → pixel | palette colours → materials; a light; a cloud material constant; a field's strength (a deformer gain). **Handoff to A** | |

**Measured [B3]:**
- `macro.swell` equals the chain of `response.bass` **one frame earlier** to 0.0002, against 0.36 at zero lag. That is
  the ADR-1573 lag, confirmed on real music.
- With stems the swell is driven by `stem.bass.onset` breaths, with depth from `stem.bass.level`. Its distribution changes
  to p50 0.50 / p90 **1.00**: it clamps at the top for more than 10% of the song. So the same knob means a different
  thing per input, as in trace 1.

**Verdict:** the plumbing is correct but lossy in time (one frame), and the knob exists only to share one conditioned
value. P0-1 is the fix.

### Trace 3: MIDI-controlled. CC 1 → the arches' glow

| Step | Code | Timing |
|---|---|---|
| Controller → engine | CoreMIDI thread → `MidiInput` inbox; drained at the top of `Engine::update` (`ControlHub::update`, `control_hub.cpp` l. 113) | MIDI → bus p50 13 ms, p90 23 ms; → present p50 18 ms [ADR-1025] |
| Binding | `control.midi.bindings`: `{kind: cc, number: 1, signal: "glow"}`; `matchMidi` → `Match{value/127}` → `ControlSource::set("glow")` | 7-bit, 128 steps; the last message in a frame wins |
| → signal | `ControlSource::update` in the rack's pass 1 | `control.glow`, the same frame |
| → routes | **16 routes**, `control.glow` → `nodes/rainbow_*/emissiveBoost` (16 nodes), add 2.0, identical chains (attack and decay 150 ms) | one-pole τ 150 ms: 63% at 150 ms, **90% at 345 ms** [analytic] |
| → pixel | `Composition` flatten reads `emissiveParam->value()` (`composition.cpp` l. 6311) → the node's material emission. **Handoff to A** | |

- Not run under `--sonic-trace`, which has no MIDI.
- The control path is deterministic from the drain onwards. MIDI's own latency is ADR-1025's measurement.

**Verdict:**
- **Predictable**, but it pays 345 ms to reach 90% of a fader move. 16 copies of one chain hold 16 states for one knob.
- It has no soft takeover, so recalling a look and then touching the knob jumps.
- Recommendations:
  - P0-1 (one signal, one chain);
  - P2-4 (1€ smoothing: smooth at rest, fast when moved);
  - P1-7 (pickup).

### Trace 4: long-timescale state. The world scan

| Step | Code | Value / timing |
|---|---|---|
| Driver | stems: `stem.synth.rms`; mix: `response.sustain` (depth fallback) | |
| Chain | `trendSeconds` 8 (STA `trendFastMs` 450 or 1000) → binary threshold 1.25 or 1.1 → peak-hold 6.5 s, refractory **12 s**, rearm 0.3 → spring 0.5 Hz → `macros/scan`, op **max** | ADR-1575 (trend) + ADR-1574 (latch) + ADR-1041 (spring) |
| Fan-out | `macro.scan` (+1 frame) → **107 routes**: effector enables (binary 0.02) and strengths on land, rocks, trees; arch flare; clouds; sun; bloom; saturation | |
| Seek | stateful chains into `macros/` trigger the control replay on every seek (ADR-1573) | exact; cost grows with position (§3.2) |

**Measured [B3]:**

| Variant | Scans (rises over 0.5) | Each held |
|---|---|---|
| mix | 4: at 9.3, 33.6, 46.0 and 118.9 s | 8.1 s |
| stems | 4: at 48.8, 66.2, 91.5 and 183.9 s | 8.1 s |

**Only the drop (46.0 or 48.8 s) roughly coincides.** The long-timescale behaviour is reproducible for a given input, but
it **depends entirely on which input feeds it**. A live show on a stereo feed gets a different structure from the one
rehearsed with stems, and from the owner's earlier "once at 48 s" (Rainbow World 23, an earlier tuning).

**Verdict:** the accumulation, latch and decay primitives work and are seek-exact. What the scene lacks is a stable
definition of "the swell" that is the same across inputs. That is P0-1's `firstActive` selection plus per-input
calibration in the scene spec, not new engine machinery.


## 11. Refactor and removal candidates, audio and modulation (feeds final report §8)

| # | Candidate | Evidence | Consequence | Simplest correction | Benefit | Risk / migration | When |
|---|---|---|---|---|---|---|---|
| R1 | Two envelope primitives disagree | §2.4 [B1] | a frame late and frame-rate-dependent peaks in `env.*` and every trace built on it | fix (P0-2); retire later (P2-1) | correctness | 2 scene users; no shims | **before** |
| R2 | Fallback doubling and chain copies in generated routes | §3.3 (405 routes, 3 identical latch chains, about 70 twins) | drift between copies, a macro frame of lag, opaque projects | P0-1, then port Rainbow World's control layer | fewer routes, one frame earlier | Rainbow World's look must be re-verified (trace comparison) | **during** |
| R3 | `audio.onsetLow/Mid/High`: one name, two detectors | §2.3 | a route tuned offline behaves differently live | P2-9 | live = file | moves offline scenes that use them | after (owner) |
| R4 | `audio.<band>` auto-gain without an absolute gate | §2.5 [B1b] | full-scale bands in quiet passages, live | P1-3 | silence reads silent | looks of scenes on `audio.*` | **during** (owner) |
| R5 | `--sonic-trace` traces events through `env.*` | ADR-1571 addendum; §2.4 | diagnostics a frame late and attenuated | record event flags directly (P1-2) | honest traces | readers select columns by name; additive | before |
| R6 | `macro.one` knob used as a constant source (48 routes in Rainbow World) | `rainbow-world.json` | a constant through a mutable knob; a MIDI or state write would move 48 baselines | a constant input in P0-1, or write the base | clarity | none | during |
| R7 | `flat-chain` false positive on trend chains | [B2] | 1,000 spurious "dead" warnings at 4,000 routes; Rainbow World's scan routes flagged | P2-5 | trustworthy audit | none | after |
| R8 | Hard-wired `StdUniforms.audio` in user shader layers | `Engine::update` l. 7380-7395 | a second audio path that bypasses the Response controls and routes | leave; document | — | — | leave alone |
| R9 | Astral Forge's own analysis and envelopes | `src/scene/astral_song.*` | duplicate semantics; ADR-1361 retrofit needed | leave (G15) | — | — | leave alone |
| R10 | `TEMPORARY: phase 2` probe instrumentation in `Engine::update` (`probe2::frame()`) | engine.cpp: 11 lines tagged TEMPORARY, e.g. l. 7151-7153, 7187-7208, 7283, 7320, 7399 | clock reads and atomics per frame in the production path, marked temporary | remove, or promote into the profiler (A and the lead to decide) | hygiene | none | after |

## 12. Gap list and prioritised recommendations

**Effort scale:**
- **S:** up to 2 days, one subsystem, tests included.
- **M:** up to a week.
- **L:** up to 3 weeks, or crosses into the renderer.
- **XL:** more.

Estimates assume the repository's usual test discipline.

### 12.1 Gap list (§2.2 labels)

| # | Gap | Label | Evidence | Belongs in |
|---|---|---|---|---|
| G1 | A same-frame, named, stateful conditioned signal | Missing | §3.1, §3.3; `macro.*` +1 frame [B1] | shared modulation |
| G2 | `EnvelopeSource` publishes a frame late and never shows its peak | Existing but limited (defect) | §2.4 [B1] | shared modulation |
| G3 | Class hits (kick, snare) are 28-44 ms late by construction; impacts lack a fast register attribute | Existing but limited | §2.3 [B1] | shared analysis |
| G4 | `audio.<band>` normalises a noise floor up to 0.9+ | Existing but limited (defect for live) | §2.5 [B1b] | shared analysis |
| G5 | Different time bases per family in GUI file playback; output latency not compensated | Duplicated or fragmented | §6.2 [code] | engine clock |
| G6 | `audio.onsetLow/Mid/High` mean different detectors live and on file | Duplicated or fragmented | §2.3 [code] | shared analysis |
| G7 | GPU `AudioHistory` (spectrum and onset fields) hears only the mix | Existing but limited | §7 [code] | signal side here; GPU side Sub-Agent A |
| G8 | No live signal scope; trace lacks `audio.*`, `control.*`, route outputs, parameter finals | Missing / Existing but limited | §8 | diagnostics |
| G9 | No end-to-end sound → light measurement | Missing | §6.1 | diagnostics, with A |
| G10 | Beat confidence cannot see octave errors; no "beat gone" | Existing but limited | §2.1 [B1] | shared analysis |
| G11 | MIDI: no soft takeover, 14-bit, relative encoders or output feedback; direct parameter bindings unsmoothed | Existing but limited | §4 | control |
| G12 | Late-published events (`field.*.enter`, `entity.*`, staging) invisible to source and state triggers | Unknown (code says yes; not probed) | §1.3 | engine order |
| G13 | Liveness `flat-chain` false positive on trend chains | Existing but limited | §3.2 [B2] | diagnostics |
| G14 | Fallback doubling: every stem route needs a mix twin | Duplicated or fragmented | §3.3 | solved by G1 |
| G15 | Astral Forge's own song analysis and envelopes | Duplicated or fragmented | §3.3 | scene-specific; leave |
| G16 | Graph propagation (an event travelling across connected scene elements) | Missing | §7 | scene-specific until the spec needs it |
| G18 | Mix kick/snare/hat classes misfire (mix kick precision 0.54-0.57) and are the most-routed audio signals in the live collection (276 uses, 41 of 44 projects) | Existing but limited | §15 [B4] | shared analysis + scene migration |
| G17 | `sonic.*` not noise-gated (`sonic.energy` 0.13 at -70 dBFS) | Existing but limited | §2.5 [B1] | shared analysis, low priority |

### 12.2 P0: required foundation

**P0-1. Conditioned signals: a chain whose output is a bus signal, the same frame (G1, G14).**

- **Problem:**
  - A conditioned musical signal (a "bass breath", a "drum hit, else the mix's onset") cannot be named once and reused.
  - Scenes copy chains (state drift, N times the work), route through macros (one frame late, measured), double every
    route for the fallback, or generate routes from Python. Rainbow World needs 405 routes for perhaps 15 musical ideas.
  - The Living Prism's brief asks for exactly this composition: many effects from a few coherent musical drivers.
- **Existing and why it is insufficient:**
  - `interpret` / `visual.*`: same frame, but stateless (no envelope, latch, trend or integral).
  - Macros: stateful through routes, but +1 frame by construction (`MacroSource::update` publishes the previous finals).
  - Neither can select "the first active of these inputs".
- **Proposed solution:** a source kind, provisionally `"signal"` (`signals/source.*`, or beside `interpret_source.*`):
  - **inputs:** a list of bus signals, combined like interpret's `combine` (mean/max/sum), plus a new `firstActive` mode
    keyed on each input's `.active` (or a declared gate signal);
  - **chain:** a full `ProcessorChain`, the same code the routes run;
  - **output:** `signal.<name>`, value or event.

  It runs in the rack's second pass, after every publisher, so routes, state triggers and effect `signal` triggers see it
  on the frame of its input. It is stateful, so ADR-1573's `controlReplayNeeded()` must include it, and its chain state
  joins the seek checkpoint like a route's.
- **Alternatives considered:**
  1. **Make macros same-frame** by evaluating routes into `macros/` in a pre-pass. Rejected. It changes the meaning of a
     knob that MIDI and states also write, and it entangles the finals reset with the rack order.
  2. **Route-to-signal targets** (`target: "signal:x"`). Equivalent power, but scatters a signal's definition across
     routes; harder to inspect.
  3. **Keep generating routes in Python.** The status quo; leaves the lag and the doubling.
- **Expected benefit:**
  - **Visual:** one frame earlier on every macro-routed effect.
  - **Authoring:** Rainbow World's 10 macro-feeding routes and roughly 70 fallback twins would become about 10 signals
    [est].
  - **Diagnostics:** a named signal is a thing the scope can show (P1-2).
- **Performance:** negative cost: N chain copies become one. Chains are about 30 ns each [B2].
- **Effort:** M.
- **Regression risk:** low. It is additive and touches no existing route. The replay condition extends ADR-1573.
- **Dependencies:** none.
- **Validation:**
  - A unit test: a signal source equals the same chain run as a route, bit for bit, on the input's frame.
  - A seek-equals-play test (`[adr1573]`'s pattern).
  - Port Rainbow World's swell and scan to it, then show with the trace that `macro.swell` and `signal.swell` differ only by
    the frame.
- **Prerequisite for the Living Prism:** yes. The scene's "previous events leave traces" and "coherent composition" are
  shared accumulators by definition.

**P0-2. Fix `EnvelopeSource`'s publish order and peak (G2).**

- **Problem:** §2.4. The envelope is +1 frame and peaks at 1 − dt/release, so its amplitude depends on the frame rate.
- **Solution:** take the trigger before stepping, and apply ADR-900's rule: the frame an instant attack completes shows
  full level, and the decay runs from the next frame. Also apply it to `RandomSource`'s S&H, after checking it the same
  way.
- **Alternatives:**
  - Retire `EnvelopeSource` in favour of P0-1. That is possible later, but the trace convention and two scenes use it now.
- **Benefit:**
  - Correct envelopes at any frame rate.
  - `--sonic-trace` event columns that are honest.
- **Effort:** S.
- **Risk:** a visible change to scenes with `env.*` (Digital Ocean's 2). There are no compatibility shims (ADR-441).
- **Validation:** re-run probe B1. The rise frame should equal the event frame and the peak should be 1.0 at 30 and 60
  fps.
- **Prerequisite:** yes, if P0-1 or the trace is to be trusted.

### 12.3 P1: high value

| # | Change | Problem → solution | Effort | Risk | Validation | Depends |
|---|---|---|---|---|---|---|
| P1-1 | **Fast impacts** (G3) | Author percussive impacts on per-source broadband onsets. Add a register attribute at the onset frame (low/mid/high share of the attack's flux) to `stem.<k>.onset` and `response.onset`, so size and colour can follow the register without waiting 30-44 ms for the class. Keep the classes for tails and counts. | S | low (additive) | B1: attribute on the onset frame; kick fixture reads low share > 0.6 | — |
| P1-2 | **Signal scope and complete trace** (G8) | An ImGui scope: pick any bus signal or route, plot its input, chain stages and final over the last 10 s, from a ring that runs only while open (ADR-951 `sought`). `--sonic-trace` gains `audio.*`, `control.*`, `lfo.*`, `signal.*` and `--trace-params <glob>` for parameter finals, and records event flags before `clearEvents` instead of through `env.*`. | M | none (diagnostic) | a trace of B1 shows events on their frames; the scope reproduces a route's `lastOutput` | P0-1 for `signal.*` |
| P1-3 | **Gate the band auto-gain** (G4) | `audio.<band>` divides by a running peak that falls to 1e-4. Hold the peak below an absolute gate (ADR-1510's -60 dBFS), so a noise floor reads 0. | S | **owner call**: 24+ live scenes route `audio.*`; quiet passages will read lower | B1b: bands < 0.05 over 80 s of -70 dBFS noise; `check_response_range.py` unchanged | — |
| P1-4 | **`AudioHistory` from a source** (G7) | Let the history (spectrum and onset rows) take a feed: `feeds.history: "stem.drums"`, or a per-field `audioSource`. Signal side: build `AudioHistory` from a `StemTrack` (file) or a channel's frames (live). GPU side: **Sub-Agent A** owns the cost of extra rows if several sources are wanted at once. | M (signal) + A's estimate | medium (fields are seek-exact; keep `whole()` for files) | an onset field keyed to drums fires on drum hits only, on the B1 fixture | — |
| P1-5 | **One time base in file playback** (G5, G6 in part) | (a) Offset the visual clock by the output device latency (device latency + buffer + safety offset, queried once per device). (b) In GUI file playback, publish `audio.*` from the whole track at the play-head, as `response.*` already is, keeping the causal runner for live input only. | S-M | medium: ADR-1025's live/file equivalence tests must still hold for live input | a click track: every family's onset frame identical in GUI playback and offline | — |
| P1-6 | **Sound → light measurement** (G9) | Extend `tools/sonic_live_latency.py` and the ADR-1571 trace with the present timestamp. A loopback click (BlackHole) gives sound → present per family; A's frame timeline gives present → scanout. Report p50/p95 per transient family. | M | none | numbers for §6.1 replacing [est]; the owner's threshold applied | P0-2 (honest events), A |
| P1-7 | **MIDI soft takeover** (G11) | For CC → parameter-base bindings and `control.*`: an optional `pickup` mode that ignores the knob until it crosses the current value, so a preset recall or state change does not jump when the knob next moves. | S | low | inject CCs around a recalled value; no jump | — |

**P1-8. Phase out the mix kick, snare and hat (G18; the owner's follow-up).** Class hits come only from a drum source; on a mix, scenes use `response.onset` (with P1-1's register), `beat.pulse`, or a P0-1 signal choosing between them. Effort: M code + M-L migration + owner review. Depends on P0-1 and P1-1. Full assessment, migration map and removal list in §15.

### 12.4 P2: optional

- **P2-1:** retire `EnvelopeSource` once P0-1 lands (migrate its 2 users) (S).
- **P2-2:** `beat.confidence` from tracker agreement, plus `beat.present` (G10, §9.2) (S-M).
- **P2-3:** nearest-frame firing for pure-in-time event timelines (§6.3) (S; it changes ADR-900's rule).
- **P2-4:** a 1€ smoothing mode for CC and OSC (§9.4) (S).
- **P2-5:** teach the `flat-chain` liveness rule the trend stage (G13) (S).
- **P2-6:** probe G12. If confirmed, publish late events into the next frame's trigger inputs, or document that only routes
  read them (S).
- **P2-7:** route-picker hygiene: list `beat.*` before `audio.beat`/`music.beat` and label the overlaps of §2.2 (S).
- **P2-8:** MIDI output (LED feedback), 14-bit CC, relative encoders (M).
- **P2-9:** publish `audio.onsetLow/Mid/High` from the causal detector in both modes (G6). Keep ADR-898's look-ahead
  onsets for `TriggerClock`/`AudioHistory`, where look-ahead is harmless. Owner call: it moves offline scenes that author
  on these (M).
- **P2-10:** noise-gate `sonic.*` (G17) (S).

### 12.5 Defer or reject

| Item | Verdict | Why |
|---|---|---|
| Live source separation (Demucs-class) | **reject** for this milestone | seconds of context; incompatible with a ≤ 45 ms budget (§9.5). Mapped channels and offline stems already exist |
| A new modulation or "VFX response" framework | **reject** | the chain is complete (§3.1); the gap is naming (P0-1) |
| A neural onset or beat detector | **reject** | not needed on separated sources (53/53 [B1]); cost and latency |
| Consolidating the loudness or beat signals | **reject** | the semantics differ deliberately (§2.2) |
| Ableton Link | **rejected (owner, 2026-10-22: "no Ableton Link support")** | also: GPLv2+/proprietary SDK against an MIT repository; MIDI clock already wins the clock |
| Rewriting Astral Forge onto `response.*` | **defer** | it works, is GPU-coupled and scene-specific (G15); revisit only if a second scene needs its song model |
| A graph-propagation primitive | **defer** until the scene spec | route `delay` (≤ 4 s) staggers by hand; ADR-1116 fields cover radial fronts (G16) |
| Per-stem Sonic Character | **defer** | `stem.*` levels, onsets and centroid already cover instrument-aware colour; the character costs a timbre pass per source |
| More signal names | **reject** | the brief's own warning; §9.6 shows the vocabulary is complete |

**Order to minimise rework:**
1. P0-2 (S), which makes the measurements honest.
2. P0-1 (M).
3. P1-2's trace half.
4. P1-6, the measurement against the owner's threshold.
5. In parallel: P1-1, P1-3, P1-5, P1-7.
6. P1-4 with Sub-Agent A.
7. P1-8 (the mix kick/snare/hat phase-out) after P0-1 and P1-1, then the owner's scene review (§15.5).

Port Rainbow World's control layer to P0-1 as P0-1's acceptance test, not as scene work.


## 13. Risks and open questions

1. **The owner's latency threshold.** Proposed: sound → light ≤ 45 ms p50 and ≤ 60 ms p95 for impacts, live, at the
   target refresh. This is from BT.1359 detectability. It needs the owner's approval and P1-6's measurement. Today's
   estimate passes only for broadband onsets.
2. **P1-3 and P2-9 change existing scenes' look.** Either gate them per project or accept the change (ADR-441 says no
   shims). Owner call.
3. **Stems in performance.** The architecture assumes the performer can deliver separated sources on input channels
   (mixer or DAW). If the Living Prism must run on a plain stereo feed, the instrument-aware parts fall back to the mix
   classes. Those confuse bass attacks with kicks (§2.3). After P1-8 the fallback is onsets and the beat clock instead
   (§15), and the scene spec must be designed for it.
4. **The beat clock on a stereo feed can lock at half or double tempo** ([B1]). For a show, recommend MIDI clock from the
   playback rig. Owner workflow decision; Ableton Link is rejected by the owner.
5. **Unknown G12** (late events never reach source or state triggers). It should be probed before the scene spec relies
   on `field.*.enter` or entity events driving state.
6. **Synthetic-fixture caveat.** B1's detector rates come from a synthetic kit. ADR-898's Low miss rate in particular may
   not transfer; Rainbow World's Feline trace (B3) is the real-material check for the classes used there.
7. **P1-8 option A or B** (§15.2): remove all three mix classes, or only the kick. Owner decision, best taken after
   reviewing a migrated scene of each kind.
8. **Coordination with Sub-Agent A:**
   - present-queue depth and scanout for §6.1;
   - the GPU cost of P1-4's extra history rows;
   - whether the frame target is 60 Hz (my per-frame numbers assume 60; at 30 fps every "+1 frame" is 33 ms).


## 14. Probes created

**Common to all probes:**
- Each runs `avgen --project … --sonic-trace … --fps N`, headless with no GPU work, under
  `AVGEN_GPU_LOCK_TIMEOUT=14400 tools/gpu-lock.sh`, with the Release build in `build/audio`.
- Fixtures and outputs go to the session scratchpad only.
- No audio, stem or render was written into the repository.
- No production code or test was changed.

| Probe | Files (repository) | What it measures | Result used in |
|---|---|---|---|
| **B1** transient timing | `probes/B1-transient-timing/make_fixture.py` (a synthetic 24 s mix + drums and bass stems with known hit times, a project with an envelope source on every detector and macro probe routes), `analyse.py` | per-detector hits, misses, false triggers, class confusion and onset-to-frame offset at 60 and 30 fps; the macro +1 frame; the envelope-source lag and peak; silence and a -70 dBFS noise floor | §0, §2, §6 |
| **B1b** long noise floor | `probes/B1-transient-timing/noise_tail.py` | 80 s of -70 dBFS noise after the music: auto-gain behaviour of `audio.<band>` and the gates of `response.*` | §2.5, G4 |
| **B2** route scaling | `probes/B2-route-scaling/make_projects.py` | `Engine::update` per frame with 0, 250, 1,000 and 4,000 routes of Rainbow World's chain shapes, 3 runs each | §3.2 |
| **B4** mix hit phase-out | `probes/B4-hit-phaseout/make_feline.py` (Feline + its drum stem on the hits tier, an envelope on every hit detector), `analyse.py` (precision, recall, latency against truth or the drum stem), `combos.py` (mix-only coincidence rules); B1's `make_fixture.py --hats` (the kit with closed hats) | the mix kick/snare/hat against per-source classes, honest onsets and the beat clock | §15 |
| **B3** Rainbow World traces | `probes/B3-rainbow-traces/make_variants.py`, `analyse.py` | the live project played from Feline Footwear, with and without the three stems; probe knobs copy production routes to expose their finals | §10 |

**Reproduce:**
```
S=<scratch>; python3 probes/B1-transient-timing/make_fixture.py $S/b1
tools/gpu-lock.sh build/audio/src/avgen --project $S/b1/project.json --sonic-trace $S/b1/trace60.csv --fps 60
python3 probes/B1-transient-timing/analyse.py $S/b1 $S/b1/trace60.csv 60
```

**Not created:**
- no screenshots or media: nothing in this audit is visual;
- nothing under `~/Desktop/av-gen-review/80-living-prism-audit/`.

## 15. Owner follow-up: phasing out the kick, snare and hat detectors on a mix

**The owner asked** (verbatim): "with these changes could we phase out the kick / snare / hh detecetors on a mix? since
they misfire often they arent very useful."

**What is in scope:**
- `response.kick`, `response.snare`, `response.hat` and their `*Env`;
- their `audio.*` equivalents `audio.onsetLow/Mid/High`, which mean ADR-898's offline detector on file and the same
  ADR-1060 classes on live input (§2.3);
- the ADR-1068 "a note is not a drum" logic that exists only to stop the mix snare and hat firing on notes.

**Probe B4** (§14) re-ran B1 with closed hats added (`make_fixture.py --hats`). It also ran Feline Footwear with its drum
stem on the hits tier and an envelope source on every detector. Scoring rule: a detector event matches the nearest
unmatched reference event of the class within −30..+120 ms.

**On Feline there is no ground truth.** The reference is the drum stem's own classifier (`stem.drums.kick/snare/hat`),
the same detector on the separated signal. On the synthetic kit that classifier scores 36/36 kicks and 17/17 snares with
no confusions. So Feline's numbers are **agreement with the drum-stem classifier**, not accuracy.

### 15.1 What the measurements say

**Precision, recall and latency.** Latency is the median, relative to the reference hit. Latencies for Feline are
relative to the stem classifier, which is itself late (kick +28 ms, snare +41 ms on the synthetic kit).

| Signal | Synthetic kit (truth): precision / recall / latency | Feline vs the drum stem: precision / recall / latency |
|---|---|---|
| **Mix classes (the ones in question)** | | |
| `response.kick` | **0.57** / 0.97 / +31 ms. False fires: 13 on bass notes, 8 on hats, 5 on snares | **0.54 / 0.62** / +17 ms |
| `response.snare` | 1.00 / 1.00 / +41 ms | **0.93** / 0.73 / 0 ms |
| `response.hat` | **0.40** / 1.00 / −2 ms. Most of its 53 false fires land on kicks (the beater's click), some on snares | **0.85 / 0.94** / 0 ms |
| `audio.onsetLow` (ADR-898, file) | 0.00 / 0.00 (a low-tuned kick) | 0.32 / 0.18 |
| `audio.onsetMid` | 0.45 / 1.00 | 0.34 / 0.63 |
| `audio.onsetHigh` | 0.66 / 1.00 | 0.50 / 0.72 |
| **(a) Per-source classification on the separated drums** | | |
| `stem.drums.kick` | **1.00 / 1.00** / +28.5 ms (p90 34.8) | reference |
| `stem.drums.snare` | **1.00 / 1.00** / +41 ms | reference |
| `stem.drums.hat` | **0.40** / 1.00 / −2 ms. It fires on kick clicks **on the stem too** | reference |
| `stem.drums.onset` (any drum hit) | 1.00 / 1.00 / −2 ms | 1.00 / 1.00 (it is the reference set) |
| **(b) Honest mix-only alternatives** | | |
| `response.onset` (broadband, against any drum hit) | **0.88 / 1.00 / −3 ms** | **0.93 / 0.83 / 0 ms** |
| `audio.onset` (linear flux, against any drum hit) | 0.57 / 0.95 / −3 ms | 0.83 / 0.87 / 0 ms |
| `response.low` (40-120 Hz attack, against kicks) | 0.43 / 0.97 / +3 ms | 0.27 / 0.66 |
| Coincidences, against kicks: `onset`∧`low`; `onset`∧beat; all three (`combos.py`) | 0.53 / 0.94; 0.69 / 0.31; 0.89 / 0.22 | 0.12 / 0.14; 0.27 / 0.21; 0.17 / 0.08. **No mix-only rule recovers a kick** |
| **(c) Beat-clock pulses, against kicks** | | |
| `beat.pulse` | **0.14 / 0.14**: the tracker locked at 60 BPM and off-phase on this pattern (§2.1) | **0.59 / 0.62**, +17 ms. On real music it is **as good a "kick" as the classifier**, and it never fires off the grid |

**Reading:**
1. **The owner is right about the kick.** On both sources, roughly half of the mix's kicks are not kicks. On Feline,
   `beat.pulse` matches the stem's kicks as well as the mix kick does (0.59/0.62 against 0.54/0.62). Its errors are
   predictable (the grid) and not random (off-grid fires on bass and snare). No combination of honest mix signals does
   better (`combos.py`).
2. **The snare and hat are not the misfiring ones by this evidence.** On Feline they agree with the drum stem at 0.93 and
   0.85 precision. Their problems are different:
   - **Snare:** 41-67 ms of decision latency (§2.3, trace 1). That makes it a poor live trigger whatever its accuracy.
   - **Hat:** it fires on kick clicks even on an isolated drum stem (0.40 on the kit). That is a classifier property, not a
     mix problem.
3. **A "hit" on a mix is best expressed as `response.onset`**: 0.93 precision against any drum hit on Feline, about 0 ms.
   Register (low/high), which the class names were standing in for, is P1-1's attribute at the onset frame.
4. **Class names are trustworthy only on a separated drum source.** The kick there is 1.00/1.00, at +28 ms.

### 15.2 Recommendation

**Phase out all three class hits on the mix (Option A, recommended).**
- `response.kick/snare/hat(Env)` fire only when a `hits` feed names a source: a drum stem offline, or a live drum input.
- On a plain mix they are not declared, so a route on them is unresolved. It is flagged unless it is `optional` or sits
  inside a P0-1 signal with `firstActive`.
- `audio.onsetLow/Mid/High` leave the bus in both modes.

**Why all three and not only the kick (Option B):**
- **One rule** an artist can hold: class names come from a drum source; a mix gives onsets, register and the beat.
- **Snare latency.** The snare's 41-67 ms latency rules it out as a live impact trigger anyway (§9.3).
- **Hat.** The mix hat is a 7-16 kHz attack; P1-1's high-register share at the onset frame says the same thing honestly
  and without delay.

**What Option A costs.** It removes signals that by measurement are decent on Feline (snare, hat). If the owner's visual
review of the migrated scenes misses them, **Option B** is the fallback: remove only `response.kick`, keep snare and hat.
**Owner decision.**

**P-level: P1, after P0-1 and P1-1. Not P0:** the Living Prism can simply not author on them. The reasons for P1:
- these are the most-routed audio signals in the live collection (276 route uses in 41 of 44 live projects);
- they are measurably the least reliable;
- the clean migration needs P0-1's `firstActive` (drum source, else a mix replacement in one signal, no doubling) and
  P1-1's register attribute.

Doing it before those two would force the ADR-1570 route doubling into 41 projects and then a second migration.

### 15.3 Migration map (the 44 live projects in `examples/index.json`)

**Survey:**
- 276 route uses in **41 of 44** live projects; none use the signals as a depth source.
- Also: 4 Effect Library triggers (`effects[].trigger.name`), 1 interpret input, and 71 mentions in the descriptive
  `sonicScene.vocabulary` text.
- **No live project routes `audio.onsetLow/Mid/High`.** 9 offline example files do: Digital Mosh, GV3, Rift and Chorus
  Field, plus their `build.py` generators.
- 45 example files in all mention a `response.*` class, including generators. Most live scenes are generated
  (`examples/*/build.py`, `tools/sonic_*`), so the migration is mostly edits to generators and then rebuilds.

**By class:** kick 126, snare 86, hat 64.

**By what the route drives** (classified from the target path):

| Use | Routes (kick / snare / hat) | Typical targets | Replacement on a drum source | Replacement on a mix |
|---|---|---|---|---|
| **Motion / impulse** (deform, scale, shake, field strength, lens) | 69 (43 / 18 / 8) | `procedural/*/deform/*/amount`, `field/*/strength`, `camera/lens/*`, `post/radial/amount` | `stem.drums.kick` / `.snare` | **`beat.pulse` for a kick-on-the-beat heave** (the dominant kick use: median decay 260 ms, 42% ≥ 300 ms, i.e. a pulse not a flash); `response.onset` for "any hit" |
| **Light / flash** (emission, light intensity, bloom) | 60 (22 / 20 / 18) | `material/*/emission*`, `lights/*/intensity`, `nodes/*/emissiveBoost` | stem classes | `response.onset`, sized by P1-1's register share |
| **Material constant** (program op constants: scene-specific shader drives) | 51 (22 / 14 / 15) | `material/<m>/op/<i>/constant` | stem classes | case by case: onset or beat |
| **Spawn / burst** (particles) | 45 (12 / 17 / 16) | `particles/*/burst` | stem classes | `response.onset` (one burst per hit) |
| **Texture / glitch** (mosh, tear, split, noise) | 36 (18 / 12 / 6) | `post/glitch/*`, `temporal/mosh/amount` | stem classes | `response.onset`, or `response.flux` / `hatRate` as a level |
| Other / colour | 15 | `post/*/amount`, `procedural/*/rotation` | | review by hand |

**Rules for the generators:**
- **Kick-on-the-beat uses** (long decays, motion and heave targets) go to `beat.pulse` / `beat.phase`.
  - Since the beat clock is predictive, these can even **lead** by the pipeline latency. That is the only way any kick
    response can beat §6.1's 75-85 ms live.
  - Rainbow World already does this for its arch accents.
- **Short flashes, bursts and glitches** go to `response.onset`, with the register attribute when P1-1 lands.
- **Wherever a drum source may exist** (live inputs keyed `drums`, stems offline), wrap both in one P0-1 signal with
  `firstActive: ["stem.drums.kick", "<mix replacement>"]`, so the scene uses the true class when it can.
- **Effect triggers** (4) and the interpret input (1) follow the same rules. The `sonicScene.vocabulary` text is
  rewritten.

### 15.4 What changes in code (no shim, ADR-441)

| Where | Change | Stays |
|---|---|---|
| `src/sonic/response.{hpp,cpp}` `ResponseModel` | Kick, Snare, Hat hits and envelopes only from a `ResponseFeed` of a source. With none, they are not declared. ADR-1068's drumness demotion (`pendingDrumness_`, `midiDrumness`) goes, since it only ever corrected the mix. | `low`, `onset`, `note` hits and every level |
| `src/audio/feeds.{hpp,cpp}`, `src/ui/feeds_row.cpp` | `hits` no longer defaults to `"audio"`, and `kick/snare/hat` may not name `"audio"`. A load warning if they do. | the feed keys themselves: they now *mean* "the drum source" |
| `src/analysis/analysis_runner.cpp` l. 129-138 | Stop writing the causal Kick/Snare/Hat into `lowOnset/midOnset/highOnset` on the mix | the `CausalOnsetDetector` run itself, which feeds `response.low/onset` and the classes on stems and inputs |
| `src/signals/audio_signals.cpp` | Remove `audio.onsetLow/Mid/High` from the bus | `audio.onset` |
| `src/directing/reactivity_proposer.cpp` l. 38 | The Director's "kick" template (`audio.onsetLow`) becomes beat/onset | |
| `src/sonic/signal_events.*`, `effect_timing.hpp` comments | Derived signal-trigger lists stop listing the removed names | |
| **Internal consumers of ADR-898's mix band onsets** | **Leave for now:** the GPU `AudioHistory` onset rows (ADR-1116; the live feed takes the runner's `lowOnset`), the Effect Library's band-onset triggers through `TriggerClock`, and Astral Forge's `SongAnalysis` `kickT/snareT/hatT` (`astral_song.cpp` l. 154). They are not on the bus, and they need P1-4 (`AudioHistory` from a source, Sub-Agent A's GPU side) to have a replacement. **A second phase**, owner and A to schedule. | until P1-4 |
| `CausalOnsetDetector` (`causal_onsets.*`), `test_drum_recall.cpp` | **Unchanged.** It is the per-source classifier (`stem.<k>.kick/snare/hat`, feeds). Its hat over-firing on kick clicks (0.40 on the kit, even on a stem) is a separate detector issue to look at. | all |
| Tests | About 7 files assert mix kick/snare/hat (`test_sonic_response`, `test_feeds`, `test_stem_feeds`, `test_route_fallback`, `test_signal_trigger`, `test_live_panel_no_hit_meters`, plus the drum-recall detector test, which stays). They are rewritten to assert the classes appear only with a hits source. | |
| Docs | A new ADR superseding the mix half of ADR-1060/1062/1068 and ADR-1580's `hits` default; `docs/audio.md`; the live scene guide; Sonic Garden's research notes. | |

### 15.5 Effort, risk, order, validation

**Effort:**
- **Code: M.** Response model, feeds, runner, signals, proposer, tests, ADR.
- **Migration: M-L.** 41 projects:
  - mostly generator edits, then rebuilds;
  - about 15 hand-authored projects to edit directly [est: the Sonic VFX family's generator coverage not checked file by
    file].
- **Visual review: owner time.** Each migrated live scene is played against at least two tracks:
  - one with drums (Feline);
  - one percussive and sparse (Trench).

**Risk:**
- **Medium.** Every scene's beat feel changes.
- Scenes that relied on the mix kick's false fires on bass notes for "bass punch" will lose those hits. That is the
  intent, but some will read calmer.
- A mix-only show depends more on the beat clock, so §2.1's octave and phase errors matter more. MIDI clock for shows is
  the mitigation (§13, item 4).

**Order:**
1. P0-1 (signals with `firstActive`).
2. P1-1 (register attribute).
3. This phase-out:
   - code first, behind nothing: ADR-441, one commit;
   - then generators;
   - then rebuilds and the review, family by family (Sonic Abstract, Sonic VFX, the rest).
4. The second phase, internal consumers: after P1-4.

**Validation:**
- Re-run B4. The mix classes are absent. On the stem variant, `response.kick` equals `stem.drums.kick` event for event.
- `--audit-routes` over all 44 live projects: no route on a removed name unless it is `optional` or inside a `signal`.
- A per-scene `--sonic-trace` before and after:
  - the count of fires per migrated target;
  - fires that are off any drum hit (against `stem.drums.onset` on Feline) must fall.
- The owner's visual review per scene, with stills or clips in `~/Desktop/av-gen-review/`. Merging needs owner approval
  (the standing rule).
