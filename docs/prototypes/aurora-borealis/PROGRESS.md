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

## Pass 3: Sabertooth (stop 1, 2026-10-24)

Brief: `20-brief-sabertooth.md`, `20b-brief-river-light.md`; plan `23-plan-pass3.md`; research `21-` (audio), `22-`
(scene). Owner notes relayed during the pass, applied: slower beams that fade out, more light on the mountains, the
ripples are required; no Worley caustic, a faceted underwater bed instead; beam colours from the scene's own family.

### Audio (step 1)

- **Projects.** `aurora-borealis.json` is Sabertooth, the default: nine stems by key (`stem.<key>.*`), roles Kick
  `drums`, Cloud Diving `synth`, BASS `bass`, the rest `other`, `feeds.beat = stem.kick`. Feline Footwear stays as
  `aurora-borealis-feline.json`: the same scene and signals, driven from its role stems (synth → aurora, drum
  classes → beams, bass notes → left glints). It is the hidden index entry "Sonic VFX - Aurora Borealis (Feline
  Footwear)", the Digital Mosh pattern. The owner's Sonic VFX menu is unchanged; the menu test counts 11 hidden.
- **Signals.** Every transient is the stem's onset into an attack-0 latch with its own refractory. An effect
  trigger reads the raw event, so each `<name>Ev` is the onset gated by its latch's first frames, and respects the
  refractory. A binary threshold before each latch lifts every onset to 1; an onset's value is its strength, and the
  first build's 0.9 gate dropped weak hits. No classifier, no `macro.*`. Transients are stem-only; the aurora keeps
  a mix fallback.
- **Verified against A's lists** (`--sonic-trace` at 240 fps with one-frame probe latches on each event; the trace
  row is written after the bus clears its events). Exact, no extras: **12 crashes** (+0.1 ms), **121 kicks**,
  **48 snares**, **28 tom notes**, **176 tone-block accents**. A's list has 121 kicks, the downbeat at t = 0
  included. The crashes go 8 to star A and 4 to star B. The integration test holds all of this.
- **The re-bounced Moog (19:42):** at lag 0 in all ten 7 s windows against the mix (correlation 0.14-0.34;
  `pass3-impl/moog/winlag-out.txt`); different from BASS.wav, at half its RMS. It is 23 long notes, each a hard
  attack and a slow decay, at least 1.04 s apart: not v1's plucks. A's catch-all (rms trend 0.25 s/15 ms, ×1.6)
  catches all 23 at -3.3 ms. With A's 150 ms latch it adds 3 re-steps 0.18-0.24 s after a note; a **300 ms
  refractory** removes them with no loss. Plot: `pass3-impl/moog/moog-notes.png`. The audio "note starts" my
  script lists beyond the 23 are noise blips in the decays (checked on the plot).
- **Moog subset.** An rms rise is a level, not an event, so no per-note random pick exists (onset-gated picks scored
  recall 0.32 in v1). The left comb's offset glides on a slow saw (`lfo.moogDrift`, 0.137 Hz), so notes 1.4 s
  apart light different scatterings. The toms pick a new subset per note (`random.tomPick`); the tone block steps
  through a one-bar saw.

### Engine (step 2)

- **ADR-1661: 64 material ops (was 48).** Golden pack hash of a 48-op program unchanged from the 48-op build; new
  slots zero.
- **ADR-1662: a wave can travel along a river** (`propagation.kind: "path"`). The arc along the water's own `river`
  feature, away from the camera, eased; an eight-point chain round the front. Drawn as a core or twin + halo + head
  under the reflection, ripples in the water's normal behind the front, land lit facet by facet by orientation, and
  a riverbed below the water lit from the head. `EffectField::written` keeps the Path rows out of every other
  wave's file. Off = byte-identical: the water hashes as a shader with the path blocks removed (plain and
  cinematic); ground pulses and the QA water scene render byte-identical PNGs to the build before (acf387ae).
  Tests: `test_wave_path.cpp`, `test_wave_path_gpu.cpp` (the bend is lit, the straight continuation is not).

### Scene (steps 3-5)

