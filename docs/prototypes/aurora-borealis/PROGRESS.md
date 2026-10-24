# Aurora Borealis: progress (scene agent, branch scene/aurora-borealis)

## Where it stands

| Milestone | State |
|---|---|
| A static composition | done: a river in a valley, steep walls, 64 layered spurs, three outer ranges |
| B hero aurora | done: ribbons at 48 planes, ADR-1660 coverage, stronger rays, the mix response off |
| C river and light coherence | done: E1 sky mirror, long-exposure water, one palette for aurora, facets, rim, river glow and streaks |
| D stem integration | done: stems by role, drum feeds, 02 s3 signals, E3 pulse on the kick, part knobs at 1, isolation verified |
| E live optimisation and acceptance | done: see "Acceptance" below |

## Decisions

- **The river is a sea, not a fitted channel.** A terrain node with a flat bed under `seaLevel: 0` carries the engine
  water; the faceted GLB banks start below the water, so the river is simply where the land is not. Nothing has to
  be fitted to the terrain's river profile, and the terrain is coarse and hidden.
- **ADR-1660, ribbon coverage.** The ribbons' gap noise has cells about 10 km across at crisp fold sizes, so a camera on
  a 2.6 km loop sat under empty sky for most of a minute at a time. A smaller fold size blurs every sheet at the low
  elevations a valley camera sees. One setting lowers the gap thresholds (0 is ADR-1641 exactly).
- **One palette axis.** `palette/position` is driven by `signal.paletteAxis` = 0.55 arrangement + 0.45 synth
  brightness, through four curated looks: Dusk Indigo, Violet Veil, Electric, Rose Lift.
- **One light value.** `signal.auroraLight` drives the aurora's brightness, the land's aurora-lit facets and the river
  glow on the same frame.

## Log

- 2026-10-24: generator, land, look, instrument; ADR-1660; tests `test_aurora_borealis.cpp` (CPU, 4 cases pass),
  `[adr1660]` GPU case added.
- 2026-10-24: merged main 7fcaa175 (E1-E3). ADR-1660 revised: lowering the gap noise's thresholds changed almost
  nothing, so coverage now moves the sheets' level line. The ADR-1652 pulsed march takes the same change.
- **Stop 1** (after Milestone B): `~/Desktop/av-gen-review/aurora-borealis/stop1-milestone-B/`: six stills without
  music (the aurora alone, rest palette), the same six with the stems, contact sheets, a 10 s clip (60-70 s, groove A,
  with sound).

## Known issues at stop 1

- The aurora's sheets are soft at low elevation (ADR-1641's widening grows with plane spacing over fold size); a
  valley camera sees mostly low elevations. The overhead arcs are crisp.
- Groove B (around 140 s) is still close to blowing out to pale pink in the brightest frames.
- The river's reflection has dark blotches and a fine speckle in places (the ripple and SSR-hit mix); to tune in C.
- The land reads as one value family (indigo-violet); the aurora-lit facets show, but the hero peaks need more
  separation from the walls.
- Some camera stretches have little aurora near the top of the frame.
- No performance measured yet (Milestone E).

## After stop 1 (owner: "Go ahead", plus notes)

- **Layout (owner notes A):** a river in a valley. The walls rise straight from a 2-4 m rock lip (no flat shore);
  64 spurs every 5.6 degrees, alternating banks, set back 260-780 m and rising with distance (8-12 degrees from the
  river), so near, middle and far ridges overlap as they recede; haze steps them back in value.
- **Sky (note B):** zenith #04061c, horizon #2a2466, haze 0.35; stars at 1.4 brightness, density 0.55, a Milky Way
  band 0.6; the aurora's horizon glow 0.55 as airglow. No sky colour is routed (no IBL rebuilds); only the stars dim
  with the arrangement.
- **Water (long exposure):** ripple, chop and undulation 0; the view streak at 2 smears reflections along the view;
  ADR-1643 flow streaks along a river feature on the centreline, in the palette's aurora colours; the E1 sky mirror on.
  **The SSR march is off:** its single jittered ray, spread by the streak, drew the lattice of dark dashes the owner saw
  (isolated by A/B: SSR 0 or streak 0 each removes it). The sky mirror alone reflects the sky, the aurora and the
  ranges.
