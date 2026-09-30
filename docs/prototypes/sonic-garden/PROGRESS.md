# Sonic Garden POC: progress

Resume from here. Branch `proto/sonic-garden` in `../av-gen-sonic`. ADR block 1020-1039 (used: 1020).

Staffing: the engineering agent did phases 0-4 and the engineering half of Phase 5. The art agent (sonic-art) owns
the mappings, the families, the look and the §34-36 judgements; its pass 1 is recorded in "Art pass 1" below, and the
review media and ART-NOTES.md are in `~/Desktop/av-gen-review/23-sonic-garden/`.

## Rules in force

- All GPU work, **including the full `avgen_tests`**, goes through `tools/gpu-lock.sh`, because the CPU suite
  encodes video (see the lock script's header). A filtered run of the pure-CPU `[sonic]` cases may run directly.
- Build: `cmake --preset release && cmake --build --preset release`. Reconfigure after adding a test file.
- Judge a run by the binary's exit code. One FAILED line (the `[!shouldfail]` slope lean) is expected on a clean
  CPU run.
- Don't rebuild `avgen` while a render is using the binary.

## Status

| phase | state | notes |
|---|---|---|
| 0 research | done | `RESEARCH.md`, ADR-1020 |
| 1 audio analyzer (timbre) | done | `src/sonic/timbre.*` |
| 2 musical analyzer (notes) | done | `src/sonic/notes.*` |
| 3 sonic character | done | `src/sonic/character.*`, `src/sonic/sonic_runtime.*` |
| 4 interpreter | done | `src/sonic/interpret_source.*` (source kind `interpret`) |
| 5 test material + scene (engineering) | done | `tools/make_sonic_material.py`, `examples/sonic-garden/` |
| 5 art (mappings, families, look, §34-36) | pass 1 done (art agent, 2026-09-30) | see "Art pass 1" below; `tools/sonic_garden_look.py` |
| 6-7 | not started | live input and live MIDI are Phase 7 |

## Architecture (ADR-1020; details in RESEARCH.md §3)

```
audio -> AnalysisTrack (existing STFT) -> sonic::analyzeTimbre post-pass at load -> TimbreFeatures per frame
SignalClock.sonic (SonicRuntime): walks those frames on the hop clock -> Sonic Character (medium + slow tiers)
.mid -> NoteTrack -> contextAt(t): a pure function of time -> Musical Context
  => bus: timbre.*  sonic.*  sonic.*.slow  sonic.transient  notes.*
SourceRack "interpret" source: weighted blends of any bus signals -> visual.<mapping>
ordinary routes -> parameters
```

- **Optional.** A project with no `sonic` block runs no timbre pass and publishes zeros. The names are declared
  on every bus (last in `declareFrameSignals`, so every older id is unchanged).
- **Deterministic.**
  - The timbre is a function of the frame.
  - The character is a function of the frame sequence, and walks from zero after any backward jump.
  - The context is a function of time.
  - Seeks replay it; this is tested.
- **Off the render thread.** The DSP runs at load, next to the existing whole-track analysis. Per render frame the
  cost is about 1.3 us.

## Signals

- `sonic.<dim>`: the medium tier. `sonic.<dim>.slow`: the slow tier. The dims are energy, brightness, warmth,
  roughness, sharpness, smoothness, harmonicity, inharmonicity, density, complexity, stability, movement, organic,
  mechanical, spatial. `sonic.transient` is the fast event.
- `timbre.*` (raw): loudness (dBFS), pitch (Hz), pitchConfidence, harmonicity, inharmonicity, tonalness, flatness,
  dissonance, rolloff (Hz), bandwidth (Hz), transient, levelSlope (dB/s).
- `notes.*`:
  - Events: noteOn, noteOff, phraseStart (strength = velocity).
  - `active`: a count.
  - 0..1: polyphony, density, rhythm, velocity, pitch, range, motion, duration, legato, regularity, chord, tension,
    repetition, phrase.
  - `direction`: -1..1.
- `visual.<mapping>`: whatever the project's interpret source defines.

## Feeding MIDI

A Standard MIDI File beside the audio, named in the project:

```json
"sonic": {"notes": "notes/phrase.mid"}
```

The path is relative to the project and saved back relative. Type 0 and type 1 files are read, with tempo maps and
running status; SMPTE timing is refused. Live MIDI (Phase 7) would append to the same `NoteTrack`.

## Test material

- `python3 tools/make_sonic_material.py` (numpy, about 35 s) writes the audio to `assets/audio/sonic-*.wav`. The
  WAVs are gitignored; `assets/audio/manifest.json` has their hashes.
- The note files are written to `examples/sonic-garden/notes/*.mid`, which are tracked.
- The files:
  - `phrase.mid`: 8 bars at 96 BPM, 20 s. A sustained chord, a rising and falling arpeggio, a melody over a held
    chord, then dense stabs.
  - The phrase through four sounds: `sonic-pad`, `sonic-bell`, `sonic-bass` and `sonic-perc`.
    - pad: three detuned low-passed saws, slow attack.
    - bell: FM with a 1:1.4 ratio and a decaying index.
    - bass: a saw/square table with a sub, tanh drive per voice and on the bus.
    - perc: band-passed noise bursts and a pitched click.
  - `context.mid`: §35, 30 s. Sustained, then a 16th arpeggio, then dense 5-note chords. It is rendered as
    `sonic-context-pad` and `sonic-context-bell`.
  - `morph.mid`: §36, a 2-bar cell 6 times. `sonic-morph` goes clean, then bright, resonant, distorted, noisy, as
    continuous ramps.

## The scene: `examples/sonic-garden/` (also in the Examples menu, Lab, "Sonic Garden")

- **Both files are written by `tools/sonic_garden_look.py`** (art pass 1). Edit the tool, run it, then run the
  variants tool. The scene and the master's `sonic`, `sources`, `routes`, `timeline` and `parameters` are its output;
  hand edits to them are overwritten. The node and mapping inventory is in "Art pass 1" below.
- `sonic-garden.json`: the **master project** (the pad's audio, the `sonic` block, two interpret sources, 251 routes,
  a keyed camera). `--audit-routes` finds every route live.
- `variants/*.json`: the master with only the audio and notes swapped. **Don't edit these**: edit the master, then
  run `python3 tools/sonic_garden_variants.py`. Its `--width/--height/--out` flags write low-res copies elsewhere.
  It also stretches the master's timeline (the camera move) to each file's length: the context and morph files are
  31.5 s against the phrase's 21.5 s.

## For the art agent: how to tune without engine edits

1. **Interpreter mappings.** Since art pass 1 these are authored in `tools/sonic_garden_look.py` (`GARDEN_MAPPINGS`,
   `WORLD_MAPPINGS`), which writes the master's `sources`; run it, then the variants tool.
   - Inputs can be any bus signal, with a weight and an invert flag.
   - `combine` is mean, sum, product, max or min, followed by bias, gain and curve.
   - Mappings can share a `group`, whose `sharpness` sets how they compete.
   - The weights are also live parameters under `sources/garden/<mapping>/in<k>`, together with bias, gain and
     curve, and `sources/garden/group/family/sharpness`.
2. **Routes.** Map `visual.*`, or anything else, to scene parameters with ordinary chains: smoothing, envelopes and
   depth. Also in `tools/sonic_garden_look.py` (`R`, `palette`, `scalar`). `op: multiply` gives base x chain(x), so
   `gain`/`offset` in the chain set a range (`gain 0.4, offset 0.8` is 0.8..1.2 x base).
3. **The character itself.** A `sonic.character` block in the project overrides any dimension:

   ```json
   "sonic": {"notes": "...", "character": {"gateDb": -50, "dimensions": {"brightness":
     {"attack": 0.1, "release": 0.4, "slow": 4, "terms": [{"feature": "centroid", "lo": 250, "hi": 4000, "log": true}]}}}}
   ```

   - The features are listed in `src/sonic/character.hpp`.
   - A term can read an earlier dimension (`{"dimension": "warmth"}`).
   - There are also optional `timbre`, `context` and `scale` blocks (see `SonicSetup::fromJson`).
4. **Checking without the GPU:**

   ```
   build/release/src/avgen --project <p.json> --sonic-trace out.csv
   ```

   It prints the per-dimension means and the costs, and the CSV holds every sonic, notes, timbre and visual signal
   per frame. In the window, the Analysis panel has a compact "Sonic" section: bars for the medium tier, a tick for
   the slow tier, the context in numbers, and the `visual.*` outputs.
5. **Rendering the comparison:**

   ```
   python3 tools/sonic_garden_variants.py --out <dir> --width 640 --height 360
   tools/gpu-lock.sh build/release/src/avgen --headless --project <dir>/<sound>.json --render <out>.mp4 --range 0:21.5
   ```

   This took about 22 s per sound on the first scene; the art-pass scene takes about 25 s at 640x360, 35 s at
   960x540 and 85 s at 1920x1080 per 21.5 s. The render size flag is `--size WxH`. This ffmpeg has no `drawtext`,
   so labels are PIL PNGs overlaid.

## Art pass 1 (art agent, 2026-09-30)

Everything is data, written by `tools/sonic_garden_look.py`; the only other changes are the new rig
`examples/lightrigs/sonic-garden.rig.json` and the timeline stretch in `tools/sonic_garden_variants.py`. No engine
code changed. ART-NOTES.md in the review folder has the reasoning per family and per sound.

### The mapping language (interpreter `garden`, then `world`)

| layer | mappings | reads |
|---|---|---|
| families (group `family`, sharpness 3) | `organic` = warm x smooth x (1-bright)^1.5 x (1-inharmonic); `crystalline` = bright^.5 x inharmonic^.5 x (1-rough)^2; `chaotic` = mean(2 rough, sharp, 1-smooth), bias -0.2 gain 1.6; `silence` = full at zero energy, gone by 0.2 | slow tier |
| weight | `mass` = energy x density^3 x harmonicity^3 (loud AND full AND pitched), bias -0.3 gain 5 curve 1.5 | slow tier |
| world (second source) | `tectonic` = chaotic x mass; `impact` = chaotic x (1-mass) | this frame's garden outputs |
| qualities | `radiance`, `grain`, `edge`, `shimmer`, `breath`, `energy`, `tension`, `swarm` | medium tier |
| musical context | `sustain` (legato, duration, not rhythm), `figure` (rhythm x regularity x not chord), `stack` (chord, polyphony), `lift` (pitch) | `notes.*` only |

- Character tuning (the one retune): `roughness` also reads `bandwidth` (1.5-6 kHz, log). Distortion spreads the
  spectrum; before it, the bass read "dense and bright" (0.41) rather than rough.
- Phrase means: pad organic 0.93; bell crystalline 0.75 (organic 0.12, chaotic 0.13); bass tectonic 0.96; perc
  impact 0.98. The morph: organic 1.00 -> 0.93 -> 0.59 -> 0.25 -> 0.04 -> 0.00, crystalline peaking at 0.72
  (15-20 s), chaotic 0.53 (20-25 s) -> 0.95 (25-30 s). Context-pad: organic holds at 0.90-0.94 while sustain,
  figure and stack each lead their own section (0.80, 0.60, 0.77).
- **The same note-on, four gestures.** `notes.noteOn` is routed with `depthSource` = a family, so the family decides
  what the event looks like: organic, a 1.4-1.8 s swell of light; crystalline, a 90-320 ms ring of the crystals and
  halos; tectonic, a 450 ms heave of the core's scale; impact, `sonic.transient` (the audio's own attack) flashes and
  bursts. This is §19: MIDI says a note happened, the sound decides its form.
