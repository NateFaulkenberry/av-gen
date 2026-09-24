# ADR-712: The storms re-tuned on the converged march

**Status:** Accepted -- by eye; the owner's eye decides (ADR-441)
**Date:** 2026-09-24
**Resolves:** plan §2 item 4 (the hero's art direction); ADR-710's "the labs and night deliverable need a re-tune"; ADR-711's "re-judge the strength".
**Implemented by:** content only -- `examples/treeisland/tree-of-life-floating-island{.json,.scene.json}`, `examples/labs/tornado-lab.scene.json`, `tornado-modes-{a,b,c,d}-*.scene.json`, `tornado-showcase.scene.json` and its seven regenerated `_tc-*` arms. No shader or engine change.

## Findings

1. **The hero's cyan was a route, not its emission.** The day project routes `state.progress -> fx/cosmic-tornado/emission` with amount 0.08, and `state.progress` is the state machine's transition progress -- 1.0 whenever no transition is running, i.e. nearly always. The storm therefore emitted 0.084, twenty times its authored 0.004. Rendered: emission 0 changes nothing visible; emission 0 AND scattering 0 still shows a bright cyan column; removing the route as well leaves it black. That is ADR-711's "shadowed side is cyan emission": it was this route. It also means `emission` in the day file was never the number the frame used.
2. **On a converged march, optical depth is the form lever.** At the labs' 0.06/m the columns are opaque, so the lit/shadow split and the detail stack both average out (ADR-710's "detail barely shows"). Halving density to 0.03 brings the bands, the shell and the B/C/D noise back into view; the self-shadow (4 @ 0.45) gives the dark storm base and a lit side; a small emission (a third of the density) stands in for the ambient the march does not have, so the wall cloud's underside is dark grey, not ADR-711's black.
3. **The structure tests are also contrast tests.** At emission two-thirds of the density the storms came back pale enough to match the lab sky's luminance: the Tall Column's continuity guard saw the column in 55% of rows, and the debris guard moved 88 px (< 150). At one third both pass (100%, 431 px). The guards were right to fail -- a column the colour of the sky is not legible -- so the ratio shipped is one third.

## Decisions

- **Hero (day):** route amount 0.08 -> 0.006 and `emission` 0.004 -> 0.002 (0.008 in effect); `scattering` 0.15 -> 0.1; `colorThin` (0.28,0.62,0.78) -> (0.2,0.72,0.88), `colorThick` (0.06,0.1,0.3) -> (0.1,0.06,0.32); the wall cloud brought into frame by `base.y` -620 -> -1000, `height` 1400 -> 1150, `cloudWidth` 5 -> 2.6. The route is kept (it is the owner's) at the amount that is the look.
- **Hero (scene, read by night):** `scattering` 0.1, `emission` 0.008, the same two colours; geometry unchanged, since the night camera already frames the wall cloud.
- **Shadow strength stays 0.45.** 0.8 and 1.0 were rendered on the re-tuned hero and barely differ.
- **Debris stays 0 on the hero.** At 0.5 it is a pale lump hanging in space: ADR-706's reason holds.
- **Labs** (lab, modes A-D): density 0.06 -> 0.03, emission 0 -> 0.01, `volumeShadowSteps` 4 / strength 0.45. **Showcase:** the same shadow; density halved except the rope and tall column (thin, already marginal); emission a third of density (dust devil 0.045); the Cosmic Storm untouched. `make_tornado_showcase.py` gives an arm a dark sky when emission exceeds 0.05, so every grey variant stays below it.

## Measurements

Grain at matched luminance (`PROBE the march's grain at matched luminance`, t = 6), before -> after: hero 32 steps 0.0254 -> 0.0089 of the medium (medium mean 1.15 -> 0.41, and the storm covers 19.7k -> 29.2k px at 640x360); with 4 @ 0.4 shadow 0.0373 -> 0.0145. The showcase and modes-d author jitter 0 and measure 0 both sides. The Detail=0 gate and every `[tornado],[volume],[fog]` case pass.

## Owner's eye

The composition change (a lower, shorter storm whose wall cloud now sits upper right, still cut by the right edge at t = 6); how much cyan the palette wants (0.006 and 0.008 in effect were both rendered); and the dust devil, now translucent and sandy.
