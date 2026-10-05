# ADR-1112: The Critic is asked last, and the optimizer never reads its answer

**Status:** Accepted (live optimizer, Phase 5 stage E). **Date:** 2026-10-04

The brief's 5.4 loop (optimizer -> render -> Critic -> accept/reject) with its rule that the two tools stay separate.
`--ab-critic` (with `--compare` or `--optimize`): after the objective comparison, the ORIGINAL and OPTIMIZED frame runs
are submitted to the Creative Critic (`critic submit --mode preview --sequence <dir> --fps <target> --wait --json
--no-autostart`, the same CLI `director.evaluate` uses, found by `--critic` / `AVGEN_CRITIC`), then `critic compare
<a> <b> --json`. The answer is stored as `critic` beside the comparison, labelled as the Critic's; `chooseCombination`
never reads it, so a missing, stopped or failing Critic changes nothing else. Unavailable is recorded with the reason
(not configured; exit 4 "not running" with the `critic start --daemon` hint; any other exit). Only the chosen
combination is sent from a search.

**Why preview, measured 2026-10-04:** with `--mode fast` on a bare frame sequence the Critic answered and compared
nothing (every dimension "not comparable", 0 matched frames: fast mode is scene-only). `--mode preview` on the same
Sonic ORIGINAL/`scale71` runs scored 11 dimensions (lighting 0.819, technical quality 0.905, both unchanged) and
compared one matched frame (delta E mean 0.112, 0.01% of pixels over dE 5) -- "no dimension moved beyond the noise
band", while the Quality Lab measured SSIM 0.926 on the same frames. The Critic's job is marked PARTIAL (no audio on a
frame sequence); the optimizer does not use `--strict`, so a PARTIAL answer is kept and labelled as the Critic's.
