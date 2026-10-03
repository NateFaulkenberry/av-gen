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

## Results (2026-10-03, all measurements done)
Tables and raw data: `~/Desktop/av-gen-review/30-live-quality/data/` (`matrix-tables.txt`, `sweep-tables.txt`,
`cpu-tables.txt`, `raw/`, the scripts). The §23 report was refused as a file by the agent harness and was delivered
in the agent's final message to the coordinator.
- §19 matrix (after vs before, fps): Sonic 60: 72-85 vs 64 (judder); 90: 105 vs 91; 120: 104 vs 97. Glowmere (no
  characters) 60: 92 vs 64; 90/120: 111-112 vs 70-72. Liminal 60/90/120: 117-118 vs 59-60. At 60 every scene settles
  a level lower than needed (coarse steps + 0.8 raise margin) -- a tuning point.
- Transitions: 27 sweep + 10 natural, worst +0.9 ms over the steady-state max. Resize path kept (ADR-1086).
- §9: diag 0.6-0.75 -> 0.013 ms and render.record -0.7 ms on 22 x imported/alien.gltf; Glowmere here is within noise
  (characters missing).
- §17: simulated 15 ms projection block: unpaced 52-55 fps, paced 85 fps (healthy 88).
- Full CPU suite at 18935d63: exit 42, 26 failed + 1 failed as expected; all 26 are alien/farm/musicians/city-kit/
  quaternius/audio assets missing from this worktree (failure bodies show "glTF file not found"/missing files).

## Commits that are temporary-and-reverted (history only)
7a67ace1, b0c652e1, 299e7a99 (reverted in aee7e18b), 2f28e016 (reverted in 989fe61f).

## Next (for whoever resumes)
- Owner: link `assets/aliens`, `assets/farm` (and quaternius, musicians, kenney/city, 100STYLE) into this worktree
  as in `../av-gen-qa-coord`, then re-run the CPU suite and `lq/cpu.sh` for the real Glowmere §9 number.
- Owner review of the Live panel block and the levels' look while projecting.
- Tuning candidates: raiseMargin ~0.9; resolution_first Emergency 0.44; a frame cap to the target.
