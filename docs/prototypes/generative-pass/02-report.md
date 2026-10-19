# Generative pass: report

- **Brief:** [00-brief.md](00-brief.md) (the owner's, verbatim). **Plan:** [01-plan.md](01-plan.md) (the audit, the
  debate, the proofs).
- **Branch:** `art/generative-pass`, from main `a55b49f9`. Main's performance work (`99d45f13`, ADR-1490..1493) is
  merged in, so the profiles below measure what ships.
- **Media:** `~/Desktop/av-gen-review/70-generative-pass/`:
  - `baseline/`;
  - `proofs/` (P1-P6, R1-R3, and the ADR-1502 evidence);
  - `stage2-rd-pins/`;
  - `stage3-phase-hall/`;
  - `stage4-final/` (the final evidence);
  - `stage4-profiles/`.
- **Audio:** Feline Footwear, fed with `--audio` (live scenes carry no track), plus Trench and Hyperborea.
- **Engine:** ADR-1500, ADR-1501 and ADR-1502 (range 1500-1509).

## Summary

| | RD Pins | Phase Hall |
|---|---|---|
| The main change | Contours of light (a hot core, a halo, dark channels) on a field excited by waves at three scales, the bass history, kick and snare rings, and density | The room is gone: a sphere of luminous blade columns seen from inside, caps as their own pattern, a membrane displaced by the music, a light hierarchy by region |
| Fields (of 16) | 13 → 15 | 14 → 13 |
| Main program ops (of 48) | 17 → 48 | 37 (×4 walls) → 48 (one program), plus a 24-op chip program and an 18-op membrane program |
| Geometry | 36,100 pins (unchanged), 3 sine deformers, 5 effectors | 8,064 wall blades → 14,161 blades + 3,618 cap chips (`points`) + a membrane sphere |

## 0. The audit's main finding: the level signals are pinned

The response model's levels sit at the top of their range for most of each test track, so every route from them is
a constant. `avgen --sonic-trace` gives every frame, measured from 10 s in to 10 s before the end, at the default
Response sliders (sensitivity, transient and sustain 0.5; attack and release 1). The full table is in
`baseline/response-saturation.txt`.

| Track | `response.bass` > 0.9 | `response.level` > 0.9 | `response.levelSlow` > 0.9 | `response.sustain` p10 / p50 / p90 |
|---|---|---|---|---|
| Feline Footwear | 88% of frames (p50 1.00) | 98% | 92% | 0.74 / 0.89 / 0.92 |
| Trench | 90% | 98% | 98% | 0.87 / 0.90 / 0.94 |
| Hyperborea | 83% | 96% | 95% | 0.81 / 0.91 / 0.96 |

- **Lowering the Sensitivity only shifts the band.** On Feline at Sensitivity 0.4, 0.3 and 0.2 (sustain 0.5),
  `response.bass` spans 0.82-1.00, 0.73-0.99 and 0.62-0.89 (p10-p90).
- **At level sensitivity 0 the levels are silent.** Sensitivity + sustain - 0.5 = 0 gives 0 everywhere.
- **What does move:** the hit envelopes (`response.kickEnv`, `hatEnv`, ...), `response.hatRate` (0-0.65), the onset
  history and the spectrogram history.
- **What changed in these two scenes:**
  - bass shape now comes from the spectrogram history (Spectrum fields);
  - events come from the onset history (Onset fields);
  - density comes from `response.hatRate`;
  - the pinned levels drive only slow "presence" terms.
- **The response engine is unchanged.** The coordinator is raising it with the owner.

## 1. RD Pins

### Changes

**Contours of light.**
- The pattern's boundary is drawn as a fine hot core, `(1 - d²)^12` in the palette's new `hot` role, and a halo,
  `(1 - d²)²` in `ring`, over dark channels.
- Their intensity has a floor (0.32) plus four terms:
  - where the broad swell's crest meets the bass pool (two wave systems interfering), up to ×3;
  - the kick's band of light (accents bright, ghost hits under 0.08 nothing);
  - the snare's;
  - beads where the swell's sharp crest crosses a contour, gated by the hats.
- Density (`response.hatRate`) multiplies the core and halo (×0.55-1.7), so a busy mix lights the field and a
  breakdown lets it settle.