- Palettes are blended, never switched: every world colour (sky zenith and horizon, fog, ambient, hero, heart light,
  grade) is one add route per family and channel from a zero base, so an in-between sound gets an in-between world.
  Light colour is the rig's `temperature` (rig lights have no colour parameter).
- Each particle system belongs to a family through `particles/<n>/emissive` x family (spores organic, glints
  crystalline, dust tectonic, sparks impact), so a gesture in the wrong world spawns nothing visible.
- `silence` (a fourth family member: full at zero slow energy, gone by 0.2) owns the world before the first sound.
  Without it, frame 0's all-zero character made `chaotic` (via "not smooth") the only non-zero family, the slow
  route chains snapped to it, and every world opened with blades shrinking away; now each world grows in from a
  bare plain in about a second.

### The Creative Critic (one pass, preview mode, on the §34 sequence)

Job `job_1a0f22441b5f3737c` (session `sonic-garden`, track `s34`): 8 findings, 0 high, 4 medium; technical
quality, colour, composition and intent adherence 1.0. Acted on:
- F001 "the whole frame pulses with the audio" (98% of regions, 12% of mean luma, onset-locked): the heart light's
  note gains halved and its range cut from 14 to 9 m, so the response stays on the core and the ground near it.
- F003 "camera shake" in the bass: it was the ground swell and the monoliths following the energy at 60-120 ms;
  now 350-500 ms attack and 1.2-1.5 s decay (heavy things move slowly), and the core's note heave is slower.
