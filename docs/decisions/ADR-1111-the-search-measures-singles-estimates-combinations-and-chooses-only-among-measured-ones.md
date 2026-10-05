# ADR-1111: The search measures singles, estimates combinations, and chooses only among measured ones

**Status:** Accepted (live optimizer, Phase 5 stage D). **Date:** 2026-10-04

## Decision

`avgen --live-profile --optimize` (headless). Target: max(GPU, CPU work) median <= budget x `--optimize-margin` (0.9,
ADR-1107's margin).

1. The baseline is the profile's own measurement.
2. **Singles**: the candidate rules' levers (one per lever), plus the search's own extras -- `scale71` (risk high) and
   `volumesteps` when a volume is drawn -- admitted by `leverAdmitted` (a ceiling form, the hero policy, risk <=
   `--optimize-risk`, default medium; at most `--optimize-candidates`, 8). Each is MEASURED: time and picture (ADR-1110).
   A single is *usable* only with a saving outside the noise, not void, and a picture comparison.
3. **Plans** (`planCombinations`): every 2-3 lever subset of usable singles (two render scales are not a combination),
   with an ESTIMATED cost (singles' measured savings summed, assumed additive) and an ESTIMATED rank key; those that
   reach by estimate first, least estimated change first.
4. **Round 1** measures the top three plans as combinations; **round 2**, if none reaches, measures every usable lever
   together.
5. **Choice** (`chooseCombination`): among MEASURED results (singles included) that reach the target, the least rank
   key = (1 - SSIM at matched luminance) + (1 - hero-region SSIM at matched luminance). No metric decides quality
   (ADR-250); this is a rank, and the record shows every metric. None reaches: the largest measured saving is named,
   nothing is chosen, and the live controller's levels are what remain.
6. **The low-risk set**: low-risk, hero-safe singles with a measured saving -- what
   `performance.apply_low_risk_optimizations` applies.

## Measured (headless, Ultra, 1080p, M2 Max; data in ~/Desktop/av-gen-review/32-live-optimizer-phase5/data)

| scene | baseline | best measured | chosen |
|---|---|---|---|
| Sonic (risk medium) | 21.76 ms | scale85+nodof 18.02 | none reaches 15.00 |
| Sonic (risk high) | 21.69 ms | scale71 14.35 (nodof+scale71 14.09, larger change) | **scale71** |
| Glowmere Valley 2 | 36.44 ms | all four usable 20.84 | none; low-risk set volumequarter, lodbias2 |
| Liminal @60 s | 51.51 ms | all four usable 22.87 | none; low-risk set volumequarter |

Additivity is not assumed past the estimate: Glowmere scale85+volumequarter+nomotionblur was estimated 20.12 and
measured 22.87 ms.
