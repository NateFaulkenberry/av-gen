# ADR-1510: The response levels read against an adaptive loudness reference

- Status: Accepted (2026-10-19), eng/response-range. The owner: "The engine's smoothed bass, level and sustain signals
  stay pinned - fix this."
- Supersedes: the fixed level window of ADR-1062 (`floorDb` -60, `rangeDb` 48, `sensitivityDb` 18), and ADR-1361's
  "levels are fixed-dB and never auto-gained" (a quieter take reading quieter).
- Code: `src/sonic/response.{hpp,cpp}` (`ResponseSettings` gate, reference and windows; `conditionLevel`;
  `ResponseModel::step`; `ResponseModel::referenceDb`), `src/ui/live_panel.cpp` (the Input gain and Sensitivity
  tooltips), `tools/check_response_range.py`.
- Tests: `tests/unit/test_response_range.cpp` and `tests/support/mastered_song.hpp` (a loud-mastered synthetic song
  keeps its range at 0 and -24 dB; Sensitivity and Sustain still move it; the gate and the reference floor),
  `tests/unit/test_response_live.cpp` (a live scene switch carries the reference; the first bars of a set; loading
  a second live project keeps the session), `tests/unit/test_sonic_response.cpp` and
  `tests/integration/test_live_response.cpp` (updated to the new meaning).
- Evidence: `docs/development/response-range-report.md`; media in `~/Desktop/av-gen-review/71-response-range/`.

## Context

`response.bass`, `level`, `sustain`, `levelSlow` and `intensity` were read on a fixed window: the analyser's band
power in dBFS (a full-scale sine reads 0 dB) at -60 read 0, at -12 read 1, and Sensitivity slid the floor +-18 dB.
A dump of the analyser's levels on the three test tracks:

| track | levelDb p10 / p50 / p90 | bassDb p10 / p50 / p90 |
|---|---|---|
| Feline Footwear | -15.2 / -8.8 / -5.1 | -26.7 / -11.9 / -5.5 |
| Trench | -13.5 / -9.8 / -7.2 | -22.4 / -11.6 / -7.7 |
| Hyperborea | -17.8 / -7.1 / -4.1 | -33.7 / -9.3 / -5.5 |

Mastered music lives above the window's top. Every level read 1 on 80-98% of the frames (the table below), and every
route on these signals was a constant. A second, independent problem: the followed level moves only about 6 dB
between its p10 and p90 on these masters, and the 3 s and 12 s averages about 2 dB outside breakdowns, so even a
correctly placed 48 dB window would have squeezed them into a fifth of the range. The Sonic Abstract scenes'
`-44 / 42` override still topped out at -2 dBFS and was as wide. The tests never disagreed: they used a moderate
synthetic kit, and the scenes had been tuned against the pinned values.

## Decision

**One loudness reference, per-signal windows below it.**

- The reference is the gated level's power averaged over `referenceSeconds` (3 s) -- until that much has been heard,
  the plain average of what has (the character tiers' warm-up, so the first bars are read against themselves, not
  against silence; for the first 6 s the reference is that average with no lag on top) -- then followed up with
  `referenceRise` (2 s) and down with `referenceFall` (60 s). A breakdown therefore reads quiet for as long as a
  breakdown lasts, and a drop after it reads at the top for a few seconds. Frames below `gateDb` (-60 dBFS) leave it
  where it is. It never sinks below `referenceMinDb` (-40 dBFS), so noise is never normalised up.
- Each level is followed in dB (bass and level, with their old attack and release times) or as power (`levelSlow`
  over 3 s, `intensity` over 12 s: an average of dB sinks into every gap between two hits), then read in its window:
  1 at `reference + topDb`, 0 at `spanDb` below that, clamped, then the response curve. The windows (top / span, dB):
  bass 5.5 / 12, level 6 / 10, sustain's instant level 4 / 9 (less its percussive share, followed as before),
  levelSlow 1.5 / 6, intensity 1.5 / 6. They were calibrated on the three tracks' followed levels against the
  reference, and are project-settable (`sonic.response.windows.<name>: [topDb, spanDb]`).