- F004-F006 in the perc: the blade cloud no longer pumps on every note; the audio's transients expand it only above
  0.6 (a chain `threshold: gate`), and the heart flash is half as bright.
Not acted on: F002 (cuts off the beat: the "cuts" are the joins between four renders), F007 (the pad "shows
nothing new after 0.13 s": it is the calm world by design), F008 (bass and perc "share a composition": the same
camera move is the point of §34; the representative frames were the first, still-forming ones).

Second pass on the final sequence, `job_1a0f2444b7c997dcd`, compared with the first (`critic compare`): still 0
high; the whole-frame onset response fell from 98% of regions at 12% of mean luma to 94% at 9%; the bass's
high-frequency motion share from 28% to 13%; perc jitter from 0.18 to 0.06. The perc's "camera shake" remains
(its blade bursts fill much of the frame, which the global-motion estimate reads as camera motion), and three low
"long flash" findings in the dark perc world, where small absolute changes are large ratios. Accepted as the light
world's character; not iterated further.

### Review media: `tools/sonic_garden_review.py`

```
python3 tools/sonic_garden_review.py render --out <dir> [--size 1920x1080] [--stills-at 17]
python3 tools/sonic_garden_review.py assemble --out <dir> --review ~/Desktop/av-gen-review/23-sonic-garden
```

