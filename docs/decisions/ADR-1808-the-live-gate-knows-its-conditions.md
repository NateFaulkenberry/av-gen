# ADR-1808: The live perf gate records its conditions and says when they differ

- Status: Accepted (eng/render-improvements, 2026-10-28). Render audit recommendation R2 (gap G2), approved by the
  owner.
- Code: `tools/live_perf_baseline.py` (`LoadSampler`, `run_conditions`, `baseline_conditions`, `conditions_differ`,
  `--max-load`, `--ignore-conditions`), `tools/ci/live-perf.sh` (exit 3), `docs/perf/live-baseline.json`
  (`gateConditions`), `docs/performance.md`.

## Problem

The live baseline gate (ADR-1608) judges deadline misses and the frame interval's p99 against a committed baseline. Both
depend on more than the code:

- **The display's refresh rate.** The live loop presents with Fifo, so a 30 fps target paces against a 60 Hz or a
  120 Hz vsync differently. The baseline was measured on a 60 Hz display; the render audit's run reported 120 Hz.
- **What else the machine is doing.** The audit's run shared the machine with another agent: load average 3-23.

In that run every GPU median reproduced the baseline within 5%, while misses rose from 1 to 12 per 900 frames on Rift
and from 1 to 5 on Rainbow World, and `check` said **FAIL** (T1 lost, T9 regression). The tool recorded neither
condition, so nothing in its output said the comparison was invalid. Each live profile already recorded
`displayRefreshHz`; the tool did not read it.

## Decision

- **`run` samples the 1-minute load average** every 2 s while each avgen run holds the GPU lock, and writes
  `<run>.load.json` (samples, median, max). `conditions.log` gains the median and max per run.
- **Every live rep in a summary carries `displayRefreshHz`** (from its profile), `loadMedian` and `loadMax`.
- **`check` compares the run's conditions with the baseline's** and answers **CONDITIONS DIFFER** (exit 3) instead of
  PASS or FAIL when:
  - a live rep ran at a refresh rate other than the baseline's (or the reps disagree among themselves, or either side
    recorded none);
  - the run's median load during its live reps is above `--max-load` (default **4.0**, or the baseline's own median
    + 2 if that is higher), or the run recorded no load (made before this ADR).

  The full table is still printed, and the line says what the verdict would have been. A run that **exited non-zero**
  still FAILs: a crash is not a condition. `--ignore-conditions` judges anyway.
- **`write` refuses** a run whose live reps recorded no refresh rate or more than one, a run with no load record, and a
  run whose median load was above `--max-load`. It stores `gateConditions` (refresh rate, load median and max) in the
  baseline and puts the measured refresh rate in `machine`. `--add` refuses scenes measured at another refresh rate
  than the baseline's.
- **The committed baseline gains `gateConditions` by hand**: `displayRefreshHz` 60, taken from its own `machine`
  record ("60 Hz display"), and `loadMedian`/`loadMax` null (not recorded at the time). No measured number was
  changed and `write` was not run.

The 4.0 threshold: the M2 Max has 12 cores and avgen contributes about 1-2 to the load average itself. The audit's
shared-machine runs sat at 3-23.

## Consequences

- **The audit's run now reads CONDITIONS DIFFER**, not FAIL (acceptance for R2):
  `check --out <audit run>` prints `conditions differ from the baseline's: display 120 Hz, baseline 60 Hz; the run
  recorded no load average (made before ADR-1808)` and `RESULT: CONDITIONS DIFFER (would have been FAIL ...)`, exit 3.
- A copy of that run with every rep set to 60 Hz and a load median of 2.25 is judged normally (FAIL, on the same T9
  lines); at a load median of 9.0 it is CONDITIONS DIFFER, and `write` refuses it.
- To get a verdict, re-measure on a quiet machine at the baseline's refresh rate (System Settings > Displays > Refresh
  Rate). On this M2 Max the owner may need to set the display to 60 Hz for the gate to be meaningful.
- The load average is a 1-minute exponential average, so it lags: a burst just before a run inflates it and a burst
  inside a short run may not show. It is a coarse guard against a busy machine, not a measurement of interference.
- `tools/ci/live-perf.sh` documents exit 3.
