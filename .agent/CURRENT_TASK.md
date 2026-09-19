# Current Task

## Objective
Three owner-priority tracks running as agents in worktrees, plus merges landing on main.

1. **`glowmere-valley-2-multicam` performance regression** (`agent/mcperf`, running). Owner: it "has
   ground to a halt" since the port to the new animation system. Also fix animation correctness.
2. **Tree of Life COSMIC art pass** (`agent/cosmicart`). **Phase 0/1 delivered and being merged.**
   Remaining S3-S6: leaf cards, cosmic vortex, tree energy / canopy shimmer / tree particles,
   island underside, comet coupling, bespoke UI panels.
3. **Multi-backend offline rendering + Metal hybrid RT + video output** (`agent/mbackend`, running).
   Currently on the audit; not to start a large refactor before the audit is written down.

## Current Status
Main at `8f23d2ec` plus an in-flight merge of `agent/cosmicart` (build + full suite running).

## Two decisions open for the owner
- **`nodes/tree-foliage/emissiveBoost` 2.2 -> 0.5.** Measured with `tools/light_probe.py`, which
  partitions subject pixels by the sign of n.L from the normal AOV: key-to-shadow contrast 5.537 at
  2.2 versus 8.368 at 0.5, because the emission lifts the shadow side 60% and the key side only 6%.
  `shadow_floor` unchanged at 0.0001, so 0.5 does not crush to a silhouette. Agent recommends 0.5
  and deliberately did not edit the owner's live UI value.
- **`sky.groundColor`** was raised ~7x in three doublings by the key-light pass
  (0.0052 -> 0.0105 -> 0.0210 -> 0.0360 on red) to lift the island underside out of black. Brief
  section 17 wants "deep environmental shadow" and warns against "flat HDRI illumination", which
  argues the other way. Test ceiling split and loosened with the reasoning recorded; unresolved.

## The strongest open lead on the performance regression
**An app save silently drops the baked camera.** At 10:07 the running app wrote
`glowmere-valley-2-multicam.json` and removed two whole top-level keys -- `cameraAimFollow` (37
entries) and `cameraShotSpans` (42), the bake from `9433044d`. `parameters` went *up* 5502 -> 5530,
so not a truncated write. Restored from git; both copies in the scratchpad. If a save destroys the
bake, the camera must derive the cut live, which fits "ground to a halt" and fits "after I asked you
to fix animation". **Hypothesis, not a finding** -- the frame-time cost must be measured.

## The pattern that keeps recurring: reader and writer disagree
Three instances on 2026-09-19, all the same family:
- `DayNightSettings` (ADR-350): reader, no writer -- saving a scene destroyed the block.
- `wind.enabled` (ADR-360): writer gated on the flag its own reader checks, so nothing in the app
  could ever turn it on. The owner's saved `scene/windSpeed` had never moved a vertex.
- `cameraAimFollow` / `cameraShotSpans`: loads fine, save drops them.
Still open and unfixed: **`softness` (soft particles) is authored, serialised, uploaded into
`u.turb.z` and read by no shader.** Same family, found and flagged, not yet fixed.

The test that catches all of them is ADR-350's prescribed pair: set a non-default value, save, load,
save, assert it survived. Check *named keys*, not file size -- the camera-bake loss shrank the file
by 10,000 lines while the parameter count rose, so every cheap check said healthy.

## ADR numbering is colliding constantly
Three collisions in one session (354 twice, 359 twice). Agents branch from the same main and mint
the next free number independently. **Before merging any branch that adds an ADR, check the number
against main, and renumber per file** -- a blanket sed rewrites unrelated documents that cite the
*other* ADR of that number. Current high-water mark: **ADR-360**.

## Known Problems
- Build the `avgen` app target explicitly before rendering; a stale `src/avgen` once produced an
  hour of byte-identical render pairs.
- `cmake -S . -B build/release` after every merge -- the test glob is configure-time.
- Do not `pkill -f avgen_tests`. Copy to a distinct name and `codesign -s - -f` it.
- zsh does not word-split unquoted variables; verify by counting, not by absence of an error.
- `pgrep -f <cmd>` matches your own command line, so `until ! pgrep ...` waiters never fire.
- The owner edits projects in the running app while agents work.
- **On main and not anyone's branch:** `avgen_render_tests` dies with a bus error in full-suite runs
  (`test_wind_gpu.cpp:272`, then SIGABRT while Catch2 stringifies a 4 MB byte vector). Reproduces at
  `e41660d5`. Two separable bugs: a byte-identity check that passes alone and fails in a suite, and
  a reporter that cannot survive printing its own failure.

## Tests
Main baseline **2452 cases** before the cosmicart merge; 1 `[!shouldfail]` by design
(`test_character_lab_slopes.cpp`, ADR-260). `python3 tools/check_project_integrity.py` is read-only,
exits non-zero, and must be clean before any commit touching `examples/`.

## Next Action
Confirm the cosmicart full-suite numbers, commit the merge, remove the worktree. Then await mcperf
and mbackend.

## Last Updated
2026-09-19 12:35