`render` regenerates the variants, takes the `--sonic-trace` CSVs (CPU), and renders all seven variants and four
supersampled stills in one `gpu-lock.sh` batch (about 16 minutes at 1080p: 85 s per 21.5 s file, 125 s per 31.5 s
file). `assemble` builds the grid, the sequence with audio, the stills sheet, and the §35/§36 videos with section
labels and corner readouts drawn from the traces. Both steps were run end to end at 320x180 before commit.

### The scene (20 nodes)

`ground` (150 m, undulating, with a swell only the heavy world raises), `horizon` (a far ring of mesas), `hero` (a
lumpy sphere: noise, sine breath, twist without speed, displacement), `facets` (a gem: a chamfered cube with two
rotated hierarchy copies), organic `stalks` (tubes), `caps` + `gills` + `capstems` (lathe-profile tubes; the gills a
glowing lip), `buds` (glowing bulbs raised only by dense chords: organic x stack), `petals`, crystalline `prisms` +
`spires` (cubes stood on a vertex and stretched: rhombohedra, the one faceted crystal the primitives allow),
`cluster` (small gems raised only by dense chords: crystalline x stack), `halos` (three thin tori), chaotic `shards`
(blades on a spiral) and `slabs` (monoliths that rise out of the ground), particles `spores`, `glints`, `sparks`,
`dust`, and a `heart` point light inside the hero.
The camera is keyed (`timeline`): a low wide establishing view from the front left, an arc right and in, closest on
the stabs, a lift at the end. Weight lowers it by up to 1 m and tips it up.

