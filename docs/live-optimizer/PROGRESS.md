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
| 4 pre-warm, minimum, recovery, priority | DONE: async SDF variants + pre-warm (ADR-1102, `--no-prewarm` for A/B), minimum + UNSUSTAINABLE (1103), revert after a failed raise (1104), priority ladder (1105). 60-target tuning NOT touched. |

## Measurements so far (measured, M2 Max, headless 1920x1080 unless said)
- Glowmere at Ultra, `--verify-candidates 4` (2 counterbalanced pairs each): scale85 -7.86 ms GPU, volumequarter
  -4.65, nomotionblur -4.19, lodbias2 -3.21; all outside the 2% floor. Estimates were 5.6-7.5, 3.4-4.9, 3.6-4.2,
  0.7-4.6.
- Liminal at 60 s: SDF tree variant compiled in 807 ms at first use (the mid-run compile Stage 4 removes).

## Tests
- CPU `[live-profile]`, `[live-optimizer]` (tests/unit/test_live_profile.cpp, test_live_optimizer.cpp); GPU
  `[live-optimizer]` (tests/rendering/test_live_optimizer_gpu.cpp). All pass (filtered runs, under the lock).

## Next
1. Final measurement set (scratchpad `$SP/final.sh`): live mode 3 scenes x 2 (agreement with the live-quality matrix),
   profiles x 3 scenes, Liminal pre-warm before/after, verify-candidates on each scene.
2. REPORT.md, both full suites at the end.
