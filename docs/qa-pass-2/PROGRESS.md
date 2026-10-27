# QA pass 2: progress log

Resume from here after a compaction. Brief: `00-brief.md`. Plan: `PLAN.md`. Branch `eng/qa-pass-2` in
`../av-gen-qa2`, from main `3ea9b678`. ADRs 1800-1849. Review media: `~/Desktop/av-gen-review/100-qa-pass-2/`.
Scratch: `$SCRATCH/qa2/` (= `/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/c334afa4-33e7-45d3-b0f3-889614b6118b/scratchpad/qa2`).

## Environment (2026-10-27)

- Apple M2 Max, 64 GB, macOS 26.7.1, Xcode 27.1 (Apple clang 21), CMake 4.0.1, Ninja. bash 3.2 at `/bin/bash`.
- No ccache installed.
- Display: LG UltraFine 5K, 2560x1440 UI at 60 Hz (main). Power: AC (logged with every perf number).
- Audio devices: LG UltraFine Display Audio, Logitech BRIO, BlackHole 2ch, MacBook Pro mic/speakers, iPhone mic,
  Teams, Zoom, Multi-Output. No MIDI hardware available to this agent: MIDI is synthetic coverage only.
- Assets symlinked into the worktree (1836 files) by the coordinator; never committed.

## Log

- 07:50 Worktree clean at 3ea9b678. Release configure from scratch (CPM fetch) took ~6 min.
- **Defect found at once:** `tools/ci/build.sh` dies on macOS bash 3.2 without ccache
  (`LAUNCHER[@]: unbound variable` under `set -u`), so `run-suite.sh --build` cannot build locally. Fixed with
  `${LAUNCHER[@]+"${LAUNCHER[@]}"}`.
- Talking Heads lead: the coordinator's live CSV shows a miss every 30-31 frames, a 1.00 s wall-clock period
  (3.640, 4.671, 5.676 ... 13.734 s); headless (no UI) has no such spike. Prime suspect: the Control panel's
  ADR-902 route-liveness badges, re-checked once a second by `ImGui::GetTime()` over all of Talking Heads'
  1,306 routes (`src/ui/control_panel.cpp`). To be measured.

## Baseline (binaries of 3ea9b678 + the build.sh fix; snapshot in $SCRATCH/qa2/base-bin)

- Build: release, exit 0, 478 s, 3,758 warnings (3,577 -Wdouble-promotion, 2,384 of them from one header,
  astral_conductor.hpp). Correctness-relevant: 3 -Wswitch, 3 ignored [[nodiscard]] in src, dead code.
- CPU suite (cpu-assets): **exit 1**, 4544 pass / 1 fail / 29 skip / 1 shouldfail, 16 min. The failure: "every live
  scene has the rig's common parameters" (108 shared-but-not-common): environment-dependent, see ADR-1804.
- GPU suite: **exit 0**, 711 pass / 0 fail / 1 skip (NDI), 47m52s (run while I was building: contention).

## Done (commits on eng/qa-pass-2)

- cee4beef build.sh bash 3.2; 9b0305f9 MIDI wildcard flake (cross-process CoreMIDI race).
- 62878974 warnings that hide something; 91926947 CA-017, CA-020.
- 97f95229 engine.cpp -> engine_project.cpp (2,354 lines moved verbatim; 8,091 -> 5,737).
- 236b3153 ADR-1801 seek = play for integrating routes / random+envelope sources. Scan + probe numbers in the ADR.
  Probe source kept in $SCRATCH/qa2/zz_qa2_seek_probe.cpp (not in tree).
- ADR-1804 rig common set (the baseline CPU failure); ADR-1803 env overrides + docs + hygiene test + baseline tool.
- eceeecf7 + 98a5e4da ADR-1802 Talking Heads: the 1 Hz miss was the route-badge recheck; plus off-screen row clipping.
  TH live: 32 -> 4 misses (quiet-ish), p99 47.3 -> 36.5. Later runs under another agent's compile load: 6-11 misses.
  **TODO:** quiet re-run, then `write --add` for talking-heads if T1-T3 pass.

## Findings to report (not fixed)
- Desktop Feline project: current .json files carry no retired signal; only *.bak files and a stale ADR-1630 comment in
  make_project.py (line 401). Read-only, not edited.
- Rainbow World's generated scene is absent from worktrees too; I symlinked the owner's (gitignored, excluded locally).
