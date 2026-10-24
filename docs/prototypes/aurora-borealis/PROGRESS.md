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
