# ADR-1098: Far emitters stop, and material programs can be measured

**Status:** Accepted (live optimizer, Stage 2). **Date:** 2026-10-03

- **`particleCullDistance`** (0 = none): a system whose emitter is farther than this from the camera (divided by its
  importance weight; a hero's never) is treated as disabled for the frame -- skipped and its pool emptied, so it
  blooms back in over a lifetime when the camera returns rather than resuming stale. Spline and scatter-anchored
  emitters have no single position and are never culled. `particleSpawnScale` (ADR-382) is the other particle lever.
  Arm `particlelod` (70% spawns, 60 m). The optional simulation substep/rate was not built: halving the step rate
  changes the particles' motion, not only their cost, and nothing measured asked for it.
- **`materialProgramsOff`**, a diagnostic arm only (`noprograms`): every program-driven material binds the "no
  program" select region, so a profile can measure what the programs cost. No tier, level or profile sets it.
