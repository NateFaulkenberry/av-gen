# Aurora Borealis: progress (scene agent, branch scene/aurora-borealis)

## Where it stands

| Milestone | State |
|---|---|
| A static composition | built: `examples/aurora-borealis/` (generator, land, river, camera loop) |
| B hero aurora | built: ribbons at 48 planes, ADR-1660 coverage, the mix response off |
| C river and light coherence | partial: one palette drives the aurora, the facets and the river glow; E1-E3 are the engine agent's (not merged yet) |
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
