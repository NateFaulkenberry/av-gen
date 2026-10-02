# Sonic Garden VFX expansion: architecture proposals (deliverables 7, 8 and 9)

Engineering agent, 2026-10-02. The owner's brief is `02-brief-vfx-expansion.md`. The research behind these proposals is in
`research/02-modulation.md`, `04-glitch-feedback.md` and `05-evaluation.md`. The scene catalog and the per-scene mappings
(deliverables 6 and 17) are the art agent's. This document provides the engine vocabulary they are written in. ADR block:
1060-1079.

## 0. What exists, in one table (surveyed 2026-10-02)

| layer | exists | gap the brief names |
|---|---|---|
| analysis | 2048/512 STFT; broadband flux onset (live and offline); offline-only kick/snare/hat (ADR-898, look-ahead); causal and offline beat trackers | no causal per-band onsets, so **live has no kick/snare/hat**; live events between render frames are lost (the render thread reads only the latest analysis frame) |
| Sonic | 15-dimension character (medium and slow tiers), one level-rise `sonic.transient`, 18 `notes.*` context signals and 3 note events, the interpret source | no transient/sustain split, no signals by kind (hit, level, presence), no sensitivity model, no per-note MIDI signals |
| routes | chains (gain, curve, threshold, attack/decay, envelope, spring, integrate), `depthSource` (a signal scales a route), the macro bridge (`macros/<k>` written by a route is published as `macro.<k>`) | route amounts are not parameters; no effect's final value is on the bus |
| Effect Library (ADR-702) | 48 kinds, every leaf a routable parameter `fx/<id>/<leaf>`, owners World/Entity/Camera/Light, EVENT activation via `TriggerClock` | **the trigger sources read only the offline analysis track: live, Shockwave, Ripple, Lightning, Discharge, Bounce, Shake and a triggered Dissolve never fire**; no routable opacity on procedural materials |
| post | lens CA and distortion, bloom, halation, grade, sweep, vignette, grain, motion blur, FXAA | no shockwave, block displacement, tear, RGB split (directional), pixel sort, scanlines, mosaic, posterise, radial blur |
| temporal | echo and mosh, both FIR over a clean ring (ADR-410, ADR-1049) | no feedback, no slit-scan; no GPU timer on the temporal passes; no ring pre-roll |
| evaluation | the Creative Critic (separate repo, numpy/OpenCV), `av.reactivity` over five fixed audio features | it cannot correlate against Sonic's own signals, has no slow-against-fast test and no motion tiers |

## 1. Deliverable 7: the audio/MIDI response architecture

```
audio -> Analyzer frame --+--> CausalOnsetDetector (NEW, src/analysis, one per frame stream, causal)
                          |        kick / snare / hat / onset / low (bass attack) events + continuous ODFs + fast band dB
                          |        - live runner: also fills audio.onsetLow/Mid/High, so live has kick/snare/hat
                          |        - AnalysisTrack: the same detector over the file, in order (file == live)
                          v
                    TimbreAnalyzer (copies the causal block into TimbreFeatures: the snapshot the live queue carries)
                          v
                    SonicRuntime::step (hop clock, both paths)
                          +-- character (unchanged)
                          +-- ResponseModel (NEW): conditioning chain per signal, with the live controls (section 2)
                          v
                    bus: response.*   (NEW, appended after notes.* so every older id is unchanged)
.mid / live MIDI -> NoteTrack -> contextAt + noteFacts (NEW per-note signals) -> notes.* (appended)
                          v
         interpret source -> visual.*    routes -> any parameter    TriggerSource::Signal (NEW) -> Effect Library fronts
```

### 1.1 The signal set, by kind (Synesthesia's lesson)

Every `response.*` signal is 0..1 and has passed through the conditioning chain in section 2. Hits are bus events (a
one-frame pulse whose value is the strength) and each has a shaped envelope beside it, so a route can choose either.

