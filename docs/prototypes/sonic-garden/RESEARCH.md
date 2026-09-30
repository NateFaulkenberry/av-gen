# Sonic Garden: research and recommended architecture (Phase 0)

Engineering agent, 2026-09-30. The brief is `00-brief.md`. This is the Phase 0 deliverable (§30):
what AV Gen already has, what the outside references say, the architecture I recommend, the
descriptors to build and to reject, and the risks. The ADR is 1020.

## 1. What AV Gen already has

| area | what exists | consequence for this work |
|---|---|---|
| Audio analysis | `analysis::Analyzer` (ADR-004): a 2048/512 Hann STFT at 48 kHz, so about 94 frames/s. Per frame: rms, peak, 5 bands (auto-gained and ADR-897 fixed-dB levels), log-normalised centroid, flux, onset strength and onset event, the high-band ratio, relative flux, stereo width, and the full sine-normalised **magnitude spectrum**. | Every spectral descriptor the brief asks for can be computed from `AnalysisFrame::magnitude`. No second FFT is needed. |
| Offline analysis | `analysis::AnalysisTrack` analyses the whole file at load, in both engine modes, then runs whole-track post-passes: the Ellis beat tracker, the meter, and the ADR-898 band onsets. | A timbre pass is one more post-pass of the same shape. It is deterministic by construction: same file, same frames. |
| Live analysis | `AnalysisRunner`: the audio callback feeds an SPSC `AnalysisStream`, a background thread runs the Analyzer, and a `TripleBuffer` carries the frame to the render thread. | This is exactly the brief's §22 model. Live input (Phase 7) would run the same timbre function on this thread. |
| Signal bus | `signals::SignalBus`: flat names, dot-separated. Families are `audio.*`, `beat.*`, `time.*`, `section.*`, `music.*` (the ADR-073 structure events: beat, downbeat, build, drop, impact...), `lfo.<name>`, `env.<name>`, `noise.<name>`, `random.<name>`, `timeline.<name>`, `control.<ch>`, `macro.<knob>` and `state.*`. Suffix variants exist too (`lfo.<name>.bipolar`). Events are one-frame pulses carrying a strength. | New families should be `sonic.*` and so on. **`music.*` is already taken** by audio-derived structure events, so the brief's `music.noteDensity` would put note context beside them and mean something else. I use `notes.*` instead (see 4.3). |
| Per-analysis-frame runtimes | `MusicRuntime` lives in `SignalClock`. It consumes every analysis frame (the hop clock, never the render clock, so any frame rate gives the same answer) and publishes once per render frame. | The Sonic Character should be built the same way, and in the same place. |
| Seek replay | ADR-870/901: a seek replays the deterministic prefix of the bus (`declareFrameSignals`) on its own `SignalClock` and checkpoints it. Pure-in-time sources are re-sampled onto an engine-shaped bus, so routes reading them are replayed exactly. | Two consequences. Sonic state must live in `SignalClock`, which makes it checkpointed for free. Its signals must be declared in `declareFrameSignals`, so they get the same ids on both buses. |
| Modulation | `ModRoute`: source signal, then a processor chain (smoothing, attack/decay envelope, curve, remap, clamp, delay, threshold), then amount, op, an ADR-900 depth signal (a multiplicative gate), then the parameter. Sources (`SourceRack`) are a factory of kinds, each registering `sources/<name>/...` parameters and saved in the project's `sources`. | The per-target shaping the brief wants already exists. What routes cannot do is **many-to-one semantic blending**, which is the Interpreter's actual job. |
| MIDI | `control/midi`: CoreMIDI input, a byte parser, and `ControlMap` bindings that turn notes, CCs and bends into `control.<ch>` signals. There is also a MIDI clock tempo source. **No MIDI file reader, and no note-event model**: a note is just a 0..1 on a control channel. | Musical context needs a new note-event representation and a way to feed it offline. |
| Threading | The audio callback feeds the ring; the analysis thread runs the Analyzer; the render/main thread consumes frames, updates the bus, sources and modulator, and renders. The offline `RenderJob` uses its own Offline engine, where analysis is precomputed at load. | Offline, no DSP happens per render frame at all: the timbre pass runs at load. Per render frame there are only O(1) smoothing and mapping updates. |
| Generators | Composition `procedural` nodes (boxes, cylinders, spheres, tubes, bevels, distributions, per-frame GPU deformers, material parameters), particles, fog/sky, light rigs, and a free camera. Many parameters are per-frame uniforms (deformer amount/speed/frequency, transform, material, emissive). | The Sonic Garden can be a composition built from these, with no engine work. |
| Debug UI | The Control panel's `drawAnalysis` section: text and ImPlot bars. | The §24 view is a compact section appended to it. |