- **Sensitivity** moves a window by `sensitivityShare` (0.375, the old 18 of 48 dB) of its span and curves it, as
  before: it lifts quiet values and never pushes past 1. Transients, Sustain, Attack and Release keep their meanings;
  0 Sustain still turns the levels off. **Input gain** no longer scales the response levels (they follow the
  music's own loudness); it still sets what the meter, the gate, the hit detector and the Sonic Character hear. The
  Live panel's tooltips and LIVE-QUICKSTART say so.
- `floorDb`, `rangeDb` and `sensitivityDb` are retired (ADR-441: no aliases). A stale key is warned once and ignored;
  the -44 / 42 overrides are stripped from the 9 Sonic Abstract scenes, Sonic Live, Digital Mosh Live and THE RIFT
  LIVE, and from their generators, in the same commit.
- **Not changed:** the hits and their envelopes, `transient`, `flux`, `hatRate`, `melodic`, `pitch`, `sonic.*`,
  `notes.*`, and every `visual.*` that does not read these five. Every other column of `--sonic-trace` is
  bit-identical before and after on all three tracks. ADR-1421's palette `advance.change` reads `sonic.energy`,
  `brightness` and `density`, so it fires exactly as before.

**Determinism.** The reference and followers are state stepped once per analysis hop inside `ResponseModel::step`,
which `SonicRuntime` replays from zero on a backward seek, as it does the character: seek equals play, offline renders
and the fixed-step clock are unchanged in kind. Nothing touches the audio callback. **Live:** the live session's
runtime is not reset when the switcher opens another live project (`Engine::setLiveSonic(true)` returns while the
session runs), so the reference carries across a scene switch; a test pins it.

## Measurements

Default controls, every frame from 10 s in to 10 s before the end, `avgen --sonic-trace`. p10 / p50 / p90 (share of
frames above 0.9).

| track | signal | before (main 99d45f13) | after | after, -24 dB input |
|---|---|---|---|---|
| Feline | bass | 0.88 / 1.00 / 1.00 (88%) | 0.00 / 0.49 / 0.79 (3%) | 0.00 / 0.49 / 0.78 (2%) |
| Feline | level | 0.99 / 1.00 / 1.00 (98%) | 0.24 / 0.50 / 0.71 (1%) | 0.24 / 0.50 / 0.70 (0%) |
| Feline | sustain | 0.74 / 0.89 / 0.92 (29%) | 0.40 / 0.52 / 0.58 (0%) | 0.39 / 0.51 / 0.58 (0%) |
| Feline | levelSlow | 0.91 / 1.00 / 1.00 (92%) | 0.64 / 0.72 / 0.77 (1%) | 0.64 / 0.72 / 0.76 (0%) |
| Feline | intensity | 0.80 / 1.00 / 1.00 (81%) | 0.63 / 0.71 / 0.73 (0%) | 0.58 / 0.71 / 0.73 (0%) |
| Trench | bass | 0.91 / 1.00 / 1.00 (90%) | 0.03 / 0.47 / 0.73 (2%) | 0.03 / 0.47 / 0.73 (2%) |
| Trench | level | 1.00 / 1.00 / 1.00 (98%) | 0.26 / 0.46 / 0.67 (1%) | 0.26 / 0.46 / 0.67 (1%) |
| Trench | sustain | 0.87 / 0.90 / 0.94 (54%) | 0.38 / 0.51 / 0.58 (0%) | 0.38 / 0.51 / 0.58 (0%) |
| Trench | levelSlow | 0.97 / 1.00 / 1.00 (98%) | 0.54 / 0.70 / 0.79 (0%) | 0.54 / 0.70 / 0.79 (0%) |
| Trench | intensity | 0.89 / 0.99 / 1.00 (89%) | 0.57 / 0.66 / 0.71 (0%) | 0.57 / 0.66 / 0.71 (0%) |
| Hyperborea | bass | 0.81 / 1.00 / 1.00 (83%) | 0.00 / 0.37 / 0.63 (1%) | 0.00 / 0.37 / 0.63 (1%) |
| Hyperborea | level | 0.97 / 1.00 / 1.00 (96%) | 0.00 / 0.41 / 0.62 (2%) | 0.00 / 0.41 / 0.62 (2%) |
| Hyperborea | sustain | 0.81 / 0.91 / 0.96 (55%) | 0.18 / 0.49 / 0.67 (1%) | 0.16 / 0.49 / 0.67 (1%) |
| Hyperborea | levelSlow | 0.96 / 0.98 / 1.00 (95%) | 0.19 / 0.65 / 0.81 (4%) | 0.13 / 0.65 / 0.81 (4%) |
| Hyperborea | intensity | 0.85 / 0.97 / 0.99 (86%) | 0.35 / 0.60 / 0.66 (0%) | 0.34 / 0.60 / 0.66 (0%) |

kickEnv and hatEnv are bit-identical (Feline 0.00 / 0.17 / 0.54 and 0.00 / 0.04 / 0.48). Sensitivity on Feline's
level, p50: 0.17 at 0.2, 0.33 at 0.35, 0.50 at 0.5, 0.66 at 0.65, 0.79 at 0.8.

**The targets** (`tools/check_response_range.py` enforces them): every level at most 15% of frames above 0.9 (10% for
sustain and the slow ones), p50 between 0.3 and 0.7 (0.4-0.8 for levelSlow and intensity), p90 at most 0.95 (0.9),
p10 at most 0.3 for bass and level (0.5 for sustain), and a minimum p90 - p10 spread. The slow levels' p10 is not
bounded: Feline and Trench hold one loudness for most of their length, so the honest 3 s and 12 s levels rest around
0.7 and dip only at the breakdowns. Main misses a target on every one of the 15 track-and-level rows at 0 dB, and on
some at -24 dB (where its fixed window happens to sit nearer the music); this passes all of them at 0 and -24 dB.

**The synthetic test fails on main.** `test_response_range.cpp`'s first two cases, compiled against main's
`response.cpp` (99d45f13): 2 of 2 cases and 28 of 77 assertions fail (level above 0.9 on 77% of the frames at 0 dB,
p50 1.0; at -24 dB the same song read level 0.19 / 0.66 / 0.73, so the 0 dB and -24 dB distributions disagree).

## Alternatives rejected

- **A retuned fixed window** (for example -36..-3 dBFS): holds at one input level only, and no single span suits
  both Trench (6 dB of movement) and Hyperborea (14 dB).
- **Mean and deviation (z-score) per signal**: the quiet tails skew both, and the slow levels came out at 0.45-0.65.
- **Stochastic quantile trackers for a floor and a ceiling**: a step proportional to the current span collapses, and
  could not climb out of a fade-in.
- **Fast floor and ceiling followers**: they pump visibly on every breakdown.

## Consequences

- A quieter interface channel reads the same music the same way; a quiet passage inside a performance still reads
  quiet for about a minute. A long, permanent change of loudness (a set moving to a quieter song) is re-learnt over
  one to two minutes.
- Scenes tuned against levels pinned at 1 now see about half that; the scene-impact check and retunes are in the
  report.
- The first seconds of a piece read against the piece's own first seconds: a sparse intro's hits can reach 1, and a
  drop after a quiet intro reads at the top for a few seconds while the reference rises.
