# ADR-1817: The audit's live misses were its conditions, not regressions; a live-only check leaves T4 unjudged

- Status: Accepted (eng/render-improvements, 2026-10-28). Render audit recommendation R3, run once the owner's LG
  UltraFine was back as the main display at 60.00 Hz.
- Code: `tools/live_perf_baseline.py` (`thresholds`, `check`).

## Problem

The render audit's live baseline run failed T9 on three scenes: Rainbow World 5 misses per 900 frames (baseline 1),
Rift Live 12 (baseline 1) and Talking Heads 4 (baseline 1). That run was on a 120 Hz display (the baseline was taken at
60 Hz) under a load average of up to 23. ADR-1808 made the gate report such a run as CONDITIONS DIFFER instead of
FAIL. That left the question open: were the misses regressions in `eda492a6..9b4156ea`?

Separately, `check` on a `--live-only` run reported T4 as LOST. T4 is the headless Ultra threshold, and a live-only
run has no headless run, so every live-only check failed whatever its live numbers were. The tool's own help says T4 is
unjudged in that mode.

## Decision

- **R3 measured it**: `eda492a6` (the baseline's revision, built in a detached worktree) and the current branch,
  interleaved rep by rep (eda, current, eda, current, ...), three reps each, `live_perf_baseline.py run --live-only` on
  Rainbow World, Rift Live and Talking Heads. Conditions: the LG UltraFine at 60.00 Hz (checked before each rep and
  recorded by every run as `displayRefreshHz` 60), AC power, load median 1.9-2.4 over the live reps (one of eda's waits
  in the GPU lock queue saw 17.6 while another agent's suite ran; the gate's limit is on the median).
- **`thresholds` returns T4 as `None` (not judged) when there is no headless run**; `check` prints `-` and never counts
  it as lost. A run with its headless runs is judged exactly as before.

## Consequences

| Scene | eda492a6: misses/900, p99, Medium+ | Current branch | Baseline (eda492a6, 2026-10-27) | T9 |
|---|---|---|---|---|
| Rainbow World | 1, 36.5 ms, 100% | 1, 36.9 ms, 100% | 1, 38.8 ms | none for either |
| Rift Live | 2, 36.9 ms, 70% | 3, 37.3 ms, **100%** | 1, 37.1 ms (worst + 2 = 3) | none for either |
| Talking Heads | 1, 36.7 ms, 100% | 1, 37.0 ms, 100% | 1, 36.1 ms | none for either |

- **Both builds PASS** under matched conditions (`check`: CONDITIONS match, T1-T3 met, T9 none). The audit's 5, 12 and
  4 misses were its 120 Hz display and its load, not a code regression in `eda492a6..9b4156ea`. Nor did this branch's
  work (Parts 1-3) regress the live frame: Rift on the current branch held Medium or better on 100% of its frames, where
  `eda492a6` held 70%.
- The audit's own run is still reported CONDITIONS DIFFER (120 Hz, no load record), unchanged.
- The baseline was not rewritten (no `write`, no `--add`).
