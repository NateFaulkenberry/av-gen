# ADR-1600: The instruments say what the performed frame costs, and why a level dropped

- Status: Accepted (eng/living-prism-p0, The Living Prism P0-1)
- Amends ADR-119 (the timestamp quantum), ADR-1070 (the live sky schedule, now in the headless profile too), ADR-1090/
  1571 (the live profile and its traces), ADR-1362 (the paced controller, which now records a drop reason).
- Code:
  - `src/gpu/context.cpp`: `timestamp_quantization` is in the device's disabled toggles, always;
  - `src/app/live_profile_run.cpp`: the headless profile sets `setInteractiveEnvironmentBudget(2.0)` and
    `setLiveSkyLighting` as `Application::runLive` does; `--warmup-frames`; drop reasons in the live trace (`misses`,
    `drop` columns) and in the record's notes (live and headless);
  - `src/rendering/scene_renderer.cpp`, `procedural_renderer.{hpp,cpp}`, `shadow_renderer.hpp`:
    `RenderStats::shadowDraws` adds the procedural shadow draws, `shadowCasters` the procedural objects a shadow view
    can reach (`ProceduralStats::shadowCasterObjects`);
  - `src/app/live_profile.{hpp,cpp}`: live (refresh known), "over budget" is a missed deadline; the text says "on
    time / late"; `--warmup-frames`, and a capturing run defaults to a fixed warm-up of warmup x target fps frames;
  - `src/app/live_drop_reason.{hpp,cpp}`: `PassGrowthTracker` (per-pass medians at the level in force: the most
    recent 60 frames against the level's earlier ones, reset on every move) and `LiveDropReason`;
  - `src/app/interactive_resolution.{hpp,cpp}`: `missesAtLastMove()`, `gpuAtLastMove()`;
  - `src/app/application.cpp`, `src/ui/live_panel.cpp`: the reason is logged, kept, and shown in the Live panel
    ("Last drop: ...").
- Tests: `tests/unit/test_live_profile.cpp` (`[adr1600]`: the pass that grew, a late pass, nothing grew, the text, a
  probation revert; a fixed warm-up and the capture default; live budget = misses, headless strict),
  `tests/unit/test_interactive_resolution.cpp` (`[adr1600]`: a paced drop remembers its misses).

## Problem

The Living Prism audit (01 §4.2-§5.3, §7 C1-C4) found the profiling instruments describing a different frame from the
one performed:

- **C2.** All 432 pass medians were exact multiples of 65.536 us: Dawn quantises timestamp queries by default, a
  browser anti-fingerprinting measure. Small passes read 0 or 0.066 ms, so a new shared effect could not be costed.
- **C1.** `--live-profile --mode headless` never installed the live sky schedule, so Rainbow World rebuilt its sky
  lighting on 697 of about 1,060 frames: 6.2 ms of CPU a frame live does not pay (0.03 ms).
- **C3.** Rainbow World reported "0 casters ... 0 draws" beside a 1.7 ms shadow pass: only entities were counted.
- **C4.** Live, about half of every run's frames were "over budget" at 33.34-33.36 ms against 33.33 ms (vsync jitter)
  beside 1-13 real misses.
- Captures were "the last measured frame after an adaptive warm-up", so two runs' captures were at different moments.
- The controller dropped levels without saying why.

## Decision

Each item above is fixed where it is measured. The drop reason is two facts a performer can act on: the deadline misses
in the window that decided the drop (taken before the new level's window restarts), and the GPU pass whose median grew
the most at the level being left. A probation revert (ADR-1104: one miss undoes a raise) is labelled as one. "No pass
grew" says the misses were not the GPU's content.

## Evidence (`~/Desktop/av-gen-review/83-living-prism-p0/P0-1/`, 2026-10-22, M2 Max, AC power logged per run, Release,
1920x1080, 30 fps, Feline Footwear muted, from 60 s; one run each)

| | audit (01, before) | this branch |
|---|---|---|
| pass label medians on the 65.536 us grid | 432 / 432 | **0 / 81** non-zero label medians over five runs; smallest: clusters 5.5 us, post/exposure 25.8 us, post/look-atmos 27.9 us |
| Rainbow World headless CPU work p50 | 11.21 ms (resource uploads 6.16, the sky) | **5.08 ms** (resource uploads 0.00) |
| Rainbow World live CPU work p50 | 13.38 (UI 7.8) | 11.45, of which UI 6.24: **headless (5.08) is the live frame without the editor UI** |
| Rainbow World shadow casters / draws | 0 / 0 | 47 / 63 headless, 51 / 68 live |
| Digital Mosh live shadow casters / draws | (entities only) / 134 | 109 / 142 |
| live "over budget" (RW / EH / Mosh) | 469 / 463 / 473 of ~900 | **1 / 3 / 6 of ~900 = the deadline misses** (1 / 3 / 6) |

Digital Mosh live (30 s; the separate investigation ticket's scene) recorded five drops, each with its reason, in the
trace's `drop` column and the record's notes:

```
Ultra->High: 6 misses in 300 frames; shadow +1.33 ms (1.20->2.53)
High->Medium: 3 misses in 300 frames; scene +4.84 ms (11.23->16.07)
High->Medium: 1 miss in 300 frames; particles +1.97 ms (0.45->2.42)     <- a probation revert (labelled as such now)
Medium->Low: 3 misses in 300 frames; sdf +4.42 ms (0.00->4.42)
Low->Emergency: 3 misses in 300 frames; sdf +1.31 ms (5.52->6.83)
```

## Consequences

- Pass times are unquantised; a profile's per-pass costs and the ADR-1571 trace columns are comparable down to
  microseconds. Old baselines are on the 65.5 us grid.
- Headless CPU numbers for a sky-moving scene drop to what the performance pays; `--verify-candidates` and `--optimize`
  measure the frame a performer runs.
- The live trace gained two trailing columns (`misses`, `drop`); readers select columns by name.
- `overBudget`/`percentUnder` mean "late" live and "over budget" headless; the JSON keys are unchanged.
