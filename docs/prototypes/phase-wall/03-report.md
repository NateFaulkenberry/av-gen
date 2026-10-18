# Phase Wall: final report

Branch `proto/phase-wall` (from `proto/generative-rd` d0e62cf9). Listed in Examples > Live > Experiments as
**"Phase Hall"**. The R&D's `osc-flaps` already holds the name "Phase Wall"; the coordinator settles the final name at
merge.

**Docs:** `01-research.md`, `02-plan.md`, `PROGRESS.md`.
**Media:** `~/Desktop/av-gen-review/66-phase-wall-scene/`:
- `00-proofs/`: stage 0;
- `01-hall/`: the hall;
- `02-colour/`: the palettes;
- `03-final/`: the 30 s Feline Footwear clip on the promenade, stills and the dead-space sweep.

## What it is

A closed hall, 128 × 48 × 30 m, whose four walls are one kinetic installation. About 8,000 metal louvre blades
(0.745 × 1.585 m) each swing ±1 rad about a vertical hinge with the sine of their own Kuramoto-Sakaguchi oscillator
(ADR-1450).

**One network for the room.** A single 220 × 19 oscillator strip (the hall's perimeter by its height, 1.6 m cells,
wrapped) is read by all four walls. Each wall reads its own stretch of the strip through a pair of grid readers whose
frames take the wall's unrolled coordinate onto the grid. Islands, fronts and seams cross the corners and circle the
room. This needs no engine change: 8 of the 16 GPU field slots, 14 with the music.

**The arc.** The coupling breathes on a 96 s timeline:
- disorder;
- islands, held at K 4-5.5 from 16 to 34 s and from 66 to 80 s;
- unison, K 12 from 44 to 56 s;
- then back.

A periodic frequency tilt (one sine round the hall) keeps unison rolling.

**The world fills every frame by construction.**

| element | how it is built |
|---|---|
| Walls | Bay piers every 16 m, with lit strips in the palette's trim colour |
| Ceiling | Coffered, with faintly glowing panels against dark beams |
| Air | Dark, tinted haze (post atmospheric) |
| Gaps between blades | A lit backplane that pours colour through as blades turn edge-on |
| Floor | Unlit dark lacquer, mottled, carrying each wall's own colour pooled beneath it. The floor program reads the walls' sin θ readers, since the engine has no planar reflection |

**The blades read as blades.** Their shading is computed in each blade's own frame:
- a dish across the width, with a thin highlight on the leading edge;
- a lift toward the top;
- soft shadow lines at the four edges.

Value comes from a three-stop ramp over the full sin θ range (trough, mid, crest). Strain lights whole blades: an
emission effector feeds `instanceEmissive`, gated by a breath-driven gain so that disorder doesn't flood the walls.

