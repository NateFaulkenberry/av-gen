# Latest Agent Handoff

**2026-09-19 06:20** — end of a long multi-agent session. 20 ADRs landed (335–354).

## Completed
Merged, in order: Glowmere cast scale 1.94x; Character AI P9 root motion and P11 route pricing;
Tree of Life island, its Glowmere material pass, HDRIs, ocean + day/night cycle, IBL/skybox
decoupling, faster environment swap; Glowmere Valley 3 retired and its tree work ported into the
multicam with the cast on `decide` and cameras re-baked; foot IK; asset LOD; the Embree path tracer
with CLI and Render-panel option; the multicam cast defects.

## Changed (this final unit)
- `obstaclesFromHeroes` and the walker's clearance field now exclude heroes an entity drives — a
  starred character was a blocking cylinder around its own feet, freezing three aliens at 0.00 m.
- Twelve farm animals moved off `slopeAlign: 1.0` (a value copied from a flat-pad lab fixture);
  `bodyRadius` and `footprint` set from measured world bounds.
- `tests/unit/test_glowmere_multicam_defects.cpp` — loads the **project**, not the scene.

## Verification
- Build: PASS. Suite: 2,421 / 3,796,443 on the branch, 0 real failures.
- Frame: `examples/world/renders/v2modern/defect3-bull/{before,after}.png` — bull pitched into the
  slope with forelegs under the surface, then level with all four hooves on it.
- Control names the three frozen bodies by measuring hero penetration at the query the engine
  actually makes.

## Failed / discarded
- My hypothesis that `interest` scored unreachable places was **refuted by measurement**. The agent
  built `GoalTaste::requireReachable`, measured zero change at four seeds, and removed it.
- Earlier cast numbers were taken on the **scene**, not the project — a world that does not ship.

## Remaining
- **Suspected colour regression at t=48.5** — see CURRENT_TASK.md. Highest priority.
- `agent/cosmickey` (3 commits) and `agent/assetlod` — both live, both mid-task, resumable.
- The ~5 fps investigation has a fix and no *before* number.
- Owner decisions open: leaf speckle (4.8x vs 8.1x contrast), island grass desaturation, Valley Wide
  at 44 degrees losing the second valley wall, whether to chase the bokeh-as-a-cut.

## Next Action
See `.agent/CURRENT_TASK.md` — build `src/avgen` at two commits and render t=48.5 from each.

## Important
Build the **app** target for any render comparison, not just `avgen_tests`. A render is evidence
about the binary that produced it, and a stale `src/avgen` cost an agent an hour of byte-identical
pairs it read as "the fix does nothing".