- **Land (glass):** deeper shadow, aurora light at power 4 and glow 1.5, a fresnel rim in the aurora's low colour.
- **Aurora softness:** the stronger lever was ray structure (filaments 5, edge 5, complexity 90). 64 planes and shorter
  curtains measured no visible gain. **Rejected engine change (not committed):** a crossing-based ribbon march
  (draw each sheet where the ray crosses it instead of widening it). It produced aliasing speckle and hard edge-on
  sheet cuts: at the horizon many sheets fall in one pixel, and ADR-1641's widening is in effect its anti-aliasing.
- **Peak brightness:** base intensity 3.0, music adds at most 1.2 x auroraLight; Rose Lift's low colour is a
  saturated #ff6fd0.

## Acceptance (M2 Max, AC power, 60 Hz LG UltraFine, Release, 1920x1080, 30 fps target)

| # | Test | Result |
|---|---|---|
| P1/T1 | live misses / 900, median of 3 (start 60 s) | **3** (2, 3, 3), target <= 5 |
| P2/T2 | live frame p99, median | **39.2 ms** (40.1, 39.0, 39.2), <= 40 |
| P3/T3 | Medium or better | **100%** (Ultra 100%) |
| P4/T4 | headless Ultra GPU p50 / p95 | **13.8 / 17.7 ms**; by window 11.5-14.8 ms p50 (20, 60, 110, 152, 170, 196 s): stretch target <= 15 met |
| P5 | CPU work | headless p50 2.2 ms, p99 7.4 ms (target p99 <= 5: **not met**; the live CPU p50 is 6.0 ms, p99 9.5-10.1) |
| P6 | whole song, live | 6,151 frames, 11 misses (1.6 / 900), Ultra 100%, frame p99 39.2, max 58.9 ms; frame-time slope +0.005 ms/min. The live GPU timestamp climbs from 16.6 to 28.8 ms after about 150 s while headless costs at the same song positions stay at 11.5-14.8 ms, so it is the paced clock (ADR-1362), not work |
| P8/T9 | live-perf.sh with the Aurora row | PASS |
| lift live | start 152 s, 30 s | frame p99 39.2, 2 misses, Ultra 74% / High 26% |
| S1-S6 | signals on the stems | pass (test_aurora_borealis.cpp) |
| S9 | `--audit-routes` | 18 routes, 0 dead, 0 hazard |
| isolation | `--trace-finals`, each knob at 0 | drums 0: pulse and edge flat; bass 0: rim and swirl flat; synth 0: width flat, palette held low |

**Sound to light** (`tools/sound_to_light.sh`, BlackHole, live, probe period 512; ms from the sound, p50 / p95):

| Path | bus | light (first vsync) |
|---|---|---|
| response.onset / stem.drums.onset | 24.0 / 40.2 | 58.2 / 81.6 |
| AB signal.kickWave, kick pulse route final | 51.7 / 70.9 | 87.3 / 107.0 |
| AB signal.snareFold | 62.1 / 73.7 | 95.0 / 110.8 |

The kick wave rides the drum class here, because the probe's transients give the live beat tracker no groove to lock
to, so it measures the class backstop, not the grid path. T10 (45 ms) is not met by any path, as in every scene
measured so far (ADR-1605).

## Known issues and next steps

- The sheets near the horizon are still softer than the overhead arcs: inherent in ADR-1641's sampling.
- The groove B / lift peak is bright and saturated, close to the top of the tone curve in the widest overhead arcs.
- Live CPU work p99 is about 10 ms (target 5): the UI and pacing work in the projection window; it does not cost
  frames.
- The grid path of the kick wave is unmeasured live (needs a drum loop on the loopback, 02 s5 risk 1).
- `ribbonCoverage` (ADR-1660) is not routed; a phrase-gated coverage change would be a cheap musical lever.

## Suites (final commit 02d93bf6, same build)

- `AVGEN_GPU_LOCK_TIMEOUT=14400 tools/gpu-lock.sh tools/ci/run-suite.sh cpu-assets`: **PASS, exit 0**. 4,464 passed,
  0 failed, 29 skipped, 1 expected failure.
- `AVGEN_GPU_LOCK_TIMEOUT=14400 tools/ci/run-suite.sh gpu`: **PASS, exit 0**. 702 passed, 0 failed, 1 skipped.
  The run before it failed once on `Render job readback ring matches the synchronous path frame for frame` (a hash
  mismatch). That case passed 5 of 5 in isolation and in both full runs either side of it, so I take it for a flake
  rather than a regression.
