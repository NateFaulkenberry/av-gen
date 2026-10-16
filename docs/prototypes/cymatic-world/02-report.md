# Cymatic World: report

*Branch `proto/cymatic-world` (worktree `../av-gen-cymatic`, from main `4421568b`, origin/main merged). Brief
`00-brief.md`, design `01-design.md`, log `PROGRESS.md`, field documentation `docs/resonance-field.md`. Review media:
`~/Desktop/av-gen-review/58-cymatic-world/`. Registered as the live example **Cymatic World**
(`examples/cymatic-world/cymatic-world.json`, `"live": true`, a `sonic.live` project); generator
`tools/cymatic_world/build.py`.*

**What it is.** A night basin of basalt whose ground is a resonating plate. The music excites one field of standing
waves. Grit settles on the field's nodal lines and carves the ground into rings and cells. A crystal druse on the floor
rings in the band each crystal is tuned to. A floating medusa of grains holds itself on the field's nodal shells and
trails a veil of strands. It is lit by a raking warm moon and by the medusa's own cold lamp, under a ring of basalt
spires.

## 1. The architecture, and why

The resonance field is **one new field kind** (`FieldKind::Resonance`, ADR-1400), not a new subsystem beside the
others. AV Gen's fields are already packed once a frame into the `FieldBlock` that every consumer binds. The
consumers are:

- the procedural vertex stage (Field deformer);
- the effector pass (instance records);
- material programs (`field` op);
- particle forces;
- fibre streamlines;
- SDF marching.

So one resonator, defined once, is read by the ground's deformer and material, the druse's effectors and the
medusa's particle forces. No consumer needed a change of its own, and there is no redundant FFT. The field hears
`spatial::AudioHistory` (ADR-1116), which is already shaped by the Live panel's Response controls (ADR-1361).

The resonator is a **pure function of the audio history at the clock**: nothing is stepped from frame to frame. A
seek lands exactly where play would, a reset is free, and offline and live agree. The only thing kept between frames
is a cache of filtered spectrogram rows, which changes nothing but the cost. On the GPU the modes go into a 48-mode
table in the `FieldBlock`, with no new binding.

## 2. How the field works

These are the model's steps. The details and parameters are in `docs/resonance-field.md`.

- **Modes on a dispersion axis.** Mode i, at u = i/(N-1), hears f_i = fLow·(fHigh/fLow)^u and draws
  λ_i = λmax·(λmin/λmax)^u. A low note draws a broad, simple figure and a high note a fine one.
- **Shapes.** Each mode belongs to one source:
  - a `point` source draws an asymptotic Bessel standing wave in 3D, with a regular core. It makes rings on the
    ground and nodal shells around a raised centre.
  - a `lattice` source draws Faraday-wave cells, here hexagons. A source's lattice modes share one orientation, so
    they nest.
  - Every source has optional `reach` (horizontal falloff), `depth` (vertical layer) and `scale`.
- **Excitation.** Each mode hears the spectrogram through its own resonance curve (Q) after a spectral contrast. The
  result is convolved with a unit-area rise/decay kernel: a sustained tone builds up, and low modes ring longest.
  Onsets strike the modes in their band.
- **Two time scales.** A fast envelope drives vibration and amplitude. A slow one (`memory`) drives the sand and the
  matter's force, so structure settles over a second or two instead of jumping with every hit.
- **Stability.** Mode competition (per source) keeps one legible figure per body when many modes are driven, the way
  a Faraday layer selects one pattern. A soft limiter (per source) means dense music rings more modes rather than
  louder ones. An idle level keeps a resting figure in silence.
- **Readers.** Readers share a resonator by `reference` and choose a channel:
  - `displacement`;
  - `amplitude`;
  - `sand`, where Chladni sand gathers, at F = 0 of the coherent figure: the nodal *lines*;
  - `relief`, which is sand, a crest and vibration in one pass;
  - `shading`, a colour (sand, crest, amplitude) for a material in one pass;
  - `force` (-∇F² of the slow figure);
  - `gradient`.

  A reader can listen to a band, or to a band laid along an axis (a tuned structure).
- **Coupling** (ADR-1402). A reader's probe publishes `field.<name>.probe` to the bus. A source's `drive` takes
  excitation from a route.

Three findings that changed the model were all found by looking at frames:

- The energy summed incoherently has only nodal *points*, and the ground read as an egg crate. The coherent figure
  has nodal lines.
- Many driven modes read as a maze, which competition fixed.
- Crossed lattices read as a moiré, which aligned orientations fixed.