**Motion, in layers** (all functions of the clock and the audio history; seek equals play, see ADR-1502):

| Layer | Mechanism | Driver |
|---|---|---|
| Broad swell A (38 m, 4.2 m/s) | world-space `sine` deformer, reproduced exactly in the program (`gradient` + `time` + `palette`): it moves the contour level, so contours bunch and open, and its crest carries the light and the beads | height: the bass level (0.6-1.3 m), CC 7; contour reach: `audio.mid` |
| Broad swell B (27 m, crossing) | `sine` deformer | CC 7 |
| Ripple (9.5 m, 3.6 m/s) | `sine` deformer, a small heave | `audio.mid` |
| Bass history | Spectrum field (bass band, 14 m/s, 0.15 s window) as a lift effector and a pool of light | the music's own shape |
| Kick | Onset field, a ring from a heart that wanders slowly round the bowl: lift (effector), contour swell (bounded), a band of light; also in the regime bake (spots spread, then give the ground back) | kick onsets; CC 8 |
| Snare | Onset field from off-centre: a quicker, thinner ring of light and a small lift; the brush's burst of growth (kept) | snare onsets; CC 8 |
| Hats | beads on the contours | `response.hat` (8 ms in, 260 ms out); CC 11 |

**Identity kept.**
- The labyrinth holds much of the frame. The regime front sits 9 m toward the spots side, and the held-sound pool
  pushes less (0.6 → 0.25), so spots win locally on kicks and give ground back.
- The bowl and the camera are unchanged.
- The pattern-tinted floor is off: with the pins heaving, it showed through as a ghost of the contours at the wrong
  height. The floor is plain and sits 2.5 m lower, so a pin in a trough never sinks through it.

**Palettes.**
- Six coordinated sets: Copper Night, Verdigris Reef, Garnet and Indigo, Violet and Gold, Electric Lagoon, Rose
  Quartz. They keep the brief's four families.
- Each has its own dark foundation (environment, shadow, base), a saturated contour colour (`ring`), a highlight in
  its family (`hot`), the pattern metal as accent, two lights, and its own bloom intensity (`bloomI` →
  `post/bloom/intensity`).
- The selector, the journey and CC 9 / 10 are unchanged.

**Post.** Bloom threshold 1.1 (exposed), knee 0.6, radius 0.75: the cores glow without filling the channels.
Vignette 0.4 → 0.22, grain off.

**MIDI.**
- CC 1-5 and 9/10 kept; notes 36-38 kept.
- CC 6 is now LIGHT (core and halo).
- New: CC 7 WAVES, CC 8 IMPACT, CC 11 SHIMMER.
- The CC multiply routes are neutral at rest (×1 untouched, ×2.5 full). With offset 0, an untouched controller
  would have zeroed them, which is what an early proof showed.

### Evidence (`stage4-final/`)

- `rp-before-after.png`: before and after at Feline 2 s (quiet intro), 8 s (percussion), 160 s (dense), 184/186 s
  (the drums stop), 190 s (held bass) and Trench 45 s (breakdown). Same camera, same resolution. The "before" frames
  were single-frame renders and carry the catch-up defect ADR-1502 fixes: their regime is all labyrinth.
- `rp/rp-feline-155-167.mp4` (12 s, dense) and `rp/rp-feline-180-192.mp4` (12 s across the drums stopping);
  `rp-clip-180-192.png`.
- `rp-palettes.png` (all six, locked); `rp/rp-hyperborea-48.png`.
- `stage2-rd-pins/rp-filmstrip-159-161.png` (every 5th frame over 2 s).

### Limitations

- **Kicks.** Feline's kicks come every ~0.3 s, so the rings read as a rhythmic train from the heart, not separate
  events. The wandering heart and accent weighting keep it from becoming a conveyor; isolated kicks read as single
  rings.
- **No grazing glints.** Box pins do not tilt, so the heave reads through parallax and the shafts it exposes rather
  than through specular highlights.
- **Budgets.** The pin program is at 48 of 48 ops; fields are 15 of 16.

## 2. Phase Hall

### Changes

**The room is gone.**
- No walls, floor, ceiling, piers or corners.
- The viewer is inside a sphere of radius 150 m.
- 288 columns of blades (2.2 × 5.5 m, rows 5.6 m: a column reads as one tall band) run from about 45-53° below to
  45-53° above the horizon. Each blade is placed with its own orientation (`points`, 14,161 blades), swings about its
  own hinge and moves along its own normal (ADR-1500).
