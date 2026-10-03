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

## Done (all committed and pushed)
- §9.1 `SceneRenderer::setDiagnosticRecords` (ADR-1081). §9.2 `Scene::posedMeshBounds` + cull once (ADR-1082).
- §3-4 `liveBudget(targetFps)`, `general.liveTargetFps`, `--live-target` (ADR-1080).
- §5-7 the ladder: `LiveQualityRung`/`liveQualityLadder`/`applyLiveRung` in `src/app/interactive_resolution.*`,
  `QualitySettings::motionBlur/depthOfField` gates, `general.liveQuality` (auto|level), `--live-quality`,
  `live.qualityStrategy` project key in the three baseline projects (ADR-1083, ADR-1084).
- §13-14 controller: learned step ratios, `raiseHoldFrames`, cost = min(median span, mean interval) (ADR-1085).
- §10-11 investigated and measured: transitions within normal jitter; resize path kept (ADR-1086).
- §12 Live panel status + Settings (ADR-1087). §18 projection projects the open project (ADR-1088).
- §17 `PresentPacer` in `OutputManager::presentAll` (ADR-1089, numbers pending the stall run).
- Tests: `tests/unit/test_interactive_resolution.cpp` (rewritten), `test_app_settings.cpp`, `test_projection.cpp`,
  `test_skeleton.cpp`; GPU `[live-quality]` in `test_gpu.cpp` (records gate, round trip) and `test_post_gpu.cpp` (gates).

## Measurement state (2026-10-03 ~02:30)
- Binaries in scratch `bin/`: `avgen-before` (main + probes), `avgen-after` (feature, commit 1c8fe689 + probes).
- `lq/matrix.sh` (running, nohup) = per scene `lq/mscene.sh` (native + before/after x 60/90/120 x 2), then
  `lq/stall.sh` (Sonic, 480x270 pt window, before/after x2), then `lq/cpu2.sh` (22 x imported/alien.gltf, §9 A/B).
  Progress: `lq/data/runs.txt`. Tabulate: `python3 lq/matrix.py lq/data <sonic|glow|lim> 240`,
  `python3 lq/trans2.py lq/data <tags>`, `python3 lq/cpu.py lq/data <tags>`, `python3 lq/sweep.py lq/data sweep-*`.
- Sweeps done (`sweep-sonic/glow/lim`): per-level costs and 27 transitions (worst +0.9 ms over steady max).

## Next
1. Finish the matrix, stall and a22 runs; write ADR-1089's numbers; write REPORT.md (both locations).
2. Remove the TEMPORARY probes: commits 7a67ace1, b0c652e1, 299e7a99 (by hand where they conflict).
3. Reconfigure CMake, full suites once each under the lock: `avgen_tests` then `avgen_render_tests` (nohup, poll).
