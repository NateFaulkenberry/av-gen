# Sonic Garden POC: progress

Resume from here. Branch `proto/sonic-garden` in `../av-gen-sonic`. ADR block 1020-1039 (used: 1020).

Staffing: the engineering agent did phases 0-4 and the engineering half of Phase 5. The art agent (sonic-art) owns
the mappings, the families, the look and the §34-36 judgements.

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
| 5 art (mappings, families, look, §34-36) | **next: art agent** | see "For the art agent" |
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

- `sonic-garden.scene.json`: a composition of existing generators only.
  - `ground`: a box.
  - `hero`: a 128x96 sphere with deformer slots 1 noise, 2 sine, 3 twist and 4 displacement.
  - `tendrils`: bent, swaying cylinders. `blooms`: flattened spheres. Both organic.
  - `crystals`: hexagonal bipyramids, a sphere with 6 segments and 2 rings, stretched by `sourceTransform`.
  - `shards`: thin boxes on a spiral.
  - `spores`: particles.
  - The studio-softbox rig and a dark environment with fog.
- `sonic-garden.json`: the **master project**.
  - The pad's audio, the `sonic` block, and the interpret source `garden` with 14 mappings.
    - `organic`, `crystalline` and `chaotic` compete in the group `family`, with sharpness 2.
    - The others: glow, light, warm, cool, turbulence, spikes, breath, swarm, motion, pulse, hit.
  - 24 neutral routes. `--audit-routes` finds all of them live.
  - Family presence scales each structure group's `source/scale`, so the instances shrink in place; the
    distribution's `transform/scale` would pull the whole ring in toward the hero.
- `variants/*.json`: the master with only the audio and notes swapped. **Don't edit these**: edit the master, then
  run `python3 tools/sonic_garden_variants.py`. Its `--width/--height/--out` flags write low-res copies elsewhere.

## For the art agent: how to tune without engine edits

1. **Interpreter mappings.** Edit the master's `sources[0].settings.mappings`.
   - Inputs can be any bus signal, with a weight and an invert flag.
   - `combine` is mean, sum, product, max or min, followed by bias, gain and curve.
   - Mappings can share a `group`, whose `sharpness` sets how they compete.
   - The weights are also live parameters under `sources/garden/<mapping>/in<k>`, together with bias, gain and
     curve, and `sources/garden/group/family/sharpness`.
2. **Routes.** Map `visual.*`, or anything else, to scene parameters with ordinary chains: smoothing, envelopes and
   depth.
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

   This takes about 22 s per sound. The review folder's grid was made from these with ffmpeg; this ffmpeg has no
   `drawtext`, so the labels are PIL PNGs overlaid.

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