- A membrane sphere 2.2 m behind them glows in the palette's deep `back` with a fine cellular skin and pools.

**The network wraps the sphere.** One Kuramoto strip (144 × 54 cells) once round, read through the sphere projection
(ADR-1501), so islands, fronts and seams circle the viewer with no seam.

**The poles.**
- The meridian columns never reach a pole. From a latitude that wanders round the sphere they thin out over 10-15°.
- A cap of chips fills in ahead of them: a Lambert equal-area hexagonal lattice, every chip along one parallel field
  (world Z projected), so nothing is radial, no rings, no hole. The chips are lit like the blades and as bright.
- Proofs P1-P5 rejected, in turn: a ragged hem (read as an oculus), a dissolve into the bare membrane (a dark
  opening), and Fibonacci chips (spiral parastichies read as a vortex).

**Light hierarchy** (brief §4.6-4.7):
1. Narrow hot cores down every blade, `(4u(1-u))^22`, and short streak glints on the hats.
2. Bright saturated regions: the cores' brightness is (floor + the blade's own phase crest³) multiplied by the
   membrane's zone (0.03 in a trough, 3.4 on a crest).
3. Soft spill: a faint shoulder, the membrane's pools, and bloom tuned for this scene (threshold 0.8, radius 0.95,
   intensity 0.34).
4. Deep gaps: dark bodies (the faces are 0.18 of the tone) and dark membrane.

The zone is the displacement field itself, so the regions that blaze are the ones the music pushes out: the
bass-history rings, the broad swell and a slow drifting pool field.

**Motion, in layers:**

| Layer | Mechanism | Driver |
|---|---|---|
| Bass rings | Spectrum field (bass band) laid out at 34 m/s from low on the left, out of frame: rings of displacement (up to ~10 m) and light cross the sphere as arcs | the bass history; CC 7 |
| Broad swell | planar Wave field, 260 m at 7 m/s | the held sound (+3 m), CC 7 |
| Pools | drifting fbm in the displacement: the composition's slow bright and dark regions | time |
| Kick | Onset field from off the frame to the right: a ring of push and a pulse of light across the panels; a phase front through the network | kick onsets; CC 8 |
| Snare | a quicker phase front | snare onsets |
| Panels | the swing about each blade's own hinge, islands and seams (strain glow at the strongest seams) | the network; coupling, lag, MIDI as before |
| Fine | streak glints running up the columns at 3 m/s; reflection strips sweeping across turning blades | hats (8 ms in, 260 ms out), CC 11 |
| Density | the cores' brightness ×0.45-1.4 | `response.hatRate` |

**Palettes.** The brief's six, as role sets (environment, back, trough, mid, crest, core, glint, glow, front,
peak): Polar Glass, Glacial Teal, Acid Cathedral, Molten Signal, Sunset Interference, Ultraviolet. They journey round
the wheel by neighbours; CC 9 selects across the six.

**Camera.** A fixed performance camera, a third of the radius off-centre and a little low, at 20 mm, looking across
and up at the far side, drifting a few metres over 90 s. It never frames a pole. The review cameras (zenith, nadir,
side, behind) are not in the scene.

**MIDI.**
- CC 1-5 and 9/10 kept; note 36 kept (a pacemaker).
- CC 6 is now LIGHT (cores and strips).
- New: CC 7 SWELL, CC 8 IMPACT, CC 11 SHIMMER, all neutral at rest.

### Evidence (`stage4-final/`)

- `ph-before-after.png`: the room against the sphere at the same seconds.
- `ph-views.png`: the zenith (north pole), the nadir (south pole), the side and behind.
- `ph/ph-feline-155-167.mp4` and `ph/ph-feline-180-192.mp4` (12 s each); `ph-clip-180-192.png`.
- `ph-palettes.png`.
- `stage3-phase-hall/`: the gate-3 frames.

### Limitations

- **Quiet passages in the pale palettes.** Region contrast is improved but still moderate in quiet passages for Polar
  Glass and Glacial Teal, whose cores are near-white; the hierarchy is clearest in Molten Signal, Sunset Interference
  and Ultraviolet.
