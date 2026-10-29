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

## Part 3 (owner-approved; Part 2 merged at 8262c034)

| Item | ADR | State |
|---|---|---|
| A2 lit-shader specialisation (override gates, per-scene sticky mask, on ADR-1812's variants) | 1815 | done. Lab lit pass 8.30 -> 2.70 ms (-67%, bar 30%); Rift -6.0 ms; pictures within 1 level |
| A6 contact shadows as a ladder / optimizer lever (no algorithm change) | 1816 | done. Off at Medium and below; Ultra/offline unchanged; Rift -3.1 ms, lab -0.9 ms |
| Warm-up before/after (Part 2 binary built in a temporary worktree, since removed) | 1815 | warm start-up unchanged within noise; Event Horizon +60 ms first frame |
| Both suites exit 0, commit (no push) | | done: `cpu-assets` exit 0 (4,564 passed); `gpu` exit 0 (715 passed, 1 skipped: NDI). Commits e0f7cadd (A2), e9059b02 (A6) |
| R3 eda492a6 vs current, interleaved, LG UltraFine at 60.00 Hz | 1817 | done: both PASS at 60 Hz, load median ~2; no regression (misses 1/2-3/1 per 900); live-only T4 fixed. `../av-gen-r3-eda492a6` can be removed |

Material lab (Filament S1): extract it from `exp/filament-eval:prototypes/filament-eval/scenes/s1/` into scratch and point
its HDRI at this worktree's `assets/hdri/` (it is not committed here).

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

## Part 4 (owner-approved: the rest of the Filament borrow list)

Worktree `../av-gen-render-improve2`, branch `eng/render-borrow` from main `d245d0f0`; ADRs 1818-1839. After adding the
worktree, link EVERY asset file (the link script misses some: Event Horizon's wreck boulders were absent and its
frames "changed"): `(cd ../av-gen && find -L assets -type f) | while read f; do [ -e "$f" ] || ln -s ...; done`.

| Item | ADR | State |
|---|---|---|
| 4a A5 multiple-scattering energy compensation | 1818 | done. Rough gold +17%; Rainbow/TH change most (<= 29 levels); cost unmeasurable; `nomultiscatter` |
| 4a A4 glTF glass (transmission, volume, ior) through ADR-1603's refraction | 1819 | done. S3 glass clear, not milky; ~+0.6 ms on S3 (bar 0.5); non-glass models unchanged; fixture `tools/make_glass_test_glb.py` |
| 4a review fixes (coordinator): arch hidden by glass, dark stepped rim, Low/Emergency BLEND fallback, framing | 1819 | done. Glass follows glTF (no volume = thin-walled, unbent; volume = Filament's solid sphere); silhouette coverage from N.V; BLEND glass refracts or keeps its blend per frame; the framing shift was a misreading. Crops in `a4-gltf-glass/fixes/` |
| 4b A1 glossy SSR (Hi-Z, roughness fade, env fallback, ladder lever; flicker on video) | 1820 | done. Opt-in `environment.glossyReflections`; S3 arch reflects in the floor, +0.95 ms at 1080p; reference scenes unchanged; deterministic; flicker measured on a 4 s camera move (oscillation 67 per million pixel-frames over 32 levels, on reflected silhouettes). Reads the refraction copy read-only (approved); water SSR untouched. Media `a1-glossy-ssr/`. Camera-move script: scratch `a1/move.sh` (one composition per frame) |
| 4b review fix: the floor's reflection read as a painted trapezoid | 1821 | done. Sides/corners were the 8% screen-edge fade, now a quarter-screen gradient; bottom edge is the real reflected wall top; open item: grazing-angle environment weight (experiment image in `a1-glossy-ssr/trapezoid-fix/`) |

S1 and S3 labs: extract from `exp/filament-eval:prototypes/filament-eval/scenes/s1|s3/` into scratch and point the
HDRI at the main checkout's `assets/hdri/`.