| signal | kind | what | typical use (art's choice) |
|---|---|---|---|
| `response.kick` (E), `response.kickEnv` | hit | low-band attack shaped like a drum: kick-band flux high against the flux of the bass's harmonics above it | shockwave, scene impact, geometry pulse |
| `response.snare` (E), `response.snareEnv` | hit | 150-300 Hz body and 2-8 kHz noise together | flash, burst, local distortion |
| `response.hat` (E), `response.hatEnv` | hit | 6-16 kHz attack | fine particles, grain |
| `response.low` (E), `response.lowEnv` | hit | any low attack: a kick or a bass note | "a single bass note: a sharp impact" |
| `response.onset` (E), `response.onsetEnv` | hit | broadband log-compressed SuperFlux over a causal median | any attack, level-free |
| `response.hatRate` | presence | hats a second (1.5 s leaky count) / 12 | granular activity |
| `response.bass` | level | 30-150 Hz level in dB on a fixed range, fast (attack 15 ms, release about 200 ms) | displacement, pressure |
| `response.level` | level | full-band level, the same chain | general intensity |
| `response.transient` | level | fast follower minus slow follower in dB (the transient designer) | "how attacky the sound is now" |
| `response.sustain` | level | slow level x (1 - percussiveness): energy that stays | breathing, ambient light, slow growth |
| `response.flux` | level | the broadband ODF, conditioned | change, turbulence |
| `response.melodic` | presence | audio: the rate of confident pitch change; with MIDI, the single-note onset rate x interval motion | moving lights, focal shifts |
| `response.pitch` | level | the latest confident pitch, or the latest MIDI note, 0..1 (C1-C8) | spatial and colour placement |
| `response.intensity` | presence | a 12 s follower of `response.level` (macro dynamics, not normalised away) | palette weight, density |

MIDI gets its own signals, separate from the audio. The per-note signals are new, appended to `notes.*`:

| signal | what |
|---|---|
| `notes.lastPitch`, `notes.lastVelocity` | the latest note-on's pitch (0..1) and velocity: per-note placement and strength, not the context's centroid |
| `notes.interval` | the latest melodic step, signed, semitones / 12 (-1..1) |
| `notes.lowest`, `notes.highest` | the sounding notes' range, 0..1 |
| `notes.voice.<0..7>.{held, velocity, pitch, age}`, event `notes.voice.<i>.on` | voice slots: a note owns the lowest free slot for its life (ADR-1062) |
| `notes.class.<0..11>`, event `notes.classOn.<k>` | pitch-class lanes (C = 0): the loudest sounding velocity |
| `notes.velocitySpread` | the standard deviation of velocity over the context window / 0.5 |
| `notes.held` | how long the longest sounding note has been held, log-scaled 0.05-4 s: tells a pad from a stab |
| `notes.release` (E) | a note-off whose strength is the note's duration (log 0.05-4 s), so short and long notes end differently |
| `notes.low` (E), `notes.high` (E) | a note-on below or above the register split (`sonic.response.splitKey`, default 60), strength = velocity |

As built (ADR-1062): every name above exists. `notes.step` was folded into a signed `notes.interval`.
| `notes.channel` | the latest note's channel / 15 |
| `response.note` (E), `response.noteEnv` | the note-on through the transient chain (velocity curve, attack/release) |

The existing 18 `notes.*` context signals and three events stay as they are. `response.melodic` reads MIDI when notes
are present and the audio otherwise.

### 1.2 Determinism

- The detector is a function of the frame sequence, like the character. It is run over the file at load (in the
  `AnalysisTrack` pass) and per frame live; the SAME code runs on both.
- The response model steps on the hop clock inside `SonicRuntime::step`, so a backward seek replays it from zero exactly
  as the character does (ADR-1020).
- The live controls are project parameters (section 2). They are read when stepped. With constant controls, seek equals
  play exactly. With keyed controls, a seek replays with the value at the seek target, so the envelopes can differ for
  up to one release time (documented; two renders of the same range never differ).
- `notes.*` additions are pure functions of time over the note track, like the rest of the context.

### 1.3 Live triggers for the Effect Library (the art agent's blocking gap)

A seventh `TriggerSource`, `signal`: `{"source": "signal", "name": "response.kick", "threshold": 0.3}` fires on any bus
EVENT signal whose strength reaches the threshold. The `TriggerClock` contract stays one pure function, "the newest event
times at or before t, searched backward", over a per-signal event list. How each list is built:
- **Offline and file playback:** derived once from the piece wherever the signal is a function of it:
  - `notes.*` events from the note track (`eventsBetween` over the whole track);
  - `sonic.*`, `response.*` and `timbre.*` events by walking a scratch `SonicRuntime` over the timbre track;
  - `audio.onset*` and `audio.beat` from the analysis track's frames.
  
  A seek then finds the same fronts a play reaches (ADR-091), like every other source.
- **Live, and for any other signal:** recorded from the bus each frame at the frame's transport second, append-only. A
  backward jump drops the recorded events after the new time. These lists are "what has happened", which is all live
  has. `silence()` says so on the panel when a recorded signal has not fired yet.

### 1.4 Effects modulating effects (§5, §6)

Three mechanisms, one of them new:
1. **A route scaled by a signal** (`depthSource`, exists). For example, MIDI velocity sets how much the kick route
   distorts.
2. **A parameter as a signal** (exists): the macro bridge. Route A writes `macros/<k>`, and route B reads `macro.<k>`.
3. **NEW: `fx.<id>.<leaf>` on the bus, for the leaves a project asks for.** A project's `"publish": ["fx/heroGlow/gain"]`
   publishes that parameter's final value as the signal `fx.heroGlow.gain` after modulation, so another effect's route
   can read it: audio drives the glow, and the glow drives the spill light and the post bloom. Pure given the
   parameters, so it replays on seek. Published at the frame's end, so a reader sees it one frame later (17 ms), the
   same latency the macro bridge has.

Missing, but not added (each is a different engine's work): routable material opacity, a per-node animation time scale,
and effect-light shadows. The art agent can reach dissolve through `fx/<dissolve>/progress` and animation rate through
deformer `speed` and effect `rate`/`period` leaves.

## 2. Deliverable 8: the sensitivity and transient model

### 2.1 The chain (brief §10)

```
raw  ->  floor gate  ->  normalise  ->  sensitivity  ->  response curve  ->  attack/release  ->  response.*
```

| stage | levels (bass, level, sustain, flux) | hits (kick, snare, hat, low, onset, note) |
|---|---|---|
| raw | band power in dB (fixed, never auto-gained: a quiet passage looks quiet) | the ODF's ratio over its own causal median: level-free by construction |
| floor gate | below the floor (default -60 dBFS for levels; ratio 1.0 for hits) is 0 | |
| normalise | `(dB - floor) / range`, range 48 dB | `(ratio - 1) / span`, span 4 |
| sensitivity | **moves the floor** (s 0..1, 0.5 neutral: +-18 dB) and the curve; it never multiplies the top | **moves the firing threshold** (+-0.6 of the ratio) |
| curve | `x^gamma` with `gamma = 2^((0.5 - s) * 1.6)`: higher sensitivity lifts the small values, 1 stays 1 | the same, on the strength |
| attack/release | asymmetric one-pole per signal, default times per signal (bass 15/200 ms, sustain 250/900 ms, ...), multiplied by the panel's Attack and Release | the envelope: a linear attack (default 4 ms) and an exponential release (kick 160 ms, snare 120 ms, hat 60 ms, onset 90 ms, note 220 ms), times the multipliers |

The **two sensitivities split the instrument**:
- **Transient sensitivity** scales the hits: it lowers their thresholds and lifts their strengths, and at 0 no hit fires.
- **Sustain sensitivity** shifts the levels: at 0 the sustained signals sit at 0.

The overall **Sensitivity** moves both. A quiet synth therefore gets its hits back without its pad lifting the whole
world, which `audio/inputGain` (a multiplier before analysis) cannot do. The existing input gain stays on the panel as
"Input gain", for the interface's level.

### 2.2 The parameters (project-level, routable, saved)

`sonic/response/{sensitivity, transient, sustain, attack, release}`:
- `sensitivity`, `transient` and `sustain` are 0..1 (0.5 neutral).
- `attack` and `release` are multipliers, 0.25..4 (1 neutral).

They live in the project, not the machine, so an offline render reproduces what the performer set. They are ordinary
parameters, so a scene can key them or route to them (Transient sensitivity rising into the chorus). The Live panel's
five sliders edit them. They are registered only when the project has a `sonic` block; when the block is absent nothing
is registered and nothing is computed.

Per-signal detail (band edges, refractory times, per-signal times, floors) lives in an optional `sonic.response` JSON
block. It is not on the panel. That is the line between a performer's instrument and an audio-engineering UI (research
2 section 5).

### 2.3 The live panel (brief §10, "without becoming an audio-engineering UI")

A **Response** group under the audio input:
- **Sensitivity**, **Transients**, **Sustain**;
- **Attack**, **Release** (Release is the feel knob);
- a row of four hit lights (kick, snare, hat, note) that flash when the hit fires, so the performer sees the detector
  hear the drum;
- **Reset**.

The existing Smoothing slider (the character's time constants) stays as it is.

## 3. Deliverable 9: technical feasibility and cost per new renderer capability

Costs are estimates for 1080p on this machine (M2 Max) until measured in `PROGRESS-vfx-eng.md`. Every effect below:
- is OFF by default;
- is skipped entirely when its amount is 0, so the frame is byte-identical (the ADR-1050 pattern: a structural gate,
  never a zero-strength pass);
- is a pure function of the frame, the clean ring and timeline time (hashes of `floor(time x rate) + seed`), so offline
  output is deterministic and seek equals play (ring effects after the ring's K-frame refill, as the echo and mosh are
  today).

### 3.1 Post effects as instruments (new pass `post/glitch` before bloom, and `post/display` after the composite)

| effect | parameters | musical purpose | GPU at 1080p | memory/targets | tier scaling |
|---|---|---|---|---|---|
| screen shockwave ring with CA fringe | `post/shock/{amount, radius, width, center, chroma, aspect}` (radius 0..1.5 of the half-diagonal; a route envelope drives it from `response.kickEnv`) | kick, drop: an impact through the whole picture | 1 FSP, about 0.15 ms | none | none |
| block displacement + line tear + channel swap | `post/glitch/{amount, block, rate, seed, tear, swap, drift}` | snare, fills: structural damage that holds for one quantum | 1 FSP, about 0.15 ms | none | none |
| directional/spectral RGB split | `post/split/{amount, angle, spectral}` | kick transient, a lateral smear on a hit (the existing radial CA stays) | 1 FSP, 3 taps (8 spectral), about 0.1-0.2 ms | none | spectral taps 8 -> 4 at Preview |
| fake pixel sort (masked directional max-smear) | `post/sort/{amount, threshold, length, angle, invert}` | melts on sustained, bright material; accents | 1 FSP, <= 32 taps, about 0.3-0.5 ms | none | taps 32/24/16 by tier |
| radial (zoom) blur | `post/radial/{amount, center}` | risers, impacts | 1 FSP, 12 taps, about 0.2 ms | none | taps 12 -> 8 at Preview |
| scanlines + mosaic + posterise/Bayer dither (one display pass) | `post/display/{scanlines, lines, pixelate, posterize, dither}` | section colour, the digital worlds | 1 FSP, about 0.1 ms | none | none (the mask period is in output pixels, the line count in UV) |

All of these are placed beside `lens` (geometric, HDR, before bloom so the glow follows the warp) except the display
pass. That runs after the composite and look, before FXAA, in display-referred colour. Each pass is gated separately,
so off costs nothing and the guarantee is structural. Hash randomness uses `renderTime`, which `PostFrameInputs` gains.

### 3.2 Temporal (FIR over the clean ring, ADR-410)

| effect | parameters | purpose | GPU | memory | notes |
|---|---|---|---|---|---|
| feedback (zoom/rotate/drift/hue, unrolled) | `temporal/feedback/{enabled, frames, amount, decay, zoom, rotate, driftX, driftY, hue}` | pads, builds, tunnels: MilkDrop's look | 1 FSP, K taps (K = `frames`, default 8, at most 16): about 0.3-0.6 ms | the existing ring (K x 2 MB at half res) | `F = C + amount * sum_k decay^k R_k(T^k(uv))`, hue rotated k x hue in OKLab. Exact, seekable, never an accumulator |
| slit-scan time displacement | `temporal/slit/{enabled, frames, amount, mode, angle}` (mode: rows, columns, radial, luma) | ambient, sustained: the picture's past folded across space | 1 FSP, 2 taps (blended layers): about 0.15 ms | the ring | stutter is slit-scan with a constant offset; the beat grid provides it by routing |

Also in this family: **the temporal passes get their GPU timer** (`temporal/echo`, `temporal/mosh`,
`temporal/feedback`, `temporal/slit`, `temporal/capture`). Today their cost is charged to `post/meter`.

### 3.3 Considered and not built now

| technique | why not now |
|---|---|
| exact segmented bitonic pixel sort (compute, 0.5-1.5 ms per axis) | the stateless max-smear gets the look for a tenth of the cost |
| a motion-vector datamosh (advected IIR buffer) | IIR, forbidden by ADR-410; the existing FIR mosh plus the block displacement cover the purpose |
| progressive odd-even sort | stateful, and its warm-up grows with time |
| true 8x8 DCT macroblocks | a compute pipeline for a look the block displacement and posterise approximate |
| reaction-diffusion, fluid, GPU particle simulation | the brief's §18 test is "does it unlock a new visual language". These need simulation state that ADR-091 allows only with a declared warm-up, and each is its own phase. Recorded as the recommended next step if the art agent's catalog needs a fluid world |
| a ring pre-roll after seek (ADR-410's designed warm-up) | rings refill in K frames (at most 16 for these effects, 0.27 s); the cold frames are deterministic. Recorded, not built |

### 3.4 Performance tiers (§19)

The existing tiers are Preview, Realtime, High and Offline (`render_quality.hpp`). By their rule, a tier may scale
sample counts and resolution but never an artistic control or a history length. So:
- The new passes scale **taps**: the sort 16/24/32/32, the radial blur and the spectral split 8/12/12/12.
- The ring's resolution scales with the existing `temporalHistoryScale` (0.25/0.5/0.5/1).
- The new effects' amounts, sizes and frame counts are never touched by a tier.

The **LIVE** tier the brief names is the existing Preview (the Live panel's adaptive scale plus FXAA floor, ADR-1024).
Every effect can be disabled dynamically: its amount is a parameter, and 0 skips its pass.

## 4. Evaluator tooling (§16)

`tools/sonic_vfx_critic.py` (system python: numpy, PIL, ffmpeg; no new dependency). It extends the Creative Critic and
`liminal_critic.py` rather than replacing them:
- **`inputs`**: writes the Critic's `inputs.json` for a Sonic project and render. The trace's response signals are mapped
  onto the Critic's five audio features, note-on and kick times become route events, the scene's composition metadata
  (a `composition` block the art agent may add: focal, secondary, layers, regions) becomes regions, and the modulation
  is written as a list. The Critic's existing reactivity, flow, colour and defect rules then run on Sonic scenes
  unchanged.
- **`measure`**: the gaps in the Critic (research 5, section 10), as Critic-shaped findings (`key`, `rule`, `dimension`,
  `severity`, evidence):
  - composition: saliency peaks after NMS, dominance ratio, balance, symmetry, negative space, thirds;
  - colour: OKLab palette, hue spread, harmony residual, colourfulness, clipped area, muddiness, notan;
  - motion tiers per cell;
  - **per-signal correspondence**: for each `response.*`/`notes.*` signal, the event-locked latency, gain, decay and
    reliability, per region (a synchrony map), and the modulation-spectrum ratio (slow against fast);
  - **specificity**: do the kick and the hat move different regions?
  - defects: empty, overcrowded, clipped, WCAG flash risk, flicker.
- **`compare`**: two runs by stable keys.
- **`selftest`**: synthetic clips with known answers.

## 5. Implementation order (this agent)

1. The causal detector, live kick/snare/hat, and the Signal trigger source (the art agent's blocking gap): ADR-1060,
   ADR-1061.
2. The response model and the MIDI per-note signals, with the parameters and the Live panel: ADR-1062.
3. `publish` for effect parameters on the bus: ADR-1063.
4. The post glitch and display passes: ADR-1064.
5. Temporal feedback and slit-scan, plus temporal GPU timers: ADR-1065.
6. The evaluator.
7. Performance measurements, and both suites.

Each lands as a commit, with its usage sent to the coordinator for the art agent.

## 6. As built (2026-10-02)

The ADR numbers moved from section 5's plan:

| ADR | what | commit |
|---|---|---|
| 1060 | causal onsets, live kick/snare/hat | `4bc1a762` |
| 1061 | Signal triggers | `4bc1a762` |
| 1062 | the response model, per-note MIDI, Live Response group | `67f74e6a` |
| 1063 | the live scene switcher (added at the coordinator's request) | `c52d1768` |
| 1064 | the publish source | `5c94b436` |
| 1065 | the post glitch and display passes | this commit |
| 1066 | temporal feedback and slit-scan, temporal timers | this commit |

The sort's `length`, the shock's `radius`, and every pixel size are authored at 1080 lines. Measured costs are in
ADR-1065 and ADR-1066 and in `PROGRESS-vfx-eng.md`.