## 3. How each prototype reads the field

| Prototype | Reads | Mechanism | Its own identity |
|---|---|---|---|
| A, ground | `relief` (Field deformer, +Y, 2.6 m), `shading` (material program) | A 260k-triangle polar mesh over a WorldMap amphitheatre is deformed on the GPU. Sand ridges rise on nodal lines and the antinodes are scoured. The light is painted in the program on the deformed normal. | The figure is in the geometry: cells, rings and petals that settle with the slow envelope. |
| B, druse | `tuned` (displacement) and `tunedRing` (amplitude), listening along the cluster's height | 34 stepped quartz-habit crystals radiate from one root. Effectors quiver each crystal by the displacement it hears and light its growth bands by its ringing. | Tuned by length: long core crystals hear the low modes, the short fringe the high ones. |
| C, medusa | `force` (slow figure, the singer's band only) | 14k grains, critically damped (drag 14), are pushed onto the nodal shells round the heart and circulate along them (orbit), with trails. A veil of 6k strands falls from the rim through the same force. A plane field cuts the bell. | Self-organising matter: it re-forms into each new figure as the modes change. |
| Coupling | `druseFoot` probe (u < 0.22) to the druse source's `drive` | A route | The druse answers a bass note in its own higher voice. |

The ground's base relief is real terrain-system geography (ADR-046/090). It is a `WorldMap` with noise layers,
terraces, a rim, gullies, outcrops and two mountain rings, probed at every vertex through
`avgen_world_preview --heights`. It is a sculpted place in silence (`prototype-a/a3-silent-landform.png`).

## 4. Results, and what is still weak

**Strongest:**

- The ground in high-frequency and sustained passages: crisp hexagonal crater cells, lit by the medusa's lamp, with
  rings round the druse.
- The frequency selectivity: a 55 Hz tone gives broad rings, a 2 kHz tone gives a cell carpet, and the druse lights
  its core or its fringe accordingly.
- The coupling (`coupling/sympathetic-resonance-off-on.png`).
- With glow, emission and post reduced, the ground's figure and the druse's silhouette hold entirely; it is geometry
  (`final/glow-and-post-reduced.png`).

**Weak, candidly:**

- **The medusa** reads as a woven dome with a fringe, and in dense passages as a hairy ball. Grains travelling between
  shells draw radial streaks. It is coherent and field-shaped, but not yet the filament entity of the board.
- **Particle field-force strength is capped at 100** by the parameter's hard range. Stronger organisation needs that
  range widened or a different scale.
- **Aliasing.** Crater rims far from the camera show sawtooth aliasing, and the finest modes alias against the 0.36 m
  mesh at distance.
- **The druse's crystals** show stepped ledges and band glow that read as stripes from some angles.
- **The grade.** It is better than the first passes (a warm raking moon against a cold field), but the darks are
  still a uniform blue-violet. The sky is a plain gradient, and the spires are simple faceted pinnacles.
- **No refraction.** Cymatic optics are carried only by the lamp light revealing the cells and the crest accent.

## 5. Media

Everything below is in `~/Desktop/av-gen-review/58-cymatic-world/`.

- `final/four-conditions-and-probe-tones.png`: quiet, sustained, transient, dense, 55 Hz and 2 kHz.
- `final/contact-sheet-trench-40-70s.png`: the final scene, eight moments of Trench.
- `final/cymatic-world-trench-40-70s.mp4`: a 30 s clip with audio, 1280x720.
- `final/glow-and-post-reduced.png`: the scene as shipped against glow, emission and post reduced.
- `coupling/sympathetic-resonance-off-on.png`.
- Per-prototype folders:
  - `prototype-a/`: the first critique, a top-down view of the field, the silent landform, the normal-step fix;
  - `prototype-b/`: the first choir (rejected) and the druse under low, high and chord;
  - `prototype-c/`: the shells and the woven medusa;
  - `together/`.

## 6. Performance

Measured on an M2 Max through `tools/gpu-lock.sh`.

| Case | Internal | GPU median / P95 | Frame median / P95 | CPU work | Status |
|---|---|---|---|---|---|
| Live (`--mode live`, Auto, 30 fps target), 1920x1080 projection, live input | Auto -> High (0.85) | 23.3 / 27.0 ms | 33.3 / 37.0 ms | 4.7 ms | Target achieved, headroom, 0 deadline misses |
| Headless, Trench from 120 s, Ultra | 1920x1080 (1.0) | 26.6 / 27.1 ms | | | Under 33.3 ms |
| Headless, Trench from 120 s, Medium, before the optimisations | 1362x766 | 28.8 ms | | 3.5 ms | Under budget |
| Headless, Trench from 120 s, Ultra, before the optimisations | 1920x1080 | 33.2 ms | | | Over |

The scene draws 5 draws and 262k triangles, with no shadow casters and no volumes. The live controller settles at
High (render scale 0.85).

Costs, by elimination (headless, 1080p, Ultra):

| Item | Cost |
|---|---|
| Ground material (before the painted-light rewrite) | ~12 ms |
| Lit path for the ground (now unlit) | ~7 ms |
| The single `shading` field op | ~4 ms |
| Relief deformer (vertex, prepass and main) | ~2-3 ms |
| Particle simulation | 0.2 ms |
| Druse effector pass | 0.07 ms |
| CPU field resolve | 30 µs a frame, 1.8 ms after a seek (cold cache) |
| GPU memory | Mode table 2.3 KB; FieldBlock 8.5 KB |

Changes that paid:

- **The unlit painted-light ground:** 33.2 to 26.6 ms GPU at Ultra.
- **One `shading` op instead of three field ops**, and one `relief` deformer instead of three.
- **No fBm mottle.**

## 7. Tests

| Test | Covers |
|---|---|
| `tests/unit/test_resonance_field.cpp` (`[resonance]`, 21 cases) | Mode shapes, normalisation and regularity; the dispersion axis and fractional modes; frequency selectivity; build-up and ring-down, with low modes ringing longest; strikes; purity under play, seek, reset and the cache; phase continuity, the limiter and late clocks; idle; channels (sand on nodes, force toward nodes, gradient); nodal lines of the coherent figure; readers and listening windows; JSON, validation and hash; parameters; memory; competition (per source); relief; depth and scale; probes; drive |
| `tests/rendering/test_resonance_gpu.cpp` (`[gpu][resonance]`) | CPU/GPU parity in every channel, the colour channel included, through the production FieldUniforms at four clocks; the 48-mode table limit |
| `tests/unit/test_procedural_normal_step.cpp` | ADR-1401 |
| Hidden `[.resonance-probe]` | A diagnostic that prints a scene's modes, reader statistics and resolve timings for a real song |

Final checks, each binary's own exit code:

| Check | Result | Exit |
|---|---|---|
| `tools/ci/run-suite.sh cpu-assets` | 4276 passed, 0 failed, 19 skipped, 1 expected failure, 4296/4296 | 0 |
| `tools/ci/run-suite.sh gpu` | 663 passed, 0 failed, 1 skipped, 664/664 | 0 |
| `python3 tools/review_examples.py --only "Cymatic World"` | ok (live) | 0 |
| `python3 tools/check_project_integrity.py` | 0 problems | 0 |

## 8. Documentation and decision records

- **ADR-1400**: the resonance field kind.
- **ADR-1401**: a procedural object's `normalStep`. A 5 km mesh's automatic 3.7 m finite-difference step lit every
  crater as a black hole.
- **ADR-1402**: probes, source drive, and per-source competition and limiter.
- `docs/decisions/README.md` rows.
- `docs/resonance-field.md`: the model, data flow, parameters, costs, extension steps, limitations and coupling.
- `docs/prototypes/cymatic-world/{01-design,PROGRESS,02-report}.md`.
- `tools/world_preview.cpp` gained `--heights` (the batch form of its probes).

## 9. Limitations and the next experiment

**Limitations:**

- The modes are analytic standing waves, not a PDE solved on the real geometry. The ground does not reflect waves
  off its own walls; that is the trade for being exact under seek and cheap per vertex.
- The table holds 48 modes, and a resonator that does not fit samples 0 on the GPU.
- Memory is bounded by the history ring (~12 s).
- The particles are stateful. An offline render warms them up from its start rather than seeking exactly.
- Live, the scene holds 30 fps at 1080p only through the quality ladder's render scale (High, 0.85), with modest
  headroom.

**The most valuable next experiment:** bake the field into a screen-independent texture once a frame. A plane grid
over the basin, computed in one compute pass, would be read by the deformer, the material and the particles. Today
the same 14-mode sum runs per vertex (three times, for the normal), per pixel and per grain. A baked field would cut
the ground's cost by several milliseconds and allow more modes and a finer mesh (no rim aliasing). Close behind it:
give the medusa true filaments, either fibres (ADR-1180/1181) following the force field's streamlines between the
shells, or a stiffer constraint so grains stop crossing between shells.