- The first cpu-assets run found a real bug, now fixed (commit 02d93bf6): the directing compiler read a freed effect
  record (`source` held across `insertEffect`).

## Pass 2: the palette flicker fix (12 s2.6, owner approved)

- `signal.synthPresence` = `role.synth.rms` {gain 12, offset -0.2, clamp, attack 600, decay 2000} is the depth of
  `signal.synthBright` (was the raw per-frame rms, depthMax 12).
- The centroid mapping is scaled by 1.25 (gain 6.25, offset -3.25: the old curve x 1.25), so the lift reaches the top.
  Raising only the gain to 6.2, as 12 suggested, saturated the grooves (A 0.91, B 0.98) and flattened the ordering.
  The presence envelope is 600/2000 rather than 300/1500: at 300/1500 groove B's palette p99 was 0.0102.
- Measured with `--sonic-trace --trace-finals` on the stems, the same build before and after:

  | | groove B step p50 / p99 / max | whole-song p99 / max |
  |---|---|---|
  | `palette/position` before | 0.088 / 0.414 / 0.592 | 0.362 |
  | `palette/position` after | **0.0019 / 0.0090 / 0.0139** | 0.0141 / 0.0325 |
  | `synthBright` after | 0.0018 / 0.0067 / 0.0103 | 0.0094 / 0.0201 |

  Section medians of `palette/position` after (before): intro 0.90 (0.90), calls 1.48 (1.42), groove A 2.18 (2.19),
  groove B 2.41 (2.56), lift 2.88 (2.93), pedal 2.55 (2.57), riser 2.05 (2.37), outro 1.90 (2.06). Same ordering.
- Test: `[aurora-borealis]` now also checks `synthBright`'s groove B step p99 < 0.02.
- Stills: `~/Desktop/av-gen-review/aurora-borealis/pass2-palette-flicker/pair-t{140,160}.png` (before above).

## Pass 2, Gate A: composition (STOP for owner review)

