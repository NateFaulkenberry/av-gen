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

## Part 2 (after the coordinator resumes)

- P1 compiled material programs for meshes, SDF surfaces and offline before frame 0 (ADR-1493 covers only live
  procedurals). Acceptance: recover >= half the `noprograms` share (Aurora 5.5/18.6 ms, TH 4.5/14.6, Rift 7.7/43.5),
  pixel-identical or documented-equivalent.
- P3 real normals for GTAO written in the depth prepass.
- R3 quiet-machine re-measure of Rainbow, Rift, Talking Heads, eda492a6 vs current, at the baseline's refresh rate
  (60 Hz: the owner may need to set the display).

## Notes for resume

- Every avgen/GPU run goes under `tools/gpu-lock.sh`; another agent shares the GPU.
- The picture store is outside the repo by design (`AVGEN_PICTURE_STORE` overrides the default
  `~/Library/Application Support/avgen/picture-store`); never commit frames.
- `examples/aurora-borealis/scene/` is generated and gitignored.
- Picture store used for R1/R4: the session scratchpad's `render-improve/picture-store` (labels `base-9b4156ea`, made
  with the audit worktree's 9b4156ea binary; `r1-a`, `r1-b`; `seed-exposure-2pct`).
- P2 experiment scripts: scratchpad `render-improve/p2.sh`, `p2b.sh` (they stamp `main.cpp` and restore it).