## 2. Outside references, briefly, and what each changes

- **Essentia / librosa.** These are the definitions I follow. Centroid; spread (bandwidth) as the
  magnitude-weighted standard deviation around the centroid; rolloff at 85%; flatness as the
  geometric over the arithmetic mean of power (librosa); inharmonicity as the energy-weighted
  divergence of partials from the nearest multiple of f0 (Essentia `Inharmonicity`, 0..1); sensory
  dissonance as a Plomp-Levelt pairwise sum over spectral peaks (Essentia `Dissonance`, 0..1).
  Essentia's streaming extractor is a batch design; the useful lesson is its *pipeline*, peaks then
  per-peak descriptors, not the extractor itself. Taking Essentia as a dependency is not justified
  (it is AGPL/large); the handful of descriptors needed are about 400 lines.
- **Timbre Toolbox (Peeters et al. 2011).** Its main finding helps: its ~160 descriptors collapse into
  about **ten roughly independent groups**. Central tendency of the spectrum (centroid, spread,
  rolloff), temporal envelope (attack, decay), spectral variation (flux), noisiness (flatness), and
  harmonic energy. One representative per group is enough; the rest are redundant for mapping. This
  is why I reject MFCCs, contrast, crest and skewness below.
- **McAdams timbre spaces.** Across studies, the perceptual axes are attack time, spectral
  centroid, and spectral flux or irregularity. Those three are the spine of the character model:
  `sharpness` (attack), `brightness` (centroid), `movement` (flux).
- **Cross-modal correspondences.** None of these is a law; each is a tendency that informs defaults.
  - Pitch and timbral brightness go with visual lightness, both robustly (Spence 2011 review; Adeli
    2014; a 2024 Frontiers study on instrument timbre and colour).
  - Heavy/dark/low sounds go with *more saturated*, darker colours.
  - Soft timbres go with rounded shapes; harsh timbres go with angular, jagged shapes (Adeli 2014,
    "bouba/kiki").
  - Hue associations are weak and inconsistent, except for a few instruments; "warm" timbre does
    *not* reliably map to warm hue.
  - For the art agent: shape and lightness are the dependable channels, and hue is the free
    artistic channel. Adeli found harsh timbres picked red and yellow, which the brief deliberately
    overrides (§14, "do not map aggressive to red"). That is an artistic choice against a tendency,
    and is fine.
- **Web Audio AnalyserNode.** Two ideas carry over. `smoothingTimeConstant` (a one-pole across
  frames), and **a fixed dB window** (`minDecibels`/`maxDecibels`) rather than auto-gain. AV Gen
  already learned the second lesson in ADR-897.
- **MilkDrop, Synesthesia, TouchDesigner.** These normalise bass/mid/treble against their own recent
  average. That is right for "react to this track", and **wrong for this brief**: normalising each
  sound against itself erases the difference between sounds, which is the whole §34 test.
- **MIDI 2.0.** It adds 16-bit velocity, 32-bit controllers, per-note controllers, per-note pitch,
  and Note On attributes (articulation, exact pitch). The note model below has float velocity, a
  note id, and a float pitch, so none of that is precluded. Parsing UMP is not in this POC.
- **CLAP events.** Notes are addressed by (port, channel, key, note_id), velocity is 0..1, events are
  sample-timestamped and sorted, and there are seven per-note expressions (volume, pan, tuning,
  vibrato, expression, brightness, pressure). I adopt the addressing tuple and float velocity. Per-
  note expression is future work; the note struct has room for it.
- **Mapping many dimensions to visuals (NIME: Hunt and Wanderley, and others).** Two findings apply.
  - A mapping through an **intermediate perceptual layer** beats a direct one-to-one mapping. That
    layer is the Sonic Character.
  - **Many-to-one** blends (several descriptors into one visual control) read as more "intentional"
    than one-to-one mappings.

  This argues for the Interpreter being a small blending language over named signals, not code.
- **Smoothing perceptual features.** The candidates were a one-pole with asymmetric attack and
  release, a median for pitch, hysteresis for discrete choices, and the 1-euro filter (Casiez 2012),
  which adapts its cutoff to the speed of change.
  - For the POC, **asymmetric one-poles at three tiers** are enough. They are deterministic, cheap,
    and checkpointable (a single float of state).
  - Discrete choices ("which visual family") are replaced by **continuous competing weights** read
    from the slow tier. This gives the continuity §36 asks for without hysteresis.