### Found while doing it (for the engineering agent)

- **Twist deformers invert normals past 90 degrees of turn.** `shaders/procedural.wgsl`, `vs_proc`: the finite-
  difference normal is flipped whenever `dot(nw, nRef) < 0`, where `nRef` is the *undeformed* normal. Any twist whose
  angle passes 90 degrees (always, once `speed * t` has run for a while) turns the band of faces around the axis inside
  out: a sphere with `twist speed 0.3` renders a black equator from about 5 s on (the caps, whose normals lie along the
  axis, stay right). The flip is only needed for a mirrored transform, so a fix is to take the sign from the
  determinant of the instance scale and object matrix instead of from `nRef`. Worked around here: no twist speed,
  and spins are `time.seconds` rotation routes (which rebuild the instance cloud per frame, cheap for these nodes,
  and are held inside the +-360 degree parameter range for 31.5 s).
- Interpreter parameters clamp silently: `bias` is +-4 and `gain` +-16, so a mapping authored with bias -9 runs at -4.
- The trace's event columns are always 0 (rows are written after `clearEvents`); watch a `visual.*` that reads them.
- Emission at note rate reads as a steady glow: a flash every 0.3 s with a 150 ms decay is lit half the time. Keep
  event decays under about 100 ms where notes are dense.

### Next

- Engineering: the twist normal fix above (with a render test); Phase 6/7 as planned. When live input arrives, the
  families and gestures here need no change (they read bus signals), but `silence` and the slow tier will define how
  a live world starts and how fast it changes; tune `mass`'s bias and the family sharpness on real material first.
- Art: the §35 bell sustained section starts half organic (a slow, single bell note genuinely reads soft and warm),
  which confounds "same timbre, different context" there; the pad version is the clean §35 demonstration.

## Readings (the default character, mean of the medium tier over voiced frames, phrase)

| dim | pad | bell | bass | perc |
|---|---|---|---|---|
| brightness | 0.14 | 0.51 | 0.72 | 0.88 |
| warmth | 0.87 | 0.74 | 0.52 | 0.17 |
| roughness | 0.10 | 0.25 | 0.41 | 0.75 |
| sharpness | 0.18 | 0.33 | 0.38 | 0.76 |
| harmonicity | 0.95 | 0.88 | 0.88 | 0.56 |
| inharmonicity | 0.28 | 0.52 | 0.30 | 0.69 |
| density | 0.54 | 0.68 | 0.94 | 0.98 |
| stability | 0.67 | 0.33 | 0.48 | 0.22 |
| movement | 0.36 | 0.71 | 0.54 | 0.82 |

- The pad and the bell are the closest pair. Brightness, inharmonicity, stability and movement separate them, but
  warmth, smoothness and harmonicity do not.
- The bass reads "dense and bright" more than "rough": its roughness is 0.41, against the percussion's 0.75. If
  the art agent needs the bass rougher, the roughness terms (dissonance range, flatness) are the place to tune.

## First visual result (the neutral mapping, before any art pass)

Review media is in `~/Desktop/av-gen-review/23-sonic-garden/`:
- `01-same-midi-four-sounds-grid-silent.mp4`: 2x2, 640x360 each.
- `02-same-midi-four-sounds-in-sequence-with-audio.mp4`.

The four sounds already look different, mostly through the family weights and the hero:

| sound | family weights | hero | other |
|---|---|---|---|
| pad | organic dominates: tendrils and blooms at full size | smooth, warm pink | crystals nearly gone, no shards |
| bell | organic still leads | paler | crystals grow; the tendrils thin |
| bass | a mix | grey and lumpy | small shards around it |
| perc | chaotic dominates | cool blue, rough | a cloud of shards, crystals, no organic forms |