Built per 13 and 11 s2.3 (probe E's read):
- **One river curve:** the rounded-triangle loop `rc = R0 (1 - 0.10 cos 3θ + 0.016 sin(8θ+0.7) + 0.007 sin(13θ+2.3))`,
  R0 3000 m, half-width 72 ± 14 m. The land rows, the water's river course (255 points), the camera arc table and
  (later) the pulses and edges all derive from it. Loop 880.7 s at 22 m/s.
- **No lip:** a continuous 7-15° shore slope through the waterline (the bed continues it to -8 m), about 8° lower
  slopes to a floor edge at 200-420 m, then the wall. Outer bank 190-330 m and steeper, inner 110-220 m and rolling.
  The near rows' vertices jitter by ±40% of the row spacing. 140 spurs set back 450-1100 m, gated behind the floor,
  ×1.15 outer and ×0.85 inner. The outer rings are scaled to 4.3-7, 6.5-11 and 10.5-19 km; haze 300 m-9.5 km.
- **Camera:** 95 m, pitch +2.5°, target 2 km ahead, 24 mm, 500 keys from the arc table, roll kept.
- **Water terrain:** 7.4 km, `chunkSize` 400, view distance 6 km. The nav grid is skipped as too large (a harmless warning).
- **A's two no-code defects:** the river's real speed (`terrain.flow.speedOverride` 3.0 m/s, bankShear 0.5, meander
  0.12, turbulence 0.3; `rippleSpeed` 1.0, ripple 0.01 at scale 0.03, undulation 0.03, streaks 50 m at speed 1.8) and
  the Milky Way (bandTilt 1.0 rad, density 0.03, band 1.4).
- Triangles: 39,920 (was 24,192).

**Headless Ultra 1080p A/B**, interleaved, 3 reps per window, AC power, same build. GPU p50 by rep, then the median:

| | t 60 s | t 140 s | CPU work p50 (60 / 140) |
|---|---|---|---|
| before (pass 1 look) | 14.39, 13.68, 13.00 → **13.68** | 14.90, 14.75, 13.35 → **14.75** | 3.35 / 3.79 |
| after (Gate A) | 13.83, 14.22, 12.34 → **13.83** | 13.26, 13.57, 13.11 → **13.26** | 3.46 / 2.25 |

Media: `~/Desktop/av-gen-review/aurora-borealis/gateA/`:
- `pair-t{20,50,100,140,160,200}.png`: before above, after below, same build, same song times (the camera path
  changes with the layout);
- `sheet-after.png`;
- `clip-40-60s.mp4`: 20 s with sound;
- `topdown-map.png`: the land's heights, the river and the camera path with minute marks.

**Open for the owner:** the river still turns out of view behind the bends in some views (A's risk 1). The levers are
camera height (85-95 m), a target further ahead, and smaller meander amplitudes.

### Gate A, second showing (owner sent the first back)

Kept: one river definition feeding land, water and camera internally, and the no-lip shoreline. Changed:
- **A snaking river:** 17 broad S-bends round the loop, about 1.1 km apart, alternating left and right, their amplitude
  150-255 m varying slowly, on the rounded-triangle valley axis. Two or three bends show at once from the camera. The
  camera flies the smoothed valley axis, not every meander.
- **A substantial river:** half-width 155-215 m across the flow (was 72), widened radially where the river runs at an
  angle so the bends do not pinch.
- **Layered ranges:** 144 distinct peaks in 3 layers on the outer bank (650-1100 m, 1500-2600 m and 3400-6000 m back;
  260-420, 420-650 and 600-950 m high) and 2 on the inner, at random bearings so left and right never repeat. The
  outer layers stand in the ring meshes. The valley walls are lowered (outer 110-220 m, inner 80-170 m) so the peaks
  stand out above them. Haze pales the far layers.
- **Rocky shoulders:** large ridged facets from 30 m up the shore. The waterline stays a gentle slope.
- **Camera:** 65 m, pitch +1.5°, target 1.6 km ahead. The near water carries the aurora again.
- Water terrain 8.4 km; the river course is 200 m wide in the flow model; 600 streak lanes.

Headless Ultra A/B (pass-1 layout before, this one after), interleaved, 3 reps, AC, same build, GPU p50:
before 14.90 (t60) / 14.50 (t140); **after 14.81 / 13.58**. CPU work p50 3.53 / 3.35 against 3.39 / 2.72.

Media: `~/Desktop/av-gen-review/aurora-borealis/gateA2/`:
- `pair-t{20,50,100,140,160,200}.png`: pass 1 above, this layout below, same build;
- `sheet-after.png`;
- `several-bends-t100-t200-t230.png`;
- `vanish-moments-prevGateA-top-new-bottom.png`: the first Gate A at the times its river vanished, above, and this
  layout at the same times, below. The camera paths differ;
- `clip-40-60s.mp4`, 20 s: the river stays visible in every sampled frame;
- `topdown-map.png`: the meanders in blue, the camera's axis path in yellow, minute marks.

### Gate A, third showing

- **Straights and mild bends** (owner: "some straight aways and mild bends"). The sine meander is gone. The river
  carries 7 discrete bends: each is one broad swing to one side, 370-470 m either side of its apex, 109-179 m out,
  alternating sides, each placed, sized and spaced on its own. Between them the river runs straight down the valley
  axis. 54% of the loop is straight (within 15 m of the axis), in runs of 0.6-2.1 km.
- **Bends widen less:** half the radial correction, so the bends read as river, not lake. The base half-width is
  unchanged (155-215 m).
- **Taller far ranges:** the middle layer is 480-720 m, the far layer 850-1300 m, and the distant ring 600-1300 m relief.
  Haze still pales them with distance.
- **Rock throughout the near slopes:** stronger ridged facets from 30 m up, plus a coarser set of buttresses from 40 m.
  The waterline stays gentle.
- **Headless Ultra A/B** (take 2 against take 3), interleaved, 3 reps, AC, same build, GPU p50: take 2 14.20 (t60) /
  13.86 (t140); **take 3 13.87 / 14.29**. CPU work p50 3.62 / 3.75 against 3.41 / 3.53.
- Media: `~/Desktop/av-gen-review/aurora-borealis/gateA3/`:
  - `pair-t*.png`: take 2 above, take 3 below;
  - `sheet-after.png`;
  - `straight-t214-and-bend-t12.png`;
  - `several-bends-t100-t140-t200.png`;
  - `clip-40-60s.mp4`;
  - `topdown-map.png`: straights in green, bends in orange, the camera path in yellow.

### Menu: under Sonic VFX (owner request)

The index entry is now category "Sonic VFX", named "Sonic VFX - Aurora Borealis" (the Live panel strips the prefix),
directly after "Sonic VFX - Event Horizon". The project files stay in examples/aurora-borealis/. The Sonic VFX menu
test now expects seven offered scenes. Merged main 250564e9 (the Digital Ocean cut) in the same step.
`check_project_integrity.py` reports 0 problems, and the `[examples]` and `[aurora-borealis]` tests pass.

## Pass 2, Gates B-G: status at the pass-3 stop (2026-10-24)

The owner's pass-3 brief (Sabertooth) supersedes the comet, river-pulse and edge-split gates, so the final stop was
not reached. Live acceptance, sound to light and the suites were not run for pass 2.

| Gate | State | Commits |
|---|---|---|
| A composition | done, approved (take 3) | c61111e6 |
| B ridge edges | built: baked one-edge-per-facet mask in TEXCOORD_0 (research A probe F), a 17-op line term, palette colour, bass breadth + kick/snare/comet accents. **Simplified:** no beat-hash subset, no hat sparkle | 5049f3c3, f1dd701a |
| C moving water | built: speedOverride 3 m/s, streaks 50 m at 1.8x, ripple 0.01; bass and synth bed → streak strength; small drum ripple | 981826d7, 5049f3c3 |
| D river pulses | built: two directional groundPulse fronts, ring-of-two triggers (88 + 86 launches, at most 2 alive), snare flare | 5049f3c3, 850f3e40 |
| E celestial sky | comparison of 4 variants from existing star parameters (`sky-variants-comparison.png`); none clearly wins, so the current sky stays. **Not built:** engine S1/S2 | — |
| F comet | built: one comet on `signal.cometEv` at 48.27, 87.53, 114.6, 151.0, 185.33 s, azimuths keyed from the camera track, environment wash | 5049f3c3, 850f3e40 |
| G integration | partial: rockier near slopes on both banks (the owner's right-side note), snare attack 0 with a 150 ms refractory, clip planes, flash budget | 5049f3c3, f1dd701a |
| menu | done: "Sonic VFX - Aurora Borealis" after Event Horizon, merged main 250564e9 | b3437e26 |

**Fixes found on the way:**
- Effect triggers need event signals. The latches are levels, so the first build's pulses and comet never fired.
  `pulseAEv`, `pulseBEv` and `cometEv` are the beat pulse gated by each latch's first half-beat.
- Black frames: the automatic far plane (about 1,000 km) made the sky direction NaN on some frames, and the bloom
  blacked the frame. Explicit near 1 m / far 45 km.
- Flash audit (gate: relative-luminance swing ≤ 0.05 in 267 ms): it failed at lift kicks until E3's front stopped
  widening the sheets 1x (now 0.3x at gain 1.0), the swell ramped in over 0.6 s, and the kick and bass accents were
  reduced. Final windows: 0.024 (44-58 s), 0.029 (60-70 s), 0.046 (150-160 s), 0.006 (183-193 s).

**Stage measurements** (headless Ultra 1080p, GPU p50 ms, median of 3 interleaved reps, AC). Stages 1-3 and 4-8 ran
as two batches, so the 3→4 step is not interleaved. Stages 5-8 were measured with working triggers but before the
f1dd701a tuning:

| t | 1 pass-1 | 2 land | 3 +edges | 4 +water | 5 +pulses | 6 +sky | 8 final (+comet) |
|---|---|---|---|---|---|---|---|
| 60 | 14.54 | 12.91 | 13.25 | 11.72 | 11.94 | 12.00 | 12.08 (p95 13.18) |
| 152 | 13.99 | 14.86 | 14.62 | 12.32 | 12.67 | 12.74 | 12.70 (p95 13.89) |
| 186 | 14.34 | 14.12 | 12.90 | 11.95 | 11.98 | 11.95 | 12.03 (p95 13.76) |
| comet window ~49-55 s | | | | | | 11.57 | 12.63 (+1.06) |
| comet window ~186-192 s | | | | | | 12.09 | 13.31 (+1.22) |

- Pulses add +0.03 to +0.35 ms, the sky +0.06, and a comet +1.1-1.2 ms while alive. Every stage-8 window is under
  17 ms p50 with p95 ≤ 1.3 x p50.
- Stage-8 CPU work p99 was 3.2-3.8 ms.
- Media: `~/Desktop/av-gen-review/aurora-borealis/final2/`: clips, sheet, comet and pulse frames, the sky comparison,
  the map.