## 3. Recommended architecture (challenging §8 where the code suggests a better fit)

```
audio file ─► AnalysisTrack (existing STFT) ─► sonic::analyzeTimbre post-pass (load time, offline)
                                                     │  per analysis frame: TimbreFeatures
                                                     ▼
SignalClock.sonic : SonicRuntime ── per analysis frame (hop clock) ── Sonic Character (tunable terms)
                                  │                                      fast / medium / slow tiers
.mid file ─► sonic::NoteTrack ───┤── per render frame, a pure function of time ── Musical Context
                                  ▼
                    bus: timbre.* (raw)  sonic.* (character)  sonic.*.slow  notes.* (context)
                                  ▼
       SourceRack: "interpret" source(s) ── weighted blends of any bus signals ──► visual.<name>
                                  ▼
                    ordinary ModRoutes (chains, depth) ──► parameters ──► Sonic Garden composition
```

The challenges to the brief's §8, each adopted.

1. **The Visual Interpreter is a Source kind, not a new layer.**
   - The engine already has a registry of signal producers that are optional, saved in the project,
     tuned through parameters (`sources/<name>/...`), and (if pure) replayed by seeks: the
     `SourceRack`.
   - An `interpret` source holds named mappings. Each mapping is a weighted blend of any bus signals
     (sum, product, min or max), shaped by bias, gain and a curve. It publishes `visual.<mapping>`.
   - Mappings may **compete in a group**: their outputs are renormalised to sum to 1 with a
     sharpness exponent. That is how visual families become continuous weights rather than `if`s
     (§26).
   - This is all data: the art agent edits the project, and the engine does not change.
   - It is pure given the bus, so a seek replays it exactly.
2. **Absolute normalisation, never auto-gain.** Every character term maps a physical feature through
   a fixed, tunable range (Hz on a log scale, dB, ratios). A running-max normaliser would make the
   pad and the distorted bass both read "bright 1.0" within seconds.
3. **Timbre is an offline post-pass over the stored spectra, not a change inside the streaming
   `Analyzer`.**
   - The Analyzer is untouched, so no existing project's analysis moves.
   - The pass runs only when a project enables the sonic block, so an unused subsystem costs nothing.
   - It is a pure function of the frame sequence, so offline renders are deterministic. Live input
     in Phase 7 calls the same per-frame function on the AnalysisRunner thread.
4. **Musical context is a pure function of time** over a sorted note list. The note-derived
   quantities are exponential-kernel sums over the notes before `t`: density, rhythm, motion,
   duration, repetition. Only the note-on/off events need the previous frame's time. A seek therefore
   lands exactly where a play does, with no state to replay.
5. **The `notes.*` family, not `music.*`.** `music.*` means audio-derived structure events
   (ADR-073), and they fire with no MIDI at all. Putting MIDI note context beside them would make one
   family mean two things.
6. **MIDI is fed as a Standard MIDI File beside the audio** (`sonic.notes` in the project). It is the
   format a DAW exports with the bounce, and the reader is about 150 lines with no dependency. The
   test-material synth writes the `.mid` it played. Live MIDI (Phase 7) would fill the same
   `NoteTrack` incrementally from `control::MidiInput`.
7. **The raw measurements are on the bus too** (`timbre.*`). A diagnostic view or an advanced
   mapping can then read them without a detour, and the character layer stays a documented,
   replaceable interpretation.

The Director (§21) sits above this, unchanged: it can author or scale interpreter mappings and
routes like any others.

## 4. Descriptors

### 4.1 Implemented (raw, `timbre.*`, per analysis frame)

