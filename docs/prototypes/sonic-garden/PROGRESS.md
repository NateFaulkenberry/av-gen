# Sonic Garden POC: progress

Resume from here. Branch `proto/sonic-garden` in `../av-gen-sonic`. ADR block 1020-1039 (used: 1020-1026).

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
| 5 art (mappings, families, look, §34-36) | pass 1 and **pass 2 done** (art agent, 2026-09-30; pass 2 is the second brief's PART 1) | see "Art pass 1" and "Art pass 2" below; `tools/sonic_garden_look.py`; review media in `23-sonic-garden/pass2/` |
| 5 engineering follow-up | done (2026-09-30) | twist normals (ADR-1021), sky rebuild tolerance (ADR-1022), live-rate profile; see "Engineering follow-up" |
| second brief PARTS 2-7 (live AA) | done (2026-09-30) | `AA-RESEARCH.md`, ADR-1024; see "Live anti-aliasing" below |
| second brief PART 8 (merge prep) | see "Live anti-aliasing" below | |
| second brief PARTS 9-14 (integration, live MIDI, live audio, sync, UI, demo plumbing) | done (2026-09-30), ADR-1025 | see "Live input" below; the owner's hands-on test is `LIVE-QUICKSTART.md` |
| second brief PARTS 15-16 (live demo art, live art direction) | done (art agent, 2026-09-30) | see "Live art (PARTS 15-16)" below; `tools/sonic_live_project.py` writes the live tuning; review media in `23-sonic-garden/live-art/` |
| second brief PART 22 (the hardware test) | the owner's | `LIVE-QUICKSTART.md` |
| projection (owner's request, 2026-10-01) | done (engineering agent), ADR-1026 | see "Projection" below; Start projection at the top of the Live panel |

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
| families (group `family`, sharpness 3) | `organic` = warm x smooth x (1-bright)^1.5 x (1-inharmonic) x steady^.5; `crystalline` = bright^.5 x inharmonic^.5 x (1-rough)^2; `chaotic` = mean(2 rough, sharp, 1-smooth), bias -0.2 gain 1.6; `silence` = full at zero energy, gone by 0.2 | slow tier |
| weight | `mass` = energy x density^3 x harmonicity^3 (loud AND full AND pitched), bias -0.3 gain 5 curve 1.5 | slow tier |
| world (second source) | `tectonic` = chaotic x mass; `impact` = chaotic x (1-mass) | this frame's garden outputs |
| qualities | `radiance`, `grain`, `edge`, `shimmer`, `breath`, `energy`, `tension`, `swarm` | medium tier |
| musical context | `sustain` (legato, duration, not rhythm), `figure` (rhythm x regularity x not chord), `stack` (chord, polyphony), `lift` (pitch) | `notes.*` only |

- Character tuning (the one retune): `roughness` also reads `bandwidth` (1.5-6 kHz, log). Distortion spreads the
  spectrum; before it, the bass read "dense and bright" (0.41) rather than rough.
- Phrase means: pad organic 0.89; bell crystalline 0.82 (organic 0.05, chaotic 0.13); bass tectonic 0.96; perc
  impact 0.98. The morph: organic 1.00 -> 0.90 -> 0.51 -> 0.20 -> 0.03 -> 0.00, crystalline peaking at 0.76
  (15-20 s), chaotic 0.53 (20-25 s) -> 0.95 (25-30 s). Context-pad: organic holds at 0.82-0.90 while sustain,
  figure and stack each lead their own section (0.80, 0.60, 0.77).
- `organic` requires a steady sound (`stability^0.5`): a decaying FM bell chord reads warm and soft too, and only
  its moving spectrum (stability 0.53 against the pad's 0.80) tells it apart. Without the term the bell's first five
  seconds drifted half organic (0.39); with it 0.18.
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

Second pass, `job_1a0f2444b7c997dcd`, on the sequence just before the last family retune (organic's steadiness term,
which moves only the pad/bell balance), compared with the first (`critic compare`): still 0
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

- **(Fixed by ADR-1021, see "Engineering follow-up".) Twist deformers invert normals past 90 degrees of turn.** `shaders/procedural.wgsl`, `vs_proc`: the finite-
  difference normal is flipped whenever `dot(nw, nRef) < 0`, where `nRef` is the *undeformed* normal. Any twist whose
  angle passes 90 degrees (always, once `speed * t` has run for a while) turns the band of faces around the axis inside
  out: a sphere with `twist speed 0.3` renders a black equator from about 5 s on (the caps, whose normals lie along the
  axis, stay right). The flip is only needed for a mirrored transform, so a fix is to take the sign from the
  determinant of the instance scale and object matrix instead of from `nRef`. Worked around here: no twist speed,
  and spins are `time.seconds` rotation routes (which rebuild the instance cloud per frame, cheap for these nodes,
  and are held inside the +-360 degree parameter range for 31.5 s).
- Interpreter parameters clamp silently: `bias` is +-4 and `gain` +-16, so a mapping authored with bias -9 runs at -4.
- **(Fixed by ADR-1022.) The sky's lighting cube is rebuilt every frame.** The world palettes drive `env/sky/zenithColor`,
  `horizonColor` and `haze` through slow chains, which never quite settle, and the renderer rebuilds the procedural
  sky (a 1024 cube with prefiltered mips) whenever a sky value moves. Measured by A/B on 10 s of the pad at 1080p:
  38.5 s with the 25 sky routes, 32.4 s without, so about 20 ms a frame. The log's "procedural sky built in ~110 ms"
  includes GPU backpressure and overstates it. For live use, rebuild on a meaningful change (an epsilon on the sky
  parameters) or amortise the prefilter; the art-side fallback is a fixed sky (the fog, light and grade still carry
  the palettes).
- Frame cost of the garden offline at 1920x1080, encoding included: about 108 ms a frame without the sky rebuilds
  and 128 ms with them (7.6-7.8 fps). No live-rate profile was taken; the engineering agent should profile it before
  Phase 7.
- The trace's event columns are always 0 (rows are written after `clearEvents`); watch a `visual.*` that reads them.
- Emission at note rate reads as a steady glow: a flash every 0.3 s with a 150 ms decay is lit half the time. Keep
  event decays under about 100 ms where notes are dense.

### Next

- Engineering: the twist fix and the sky fix are done (ADR-1021, ADR-1022) and the live-rate profile taken;
  Phase 6/7 wait for the owner's art review (the next brief is `01-brief-live.md`). When live input arrives, the
  families and gestures here need no change (they read bus signals), but `silence` and the slow tier will define how
  a live world starts and how fast it changes; tune `mass`'s bias and the family sharpness on real material first.
- Art: the §35 bell sustained section is still partly organic (0.34: a slow, single bell note genuinely reads soft
  and warm), which confounds "same timbre, different context" there; the pad version is the clean §35
  demonstration.

## Art pass 2 (art agent, 2026-09-30, `01-brief-live.md` PART 1) -- done

The owner's verdict on pass 1: promising, "visually primitive". Pass 2 keeps the mapping language (families,
qualities, context, note gestures) and rebuilds what it is expressed through. Everything is still written by
`tools/sonic_garden_look.py` (run it, then `tools/sonic_garden_variants.py`); the rig is
`examples/lightrigs/sonic-garden.rig.json`.

- **Surfaces: six material programs** in the scene (`sgGround`, `sgCore`, `sgFlesh`, `sgGlass`, `sgObsidian`,
  `sgBasalt`). Each shapes the material's OWN emission through the `materialEmission` input (ADR-904): the program
  decides where the light lives (rims, veins, fissures, tendril tips, fracture lines), the routes still decide its
  colour and strength. Base colour and roughness stay the material's own wherever a route drives them.
- **ADR-1023 (engine, small):** the route auditor called every route into such a program's material emissive dead
  (`program-owns-emission`, ADR-179 predates ADR-904). `MaterialProgram::emissionReadsMaterial()` follows the ops'
  data flow; the rule now fires only when the emission ignores the material's own. Test `[adr1023]` in
  `tests/unit/test_route_liveness.cpp`.
- **Forms:** a lotus of cupped petals (two bends on a flat ellipsoid: the deformers act after the source transform)
  round a small seed of light, in place of the orb; an armillary of platinum rings round the dark-glass gem;
  basalt monoliths with glowing seams; obsidian blades whose rims go white-hot on a strike; a shock ring that races
  out along the ground from each hit; per-world skylines (giant fungi, glass spires, a basalt ridge).
- **Light:** the key and rim are fixed in the world (not to the camera) and placed per world by routes (the rig's
  authored elevation 20 plus the route). The sky's sun is **placed**, not taken from the key: the key's direction
  is a blend of the families' and moves a little every frame with the sound, and a moving sun rebuilt the sky's
  cube every frame (6-10 ms of CPU, measured). The warm world's deep orange dusk sun sits low behind the giant
  fungi on the right. A `note` light rides the pitch; a hard `top` spot exists only in the void.
- **Fog economy:** `scene/volumeMaxDistance` is routed from organic and tectonic only (exactly 0 below a weight of
  0.2), so the observatory and the void keep closed-form distance fog (ADR-705) and skip the 21 ms march.
- **Camera:** a 50 mm lens (was 35) and one slow push-in (about 7 m in 21 s) holding the hero left of centre.
- **MIDI against timbre:** `glow` (brightness alone, medium tier) is the continuous "filter" channel; `high`/`low`
  (register) split each family's note gesture so low notes answer low in the world and high notes high.
- **Interpreter shaping is `x * gain + bias`** (then the curve), not `(x + bias) * gain`: a mapping meant to span
  pitch 0.38..0.54 is gain 6.25, bias -2.375.
- **A route chain's envelope follows its output after gain/offset:** a "hide" route (gain -1.25, offset 1) falls
  as the family rises, so its decay times the hide and its attack the reveal.
- **Traps found by eye** (each cost an iteration): `env/sky/sunSize` and `sunGlow` are radians (the scene's authored
  0.3 was harmless at sun intensity 0 and a 50-degree disc once lit); a distribution transform's scale scales the
  ring radius too (the skylines were 870 m out); contour "cracks" on fBm need a very thin band (fBm bunches near
  0.5) and Voronoi F1 bands read as spots; a backlit key forward-scatters through the march (the warm haze is
  0.0004); a scene light's intensity is capped at 20x its authored value.

### Pass 2 review media and results

`~/Desktop/av-gen-review/23-sonic-garden/pass2/` (ART-NOTES.md there has the reasoning per world, the timescale
table and the honest caveats): before/after with audio, the §34 grid and sequence, a still per world, §35 (pad and
bell), §36. Made by `tools/sonic_garden_review.py render`, `assemble` and the new `beforeafter` step. §34, §35
(pad) and §36 pass by eye, as in pass 1; the four worlds now also differ in light direction, sky, skyline, material
and camera height.

### Pass 2 cost (headless, 1080p, M2 Max, the same binary for both passes, interleaved; GPU p50 / wall p50, ms)

| tier | pad | bell | bass | perc |
|---|---|---|---|---|
| default, pass 1 | 37.2 / 39.7 | 35.3 / 37.6 | 36.1 / 39.0 | 35.5 / 38.4 |
| default, pass 2 | 43.4 / 46.3 | 21.1 / 23.8 | 44.1 / 47.3 | 20.3 / 23.3 |
| preview, pass 1 | 18.4 / 20.8 | 16.6 / 18.9 | 17.5 / 20.0 | 17.2 / 19.8 |
| preview, pass 2 | 23.7 / 26.6 | 18.7 / 21.3 | 24.8 / 27.8 | 18.2 / 21.1 |

- The lit scene pass is 4.7-6.8 ms heavier: the materials and the new geometry.
- The march (21-22 ms default, 4-5 ms preview) now runs only in the warm and heavy worlds.
- CPU is unchanged from pass 1 once the sky's sun was placed rather than taken from the key: following the key cost
  6-10 ms a frame in rebuilds.
- Unpulled levers: `scene/volumeSteps` in the marching worlds; a distance gate on `sgGround`.
- Records: `bench-c/` in the art agent's session scratchpad, not kept.

### Next (after pass 2)

- **PART 2 (live AA, engineering).** This scene is the test case: thin armillary rings (32 mm tubes, 4-5 px),
  the shock ring (about 1 cm at the hit), tapering tendrils, emissive rims that go black to white in one frame on
  a strike, contour lines in the materials, and deformation driven by sound. `temporal/echo` (organic 0.5,
  crystalline 0.25) is a history accumulation of its own: hold it off, or identical, when comparing AA modes. The
  pass 2 notes have the list.
- **PARTs 15-16 (the live demo scene and its art direction, art agent).**
  - Nothing here reads the file analysis directly, so live input needs no art change.
  - Tune on real material first: the `high`/`low` register split (centred on this phrase's pitch-centre span,
    0.38-0.54), `mass`'s bias, and the family sharpness.
  - `glow` is the filter-sweep channel.
  - The warm world's temporal echo ghosts the rippling tendrils: lower it for live play.
- **Art debts recorded in the pass 2 notes:**
  - the petals are ellipsoids (no point);
  - the gem's top face goes pale (a cut-gem SDF would fix it);
  - the note light is subtle;
  - the glass floor mirrors the rim light near the camera at times.

### Tests (art pass 2)

At `d1f75757` plus the final look (the binary includes ADR-1023), under `tools/gpu-lock.sh`:

- `avgen_tests "[sonic]"`: exit 0, 16 cases (pass 1's 15 plus `[adr1023]`), 334 assertions.
- `avgen_tests "[liveness]"`: exit 0, 34 cases.
- `avgen_tests "[material]"`: exit 0, 46 cases.
- `avgen_render_tests "[material],[emission]"`: exit 0, 24 cases (23 passed, 1 skipped). ADR-1023 refactors the
  helper that feeds the shader's emission flags.
- `avgen --project examples/sonic-garden/sonic-garden.json --audit-routes`: all 371 routes and both timeline tracks
  live. The one load-time hazard is the shock ring's scale route (a one-frame transient reaches 44% of its amount
  at 60 fps). The ring still starts visibly small at a hit and races out.

## Engineering follow-up (engineering agent, 2026-09-30)

The art is final; nothing here changes how the Sonic Garden looks (measured below).

### 1. Twist normals (ADR-1021) -- fixed

- Cause: `vs_proc` flipped the finite-difference normal when it faced away from the *undeformed* normal, so any
  twist past 90 degrees rendered its band inside out.
- Fix: the flip comes from the chain's handedness only (object-matrix determinant x instance scale sign x a path
  deformer with negative `pathScale`). It is computed *before* the three `deformChain` calls: the same arithmetic
  placed after them cost 2 ms GPU a frame at 1080p (shadow 0.85 -> 2.4 ms) with identical images. Where it is now,
  the Sonic Garden's GPU median is 37.09 ms against head's 36.96 (same binary, shader directory swapped).
- Test: `[adr1021]` in `tests/rendering/test_procedural_gpu.cpp` renders a sphere twisted by a half-turn phase,
  by +-344 degrees, and with speed 0.3 at 10 s, against the plain sphere. Before: mean abs diff 74 / 37 / 72, 17% /
  12% / 16% of the lit sphere darkened, 6 failed assertions. After: passes.
- Frame diffs (960x540, head binary; before = head shaders, after = fixed; noise floor = a second head render,
  bit-identical everywhere):

  | project | 1 s | 12 s | 20 s | reading |
  |---|---|---|---|---|
  | Sonic Garden pad, bell, perc, morph, context-pad | 0 | 0 | 0 | bit-identical (also at 6 s) |
  | Sonic Garden bass | 0.00% px | 0.10% px | 0.22% px | the fractured hero's folded facets (below) |
  | Temple | 0.01% px | 0 | 0 | |
  | Worlds | 0 | 0.6% px | 29% px, mean 5.9 | its pillars/spiral twist speed passes 90 deg |
  | Helix | 0.04% px | 17% px, mean 2.4 | 17% px, mean 2.2 | world twist, speed 0.15 |
  | Hyperspace | 1.7% px | 2.7% px | 19% px, mean 1.5 | world twist along the tunnel |
  | Cathedral | 2.9% px | 5.3% px | 14% px, mean 0.5 | rosette/ornaments speed 0.35/0.5 |
  | Chamber | 12% px, mean 0.2 | 4.9% | 7.6% | world ring twist along a 100 m+ tunnel |
  | Lab | 0.45% px | 0.47% | 0.44% | the D_twist column |

  Every change measured is on twisted geometry whose angle had passed 90 degrees (Helix and Worlds inspected by
  eye: the twisted blades and pillars now shade consistently with the key, where some faces were lit as if
  facing away). Triptychs of before/after/diff were made in the session scratchpad, not kept. The one change that is not a twist past 90 degrees is folds: where the bass hero's noise and
  displacement fold facets over, the old test forced their normals back and the fragment stage's `frontFacing`
  correction then darkened them. 0.1-0.2% of the bass's pixels, small dark flecks on the core become lit.
- The art's workaround (spins as `time.seconds` rotation routes, no twist speed) is no longer needed but is left as
  it is: the art is final.

### 2. The sky rebuilding every frame (ADR-1022) -- fixed

`scene::skyWithinRebuildTolerance`: the renderer keeps the built cube while every sky colour is within 1/128 of
its brightest channel + 1e-5, every scalar within 1/128 + 1e-5, and the sun within 0.25 mrad of the sky it was
*built* from (so drift adds up and still rebuilds). Against a rebuild on every frame, same session, head shaders:

| clip | builds before -> after | render time | frames differing | worst pixel |
|---|---|---|---|---|
| pad 0-10 s, 960x540 | 301 -> 75 | 17.8 -> 13.6 s | 254 of 300 | 1 level (mean 0.02) |
| morph 8-24 s, 640x360 | 481 -> 169 | 20.3 -> 16.6 s | 479 of 480 | 1 level (mean 0.01) |
| perc 0-12 s, 960x540 | 361 -> 111 | | 304 of 360 | 1 level |
| night-shift 0-20 s (sequence-keyed sky) | 2 -> 2 | 23.0 -> 23.1 s | 0 | 0 |

GV3 was suggested as the sky-routed project, but it sets `env/sky/*` only as static parameters; night-shift keys
its sky's colours on a sequence and is unchanged. Seeks build the exact sky; playback may carry one up to the
tolerance old (at most one 8-bit level on this material).

### 3. Live-rate profile (§29 criterion 7)

Headless fixed-step playback (`--headless --range 6: --frames 360 --bench-json`), Apple M2 Max, Metal, 12 warm-up
frames, 348 measured. "1080 preview" is `--tier preview` at 1920x1080 (the editor's preview scale); "360" is
640x360 at the default tier. The editor window itself was not driven (no display session); headless is the same
renderer without ImGui. GPU = timestamp median; CPU = the render thread's own work excluding the wait on the GPU.

| variant | size / tier | wall p50 / p90 ms | GPU p50 ms | CPU (excl. GPU wait) ms | top GPU passes (ms) | sky builds /360 |
|---|---|---|---|---|---|---|
| pad | 1080 default | 40.1 / 45.6 | 37.4 | 1.3 | volume.march 21.0, scene 12.4 | 95 |
| bell | 1080 default | 37.9 / 44.2 | 35.4 | 1.1 | volume.march 21.0, scene 10.3 | 102 |
| bass | 1080 default | 39.1 / 44.4 | 36.2 | 1.5 | volume.march 21.0, scene 11.0 | 94 |
| perc | 1080 default | 38.4 / 44.4 | 35.6 | 1.5 | volume.march 21.0, scene 10.7 | 120 |
| pad | 1080 preview | 20.9 / 26.5 | 18.4 | 1.1 | scene 11.3, volume.march 4.5 | 95 |
| bell | 1080 preview | 19.0 / 25.2 | 16.6 | 1.0 | scene 9.5, volume.march 4.5 | 102 |
| bass | 1080 preview | 20.3 / 25.5 | 17.5 | 1.4 | scene 10.2, volume.march 4.5 | 94 |
| perc | 1080 preview | 21.9 / 29.3 | 17.0 | 2.5 | scene 9.9, volume.march 3.9 | 120 |
| pad | 640x360 | 15.6 / 21.7 | 13.2 | 1.0 | scene 6.0, volume.march 5.7 | 95 |
| bell | 640x360 | 13.8 / 25.0 | 11.6 | 0.9 | volume.march 5.7, scene 4.3 | 102 |
| bass | 640x360 | 14.4 / 30.3 | 12.3 | 0.8 | volume.march 5.7, scene 5.0 | 94 |
| perc | 640x360 | 14.8 / 21.3 | 12.5 | 1.0 | volume.march 5.7, scene 5.0 | 120 |

Before the sky fix (head binary, 1080 default): wall p50 43.2-45.3 ms, CPU 6.7-6.9 ms, 360 of 360 frames rebuilt
the sky; the environment update was 6.1-6.5 ms a frame, now 1.6-2.1 ms.

The sonic subsystem's own cost, per frame, from the same runs: the whole signal update (which includes the Sonic
Character walk, the note context and the publish) 3-9 us; all modulation (the two interpret sources and 251 routes)
16-37 us; the scene evaluation (controller) 0.3-0.6 ms. The load-time timbre pass is unchanged (about 0.2 s per
20 s of audio). The Sonic Garden's frame is GPU-bound and the sonic system is not a measurable part of it.

Architectural findings, recorded, not optimised:
- **The volumetric march is 56% of the 1080 frame**: 21.0 ms, constant across the four worlds, although three of
  them run almost no fog (crystalline 0.0002 density). It is the default tier's 48 steps at a 960x540 target; the
  preview tier cuts it to 4.5 ms. For live use at 1080 the preview tier or a density-gated march is the lever.
- **The remaining sky rebuilds are p90 spikes**: each build at the default tier is about 6 ms of blocking CPU
  (47 ms at the offline tier's 1024 faces), and 26-33% of frames still build while the worlds are moving. The p90
  wall is 5-6 ms above the median for that reason. Amortising the prefilter across frames would remove the spike;
  not done (not needed for correctness, and the p50 is GPU-bound).
- At preview tier 1080 the garden runs at about 48-53 fps, at 640x360 about 65-72 fps: live use is viable.

## Live anti-aliasing (engineering agent, 2026-09-30, `01-brief-live.md` PARTS 2-8)

The full record is `AA-RESEARCH.md`, and the decision is ADR-1024. Review media is in `23-sonic-garden/aa/`.

- **Cause.**
  - The adaptive render scale (on by default, 16.67 ms budget) takes the garden, GV3 and the Space presets to its
    0.5 floor on the owner's 1640x1326 canvas: a quarter of the pixels, bilinear-stretched in the tone map.
  - Nothing antialiases the live frame: FXAA runs only if authored, and the garden authors none.
  - The offline path is 1:1, and for GV3 and the review stills it is supersampled 2x.
  - The tier is 0.6% of the edge error; the floor doubles it.
- **Implemented.**
  - `QualitySettings::antialiasFloor`: FXAA runs at `max(authored, floor)`.
    - It is 0 at every tier and must be 0 offline.
    - Settings > Rendering > "Anti-aliasing" (FXAA by default, or Off) sets 0.75 in the live editor.
    - Also `--live-aa`, and the quality arm `liveaa`.
  - Settings > Rendering > "Lowest scale", also `--adaptive-floor`, default 0.50x (unchanged).
  - The status bar shows `(scene WxH)` when the scene is below the canvas.
  - Settings keys: `general.liveAntialias` and `general.adaptiveCanvasFloor`.
- **Measured.**
  - FXAA is below timer resolution (at most 0.066 ms). It takes 12-16% off edge error and crawl at full
    resolution, and 2-3% at the floor.
  - Garden GPU p50 at 1640x1326: 0.5 = 22.9 ms, 0.71 = 28.6, 0.85 = 32.8, 1.0 = 37.4.
- **Offline.** 180 frames across four projects are byte-identical to head.
- **Tried and reverted.**
  - A Karis-weighted (`c/(1+luma)`) FXAA resample: no measurable difference on the strike frames.
  - The FXAA shader is unchanged.
- **Open, for whoever picks this up.**
  - Phone-wire widening of `torus`/`tube` procedurals: the thin-geometry fix at any resolution.
  - SMAA, only if FXAA is judged soft.
  - `"supersample": 2` in the garden's own `render` block: the art owner's call, since it doubles render time.
  - The live default floor stays 0.5 by decision (ADR-1024). Raising it is the owner's frame-rate choice.
- **PART 8, merge preparation.**
  - The diff against main contains only the Sonic Garden art and engineering and this change. There is no temporary
    code or debug-only behaviour, and the feature is disabled with Settings "Anti-aliasing: Off" or `--live-aa off`.
  - The Sonic Garden master, its 7 variants and the 8 Space projects load with no errors or GPU errors.
    `--audit-routes` finds 371/371 routes live.
  - ADR-1023, which art pass 2 left out of the index, and ADR-1024 are now indexed. `test_repo_hygiene` had caught
    it.
  - Both suites were run under `tools/gpu-lock.sh`, one after the other, at `6de4561f`:
    - `avgen_render_tests`: exit 0; 559 cases, 558 passed, 1 skipped. This ran before the index commit, which is
      docs-only.
    - `avgen_tests`: exit 0; 3,920 cases, 3,900 passed, 19 skipped, 1 failed as expected (the `[!shouldfail]`
      slope lean).
  - The root-level `indtune.cpp` and `temporal-*.png` are tracked on main already, not by this branch. I left them
    alone.

## Live input (engineering agent, 2026-09-30, `01-brief-live.md` PARTS 9-14) -- ADR-1025

The owner's hands-on test is `LIVE-QUICKSTART.md`. Review media are in `~/Desktop/av-gen-review/23-sonic-garden/live/`.

### PART 19, before and after

```
BEFORE (files only)
  .wav -> AnalysisTrack (whole file, at load) -> analyzeTimbre (load-time pass) -> SonicRuntime.advance (hop clock)
  .mid -> NoteTrack -> contextAt(t)                                                        \
                                                         sonic.* timbre.* notes.* -> interpret -> visual.* -> routes
  (live app: CoreMIDI -> ControlHub -> control.* only; --input -> AnalysisRunner -> audio.* only)

AFTER (live added; the file path unchanged)
  LIVE MIDI  CoreMIDI -> MidiInbox -> ControlHub.update (engine thread) -> LiveNotes (NoteTrack, host clock)
                -> contextAt / eventsBetween (the SAME functions) -> Musical Context -> notes.*
  LIVE AUDIO device -> AudioInput callback (unchanged: downmix, ring) -> AnalysisRunner thread: Analyzer
                -> FrameTap: LiveTimbre -> TimbreAnalyzer::analyze (the SAME function) -> SPSC snapshot queue
                -> render thread: LiveSonic.frame -> SonicRuntime::step per analysis frame (the SAME step) -> sonic.*
                                             Sonic Model = LiveSonic (character + context)
                -> interpret source (Visual Interpreter) -> visual.* -> existing routes/modulators -> parameters
                -> generators -> scene -> renderer -> live AA (ADR-1024) -> display
```

Musical information (`notes.*`), sonic information (`sonic.*`, `timbre.*`), visual interpretation (`visual.*`) and
rendering stay separate layers, as PART 19 asks.

### Threading

| thread | does | never does |
|---|---|---|
| audio callback | downmix, gain, write the existing SPSC ring (128-frame capture period now) | allocate, lock, analyse |
| analysis (AnalysisRunner) | STFT, beat tracker, then the tap: timbre (0.1-0.25 ms/frame), push a ~100-byte snapshot | block on the render thread (a full queue drops the newest, counted) |
| CoreMIDI | parse into the inbox (existing) | touch the engine |
| engine/render | drain MIDI into LiveNotes; drain snapshots into `SonicRuntime::step` (~1 us each); publish | DSP |

### Entry point (PART 9)

- `Engine::setLiveSonic` (live engine only).
- A project with `"sonic": {"live": true}` turns it on when the editor opens it, and every other project turns it
  off.
- The **Sonic Live** example (`examples/sonic-garden/sonic-live.json`, from `tools/sonic_live_project.py`).
- The **Live** panel (View > Live; it opens itself when live input turns on).
- `--live`, `--midi <filter>`, `--input <device>`.
- Per-machine settings: `live.audioInput`, `live.midiInput`, `live.smoothing`.
- Turning live on with no audio file starts the transport, so `time.seconds` animation runs. With no duration it is
  unbounded.

### Latency (PART 12), measured

- **Rig:** `avgen_sonic_probe latency` sends a note over a CoreMIDI virtual source and starts its synth voice at
  the same host instant; the synth plays into BlackHole; the app captures BlackHole.
- **Clock:** every time is `mach_absolute_time` ns on both sides, joined by `tools/sonic_live_latency.py`.
- **Frames:** about 57 fps in the editor at 1640x1326 (adaptive scale at the 0.5 floor).
- **Sampling:** 48 notes per arm. The note grid steps 3.1 ms per note, so that hop and frame phases are sampled,
  not fixed; the first, unstepped runs measured one phase and read 10 ms too optimistic.

| path | p50 | p90 | max |
|---|---|---|---|
| MIDI -> bus (frame start) | 13.2 ms | 22.8 | 25.0 |
| MIDI -> present | 18.4 | 29.3 | 47.7 |
| audio -> bus | 23.8 | 33.2 | 47.9 |
| audio -> present | 28.6 | 38.2 | 52.5 |
| audio -> bus, miniaudio's default 480-frame capture period (A/B) | 27.4 | 35.8 | 48.3 |

- What follows present: the GPU frame (about 17-19 ms, pipelined) and scanout (up to one refresh).
- Most of the audio path is structural: the analyzer's centred 2048-sample window (about 21 ms of group delay),
  plus the hop, plus waiting for the next frame.
- **No compensation.** The note gesture appears about one frame before its timbre, which is the order a player
  expects. Delaying MIDI to align them would only make the instrument feel late (ADR-1025 §8).

### PART 17 and PART 18 through the live path

One held A2 (MIDI constant: `notes.active` 1, `notes.pitch` 0.25, `notes.velocity` 0.756 throughout), synthesized
audio via BlackHole.

- **Filter sweep** (low-pass 120 Hz to 9 kHz over 8 s, and back over 5 s):
  - `sonic.brightness` 0.00 to 0.57 and back;
  - `warmth` 1.00 to 0.67;
  - `roughness` 0.00 to 0.32;
  - `visual.glow` (the art's filter channel) 0 to 0.70;
  - the families cross from organic 1.00 toward crystalline (0.42 at the peak), then back.
  - The medium tier lags the cutoff by about 1 s (its time constants).
- **Distortion** (a mellow tone, low-passed at 350 Hz, into a tanh drive of 1 to 40):
  - `roughness` 0.006 to 0.28;
  - `brightness` 0.02 to 0.48;
  - `warmth` 0.99 to 0.72;
  - `visual.grain` 0.004 to 0.21;
  - `visual.glow` 0 to 0.57.
  - The families stay organic-led (0.83 organic, 0.17 crystalline, `chaotic` 0), so the world changes less than
    the numbers. The art agent should tune for live play.
  - The probe's first version drove the saws before its filter: a clipped saw is a square, with the same 1/n
    spectrum, and nothing moved. That is a lesson for the owner's test too: distortion ahead of a closed filter
    barely changes timbre.
- Review media in `23-sonic-garden/live/`:
  - `02-filter-sweep-live.mp4`, `-curves.png` and `.csv`;
  - `03-distortion-...`;
  - `01-live-demo-probe-synth.mp4` (low soft notes, high bright notes, chords, an arpeggio, a sweep, distortion);
  - `04-live-panel-and-viewport.png`.
  - The clips are about 15 fps, because `--live-capture` re-renders each frame at 640x360.

### Resume here (PARTS 9-14 state, for a cold successor)

- Done and committed on `proto/sonic-garden`:
  - `6b855427`: the implementation;
  - `ee6f1092`: ADR-1025, the quickstart, the measurements and the tools;
  - `16638fed`: a Live panel fix.
- The tree builds: `cmake --preset release && cmake --build --preset release`.
- Suites:
  - `avgen_tests` at `ee6f1092`: exit 0; 3,931 cases, 3,911 passed, 19 skipped, 1 failed as expected (the
    `[!shouldfail]` slope lean). `16638fed` changes only `application.cpp`, which neither test binary compiles.
  - `avgen_render_tests` at `16638fed`, run after the CPU suite: exit 0; 559 cases, 558 passed, 1 skipped.
- The review media are in place: `~/Desktop/av-gen-review/23-sonic-garden/live/` (3 clips, 2 curve plots and CSVs,
  the panel screenshot).
- Next:
  - the coordinator merges;
  - PARTS 15-16 go to the art agent (below);
  - PART 22 is the owner's (`LIVE-QUICKSTART.md`).
- No real MIDI device was enumerated on this Mac during the work (`avgen --list-midi` listed none; the SE49 was
  not connected). BlackHole 2ch and an Apogee HAL driver are installed.

### For the art agent (PARTS 15-16)

- The demo is the garden master with no audio or MIDI file, `sonic.live: true`, and the camera held at the
  master's 10.75 s framing. Re-run `python3 tools/sonic_live_project.py` after changing the master. Put live-only
  overrides in that tool.
- Tune on the probe or real material:
  - the `high`/`low` register split (centred on the phrase's 0.38-0.54);
  - `mass`'s bias;
  - the family sharpness;
  - `chaotic` against real distortion (see above);
  - `temporal/echo` in the warm world;
  - how the world opens from `silence`.
- To test without hardware, start the app, then run the probe:
  - `build/release/src/avgen --example "Sonic Live" --input BlackHole --sonic-live-log x.csv`
  - `build/release/tools/avgen_sonic_probe <sweep|drive|demo|latency> --out p.csv [--wav p.wav]`
- Tools:
  - `tools/sonic_live_curves.py` (the table and the plot);
  - `tools/sonic_live_clip.py` (a review clip from `--live-capture <dir> --live-capture-every 1
    --live-capture-size 640x360`).
- The windowed app runs here under `tools/gpu-lock.sh`. `--capture-ui f.png --capture-ui-panel Live` photographs
  the panel.

### Tests

`tests/unit/test_sonic_live.cpp`, `[sonic][adr1025]`, 11 cases:
- live notes equal to file notes;
- events fire once;
- pedal, all-off and retrigger;
- the note track stays bounded;
- the SPSC queue across threads;
- **the threaded live timbre path reaches the file path's character to 1e-6**;
- a disabled tap does nothing;
- engine MIDI;
- device-absent;
- `sonic.live` projects;
- a real CoreMIDI source (`[device]`).

## Live art (art agent, 2026-09-30, `01-brief-live.md` PARTS 15-16) -- done

### Resume here (live art)

- Commits on `proto/sonic-garden`: `4dcb65c5` (first pass), `cbb5bd3d` (the filter in the garden, waiting world,
  matte floor, seed, hold), `7a35999e` (rasp, forms from 0.375, the glass's blacks, composition, the quickstart's
  "What to try"), `626c64d2` (the heavy core stays dark), `9cb362a9` (note gestures as accents), `7087e3fd`
  (`--no-lock`), and this section.
- To change the live look: edit `tools/sonic_live_project.py`, run it, reopen Sonic Live. To check a change without a
  GPU: `avgen_sonic_probe <scenario> --out e.csv --wav a.wav` (into BlackHole, no app needed), then
  `python3 tools/sonic_live_replay.py e.csv a.wav <dir> --trace`. To see it: `--render clip.mp4`.
- The GPU lock is shared with the liminal worktree's agents, whose full suites hold it for 35-40 min; the art
  agent's media runs queued behind them for most of an hour on 2026-09-30.

The live demo's art direction. **Everything is data written by `tools/sonic_live_project.py`**: the live project
(`examples/sonic-garden/sonic-live.json`) and, new, its own scene (`examples/sonic-garden/sonic-live.scene.json`,
the master's scene plus what live play needs). The garden master, its variants and their review material are
untouched. No engine code changed, so no ADR. The notes, the curves and the clips are in
`~/Desktop/av-gen-review/23-sonic-garden/live-art/` (LIVE-ART-NOTES.md there has the design, the per-gesture table,
the before/after curves and the limits); LIVE-QUICKSTART.md has a "What to try" section for the owner's test.

### The design: three timescales, and the note is only the first

| layer | speed | reads | moves |
|---|---|---|---|
| the note | immediate | `notes.noteOn`, velocity, pitch | a gesture whose form is the world's, place the register, strength an accent (1 - note density) |
| the sound now | 50-300 ms | medium tier | `glow` (the filter: light, colour, focus, air, detail), `grit` (distortion: buzz, cracks, matte, fragments), light held by `energy` (release = trail) |
| the playing | 0.3-1.5 s | `notes.*` | `figure` (pattern), `stack` (density), `sustain` (trails), `lift` (height) |
| the identity | about 2 s | slow tier | the four worlds, blended |

### The engineer's three problems

1. **Distortion (PART 18).** Before, nothing the garden showed read roughness (`grain` acted on the core and the blades,
   which the garden hides) and the family never left the garden (chaotic 0.00-0.01). Now `grit` (roughness x
   energy^0.4 x (1 - inharmonicity)^2) roughens, buzzes and fragments every world within 40-300 ms: 0 to 0.48 at drive
   40 on the held note (peaks 0.77), fragments 0 to 0.42; the world goes garden -> buzzing glass 0.66, the rough
   heavy world rising to 0.22 over a 6 s hold. A driven chord reaches the heavy world (0.90) in about 2 s; a drive
   after the filter on a riff 1.00. Honest limit: on one held note, distortion and an open filter are nearly the
   same spectrum to the character, so the two tests' tops look alike; the chaotic threshold (-0.44) is set so the
   top of a filter sweep stays bright glass rather than a dim in-between world (at -0.38 a held driven note went
   rough sooner, but every sweep's top went dark for two seconds).
2. **The filter lag (PART 17).** The fast channel is now quick (up/down hysteresis 17-170 ms, routes 60/250 ms: about a
   third of a second from knob to picture, was about a second) and reaches down to a bass note's closed filter
   (`glow` 0.13 at 350 Hz, 0.27 at 1 kHz; was 0.00, 0.03). The world's identity stays about 2 s on purpose: the light
   follows the knob, the world follows where it comes to rest. The live project carries its own time constants, so
   the Live panel's Smoothing stays 1x by default.
3. **The camera.** A slow 64 s drift loop round the master's mid-move framing (about 1.6 m of travel), raised 0.6 m
   to look into the lotus's cup; it does not follow the music. Weight lowers it only in the heavy world.

### Families on synth patches (probe `patches`, the same phrase through six patches)

pad 0.94 garden; pluck 0.96, lead 1.00, FM bell 1.00 glass; distorted bass 0.99 rough and heavy; noise perc 1.00
rough and light. (The master's mappings read the pad as 0.57 glass and the distorted bass as 0.89 glass.) Family
sharpness 4; a world's forms appear from a weight of 0.375 and are full by 0.8; the fog march runs 24 steps live.

### Tools

- `tools/avgen_sonic_probe` (extended): patches (pad, keys, pluck, lead, FM bell, bass, distorted bass, noise perc),
  the PART 15 scenarios `low`, `high`, `chords`, `arp`, `distorted` (plus `sweep`, `drive` with a 6 s hold,
  `patches`, `drivechord`, `play`), and a repeated key now retriggers.
- `tools/sonic_live_replay.py`: a probe recording as a file project; `--trace` (no GPU) and `--render clip.mp4`
  (full quality, with the probe's audio and a readout strip).
- `tools/sonic_live_clip.py`: the readout shows the patch, glow, grit and the four worlds.

### Frame cost and tests

- **Live, in the real app** (the probe's `patches` through every world, live MIDI and audio, no capture; this Mac's
  window maximised: a 3304x1978 canvas, the adaptive scale at its 0.5 floor, a 1652x988 scene): default tier 29.7 ms
  p50 (34 fps), GPU 29.0-31.5 ms per world; Preview 19.8 ms (50 fps), GPU 19.0-21.6 ms. The owner's canvas
  (1640x1326) is a third of these pixels. The Live panel at Preview: 45 fps, MIDI to frame 11.7 ms, audio analysis to
  frame 14.0 ms, 0 dropped.
- **The art's own cost** (headless 1080p A/B on one recording, the engineer's live project against this one): -0.4 to
  +1.9 ms GPU in the glass, heavy and strike worlds. The garden is dearer only because a soft pad now lands there and
  marches its air; the live fog march runs 24 steps (garden at the default tier 43.8 -> 33.2 ms at 1080p), which
  took every world in the live run about 8 ms cheaper (38.0 -> 29.7 ms p50).
- **Tests:** `tools/gpu-lock.sh build/release/tests/avgen_tests "[sonic]"` at the hand-back (22:23, after the last
  change): exit 0, "All tests passed (206899 assertions in 27 test cases)". No engine code changed (only `tools/`,
  `examples/sonic-garden/sonic-live*.json` and these docs); `--audit-routes` on the live project: 452/452 routes and
  both tracks live, 0 warnings.
- **Review media:** `~/Desktop/av-gen-review/23-sonic-garden/live-art/`: a full-quality clip (1280x720, 30 fps, the
  probe's own recording rendered through the file path) and a live-captured clip (the real app, 640x360, 8-18 fps)
  per PART 15 test, the play-through both ways, the filter and distortion curves (before and after), the Live panel
  at the Preview tier, one still per state, and LIVE-ART-NOTES.md.
- **Found for the engineer (not changed):** a MIDI input chosen by name does not connect a source that appears later
  (`onSourceAdded` connects only under the wildcard); once in eight runs All MIDI inputs connected the probe's new
  virtual source about 2.7 s late.

## Projection (engineering agent, 2026-10-01, the owner's request) -- ADR-1026

The owner: "a button at the top of the live panel ... 'start projection' which opens a new window with the live
workspace there ... a clean window to send to a projector".

### Resume here (projection)

- Committed on `proto/sonic-garden`: the implementation, `tests/unit/test_projection.cpp` (`[projection]`, 8 cases),
  ADR-1026 (indexed), the quickstart's "Projecting to a second screen", and this section.
- The manual check is done (one display on this Mac, so windowed on it). The media are in
  `~/Desktop/av-gen-review/23-sonic-garden/projection/`:
  - 01 is the editor window;
  - 02 is the projection window (Fit letterbox, no UI);
  - 03 and 04 are the Live panel before and during projection.
  - The window captures use `screencapture -l<window id>`, with the ids found by the app's pid. A full-screen
    capture photographs the owner's desktop: don't take one.
- Frame cost: within noise (see ADR-1026, "Measured").
- Suites at `de626773`, run under the lock one after the other:
  - `avgen_tests`: exit 0; 3,939 cases, 3,919 passed, 19 skipped, 1 failed as expected (the slope lean);
  - `avgen_render_tests`: exit 0; 559 cases, 558 passed, 1 skipped.
- Next: the coordinator merges. The owner tries it with the projector (LIVE-QUICKSTART, "Projecting to a second
  screen").
- Not tested on hardware: a real second display, fullscreen on it, unplugging. The unplug path is covered by the
  unit test only.

### What was built

- `src/app/projection.{hpp,cpp}`: GPU-free decisions (display, fullscreen, window size, Fit/Fill/Stretch mapping)
  and the state machine `Projection` (Idle -> AwaitingProject -> Running -> Idle).
- The window is an ordinary `OutputManager` output named "Live projection", with `Output::projection = true`:
  never written to the project's `outputs`, kept across project loads, Esc in it closes it.
- Settings: `settings.json` `projection` (display name, fullscreen, windowWidth/Height, scaling).
- `Application::serviceProjection` (each frame, just before the outputs present): opens the window once the
  demo has loaded, writes the scaling mapping, stops when the window is closed or its display is unplugged.
- `--start-projection` presses the button at start-up.
- "Open live demo" now goes through the unsaved-changes prompt (it called `beginOpen` directly before).

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

Engineering follow-up, at `41aa1cf7`, reconfigured, both suites under `tools/gpu-lock.sh`, one after the other:

- `avgen_tests`: exit code 0; 3,903 cases, 3,883 passed, 19 skipped, 1 failed as expected (the `[!shouldfail]`
  slope lean, `test_character_lab_slopes.cpp:187`).
- `avgen_render_tests`: exit code 0; 556 cases, 555 passed, 1 skipped.
- New cases: `[adr1021]` (GPU, twist normals), `[adr1022]` (unit tolerance, and a GPU case counting
  `environmentBuildCount()` through the renderer).

## Open items / known limits

- A full mix is one sound: the timbre describes the mix. Per-instrument tracking is future work (§28).
- MIDI-informed harmonicity (the sounding notes as the known fundamentals) is the obvious next refinement if audio-
  only harmonicity proves noisy on real material.
- The interpret source's parameters are registered at attach. Changing its settings JSON at runtime needs a
  project reload. Nothing edits it at runtime today.
- Live input and live MIDI are wired (ADR-1025). Pitch bend, MPE and MIDI 2.0 are not used yet (the note keeps a
  float pitch and velocity for them).
- The timbre pass is serial at load: about 2.5 s for a 4-minute track. If that matters, it parallelises trivially
  by frame.
