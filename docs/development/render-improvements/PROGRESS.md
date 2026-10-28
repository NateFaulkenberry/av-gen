# Render improvements: progress log

Implements the owner-approved recommendations of the render audit (branch `research/render-audit`,
`docs/research/render-audit/REPORT.md` sections 4.5, 5, 9; read only, never committed to).

- Worktree `../av-gen-render-improve`, branch `eng/render-improvements` from main `9b4156ea`. Build `build/release`
  (`cmake --preset release -DCPM_SOURCE_CACHE=<main checkout>/.cache/cpm`).
- Generated scenes: `examples/rainbow-world/scene` and `examples/bioluminescent/rift.scene.json` are symlinks to the main
  checkout's generated copies (gitignored); Aurora is generated in-tree with `python3 examples/aurora-borealis/build.py`.
- ADRs from 1807. Review media: `~/Desktop/av-gen-review/103-render-improvements/`.
- Scratch (numbers, logs, picture store): the session scratchpad's `render-improve/` folder.

## Part 1 (hand back after it; the coordinator merges)

| Item | ADR | State |
|---|---|---|
| R1 camera-basis sky rays (clouds, skybox, atmosphere) | 1807 | done. Test `[gpu][sky]`: 0 px differ (old ray: 6,910). Four scenes SAME/WITHIN vs main; Aurora's ribbon dither reshuffles (same look, documented-equivalent) |
| R2 live gate records refresh rate and load; "conditions differ" | 1808 | done. The audit's run now reads CONDITIONS DIFFER (exit 3) |
| R5 `--live-frame` for the headless benchmark and `--ab` | 1809 | done. Rainbow null A/B: 37.5 -> 26.2 ms GPU, spread 2.70%, sky rebuild gone |
| R4 `tools/picture_check.py`, local picture store | 1810 | done. Clean re-capture SAME/WITHIN; seeded 2% exposure DIFFERS on 15/15 |
| P2 Dawn persistent blob cache | 1811 | done. Fresh build 1.0x warm (Rainbow 12.7/12.6 s, Aurora 4.5/4.4 s); saves ~1.5 s/launch; cannot hold Metal binaries (the audit's 16-26 s is a Metal cache miss) |
| Both suites exit 0, commit (no push), hand back | | done 2026-10-28: `cpu-assets` exit 0 (4,562 passed, 1 expected failure, 29 skipped); `gpu` exit 0 (712 passed, 1 skipped: NDI runtime absent). Commits 07b20fdc..0bda16e8 plus this log; not pushed |

## Part 2

| Item | ADR | State |
|---|---|---|
| P1 compiled material programs: entities, skinned, SDF compiled trees; Blocking offline from frame 0 | 1812 | done. Recovered 5.18/5.95 ms Aurora, 3.78/4.69 TH, 4.02/7.48 Rift; pictures within 1 level |
| P3 GTAO normals from the depth prepass (split prepass, tilt-adaptive bias) | 1813 | done. Fixture darkness 0.024 -> 0.013; Rift ground within 3 levels; cost ~+0.06 ms |
| Rainbow World's 7 s first frame | 1814 | done. LOD chains rebuilt per level, serially: memo + parallel prefetch, 7.1 -> 1.7 s, byte-identical |
| R3 quiet 60 Hz re-measure, eda492a6 vs current (owner's LG UltraFine at 60 Hz) | - | NOT RUN: at about 17:50 the only display was the built-in XDR (system_profiler, twice); the LG was not connected. eda492a6 is built and ready in `../av-gen-r3-eda492a6` (detached worktree, `build/release/src/avgen`) |
| Both suites exit 0, commit (no push), hand back | | done: `cpu-assets` exit 0 (4,563 passed); `gpu` exit 0 (714 passed, 1 skipped: NDI). Commits 1798b90d, fbadf73a and this log; not pushed |

Part 3 (after Part 2 merges; owner-approved): A2 specialised lit shader variants (material lab lit pass 8.4 -> <=5.9 ms,
pixels within 1/255, reference scenes within picture_check tolerance, few variants, no compile stalls, first-frame time
measured); A6 contact shadows as a quality-ladder lever (live off unless a scene asks; before/after frames). Sources:
`git show exp/filament-eval:docs/research/filament-eval/REPORT.md` and `04-performance-and-latency.md`. Not approved:
A1, A4, A5.

## Notes for resume

- Every avgen/GPU run goes under `tools/gpu-lock.sh`; another agent shares the GPU.
- The picture store is outside the repo by design (`AVGEN_PICTURE_STORE` overrides the default
  `~/Library/Application Support/avgen/picture-store`); never commit frames.
- `examples/aurora-borealis/scene/` is generated and gitignored.
- Picture store used for R1/R4: the session scratchpad's `render-improve/picture-store` (labels `base-9b4156ea`, made
  with the audit worktree's 9b4156ea binary; `r1-a`, `r1-b`; `seed-exposure-2pct`).
- P2 experiment scripts: scratchpad `render-improve/p2.sh`, `p2b.sh` (they stamp `main.cpp` and restore it).
- Part 2 measurement batches: scratchpad `render-improve/p1-measure.sh` (its Aurora/TH pass was void: another agent's
  GPU work) and `batch2.sh` (the quiet pass used in ADR-1812/1813). The `--ab` arms: `interpreted`, `noprograms`,
  `aodepthnormals`.
- The GPU lock is not first-come-first-served: another agent's suite can take it while a run of mine waits.