- **The cap's edge.** In a deliberate look straight up, the belt's top still converges mildly toward the cap's edge
  (latitude 45-53°). The coordinator accepted this at gate 3; no shipped camera frames it.
- **The kick's rings** come from one point (off frame). In a dense mix they cross the frame as a train of arcs.
- **Budgets.** The blade program is at 48 of 48 ops; fields are 13 of 16.

## 3. Engine changes

| ADR | What | Test |
|---|---|---|
| [1500](../../decisions/ADR-1500-an-effector-axis-can-be-the-points-own.md) | `effectors[].local`: a scalar PositionOffset along, and a Rotation about, the point's own axis | `[spatial][effectors]` unit; `[gpu][points][effectors]` CPU-GPU parity, every blend |
| [1501](../../decisions/ADR-1501-a-grid-field-can-read-a-plane-round-a-sphere.md) | `projection: "sphere"` for a grid field: a plane grid unrolled round a sphere, every radius the same cell | `[gpu][simulation][fields]` CPU-GPU parity |
| [1502](../../decisions/ADR-1502-a-grid-re-bakes-what-it-reads-each-sub-step.md) | a grid that reads a bake grid re-bakes it at each of its sub-steps, so a seek replays exactly what play did | `[adr1502]` seek equals play exactly (0.62 before) |

ADR-1502 was a pre-existing defect on main, found while proving RD Pins. A render or seek mid-track lost most of the
pattern after its first frame, because the catch-up read a stale (zero) regime map. RD Pins, RD Pins (R&D), RD Bloom
and RD Relief were re-rendered and checked.

Nothing in the live path (presentation, pacing, the quality controller, the program compiler) was touched.

## 4. Live profiles

`avgen --live-profile --mode live --target-fps 30 --size 1920x1080 --start 150 --measure 20 --project <live project>
--audio feline-footwear.wav`, through the GPU lock.

- **Engine:** both arms run on this branch's binary (main's ADR-1490..1493 merged in). The "before" arm is the scenes
  as of `a55b49f9` (`build/gp/old/`).
- **Order:** back to back, after one warm-up run of each "after" scene, because ADR-1493 compiles a new program
  structure in about 14 s cold.
- **Records:** `stage4-profiles/*.json` and `.txt`.

| Scene | Arm | Auto level (scale) | GPU p50 / p95 | CPU work p50 | Misses |
|---|---|---|---|---|---|
| RD Pins | before | Ultra (1.00) | 26.6 / 30.3 ms | 7.4 ms | 0 / 600 |
| | **after** | **High (0.85)** | 29.6 / 32.2 ms | 7.7 ms | **0 / 600** |
| Phase Hall | before | Ultra (1.00) | 28.6 / 31.4 ms | 6.6 ms | 0 / 600 |
| | **after** | **Ultra (1.00)** | **21.8 / 22.3 ms** | 5.9 ms | **0 / 600** |

- **Phase Hall is cheaper than the hall.** About 18,000 instances in one draw, three programs where the hall had five,
  and no floor or room geometry: 7 ms less GPU at the same level.
- **RD Pins costs more.** Headless A/B at fixed High (1632 × 918, `build/gp/ab/`):

  | Configuration | GPU |
  |---|---|
  | the scenes before | 21.3 ms |
  | after, with a 0.15 s bass-history window (15 taps a fragment) | 40.2 ms |
  | after, window off | 27.1 ms |
  | after, one deformer instead of three | 24.0 ms |

  The shipped scene uses a 0.04 s window (4 taps) and two deformers. Live, Auto holds High (0.85) with no misses
  where the scene used to hold Ultra. The rest is the 48-op pin program over a full frame of pins, and the deformers'
  vertex work on 36,100 pins (×24 vertices, in the depth pass too).
- **MIDI:** neither scene was measured with a controller attached; none was available. The MIDI path is a handful of
  CC routes evaluated per frame on the CPU, the same kind as before the pass.
- **No audio** (`--no-audio`, `stage4-profiles/*-noaudio.*`):
  - RD Pins: Medium (0.71), GPU 25.0 / 32.8 ms, 0 misses;
  - Phase Hall: Ultra, 23.7 / 32.1 ms, 0 misses.

  Without audio the RD and the bake still step, and Auto settles a level lower on RD Pins.

## 5. Checks

CHECKS
