# ADR-1608: The live baseline is in the repository, and a regression fails its check

- Status: Accepted (eng/living-prism-p1b, The Living Prism P1-7)
- Implements 03 §11 T9 (with T1-T4 as the owner amended them in 03 §14 #2: T3 = Medium or better ≥ 90%).
- Code: `tools/live_perf_baseline.py` (`run`, `check`, `write`), `tools/ci/live-perf.sh` (run + check, exit code),
  `docs/perf/live-baseline.json` (the baseline: numbers only).
- Evidence: `~/Desktop/av-gen-review/84-living-prism-p1b/P1-7/` (`baseline-run1`, `baseline-run2`, `inject-ultra`,
  `clean-rift`, `check-*.txt`).

## Problem

The audit's live numbers (01 §4.3) lived in a review folder, so a change that cost a performer frames could only be
caught by someone re-running the audit by hand and remembering what it said.

## Decision

`tools/live_perf_baseline.py run` measures the reference live scenes exactly as the audit did (01 §4.1, §9): Release,
1920x1080, 30 fps target, Feline Footwear driving the analysis muted from 60 s (the representative window), three
interleaved live Auto repetitions (`--warmup 5 --measure 30`) and one headless Ultra run (`--warmup-frames 150
--measure 15`) per scene, each under `tools/gpu-lock.sh`, the power source logged before and after. `check` judges a
run (exit 0 pass, 1 fail, 2 nothing to judge):
- **T1-T4** as thresholds. One the baseline met and the run does not is **LOST** and fails; one the baseline already
  failed is shown, not failed on (the regression rule covers it).
- **T9:** misses (median of the reps) above the baseline's worst repetition + 2, or p99 above its worst repetition +
  2 ms, fails. The margins: three repetitions' worst is not the tail (the audit's reps spread 1-6 misses).
- A baseline scene missing from the run fails (a crash must not pass), unless `--scenes` narrows the check; a run that
  exited non-zero fails.

`write` turns a reviewed run into the baseline and refuses a run with a failed repetition. `tools/ci/live-perf.sh`
is the hook: run and check, the check's exit code as its own. It is local only: the analysis track
(`assets/audio/feline-footwear.wav`) and the generated scenes cannot be in either repository, and the self-hosted GPU
runner is parked (`docs/development/gpu-ci-private-assets.md`). It refuses to time on battery.

**Scenes.** Rainbow World, THE RIFT LIVE and Sonic VFX - Event Horizon. Digital Mosh Live is left out (shelved by the
owner; its misses are 03 §14 #7's separate ticket). **The Living Prism is not in the baseline**: it is on
`scene/living-prism` (../av-gen-prism) and not on main; `living-prism` is already in the tool's scene list, so `run`
skips it ("its project is not in this tree") until it merges, and one `run` + `write` adds it.

## The baseline (`baseline-run2`, 2026-10-23, M2 Max, AC power on every run, c7a211f5 + this branch's logging-only
changes; medians of 3 reps [range])

| | Rainbow World | THE RIFT LIVE | Event Horizon |
|---|---|---|---|
| live misses per 900 (T1 ≤ 5) | 1 [1-1] | 1 [1-1] | 3 [3-3] |
| live frame interval p99 (T2 ≤ 40 ms) | 38.8 [38.8-39.1] | 37.1 [37.0-37.6] | **40.2** [39.6-40.3] |
| Medium or better (T3 ≥ 90%) | 100% (Ultra 100%) | **70%** (High 37, Medium 33, Low 30) | 100% (Ultra 100%) |
| headless Ultra GPU p50 / p95 (T4 ≤ 29.3, p95 ≤ 1.5 x p50) | 18.5 / 23.9: met | **37.9 / 45.0** | 9.2 / **16.4** (1.78x) |

An earlier full run (`baseline-run1`, same binary but with a build running beside some of it) agrees: misses 1 / 1 / 2,
p99 38.9 / 37.0 / 39.8, the same levels.

What the baseline says against the owner's thresholds: **THE RIFT LIVE fails T3 even at Medium** (30% of frames at
Low; the audit measured Low 48%), and relies on the ladder (T4). **Event Horizon sits on T2's edge** (p99 39.6-40.3
across six repetitions) and its GPU p95 is 1.8x its p50: particle bursts. Rainbow World meets all four. These are
recorded as the baseline's state; the check fails only if a met threshold is lost or a scene regresses.

## The check, tested

- The baseline run against itself: **PASS, exit 0**.
- **An injected regression**: THE RIFT LIVE with the ladder pinned at Ultra (`run --scenes rift-live --extra
  "--quality ultra"`, a scene that loses its ladder: 230-259 misses a rep, p99 46.6-61.8 ms): **FAIL, exit 1**
  (`T1 LOST`, `T2 LOST`, T9: "misses 277.6 > 1.0 + 2; p99 46.8 > 37.6 + 2 ms").
- **The same scene again without it** (`clean-rift`): **PASS, exit 0** (misses 1, p99 37.2).
- The clean RIFT-only run checked without `--scenes`: **FAIL, exit 1** ("rainbow-world not measured in this run").

## Consequences

- A change to the renderer, the ladder or a reference scene is reviewed with `tools/ci/live-perf.sh` (about 17
  minutes); a failure names the scene, the threshold and the regression.
- The baseline moves only on purpose: `write` from a reviewed run, committed with the change that moved it.
- The Living Prism joins it with one `run` + `write` once `scene/living-prism` is on main.
