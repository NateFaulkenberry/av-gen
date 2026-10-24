# Aurora Borealis: progress (scene agent, branch scene/aurora-borealis)

## Where it stands

| Milestone | State |
|---|---|
| A static composition | built: `examples/aurora-borealis/` (generator, land, river, camera loop) |
| B hero aurora | built: ribbons at 48 planes, ADR-1660 coverage, the mix response off. **Stop 1 for owner review** |
| C river and light coherence | mostly: main merged (E1 sky mirror on, E3 pulse routed from kickWave, E2 automatic); one palette drives aurora, facets and river glow |
| D stem integration | built: stems by role, drum feeds, the 02 s3 signals, part knobs at 1; S1, S2, S4-S6 tested |
| E live optimisation and acceptance | not started |

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