The bell and the pad are the weakest pair: the same family leads in both.

The look is a placeholder. The ground reads grey under the softbox rig, the palette is arbitrary, and the camera
only orbits. All of that is the art agent's.

## §35 and §36, signal-level only (the visual judgement is the art agent's)

These are `--sonic-trace` means per section.

`context-pad` has one timbre and three musical contexts:

| section | polyphony | rhythm | regularity | duration | chord | brightness | warmth |
|---|---|---|---|---|---|---|---|
| sustained | 0.18 | 0.10 | 0.15 | 0.66 | 0.00 | 0.17 | 0.67 |
| arpeggio | 0.10 | 0.78 | 0.92 | 0.20 | 0.00 | 0.17 | 0.80 |
| dense chords | 0.60 | 0.20 | 0.93 | 0.50 | 0.99 | 0.10 | 0.90 |

The context moves while the brightness holds. Warmth rises with the register: the chords sit lower. The neutral
mapping barely uses the context (only swarm and breath), so the visual difference between the three sections will
be small until the art agent routes `notes.*` to something.

In `morph`, every dimension ramps rather than steps:

| stage | brightness | roughness | warmth | organic | crystalline | chaotic |
|---|---|---|---|---|---|---|
| clean | 0.10 | 0.02 | 0.87 | 0.85 | 0.14 | 0.00 |
| bright | 0.45 | 0.09 | 0.73 | 0.77 | 0.21 | 0.02 |
| resonant | 0.60 | 0.17 | 0.65 | 0.68 | 0.27 | 0.05 |
| distorted | 0.81 | 0.29 | 0.60 | 0.58 | 0.32 | 0.09 |
| noisy | 0.95 | 0.49 | 0.45 | 0.44 | 0.36 | 0.20 |

The families crossfade, but organic still leads at the noisy end. The family weights are the art agent's to
sharpen.

## Performance

- The timbre pass runs at load, about 100-135 us per analysis frame: 0.2-0.27 s for 20 s of audio, about 1% of one
  core per second of audio if it were run live.
- The per-render-frame cost (character walk, context and publish) is about 1.3 us.
- A 640x360 render of the garden takes about 22 s for 21.5 s of video (about 34 ms a frame, including encoding).
- Nothing is added to a project without a `sonic` block.

## Tests

`tests/unit/test_sonic.cpp`, tagged `[sonic][adr1020]`, 15 cases. They cover:

- term normalisation;
- absolute (level-independent) character;
- JSON overrides;
- timbre telling a tone, a chord, an inharmonic tone and noise apart;
- timbre purity per frame;
- the tiers and the gate hold;
- transient events;
- MIDI parsing (tempo map, running status, refusals);
- context (sustained against arpeggio, direction, purity) and note events;
- the interpreter's combine, shape and compete, and the source through the rack (lazy inputs, parameters, replay
  sample, round trip);
- the engine (signals, save round trip, two-engine determinism, seek equals play, absent block means zeros).

Full CPU suite (`avgen_tests`, under the GPU lock), at `c6d2475e`:

- Binary exit code 0: 3,902 cases, 3,882 passed, 19 skipped, and 1 failed as expected (the `[!shouldfail]` slope
  lean).
- `avgen_render_tests` was not run: no renderer or shader code changed.
- The Analysis panel's Sonic section compiles and is wired, but I did not see it on screen (ImGui is not
  captured headless).

## Open items / known limits

- A full mix is one sound: the timbre describes the mix. Per-instrument tracking is future work (§28).
- MIDI-informed harmonicity (the sounding notes as the known fundamentals) is the obvious next refinement if audio-
  only harmonicity proves noisy on real material.
- The interpret source's parameters are registered at attach. Changing its settings JSON at runtime needs a
  project reload. Nothing edits it at runtime today.
- The live input path (`--input`) and live MIDI are not wired to the Sonic runtime (Phase 7).
- The timbre pass is serial at load: about 2.5 s for a 4-minute track. If that matters, it parallelises trivially
  by frame.
