# ADR-1110: ORIGINAL vs OPTIMIZED is rendered at the same moments and measured by the Quality Lab

**Status:** Accepted (live optimizer, Phase 5 stage C). **Date:** 2026-10-04

## Decision

- `avgen --live-profile --compare <levers|project>` (headless only; the parser refuses `--mode live`): after the
  ordinary profile, ORIGINAL and OPTIMIZED are rendered from the measured start with the fixed-step clock restarted
  and the temporal history reset, 10 settling frames then `--ab-frames` (12) consecutive frames each, as PNGs
  (`--ab-dir`, kept for a person to open). OPTIMIZED applies the levers as ceilings (`applyLeverToCeiling` +
  `applyCeiling`, exactly what Apply writes to a project); `project` compares the project without and with its own
  live ceilings.
- **The self-difference floor:** ORIGINAL is rendered twice and compared with itself. A comparison is
  `withinSelfDifferenceFloor` only when every metric is inside that floor (plus 8-bit rounding); that is the strongest
  statement the optimizer makes, and it is worded as a statement about these frames, never as equivalence.
- **Metrics live in the Quality Lab** (ADR-250: the engine does not link the Lab): a new `avgen_quality ab
  --original --optimized --out [--regions]` subcommand (`tools/quality-lab/metrics/ab.*`, schema `avgen.abdiff/1`),
  run across a process boundary the way the Critic is. Pixel (mean |dRGB|, changed fraction, PSNR), structural (SSIM,
  MS-SSIM), edge (Sobel difference, strength ratio, edges lost/added), luminance (mean luma delta, and SSIM and pixel
  difference **at matched luminance** -- the exposure confound), temporal (difference of frame-to-frame change,
  activity ratio), each also inside the hero regions (ADR-1108). Found by `AVGEN_QUALITY`, beside the build, or PATH;
  missing, the comparison says "pictures not compared" and the time A/B still stands.
- **Time** is the same counterbalanced A/B as `--verify-candidates` (2 pairs, `compareArms`), on max(GPU span, CPU work)
  per frame; the GPU clock decides `isResult` on a GPU-bound scene. Estimated and measured never share a field.

## Measured

Sonic, `--compare scale85`: -3.15 ms (21.63 -> 18.48), SSIM 0.927 (matched 0.927), luma +0.01, floor SSIM 1.0000
(headless Sonic is bit-identical run to run). Glowmere's floor is not zero (SSIM 0.998, 0.42 steps): its world is not
bit-repeatable from a restart, which is why a floor is measured rather than assumed.
