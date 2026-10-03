# ADR-1093: Optimization candidates are rules over existing levers, measured only with the A/B machinery

**Status:** Accepted (live optimizer, Stage 1h). **Date:** 2026-10-03

## Decision

- `optimizationCandidates()` turns measured category and pass costs into candidates, each naming an EXISTING lever
  that is also an A/B arm (`--quality-arm` / `--ab` names: `volumequarter`, `volumesteps`, `posttaps`,
  `nomotionblur`, `nodof`, `castercull`, `shadowatlas1k`, `scale85`, `lodbias2`, `drawdist75`, `particlelod`,
  `noprograms`). Each has the measured cost, a suggested change, an **estimated** saving range with its basis (the
  per-pass scaling fits of `docs/live-quality/evidence-live-projection-2026-10-02.md` §D, or a stated judgement) and a
  visual risk.
- **Procedural material programs** are named with the Sonic Abstract art pass's measurement (about 3 ms at
  full-frame coverage plus about 0.09 ms per op, 1080p, M2 Max) as an estimate over 10-100% coverage, and the
  `noprograms` diagnostic arm (programs bypassed) lets `--verify-candidates` MEASURE what they cost. No shader
  compiler is built (out of scope).
- **`--verify-candidates N`** measures the top N in the same process with counterbalanced baseline/arm pairs from the
  measured start (headless) or with the level pinned while playing on (live), and `rendering::compareArms` gives the
  delta, the noise floor and the drift check. The measured saving is stored in separate fields and never merged with
  the estimate.
