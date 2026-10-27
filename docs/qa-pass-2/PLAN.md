# QA pass 2: plan

The brief (`00-brief.md`) is the spec. The owner's rules that refine it (from the coordinator): one agent, no
subagents; no compat shims (ADR-441); public repo, never commit assets or audio; never change Glowmere Valley or any
scene's look; no new or removed scenes in `examples/index.json`; live target 1080p30 on AC; every `avgen` run under
`tools/gpu-lock.sh`.

This pass builds on two earlier ones and does not redo them:
- `docs/qa-pass/` (2026-09-28): GV3 performance, CI layout, sanitizers.
- `docs/audits/repository-cleanup/` (2026-10-14): code audit CA-001..CA-025, scene inventory, docs audit, CI recovery.
  Its open items are inputs here (CA-004, CA-008, CA-011, CA-017, CA-020 and the owner-decision list).

## Stages and items

Priorities use the brief's P0-P3. Each item: evidence, change, risk, verification. Status lives in `PROGRESS.md`.

### P1
1. **Talking Heads live regression** (coordinator lead 1). Evidence: a deadline miss every 30-31 frames, a 1.00 s
   wall-clock period, only with the UI (the headless run has none). Suspect: ADR-902 route-liveness badges re-checked
   for all 1,306 routes once a second. Change: measure, then remove the burst without changing what the badges say.
   Verify: `live_perf_baseline.py run/check` on AC, T1-T3; a unit test of the scheduling.
2. **Seek is not play for integrating routes** (lead 2). Evidence: `Engine::controlReplayNeeded` ignores
   `chain.integrate`; the scan in PROGRESS lists 13 shipped projects whose seek output changes. Change: ADR-1801, the
   control replay runs for an enabled integrating route. Verify: `test_integrate_seek.cpp` fails before, passes
   after; seek cost measured on the heaviest affected project.
3. **`tools/ci/build.sh` broken locally** (bash 3.2, no ccache). Fixed; verify `run-suite.sh --build`.
4. **Flaky tests** (lead 3): the CoreMIDI wildcard test (cross-process race on a system-wide source count: fixed);
   the EXR determinism hash; the water test. Reproduce under load, root-cause, fix.
5. **Cold shader compile / scene-switch stall** (lead 4): measure what blocks the main thread on a scene switch with a
   cold program; fix if it is a main-thread wait, otherwise document.

### P2
6. Audio Response Lab: a lamp for `beat.source` 4 (the manual clock) (lead 6).
7. `live_perf_baseline.py` must not pass a run that skipped a scene it was asked for (lead 7).
8. `AVGEN_NO_CONTROL_REPLAY` (lead 5): the profiling block was removed on 10-14 (CA-005); the A/B arm stays,
   undocumented. Document or cut.
9. Prior audit leftovers: CA-004 (output-changing env vars unrecorded), CA-011 (wall-clock asserts in the default and
   sanitizer sets), CA-017 (unchecked write), CA-020.
10. Dead code and stale docs: reachability audit beyond CA's list; docs that name removed signals (kick/snare/hat,
    the drum classifier cut by ADR-1784) or retired commands.
11. One large-file refactor with a clear seam, with tests before and after.
12. Validation: scene/asset reference check in the canonical suite where cheap; an end-to-end headless workflow test
    (load, synthetic audio, synthetic MIDI, run, scene switch, shutdown) if the existing ones do not already cover it.

### P3
13. Report-only: the Desktop Feline project's retired signals (lead 8), MIDI hardware coverage (lead 9), stale
    branches and worktrees, scene-removal candidates.

## Verification
- CPU: `tools/gpu-lock.sh tools/ci/run-suite.sh cpu-assets`; GPU: `AVGEN_GPU_LOCK_TIMEOUT=14400 tools/ci/run-suite.sh gpu`.
  Exit codes only. Both on the final commit, after merging main if it moved.
- Perf: `tools/live_perf_baseline.py` on AC, power logged.
