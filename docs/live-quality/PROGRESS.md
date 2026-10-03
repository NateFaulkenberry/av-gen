# Live quality: progress (resumable cold)

**Branch:** `live/quality` in `/Users/natefaulkenberry/Documents/GitHub/av-gen-live`. ADRs 1080-1089 are this branch's.
**Brief:** `docs/live-quality/00-brief.md`. **Report target:** `~/Desktop/av-gen-review/30-live-quality/REPORT.md` and `docs/live-quality/REPORT.md`.

## Temporary commits (must be reverted before merge)
- `7a67ace1` TEMPORARY live-render-perf instrumentation (cherry-pick of 8f953f53): `AVGEN_LIVE_FRAME_CSV`.
- `b0c652e1` TEMPORARY live-projection probe (cherry-pick of 319946ad): `AVGEN_X_PROJECT_ANY`, `AVGEN_X_PROJ_W/H`.
- Revert both at the end: `git revert --no-edit b0c652e1 7a67ace1` (resolve conflicts in favour of the feature code).

## Blocker found 2026-10-03: this worktree has no alien/farm character assets
`assets/aliens` and `assets/farm` hold only `ATTRIBUTION.md`; the `.glb` files are not linked (in `../av-gen-qa-coord`
they are per-file symlinks into `../av-gen/assets`). Linking them was refused by the permission system (it touches the
owner's asset tree), so it was NOT done. Consequence: Glowmere loads with 224 entities instead of 256 (22 rigged
characters skipped: "glTF file not found"), so the skinned-bounds CPU cost the brief's §9 targets is mostly absent
here, and every Glowmere number from this worktree describes the scene without its characters.
**To unblock:** the owner/coordinator links `assets/aliens/*.glb` and `assets/farm/*.glb` into this worktree the way
`../av-gen-qa-coord/assets/{aliens,farm}` are linked, then re-run `lq/cpu.sh`.

## Measurement setup
- Scripts: scratchpad `lq/` (`run.sh <binary> <tag> <project> <projW> <projH> [args]`, `cpu.py`, `tab.py`). Data in
  `lq/data/`. Copy the final tables into `~/Desktop/av-gen-review/30-live-quality/data/`.
- Binaries: `lq/../bin/avgen-before` = baseline (branch at b0c652e1: main + probes). `avgen-cpufix` = + §9 fixes.
- Always under `tools/gpu-lock.sh`, one script per hold.

## Findings so far
1. **The "195 ms rung-change hitch" in the investigation was not the rung change.** In
   `29-live-projection-perf/data/sonic-1080-on.cpu.csv` the rung change is at frame 36-37 (17.07 ms, render.record
   +0.28 ms); the 1% low came from frame 204, a single **1,032 ms main-window swapchain acquire** (`gpu.acquire WAIT`),
   167 frames later. The other adaptive runs (rung changes at frames 38/68/98) show no spike at the changes.
2. **Glowmere CPU A/B without characters** (interleaved B/A/B/A, 1080p projection, adaptive on):
   before 4.74 / 5.64 ms CPU work p50, after 5.25 / 5.35 ms -- inside the run-to-run noise, as expected with no rigged
   characters loaded. The diagnostic timer fell from 0.042-0.053 ms to 0.005 ms.

## Done
- §9.1 `SceneRenderer::setDiagnosticRecords` (live editor off; a named entity turns it on). GPU test
  `[live-quality]` in `tests/rendering/test_gpu.cpp`.
- §9.2 `Scene::posedMeshBounds` cache (content-keyed palette + meshVersion), `cullEntityNodes` culls each entity once
  with a name index. Unit test in `tests/unit/test_skeleton.cpp`.

## Next
- Controller + ladder (written in `src/app/interactive_resolution.*`, not yet wired into application/settings/UI).
- §18 projection project selection, §17 acquire stall, §12 status, settings.

## Commands
- Build: `cmake --build build/release -j 10` (reconfigure after adding test files).
- CPU tests: `tools/gpu-lock.sh ./build/release/tests/avgen_tests "[resolution]"`.
- GPU tests: `tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[live-quality]"`.