**Light:**
- four diffuse-only wall washes, one per wall, from high in front of it (Rozin's lamps);
- a diffuse-only key;
- a coloured bounce from below;
- four vertical reflection bands (ADR-1151). As a blade swings about y, its reflection sweeps across them; this is
  Ned Kahn's mechanism.

**Camera:** the promenade, a 120 s loop of 8 oblique and off-axis framings. It never holds a dead-centre axial view;
the end wall is met close and at an angle.

## Live instrument

The project carries `sonic.live`, `response.*` (ADR-1361), no audio file and no `live.targetFps` (ADR-1360).

| input | target |
|---|---|
| response.sustain | coupling +3 K (3 s attack, 6 s release) |
| low onset (kick) | a phase-advance ring from the end wall's centre, which runs round both corners |
| mid onset (snare) | a quicker ring from the east wall's middle |
| response.bass | natural frequency +0.25 Hz |
| response.hat | phase noise +0.12 |
| response.intensity | lag +0.35 (6 s attack, 12 s release) |
| response.level | the reflection rig's rotation, integrated |
| CC 1 | coupling |
| CC 2 | complexity (spread) |
| CC 3 | motion (speed) |
| CC 4 | audio influence (the depth of every audio route) |
| CC 5 | lag |
| CC 6 | light (bands and backplanes) |
| CC 9 | palette select |
| CC 10 | palette lock |
| note 36, held | a pacemaker on the end wall |

The sustain route started at +6 K, the R&D value, and was halved to +3 K: at +6 a dense mix such as Feline Footwear
locked the hall in unison.

**Palettes.** Eight role-based states:
- **the states:** Bronze Tide, Ember, Gilt Orchid, Coral Lagoon, Verdigris, Aurora, Cobalt Ice, Nocturne;
- **the roles:** environment, back, trough, mid, crest, wash, front, peak, trim, floor;
- **every colour bound to a role:** the 4 blade programs, the floor program, the backplanes, the strips, the inlay,
  the piers, the coffers, the 6 lights, the sky, the fog and the post tint;
- **the selector:** ADR-1420, advancing on phrases, with the ADR-1421 energy-change fallback; CC 9 selects, CC 10
  locks.

## Gates

| gate | result |
|---|---|
| No dead space: `tools/phase_wall/sweep.py` (5 positions × 6 yaws × 2 pitches) | 0 failing frames out of 60 on Aurora (worst 0.8% of a frame), Nocturne (4.6%) and Ember (4.0%). A frame fails at 5% near-black, featureless blocks. On the same measure the R&D A3 stills score 37-41% |
| Strobe: `tools/phase_wall/fdiff.py` (mean frame-to-frame change at 960 px; gate p50 ≤ 6 / p95 ≤ 12; R&D A3 clip 5.4 / 7.4) | Static camera, autonomous: islands 0.5 / 0.8, unison 5.1 / 8.3, disorder 0.9 / 1.3. **Feline Footwear (static, 44-52 s): 8.2 / 11.6, so p50 is over the gate.** That is the whole room swinging under the music, not per-blade shimmer, but the metric cannot tell the two apart |
| Live, 1080p, Auto, 30 fps target (`avgen --live-profile --mode live --audio <song> --target-fps 30`) | **High** (1632×918), mean frame 33.3 ms. Feline Footwear: GPU 16.3 ms, **1 miss** / 150 frames. Trench: 17.0 ms, 1 miss. Hyperborea: 16.7 ms, **5 misses** (over the 0-2 gate). The display was 120 Hz; misses are counted against its 8.33 ms refreshes |

**Performance work:**
- the depth prepass (ADR-1406) took headless Ultra from 30.0 to 23.2 ms;
- replacing the blade bevel with a program edge highlight saved about 5 ms live and took misses from 7 to 1;
- removing key shadows saved 6 ms; they weren't visible.

Cymatic World measured on the same day and display: Low, 0 misses.

## Final checks

Run once on commit a4973dce, the final code; later commits are docs only.

| check | result | exit |
|---|---|---|
| `tools/ci/run-suite.sh cpu-assets` (under the GPU lock) | 4318 passed, 0 failed, 19 skipped, 1 expected failure, 4338/4338 ran | 0 |
| `tools/ci/run-suite.sh gpu` | 670 passed, 0 failed, 1 skipped, 671/671 ran | 0 |
| `tools/review_examples.py --only "Phase Hall"` | ok | 0 |
| `tools/check_project_integrity.py` | 0 problems | 0 |

**A process note.** `run-suite.sh gpu` takes `tools/gpu-lock.sh` itself. Wrapping it in a second `gpu-lock.sh`
deadlocks: the outer wrapper holds the lock the inner one waits for. My first gpu run did exactly that and was killed
after an hour; the GPU was held idle for that hour. The recorded run was started without the outer wrapper.

## Weaknesses, candidly

1. **Glowing defect seams are weaker than in the R&D A3.** On the 1.6 m lattice with radius-3 coupling, boundaries
   between islands are smooth phase ramps, so the strain is broad and low. The seams read as soft glowing bands in
   the islands state, and often not at all under music. Crisp, line-like seams would need either a finer lattice
   (which brings back the "mottled" look the coordinator rejected) or a sharper boundary signal: for example a
   phase-gradient readout in the grid, which is an engine change to ADR-1450's activity channel, in shared files the
   R&D agent owns.
2. **Island scale is a tuning balance, not a solved problem.** Spread 0.28 was mottled; 0.12 locked; 0.17 with radius
   3 on 1.6 m cells gives a few medium islands per wall. On dense music the hall still leans toward large uniform
   walls.
3. **Hyperborea** misses 5 frames at High on the 120 Hz display.
4. **The floor is stone, not a mirror**; the colour pooling stands in for a reflection.
5. **The test overrides** `PW_CELL`, `PW_RADIUS`, `PW_TILT`, `PW_ACT` and `PW_PEAK` remain in `build.py` (defaults
   are the shipped values). They are tuning handles for the next pass.
6. **MIDI** routes are wired and named but could not be tried with a controller; renders flag them live-only, as
   they should.
