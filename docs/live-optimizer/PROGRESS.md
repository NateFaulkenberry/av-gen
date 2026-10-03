# Live optimizer: progress (resumable cold)

**Branch:** `live/optimizer` in `/Users/natefaulkenberry/Documents/GitHub/av-gen-opt` (based on `live/quality`).
ADRs 1090-1109 are this stream's. Brief `00-brief.md`, inventory `01-research.md`, plan `02-plan.md`.
Report target: `~/Desktop/av-gen-review/31-live-optimizer/REPORT.md`.

## Build and run
- `cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release` then `cmake --build build/release -j 10` (always `-j`:
  without it the build is serial and takes over 30 minutes). Reconfigure after adding files.
- Profile scripts in the scratchpad: `$SP/prof.sh <tag> <project> <start> [args]` (SP = the session scratchpad
  `.../scratchpad/lo`), results in `$SP/data/<tag>.{txt,json,err,load}`. Each run takes `tools/gpu-lock.sh`.
- Examples:
  - `tools/gpu-lock.sh ./build/release/src/avgen --live-profile --project examples/world/glowmere-valley-2.json --quality ultra --verify-candidates 4 --json out.json`
  - `tools/live_scene_profile.py examples/liminal/all-you-got.json --start 60 --mode live`
- Unit tests: `tools/gpu-lock.sh ./build/release/tests/avgen_tests "[live-profile]"`.

## Stage state
| stage | state |
|---|---|
| 1 `--live-profile` | DONE: both modes, record builder, categories, verdict, CPU split, resources (Dawn memory, pipeline counters), candidates, `--verify-candidates` (headless and live), JSON + text, python wrapper, Stage 5 entity data, AI tools (ADR-1106). Live mode verified on Sonic and Glowmere. |
| 2 levers | DONE in code: lodBias, drawDistanceScale (+ CPU bands), shadowCasterMinPixels, postEffectQuality (taps, not resolution: ADR-1094), particleCullDistance, materialProgramsOff arm, hero/importance (ADR-1097), skip shadows when nothing lit (ADR-1096), profiles (ADR-1099). Profiles not yet measured on all three scenes. |
| 3 panel | DONE in code, visually UNVERIFIED: Live performance section of the Performance panel (graph, categories, inspector, Optimize review with Apply/Cancel/Undo), Live panel profile/lowest/Save live profile/levers; project live block (ADR-1100/1101). |
| 4 pre-warm, minimum, recovery, priority | DONE: async SDF variants + pre-warm (ADR-1102, `--no-prewarm` for A/B), minimum + UNSUSTAINABLE (1103), revert after a failed raise (1104), priority ladder (1105). 60-target tuning: decided in ADR-1107 (frame cap + margin 0.9). |

## Measurements (measured, M2 Max, LG 5K at 60 Hz; data in ~/Desktop/av-gen-review/31-live-optimizer/data/)
- Live mode, 1080p projection, auto 60, floor 0.38 (A2-*): Sonic Medium 71.7-72.0 fps GPU 13.1; Glowmere (22
  characters) Low 82.3-82.4 fps GPU 11.2; Liminal@60 s Low 82.8-83.0 fps GPU 11.5. Agrees with 30-live-quality (Sonic
  after60 72-85 fps; Liminal pinned Low 80 fps / 11.0 ms). Glowmere there had no characters: not comparable.
- Profiles at Ultra, headless (C-*), GPU p50: Sonic 16.52/16.55/13.67; Glowmere 38.34/37.16/22.48; Liminal
  49.25/49.25/30.44 (QUALITY/BALANCED/PERFORMANCE).
- Verified candidates (D-*, headless Ultra): Sonic noprograms -2.69 ms, scale85 -2.29, nodof -0.85, shadowatlas1k
  noise; Glowmere scale85 -8.06, volumequarter -4.52, nomotionblur -4.19, lodbias2 -3.21; Liminal scale85 -12.12,
  volumequarter -8.32, nomotionblur -5.05, posttaps noise (0.20).
- Pre-warm (G-*, Liminal live pinned Low from 30 s): before worst 116/141 ms (main-thread compile at the hall, 36.4 s),
  4 compiles while measuring; after worst 52/50 ms, 0 compiles, 7 variants on Dawn's workers in 0.36 s at load.
  Main-thread compile times seen in a full play without pre-warm: 36, 55, 89, 145, 182, 199 s, 89-114 ms each (warm
  Metal cache).
- The report was refused as a file by the harness; it is in the agent's final message.

## Tests
- CPU `[live-profile]`, `[live-optimizer]` (tests/unit/test_live_profile.cpp, test_live_optimizer.cpp); GPU
  `[live-optimizer]` (tests/rendering/test_live_optimizer_gpu.cpp). All pass (filtered runs, under the lock).

## Full suites (at 807879e7, under the lock, one after the other)
- `avgen_tests`: exit 0 -- 4043 cases: 4023 passed, 19 skipped, 1 failed as expected (the known [!shouldfail]).
- `avgen_render_tests`: exit 0 -- 575 cases: 574 passed, 1 skipped.

## ADR-1107: frame cap and raise margin 0.9 (2026-10-03, integration engineer)
- Code: `liveFrameCapPeriodMs` / `livePaceStep` (interactive_resolution.*), the wait at the top of `runLive`,
  `general.liveFrameCap` (default on), `--live-frame-cap on|off`, the Live panel's "Cap at target"; margin 0.9.
- Commands (scripts and raw data in ~/Desktop/av-gen-review/31-live-optimizer/data/adr1107/):
  `tools/gpu-lock.sh ./build/release/src/<bin> --live-profile --project <p> --start <s> --mode live --target-fps 60
  --size 1920x1080 --measure 15 --adaptive-floor 0.38 [--live-frame-cap off]`; `<bin>` = avgen-before (3c4c772c) or avgen.
- Round Y (valid, machine idle): see the ADR's table. Cap: steady 60.0-60.1 fps, p50 16.6, 0 misses (uncapped 4-16%);
  levels unchanged by the margin (Sonic Medium, Glowmere Low, Liminal Emergency; one capped Liminal run Low).
- Round X: repeat 1 contended (the owner's editor was running: everything at Emergency, GPU ~21 ms, discard);
  repeat 2 found the controller hunting with the interval-minus-wait reading, which was then removed (see the ADR).
- Unit tests: `the raise margin is 0.9 ...`, `the frame cap's period ...`, `the frame cap's steps ...`,
  `the live frame cap round-trips and defaults on`.

## Next
All stages done. Remaining (owner): visual review of the panels and of the profiles/new low levels on a projector; the
60-target tuning decision; merge order (live/quality first). Possible follow-ups are listed in the final report.