| descriptor | how | why it earns its place |
|---|---|---|
| loudness | rms in dBFS, fixed -60..0 | energy, and the gate below which timbre is held |
| centroid | existing (Hz) | brightness (McAdams axis) |
| bandwidth | magnitude-weighted spread around the centroid, Hz | density and fullness |
| rolloff | 85% power frequency, Hz | brightness and the reach of the upper spectrum |
| flatness | geometric / arithmetic mean power, 60 Hz-16 kHz | noisiness (Toolbox group) |
| low ratio | power below 250 Hz over the total, dB | warmth and weight |
| high ratio | existing ADR-897 (>2 kHz), dB | brightness and sharpness |
| relative flux | existing, level-free | movement (McAdams axis) |
| f0, pitch confidence | harmonic-sum salience over 40-2000 Hz on picked peaks | tonal / pitched character |
| harmonicity | share of peak energy explained by up to 4 iteratively-estimated fundamentals (estimate-and-cancel, Klapuri) | organic/crystalline vs noise. Multi-f0 so that a chord is not "inharmonic" |
| inharmonicity | energy-weighted distance of explained peaks from their nearest harmonic, unexplained peaks at maximum (Essentia's definition, extended to multiple f0) | the FM bell against the pad |
| dissonance | Sethares' Plomp-Levelt pair sum over the strongest 24 peaks, normalised | roughness: distortion intermodulation and beating |
| peak count | significant spectral peaks (log-scaled) | complexity and density |
| transient | rise of the frame level over a 150 ms follower, in dB, 0..1 on 0..18 dB | attack sharpness, from existing frame rms |
| level slope | the follower's dB/s | decay and sustain behaviour |
| width | existing | spatial |

Temporal statistics are derived in the runtime from these: the variance of the log-centroid over
about 1 s (stability), and the smoothed flux (movement).

### 4.2 Sonic Character (`sonic.<dim>` medium tier, `sonic.<dim>.slow` slow tier)

energy, brightness, warmth, roughness, sharpness, smoothness, harmonicity, inharmonicity, density,
complexity, stability, movement, organic, mechanical, spatial. There is also a fast event,
`sonic.transient`.

- Each dimension is a weighted mean of terms. A term is a feature on a fixed range, optionally log
  and optionally inverted.
- Each dimension has its own medium attack and release, and a slow tau.
- The whole table is overridable from the project's `sonic.character` block.
- Timbre dimensions are **held** while the sound is below a loudness gate, so a sound keeps its
  identity between notes; energy falls.

### 4.3 Musical Context (`notes.*`, from the note track)

`noteOn`, `noteOff`, `phraseStart` (events, strength = velocity), `polyphony`, `density` (notes/s),
`rhythm` (distinct onsets/s), `velocity`, `pitch` (centre), `range`, `motion`, `direction` (-1..1),
`duration`, `legato`, `regularity`, `chord` (simultaneity), `tension` (interval-class dissonance of
the sounding set), `repetition`, `phrase`.

### 4.4 Rejected for the POC, and why

| rejected | reason |
|---|---|
| MFCCs | Not interpretable as a visual metaphor; only useful to a learned mapping (§33, no ML). |
| Chroma / HPCP, key, chord recognition from audio | The note track states the pitches exactly. From audio they are noisy and redundant here. |
| Spectral contrast | In the Toolbox's noisiness/peakiness group; flatness plus harmonicity cover it. |
| Zero-crossing rate | Redundant with centroid and flatness on this material. |
| Perceptual loudness (LUFS, Zwicker), Zwicker/Aures sharpness | Large filterbank models; rms dB and centroid capture the POC distinctions. |
| Log attack time per note | Needs note segmentation from audio. The transient follower gives attack sharpness continuously. |
| Beat tracking, segmentation | Already exist (ADR-896/898, song analysis); reused as they are. |
| Stereo correlation, L/R balance | Width already exists; the test material is centred. |
| Per-note audio attribution (which partials belong to which MIDI note) | The most promising future refinement (MIDI-informed harmonicity), but not needed to prove §34. |

## 5. Risks

1. **Pitch and harmonicity on dense chords and on FM spectra.**
   - Multi-f0 estimate-and-cancel can explain inharmonic partials with a low subharmonic.
   - The candidate range and the Klapuri weighting mitigate this. Tests pin harmonic > FM bell >
     noise on synthesized cases.
   - If it proves unreliable on real material, MIDI-informed harmonicity (the sounding notes as the
     known f0s) is the fix.
2. **Fixed ranges are tuned on synthesized material.** Real mixes will sit elsewhere. The ranges are
   data, and the diagnostic view shows the raw values, so they can be retuned rather than coded.
3. **A mixed track is one sound.** Timbre descriptors of a full mix describe the mix, not an
   instrument. Instrument separation is §28 future work. The POC material is one sound at a time on
   purpose.
4. **Load cost.** The timbre pass adds work per analysis frame, and only for projects that enable it.
   Measured in `PROGRESS.md`.
5. **Replay coverage.** Routes from `visual.*` are replayed because the source is pure given the bus.
   A mapping reading a non-replayed signal (for example `control.*`) reads it as absent in a seek,
   as every such source does today.
6. **Declared-but-idle signals.** The `sonic.*`/`notes.*`/`timbre.*` names are declared on every bus,
   because the replay bus's layout is fixed at construction. They sit at 0 in projects that do not
   use them. That is the one visible trace of the subsystem in an unrelated project: about 60 more
   names in the route-source picker.