- **River beam.** `riverKick`: magenta-pink core (#ff2f92), 1.6 s over 1.6 km, eased, life 1.4 s with a 0.7 s
  fade. `riverSnare`: pale lilac twin filaments 10 m either side (#e2d2ff), 1.0 s over 1.3 km, life 0.9 s with a
  0.5 s fade. Both:
  - start 160 m out, fade in over 500 m;
  - ripples (`disturb` 0.045 / 0.035);
  - land light across 700 / 600 m and up 250 / 200 m;
  - a bed share 2.0 / 1.6.

  The pass-2 perpendicular pulses are gone.
- **The faceted riverbed.** `ab-bed-coarse.glb` (about 50 m facets, 3,040 triangles, chosen) or `ab-bed-fine.glb`
  (about 30 m, 7,680). Rows across the river, jittered in plan and ±7 m in depth, -9 to -21 m. A near-black program
  (`abBed`), so it shows only when the beam lights it, through the water: transmission 0.7, absorption
  0.035/0.03/0.022 per metre, refraction 1.5 m, so the ripples wobble it. The terrain bed moved to -40 m. A plain
  overhead light read as a glow; lit from the beam's head, a grazing light, the planes separate.
- **Sky.** Cloud Diving: intensity +1.6, height +250, width +0.012, swirl +0.6, filaments, edge, airglow (fast,
  300/1200); coverage +0.25 on a 0.75 base and the palette (slow, 1500/4000). The drum and bass routes into the
  aurora are cut. Crash: two fast comets (0.45 s, 900 m tail, no wisps), environment response 0.4 s (aurora +0.3,
  river glow +0.3, crest flare 0.2: at 0.7 it whitened the ranges). **Ribbons raised 600 → 1000 m:** a near sheet
  hung over the camera as a large pink wedge at the top right during swells. It was there in pass 2, smaller; the
  swell enlarged it. Owner to confirm the look.
- **Glints.** The valley is split by bank: `ab-valleyL.glb` (inner, always left) with `abRockL` (57 ops), and
  `ab-valleyR.glb` plus the three ranges with `abRockR` (64 ops). The bake now carries v = the crease's own hash;
  importance went into the line width. Moog: pale pink edge glints, 3 teeth. Tone block: lavender-white edge
  glints, 5 teeth, stepping through the bar. Toms: blue-violet plane flashes, 2 teeth, sized. A crash flares both
  banks' crests. The haze and line fade are now linear ramps on metres (one op each, not a decametre smoothstep).
- **Routes:** 29 live, 0 dead (`--audit-routes`).

### Stop 1 media: `~/Desktop/av-gen-review/aurora-borealis/pass3-stop1/`

All rendered by one build (the commit before this entry).

| File | What it shows |
|---|---|
| `01-beam-compare-2x2-test-events-10-16s.mp4` | Deterministic events, kick and snare together every 1.4 s. Panels: (1) the path beam alone (stage 1); (2) chosen: ripples + mountain light + coarse bed; (3) the same with the fine bed; (4) as 2 with more mountain light. The beam follows the left bend |
| `02-beam-underwater-bed-closeup-coarse-vs-fine-quarter-speed.mp4` | A close-up of the near water, like the owner's crop, at quarter speed: the faceted bed revealed under the beam, coarse against fine |
| `03-beam-colour-pairs-kick-snare-together-10-16s.mp4` | Three pairs side by side, kick and snare firing together: A magenta-pink + pale lilac (recommended), B deep rose + bright orchid, C fuchsia + lavender |
| `04-beam-on-sabertooth-10-16s.mp4` | The chosen beam on the real stems, with sound |
| `05-beam-on-sabertooth-busy-35-41s.mp4` | A busy section (crashes, a tom fill, kick pickups), with sound; the beam follows the right-hand curve |
| `06-sky-swells-crashes-22-27s.mp4` | A Cloud Diving swell and three crashes (23.16, 24.57, 25.26 s): fast stars, reflected in the river, with the crest flare; with sound |
| `07-sky-swells-crashes-45-49s.mp4` | The 45.6 / 47.0 / 47.7 s crash group across a swell's start; with sound |
| `08-glints-moog-left-toms-toneblock-right-40-48s.mp4` | Moog notes at 41.4, 42.8, 47.7 s on the left; the tone block and the 41.8 and 46-47.5 s tom fills on the right; with sound |
| `09-glints-test-events-moog-1.4Hz-toms-0.7Hz-block-2.8Hz-30-36s.mp4` | The glints on deterministic test events: left pink, right lavender-white lines and blue-violet planes |
| `10-full-scene-23-49s.mp4` | 26 s of the whole scene with sound |
| `11-headless-ultra-ab.txt` | The headless Ultra A/B below, every run |

### Headless Ultra A/B (stop 1)

Conditions: 1920x1080, target 30, 90 warm-up frames, 4 s measured, AC power, one build (0bb1aff0), 3 interleaved
reps. Before = pass 2's scene (b6e572c2, regenerated in an untracked folder) on Feline Footwear; after = pass 3 on
Sabertooth, each at the same song second. GPU p50, ms; median of 3:

| t | pass 2 | pass 3 | Δ | pass 3 GPU p95 range | CPU work p50 (2 → 3) |
|---|---|---|---|---|---|
| 12 s (beams, a swell) | 12.27 | 12.01 | −0.26 | 15.6-16.9 | 1.84 → 2.18 |
| 36 s (crashes, tom fill, beams) | 11.73 | 11.87 | +0.14 | 15.0-16.7 | 1.81 → 2.11 |
| 47 s (crash group, tom fill, Moog) | 12.94 | 12.00 | −0.94 | 14.4-16.4 | 1.86 → 2.16 |

The pass-2 comet (+1.1-1.2 ms while alive) is gone, and the beams, bed, stars and glints together cost about what
it did. CPU work is +0.3 ms p50: nine stems' signal chains and the path resolution. Everything sits well inside
33.3 ms. Live acceptance on the 60 Hz LG is owed after the owner's review, as planned.

### Open for the owner at stop 1

1. **The beam** (01-05): the chosen v4-style beam with the faceted bed, coarse or fine, and the colour pair (A
   recommended). Speed and fades are tuned to Sabertooth: the kick takes 1.6 s, fading from 0.7 s; the snare 1.0 s,
   fading from 0.4 s. Stage 3's sparse upward rays (E3) are not built; the ripples (stage 3's other half) are in.
2. **The ribbons at 1000 m** (was 600) to remove the near pink wedge: does the aurora still read as before?
3. **The bed is subtle**: it shows only in the near water (further out the reflection covers it, by design). It
   could be stronger at the cost of more launch flash.

## Pass 3: final (2026-10-25)

The owner's stop-1 decisions: colour pair A (magenta-pink kick #ff2f92, pale lilac snare #e2d2ff) and the coarse
bed; the aurora as it is (ribbons at 1000 m); the bed stronger, with the launch inside the flash gate.

### After stop 1

- **The bed, stronger:** bed share 3.5 (kick) / 2.8 (snare); 2.0 / 1.6 at stop 1. The land and bed light now also wait
  until the front has travelled past the near fade. Lit from a head just launched, the near bed washed the bottom of
  the frame pale lilac (the 2.8 s crash with a kick and snare). Lit after the head has moved on, the bed behind it
  reads as magenta planes.
- **Flash gate:** at the first full-song audit it failed, 0.054 in 267 ms at 24.4 s: a crash on a kick, the crash's
  river glow (0.3) on top of the beam and bed. Cuts:
  - the crash's river glow 0.3 → 0.1;
  - the stars' ground term 0.4 → 0.15;
  - the launch fade above.

  **Final, the whole song (0-70.18 s): worst 267 ms swing 0.025 at 24.37 s** (gate 0.05); frame-to-frame max 0.009;
  0 WCAG-style general flashes. Tool: `pass3-impl/scripts/flash.py` (4x4 tiles, ≥ 0.10 over ≥ 25% of the frame).
- **E3 (sparse upward rays): not built.** The beam already reads as light moving through the water: a core under the
  reflection, the ripples breaking the mirror behind it, the lit riverbed planes, and the banks' facets taking its
  colour. Rays would add vertical spikes along the river on every kick, 1.7 a second, which is the field of spikes
  the brief rules out. They would cost an M-sized engine change (Beam shells spawned per front). They do not earn
  their place.
- **Bass: tried, not assigned.** I tried `stem.bass.rms` (150/800 ms, gain 3) onto the water's flow streaks (+0.7 over
  a 0.8 base; B §8's zero-op candidate), on against off at 2-8 s. The difference was not legible at the performance
  camera (the streaks are sub-pixel lanes at that distance), so by the brief's rule 3 it stays off. The aurora's
  colour stays Cloud Diving's.
- **HH: not assigned.** 374 closed hats at 5.3 a second are the "continuous stream of flashes" the brief forbids.
  The 29 once-a-bar open hats are legible, but a detector would need to know the hat's rms 100 ms after its onset:
  that is a 100 ms-late accent, or look-ahead the live path does not have. A water micro-sparkle adds nothing the
  ripples and glints do not already say.
- **Tools:**
  - `tools/live_perf_baseline.py` runs Aurora on its own song: Sabertooth from 12 s, so the 5 s warm-up and 30 s
    measure fall inside the 70 s song (`SCENE_AUDIO`).
  - `tools/sound_to_light.py` reports the pass-3 onset paths (`signal.kickBeam`, `snareBeam`, `crashStar`).
  - `tools/sound_to_light.sh` takes `INPUT_SOURCES`, so a keyed scene gets its stems.
  - `docs/perf/live-baseline.json`: the aurora entry is replaced with this run, with a note (below).

### Final measurements

- **Headless Ultra A/B** (same build, 3 interleaved reps, AC, GPU p50 median, ms):

  | t | pass 2 | pass 3 |
  |---|---|---|
  | 12 s | 14.32 | 11.89 |
  | 36 s | 13.43 | 11.95 |
  | 47 s | 12.87 | 11.84 |

  Pass 3's GPU p95 is 14.1-17.6 ms and its max 27.8 (one frame). CPU work p50 is 2.1-2.4 ms, against 1.8-2.7 for
  pass 2. The pass-2 folder is removed.
- **Live acceptance: T1/T2 not met.** On the LG UltraFine at 60.00 Hz (verified: the only and main display; every
  run's `refreshMs` 16.667), AC power, live Auto:
  - misses per 900: 12 / 9 / 5, median 9 (T1 ≤ 5);
  - frame p99: 41.8 / 41.7 / 40.8 ms (T2 ≤ 40);
  - Ultra throughout (T3 ok);
  - headless GPU p50 15.3 ms (T4 ok).

  **A same-day control** ran the pass-2 scene (b6e572c2) interleaved, with its own pass-2 arguments: 9 and 6 misses,
  p99 41.6 and 41.4 ms, live GPU p50 19.5-19.8 ms. Its committed baseline is 3 misses at p99 39.2. Pass 3 in the same
  session: 11 and 17 misses, p99 41.8.

  The whole machine runs about 7 ms slower live today than when the baseline was taken (live GPU p50 18-20 ms against
  headless 12-15). The scene is at parity with pass 2 here, within the run-to-run spread. No thermal or power warning,
  no competing process. A re-run on a fresh boot is the open item.
- **Sound to light** (BlackHole loopback, 80 probe transients, `INPUT_SOURCES=kick@2:drums,snare@2:other,crash@2:other`):
  - `signal.snareBeam`: 20/20, light p50 67 ms, p95 79 ms.
  - `signal.crashStar`: 61/80 across families, light p50 70 ms, p95 84 ms.
  - `signal.kickBeam`: 3/20. The live stem-onset detector barely sees the probe's synthetic kick, though the real Kick
    stem is caught 121/121 offline. `response.onset` caught all 80 at 70 / 85 ms.
  - The run's live GPU p50 was 27.9 ms, the same slow machine state as above.

### Final media: `~/Desktop/av-gen-review/aurora-borealis/pass3-final/`

Cut from one full-song render (01) by the final build, except the stop-1 references.

| File | What it shows |
|---|---|
| `01-full-song-sabertooth-0-70s.mp4` | the whole song, with sound |
| `02-river-beams-10-16s.mp4` | the kick and snare beams down the river on Sabertooth, with sound |
| `03-river-beams-busy-35-41s.mp4` | a busy section: crashes, a tom fill, kick pickups; the beam follows the right-hand curve |
| `04-sky-swell-crashes-22-27s.mp4` | a Cloud Diving swell and the 23.2 / 24.6 / 25.3 s crashes: fast stars, reflected, the crest flare |
| `05-sky-crash-group-45-49s.mp4` | the 45.6 / 47.0 / 47.7 s crash group |
| `06-glints-moog-left-toms-toneblock-right-40-48s.mp4` | Moog notes (41.4, 42.8, 47.7 s) on the left; tone block and tom fills on the right |
| `07-crash-group-and-swell-56-61s.mp4` | the 56.8 / 58.3 / 59.0 s crashes across a swell's end |
| `08-ref-stop1-beam-compare-2x2-test-events.mp4` | stop-1 reference: the beam's stages and variants on deterministic events |
| `09-ref-stop1-beam-colour-pairs.mp4` | stop-1 reference: the three colour pairs (A chosen) |
| `10-ref-stop1-glints-test-events.mp4` | stop-1 reference: the glints on deterministic test events |
| `11-contact-sheet-every-5s.png` | a frame every 5 s of 01 |
| `12-moog-notes-detector.png` | the re-bounced Moog's level, its note starts and the detector's 23 fires |
| `13-riverbed-closeup-quarter-speed-2.6-5s.mp4` | the near water at quarter speed: the bed's planes revealed behind the head |
| `14-flash-audit-full-song.txt` | the flash audit of 01 |
| `15-headless-ultra-ab-final.txt` | every headless A/B run |
| `16-sound-to-light.txt` | the sound-to-light table |
| `17-live-acceptance.txt` | the live runs and the control |

### Pass 3 suites on bd0a44ca (recorded by the coordinator)
`run-suite.sh gpu` (bare): PASS, exit 0, 704 passed / 0 failed / 1 skipped, 47 min 41 s. `gpu-lock.sh run-suite.sh cpu-assets`: PASS, exit 0, 4,470 passed / 0 failed / 29 skipped / 1 expected failure, 18 min 19 s. The first cpu-assets run failed one case (the registry round-trip test expected path rows back from a non-path wave, which `EffectField::written` leaves out by design); fixed in bd0a44ca. Logs: `~/Desktop/av-gen-review/aurora-borealis/pass3-impl/suite-gpu/` and `suite-cpu-assets2/`.

## Pass 4: the owner's notes (stop, 2026-10-25)

Notes: `30-owner-notes-pass4.md`, plus later notes relayed by the coordinator: the snare as one beam; bigger,
individual fish; the valley itself straight; the shoreline seam; a smoother aurora; fireballs; the Moog's trills
running down the mountains; the right range on BASS.

### What changed

- **A straight valley.** The world is still generated round the loop, then unrolled (`world.unroll`): distance round
  the loop becomes distance down a straight axis, and radius becomes the lateral offset. The loop's triangle term is
  gone, and the bends are 12-26 m. The camera flies straight down -z for 4 km (182 s), then starts again. The seam is
  5.4 km beyond the run's end, never in view. The left side gains far and distant ranges, on the left program.
  - **Measured:** within 2 km ahead of the camera, the river's heading changes at most **5.7°** (pass 3: 46-66°). All
    of it is within 3° of the axis; 80% within 2° and 63% within 1°.
- **The shoreline seam.** Isolated in one frame from one build: hiding the bed changed nothing; hiding the left bank
  removed it. Both banks still carried a row 30 m into the river, 4-8 m down, left over from the layout where the
  mountains ran into the water. Its edge stood above the bed and showed through the water. The rows are gone. The bed's
  edges are now the banks' waterline rows, at their height, so bed and bank share an edge.
- **The river light.** Three lanes: kick 55 m left of centre, snare 55 m right, each ±25 m per hit; toms (a third
  colour, violet #8a5cff) anywhere across ±90 m. All three are one core (the snare's twin read as two beams). Each has a
  launch splash and a heavier wake near the camera (ADR-1663). After the first take left the near water permanently
  churned, both were cut to about half.
- **Fireballs** replace the stars (ADR-1663, a wave kind; the land is unlit, so an engine light would reach nothing).
  - They are thrown from behind and above the camera, enter from the near edge large, and arc away over the valley,
    each crash on its own heading (±900 m spread).
  - Look: a white-gold core in a flickering fire-orange corona with a 150 m flaming trail. Fire orange is the
    complement of the violet aurora, so it reads as the most vibrant thing in the frame.
  - They light land, water and riverbed below them. They ignite over 0.6 s; at 0.3 s a ball passing overhead swung the
    frame by 0.10-0.18 in 267 ms.
  - Cost: not resolved from noise with one alive (12.88 vs 12.84 ms GPU p50, 1.5 s windows).
- **The aurora.** The swell drives the sheets' shape on a slow envelope with a second smoothing stage: churn
  (ADR-1664, a phase routable without E4's jump), swirl, fold depth, width and height. Brightness has its own faster
  pair of stages. The swell-start pulse is gone.
  - **Measured, inside the swells at 30 fps:** every routed aurora final now has a per-frame step p99 ≤ 3% of its range
    and a jerk p99 (the frame-to-frame change of the step) ≤ 0.3%. Before: up to 4.7% step and 2.4% jerk; the pulse
    jumped 100%. Shape parameters: 1.4% step, 0.09% jerk. File: `17-aurora-smoothness-before-after.txt`.
  - **The ribbons stand at 1500 m.** With the stronger swirl a near sheet hung over the camera at 1000 m.
- **The Moog (left) and the BASS (right) run down their ranges.**
  - **Moog:** fires on every onset of its stem (346, none in its opening silence). Each note lights a fresh subset of
    crease glints, sized by the stem's level.
  - **Bass:** fires on its stem's onset with a 100 ms refractory: 215 fires, recall 0.55 / precision 0.91 against its
    level rises. An rms rise did worse: x1.3 gave 0.50 / 0.87, x1.6 gave 0.27 / 0.83, and onset-or-rise added nothing.
    It lights a fresh set of right-bank planes per note, blue-violet.
  - **The sweep:** each program lights only inside a 450 m band of distance from the camera. The band runs from the
    camera 3.6 km down the valley in 1 s after a note, and does not restart until it has run, so a trill reads as sweep
    after sweep with its glints re-picked on every note. Programs: left 64 ops, right 60.
  - **The first approach was dropped:** path-wave fronts in lanes among the mountains, too faint on the walls. Since
    then the land term's light comes level from the path, raised toward each surface's height, so valley walls take the
    river beams squarely too.
- **The tone block is unassigned.** Its signals remain, unrouted. The toms keep only their river lane.
- **Fish:** 64 individual faceted fish, 12-17 m (stylised, to read from 65 m up), 2-4 m down across the river's width.
  A quarter swim downstream, a quarter upstream, the rest across or angled. They share the bed's near-black program.
  Shallow bodies under a beam now take its light evenly, whatever their facing, so a fish lights as a whole instead of
  showing as a silhouette.

### Measurements

- **Flash gate**, the whole song: worst 267 ms swing **0.044** at 24.4 s (gate 0.05); 0 WCAG-style general flashes.
- **Headless Ultra, pass 3 (bd0a44ca, regenerated, untracked) vs pass 4, same build, GPU p50 medians:** 12.04 vs 12.67
  ms at 12 s; 12.01 vs 12.77 at 36 s; 12.02 vs 12.98 at 47 s.
  - **These were taken on BATTERY** (the Mac had come off AC). They are indicative only, and pass 4 is about 0.7-1 ms
    above pass 3. Repeat on AC before acceptance.
- **Live acceptance and the suites:** after the owner's approval, on a fresh reboot.

### Pass-4 stop media: `~/Desktop/av-gen-review/aurora-borealis/pass4/`

| File | What it shows |
|---|---|
| `00a-straight-valley-measurements.txt` | the river's heading change ahead of the camera, and the share within 1/2/3/5° of straight, before and after |
| `00b-top-down-map-straight-valley.png` | top-down map of the straight valley: river, left/right peaks, the camera's run and the song's stretch |
| `00c-top-down-map-before-pass3-loop.png` | the pass-3 loop, for comparison |
| `00d-before-after-same-times-6-21-36-51-66s-top-pass3-bottom-pass4.png` | the same song times from the same build: pass 3 above, pass 4 below |
| `00e-shoreline-before-left-after-right-top-left-bank-bottom-right-bank.png` | the shoreline before and after removing the under-water bank rows, both banks |
| `00f-straight-valley-left-mountains-visible-6-21-36-51-66s.png` | five frames down the valley, the left mountains in view |
| `01-straight-river-0-10s.mp4` | the straight river, with sound |
| `02-moog-trills-sweeping-down-left-range-with-bass-right-18-25s.mp4` | the densest Moog passage: glint sweeps down the left range, bass planes on the right, with sound |
| `03-moog-trills-left-closeup-18-25s.mp4` | the left range close up, the same passage |
| `04-fireballs-crash-group-45-49s.mp4` | the 45.6 / 47.0 / 47.7 s crashes as fireballs, with sound |
| `05-fireballs-crash-group-22-27s.mp4` | the 23.2 / 24.6 / 25.3 s crashes |
| `06-near-camera-splashes-10-16s.mp4` | launch splashes and the heavier near wake, with sound |
| `07-near-camera-splashes-closeup-quarter-speed-10-12s.mp4` | the near water at quarter speed |
| `08-wild-aurora-swell-before-twitchy-left-after-smooth-right-25-31s.mp4` | a swell, the first pass-4 drive (left) against the smoothed one (right), same build |
| `09-wild-aurora-swell-36-42s.mp4` | the smoothed aurora on another swell |
| `10-three-lanes-test-events-kick-snare-toms-8-16s.mp4` | kick, snare and toms on deterministic events, each in its lane |
| `11-three-lanes-on-sabertooth-tom-fill-23-28s.mp4` | the three lanes on the real stems, a tom fill |
| `12-fish-lit-by-beams-closeup-quarter-speed.mp4` | fish lit as beams pass under the water |
| `13-bass-right-moog-left-40-48s.mp4` | a bass-heavy passage with the Moog: right planes and left glints together, with sound |
| `14-full-song-0-70s-1080p.mp4` | the whole song, with sound (98 MB) |
| `15-full-song-0-70s-720p-small.mp4` | the same at 720p, 16 MB |
| `16-flash-audit-full-song.txt` | the flash audit |
| `17-aurora-smoothness-before-after.txt` | step and jerk p99 of every routed aurora final, before and after |
| `18-bass-detector.txt` | the bass detector candidates' recall and precision |
| `19-headless-ultra-ab-pass3-vs-pass4-and-fireball-cost-ON-BATTERY.txt` | every headless run (battery: indicative only) |

## Pass 5: the lake (stop, 2026-10-25)

Notes: `40-owner-notes-pass5.md` (verbatim). The coordinator broke them into six items.

### What changed

- **The river is now a lake in a horseshoe of mountains, seen from a static camera** (`world.py`, `build.py`).
  - **The lake:** an ellipse 1.04 km across and 2.3 km deep. Its near end passes just behind the camera, at the origin
    and 110 m up, looking down -z, pitched down 1 degree. There are no timeline tracks.
  - **The land:** the river valley's own recipe, now wrapped round the lake's centre in polar coordinates. It keeps the
    shore profile, ridged detail, spurs, three layers of peaks and the near, far and distant rings.
  - **The walls** come down closer to the water than the river's valley floor did: 90-210 m, not 200-420 m.
  - **The hero peaks** are placed by where they stand in the frame, by bearing from the camera. They are tallest at
    the frame's edges (15-17.5 degrees above the water) and lower at the back (8-9.5 degrees), so the horseshoe reads
    with its open side toward the camera.
  - **The banks:** every land mesh is split in halves. x < 0 is the Moog's left bank and x > 0 is the bass's right
    bank. The two banks' recipes blend across the back, so they meet without a step.
  - **Composition:** the lake sits in the lower middle, the arms fill the frame's sides, the back closes the view, and
    the aurora takes the top.
  - **First take:** the camera stood 48 m up and pitched up 7 degrees. The sky mirror ran off the top of the frame and
    left a black band at its bottom. From that height the 2 km lake was also a thin strip, so the pulses showed only by
    the far shore.
- **The pulses on the lake** (`look.py`):
  - **Lanes:** kick 150 m left of the lake's axis and snare 150 m right, each jittered +-50 m; toms anywhere within
    +-220 m.
  - **Travel:** each runs from 70 m in front of the camera out to the far shore (1.9-1.95 km) and fades in over 150 m.
    The river's 160 m and 500 m left only the strip by the far shore lit.
  - **Size:** cores and halos are half as wide again as the river's. Splashes and the near wake are kept.
  - **Spill onto the mountains:** cut to a quarter (`land` 0.09/0.06/0.075, was 0.35/0.25/0.30). The light in the
    water and on the bed is unchanged.
- **A deeper lake:**
  - **The bed:** a shelf falls from the shore to about 45 m at 500 m out, then a floor at 55-65 m. Pass 4's bed was
    about 20 m.
  - **Absorption** is twice pass 4's.
  - **The bed's program** (`abBed`) adds a faint blue-violet that fades to black between 3 and 38 m down: the shelves
    show and the middle drops away.
  - **The land term** only lights the bed down to 40 m, so a pulse loses the bed as it crosses into deep water.
  - The surface keeps pass 4's mirror.
  - **Honest note:** the difference reads only where the surface is not reflecting the bright aurora (clips 04/05). Most
    of the near lake is mirror.
- **The fish** (`world.py`):
  - **Fewer and varied:** 18 (were 64), of four body types: trout, deep-bodied bream with a dorsal fin, long pike, and a
    flat ray. Each is 7-18 m by type, on its own heading in any direction, and placed near the three lanes so the light
    finds them.
  - **None breaks the surface:** the fish are static (no swim motion), and each is clamped so its highest vertex (back,
    fin or tail tip) is at least 1.5 m down.
  - **Checks:** `world.check_fish()` runs in `generate()`, and the integration test runs `world.py --check-fish`.
  - **The cause:** in pass 4 the bodies' centres sat 2-4 m down but their backs reached 3.7 m above their centres.
  - **Lighting:** the fish now have their own near-black program (`abFish`), so the bed's shelf tint does not light
    them.
- **The fireball** (ADR-1665; research in `41-research-pass5-fireball.md`, with sources):
  - **Look 1, the flame billboard**, was chosen from three looks compared on the same build.
  - **What it is:** domain-warped fbm tongues, keyed in the ball's own 3D frame, boiling and swept back along the
    flight. A blackbody ramp runs white-yellow, orange, deep red, then soot. A sooty fringe dims the aurora behind it,
    and a string of puffs makes a trail cooling to smoke, with embers.
  - **Light below:** it still lights the land and water below.
  - **Look 0** is byte-identical (unit test).
  - **Flash gate:** corona 2.0 and the crash's aurora flare at 0.1 (was 0.15) keep the 47 s crash group inside it.
- **The Moog (left) and bass (right) on the mountains** (`look.py`, `music.py`):
  - **More sensitive:** the onsets' refractories are shorter (35 ms and 60 ms; the detector's own 60 ms floor binds for
    the Moog). Softer notes are now caught too: the stem's onset strength, gated at 0.9 of the detector's line, latched
    on each rise. `<part>Strike` is the larger of the two signals.
  - **Sustain:** `<part>Hold` is the stem's rms, up in 5 ms and down in 150 ms (Moog) or 220 ms (bass). It holds the
    picked subset's gate open while a note rings and closes it as the note decays.
  - **More visible:**
    - Each note lights its picked creases as wide lines (7 px Moog, 5 px bass), widened further by the strike.
    - It fills the facets beside them as planes: a 0.24 share for the Moog, 0.9 for the bass.
    - The sustained level lights every ridgeline on the bank (`edgeGlow`).
    - A fresh subset is picked per note, as before.
  - **The travelling band of pass 4 is gone,** as the owner allowed.
  - **Programs:** 59 ops each.
- **Tone block:** still unassigned. Toms keep their lane.

### Measurements

All from the final build, ee7f85a6, using the same build for every comparison.

- **Notes caught** (`19-...`): reference note starts come from each stem's own audio (spectral-flux peaks, independent
  of the engine's detector).

  | Stem | Note starts | Version | Fires | Recall | Precision |
  |---|---|---|---|---|---|
  | Moog | 339 | pass 4 | 346 | 0.88 | 0.86 |
  | Moog | 339 | pass 5 | 445 | 0.94 | 0.75 |
  | Bass | 224 | pass 4 | 215 | 0.92 | 0.95 |
  | Bass | 224 | pass 5 | 225 | 0.94 | 0.94 |

  The reference is itself a heuristic: some of the extra Moog fires are real soft notes it does not call.
- **Visibility per note** (`20-...`): the share of the note's own bank's mountain pixels that brighten.

  | Part | Median | p90 | Notes lighting at least 5% | Other bank (median) |
  |---|---|---|---|---|
  | Moog | 35% | 54% | 98% | 5% |
  | Bass | 41% | 50% | 98% | 11% |

  There is no pass-4 figure: its program does not fit the lake's camera.
- **Mountain spill** (`21-...`): pass 4's land shares against pass 5's on the slopes over the water. The frame-by-frame
  difference is mean 0.005 and p95 0.016 linear luminance, which is the three quarters removed.
- **Flash gate**, whole song: worst swing 0.042 at 47.6 s; 0 general flashes.
- **Fish:** the highest vertex of any fish is 1.50 m below the surface (`17-...`).
- **Headless Ultra 1080p, on AC**, pass 4 (f0fb48dd, regenerated) against pass 5, the same build, 2 interleaved reps.
  - GPU p50: 13.8 -> 12.5 ms at 12 s, 13.6 -> 12.8 ms at 36 s, 13.7 -> 12.9 ms at 47 s (two fireballs alive).
  - The fireball look's own cost is within the noise. CPU work p50 is 1.5-1.9 ms.

### Open at the stop

- **The fireballs fly into the back mountains:** they are aimed 2.5 km out at 230-250 m, and the lake's back ridges
  stand 300-500 m at 2.5-3.5 km, so each disappears behind a ridge near the end of its flight. Raising the targets is a
  one-line change if the owner wants them to clear the ridges.
- **Depth is subtle:** the deeper lake reads mostly as darker water where the mirror is dark; the mirror is unchanged.
- **The Moog's extra soft notes** carry some false fires (precision 0.75), and the flash gate has 0.008 of headroom.
- **Live acceptance and the suites** wait for the owner's approval.

### Pass-5 stop media: `~/Desktop/av-gen-review/aurora-borealis/pass5/`

| File | What it shows |
|---|---|
| `00a-lake-composition-still-30s.png` | the composition: lake lower middle, horseshoe left, back and right, aurora above |
| `00b-top-down-map-lake-horseshoe-lanes-fish.png` | top down: the lake, its bed depth, the shore band and near range, the view, the three lanes, the fish |
| `01-lake-composition-static-camera-26-34s.mp4` | the static composition, with sound |
| `02-pulses-on-the-lake-kick-snare-toms-8-16s.mp4` | kick, snare and toms crossing the lake in their lanes, with sound |
| `03-pulses-on-the-lake-closeup-quarter-speed-10-13s.mp4` | the lake's middle at quarter speed: cores, splashes, wakes |
| `04-deeper-lake-before-left-after-right-8-16s.mp4` | pass 4's depth and absorption (left) against the deeper lake (right), same build |
| `05-deeper-lake-closeup-before-top-after-bottom-half-speed.mp4` | the same, the lake cropped, half speed |
| `06-fish-lit-by-the-pulses-closeup-quarter-speed-10-14s.mp4` | fish of different shapes lit as the pulses pass |
| `07-fireball-variants-2x2-same-build-crash-group-44.5-49s.mp4` | A pass 4's glow, B the chosen flame billboard, C volumetric-lite, D volumetric-lite with embers and a smoke trail, with sound |
| `08-fireball-variants-closeups-A-B-C-D-left-to-right-at-46.0-46.4-46.9-47.7s.png` | the four close up, four moments |
| `09-mountain-spill-from-the-pulses-before-left-after-right-8-16s.mp4` | pass 4's spill on the mountains (left) against a quarter of it (right) |
| `10-moog-busy-trills-left-bass-right-18.5-24.5s.mp4` | the densest Moog trills on the left bank, the bass on the right, with sound |
| `11-moog-sustain-holding-and-decaying-54-60s.mp4` | Moog notes held and decaying (56.7-58.5 s), then busy bass, with sound |
| `12-bass-heavy-passage-with-moog-35.5-42s.mp4` | a bass-heavy passage with the Moog, with sound |
| `13-chosen-fireball-in-the-song-crash-group-44.5-49s.mp4` | the chosen fireballs on the 45.6/47.0/47.7 s crashes, with sound |
| `14-chosen-fireball-closeups-at-46.0-46.4-46.9-47.8s.png` | the chosen fireball close up |
| `15-full-song-0-70s-1080p.mp4` | the whole song, with sound (117 MB) |
| `16-full-song-0-70s-720p-small.mp4` | the same at 720p, 20 MB |
| `17-fish-below-the-surface-check.txt` | every fish and its highest vertex |
| `18-flash-audit-full-song.txt` | the flash audit |
| `19-moog-bass-notes-caught-before-after.txt` | notes caught, pass 4 against pass 5 |
| `20-mountain-visibility-per-note.txt` | the share of each bank lit per note |
| `21-mountain-spill-before-after.txt` | the spill measured |
| `22-headless-ultra-ab-pass4-vs-pass5.txt` | headless Ultra 1080p, pass 4 against pass 5, the same build |

## Pass 6: back to the river (stop, 2026-10-25)

The owner's notes are at the end of `40-owner-notes-pass5.md`. The lake is retired.

- **Main merged** (daf93949, the mix kick/snare/hat detection retired).
  - The only conflict, `tools/sound_to_light.py`, was resolved by keeping both: main's `signal.beatPop` path and the
    AB paths.
  - `aurora-tundra.json` merged cleanly and keeps `ribbonCoverage`.
  - The Feline project reads none of the retired signals. CMake was reconfigured.

### What changed

- **A gently curving river.**
  - `world.py` is pass 4's unrolled straight valley again, with 45-75 m bends over 390-600 m half-lengths. Pass 4 had
    12-26 m.
  - Within 2 km ahead of the camera the river turns at most 14 degrees. Pass 4 turned 5.7 and pass 3's loop 46-66.
  - It is at most 7 degrees and 72 m off the axis, which keeps both ranges in view.
  - The camera flies pass 4's straight 4 km run.
- **Simpler mountains.**
  - The bank band has about 60% of pass 4's columns and rows.
  - Detail is two broad ridged octaves at 260-320 m with less amplitude; pass 4 had three octaves at 95-260 m.
  - The peaks' crest noise is one octave at 320 m.
  - There are 80 spurs (were 140), and the ranges have about 55% of the columns and three quarters of the rings.
  - The result is larger planes and longer, cleaner ridgelines. A coarser first try, with 9 rows, flattened the peaks
    into hills.
- **The sides are swapped:** the left bank is the bass and the right bank is the Moog.
- **The mountain effect** is pass 5's: strike and soft notes, a sustain that follows the level, wide lines, facet fills
  and ridgeline glow. Its light now runs down the range.
  - Each bank's glint term is lit only inside bands that always flow away from the camera.
  - The bands are a comb over the lines' distance fade, three across 0.4-4.5 km and about 1.4 km apart. A saw LFO
    (`lfo.rangeFlow`, 1 Hz) advances its phase, so a band runs 1.4 km a second.
  - A note lights whatever bands are running, and the sustain keeps them lit as they run. Every note, including each
    trill note, reads as light travelling down the range.
  - The programs are 62 ops each.
  - **Rejected, two takes:**
    - Lighting everything nearer than a front blanked the bank and lit it again on each note: 0.052 on the flash gate.
    - A single band restarting per note had to run its length before the next could start. Only 67 of 445 Moog notes
      sent one, and the share of the bank that brightened per note fell from 35% to 8-10%.
- **Carried over:**
  - The fireball (ADR-1665), now aimed 560-600 m up (was 230-250), so it clears the ridges.
  - The fish: 18 of four types, at least 1.5 m under, checked.
  - The deeper water: a bed about 50 m deep mid-river, twice the absorption, and the shelf tint.
  - Pass 4's three lanes with pass 5's quarter spill.

### Measurements

All from 505ce9bc, the same build for every comparison.

- **Notes caught** (`15-...`): the same signals as pass 5.
  - Moog: every note 0.94 recall at 0.75 precision; onsets 0.88 at 0.86.
  - Bass: 0.94 at 0.94.
- **Visibility per note** (`16-...`), using per-frame masks because the camera moves:
  - **Lit share**, the song against the same song without the mountain instruments. The bass lights a median 37% of the
    left bank and the Moog 39% of the right; notes lighting at least 15% of the bank: 99% and 97%.
  - **Brightening**, pass 5's measure. The bass is 33% and the Moog 31%, against a floor of 19-21%: the bands keep
    running between notes.
- **Spill** (`17-...`): pass 4's spill added a mean 0.003 linear luminance over the mountains and lit 16% of their pixels
  a little brighter. Pass 6 keeps a quarter of it.
- **Flash gate:** worst 0.039; 0 general flashes.
- **Headless Ultra 1080p, on AC**, pass 4 against pass 6:
  - GPU p50: 13.85 / 13.38 ms at 12 s, 13.22 / 13.78 at 36 s, 13.14 / 13.41 at 47 s.
  - The difference is within the run-to-run spread; CPU work is 1.8 ms.
- **Fish:** the highest vertex is 1.50 m down.

### Pass-6 stop media: `~/Desktop/av-gen-review/aurora-borealis/pass6/`

| File | What it shows |
|---|---|
| `00a-river-curvature-measurements.txt` | the curve: heading change ahead, the share near the axis, the swing |
| `00b-top-down-river-pass4-straight-vs-pass6-gently-curving.png` | top down, pass 4 against pass 6 |
| `00c-river-and-simpler-mountains-6-21-36-51-66s.png` | five frames down the river: the simpler mountains, both ranges in view |
| `01-gently-curving-river-simpler-mountains-0-12s.mp4` | the opening, with sound |
| `02-busy-passage-bass-left-moog-right-bands-running-down-the-range-18.5-24.5s.mp4` | the densest Moog and bass, the bands running down both ranges, with sound |
| `03-moog-right-bank-closeup-half-speed-19-22s.mp4` | the Moog's right bank close up, half speed |
| `04-bass-left-bank-closeup-half-speed-19-22s.mp4` | the bass's left bank close up, half speed |
| `05-moog-sustain-holding-and-decaying-54-60s.mp4` | Moog notes held and decaying, then busy bass, with sound |
| `06-bass-heavy-passage-with-moog-35.5-42s.mp4` | a bass-heavy passage with the Moog, with sound |
| `07-fireballs-crash-group-clearing-the-ridges-44.5-49s.mp4` | the fireballs on the 45.6/47.0/47.7 s crashes, now clearing the ridges, with sound |
| `08-fireball-closeups-at-45.9-46.4-46.9-47.8-48.4s.png` | the fireballs close up, in the sky above the ridges |
| `09-three-pulse-lanes-on-the-river-8-16s.mp4` | the three lanes on the river, with sound |
| `10-mountain-spill-before-left-after-right-8-16s.mp4` | pass 4's spill (left) against pass 6's quarter (right), the same build |
| `11-fish-and-deeper-water-closeup-quarter-speed-10-14s.mp4` | fish and the deeper water lit by the pulses, quarter speed |
| `12-full-song-0-70s-1080p.mp4` | the whole song, with sound (117 MB) |
| `13-full-song-0-70s-720p-small.mp4` | the same at 720p, 20 MB |
| `14-flash-audit-full-song.txt` | the flash audit |
| `15-moog-bass-notes-caught.txt` | notes caught |
| `16-mountain-visibility-per-note.txt` | visibility per note, both measures |
| `17-mountain-spill-before-after.txt` | the spill measured |
| `18-fish-below-the-surface-check.txt` | every fish's highest vertex |
| `19-headless-ultra-ab-pass4-vs-pass6.txt` | headless Ultra, pass 4 against pass 6 |

### Open at the stop

- **The light runs down the range all the time, not launched per note.** The bands always flow and each note lights
  them. A true front per note would need several fronts per bank, more op constants than the 64-op ceiling leaves.
- **The Moog's soft notes:** precision 0.75.
- **Live acceptance and the suites** wait for the owner's approval: a fresh reboot, on AC, with the LG at 60 Hz.

## Pass 7: pass 3's landscape with pass 6's effects (stop, 2026-10-25)

The owner, on pass 6: "just tell it to go back to the pass 3 geometry". Then: "but keep the pass 6 effects".

### What changed

- **The landscape is pass 3's, restored from bd0a44ca rather than approximated.**
  - `world.py` is pass 3's generator verbatim: the closed-loop river with its seven mild bends and straight reaches, the
    valley walls, hero peaks, spurs, layered ranges and rock shoulders.
  - The camera (`build.camera_tracks`) is pass 3's path, height, pitch, sway and roll, less the shooting stars' azimuth
    tracks. Pass 3's water node and river feature are back too.
  - **Checked against the generator at bd0a44ca:**
    - The near, far and distant range GLBs are byte-identical.
    - Every one of the bank's 26,600 triangles is one of pass 3's.
    - The river course is identical.
  - **The only things missing are 3,600 triangles of pass 3's -30 m rows**, all under the water (their highest vertex is
    at -1.1 m). That is the bank running on under the river: the shoreline seam the owner found in pass 4. The rows are
    still generated, so every other row keeps its seed, but they are not joined.
- **Added on top:** the deeper bed (its edges on the banks' waterline rows) and the 18 fish.
- **Every pass-6 effect and setting is unchanged:**
  - bass on the left bank, Moog on the right;
  - the bands running down the range, with wide lines, facet fills, ridgeline glow, sustain and soft notes;
  - the three lanes, splashes and wake, at a quarter of the spill;
  - the fireball (ADR-1665), aimed 560-600 m up;
  - the deeper water;
  - the smoothed aurora.
- **What follows the bends:**
  - The lanes, splashes and wake follow the river feature's centreline, which is pass 3's bending course.
  - The fish sit on the lanes round the river's own centre line.
  - The mountain bands are keyed to distance from the camera, not to any axis, so they run down whichever way the valley
    turns.
- **The fireballs:** all twelve crashes were checked in the render, and every ball flies in the sky above pass 3's ridges.

### Measurements

All from 514b40a3, the same build.

- **Flash gate:** worst 0.037; 0 general flashes.
- **Fish:** the highest vertex is 1.50 m down.
- **Notes caught:** unchanged from pass 6. Moog every note 0.94 recall at 0.75 precision; bass 0.94 at 0.94.
- **Lit share per note:** bass median 38.5% of the left bank, Moog 41.4% of the right.
- **Brightening:** bass 36.5%, Moog 31.1%, against a floor of 16-22%.
- **Headless Ultra 1080p, on AC**, pass 6 (regenerated from 505ce9bc) against pass 7, the same build:
  - GPU p50 is about equal: 13.7 / 13.9 ms, 14.1 / 14.0, 13.6 / 14.1.
  - p95 is 1-2 ms higher and CPU work about 0.5 ms higher (2.3-2.9 ms): pass 3's denser land is 52k triangles against 34k.

### Pass-7 stop media: `~/Desktop/av-gen-review/aurora-borealis/pass7/`

| File | What it shows |
|---|---|
| `01-five-frames-down-the-river-6-21-36-51-66s.png` | five frames down pass 3's river |
| `02-frame-at-{6,21,36,51,66}s-1080p.png` | the same five at full size |
| `03-busy-passage-bass-left-moog-right-18.5-24.5s.mp4` | the busy bass and Moog passage, with sound |
| `04-fireballs-crash-group-22-27s.mp4` | the 23.2/24.6/25.3 s crashes, with sound |
| `05-fireballs-crash-group-44.5-49s.mp4` | the 45.6/47.0/47.7 s crashes, with sound |
| `06-twelve-crashes-closeups-left-col-2.8-36.5s-right-col-45.6-59.0s.png` | a crop round each crash's fireball (two crops caught a lit mountain instead; 06b/06c show those two whole) |
| `06b-crash-35.8s-full-frames-36.1-37.3s.png`, `06c-crash-56.8s-full-frames-57.1-58.3s.png` | those two crashes, whole frames |
| `07-full-song-0-70s-720p-small.mp4` | the whole song, 720p, 24 MB, with sound |
| `08-flash-audit-full-song.txt` | the flash audit |
| `09-notes-caught.txt` | notes caught |
| `10-visibility-per-note.txt` | visibility per note |
| `11-fish-below-the-surface-check.txt` | the fish check |
| `12-headless-ultra-ab-pass6-vs-pass7.txt` | headless, pass 6 against pass 7 |
