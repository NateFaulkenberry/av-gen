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
| 1 `--live-profile` | DONE in code: both modes, record builder, categories, verdict, CPU split, resources (Dawn memory, pipeline counters), candidates, `--verify-candidates`, JSON + text, python wrapper, Stage 5 entity data. AI tool: NOT YET. Live-mode run: NOT YET verified. |
| 2 levers | DONE in code: lodBias, drawDistanceScale (+ CPU bands), shadowCasterMinPixels, postEffectQuality (taps, not resolution: ADR-1094), particleCullDistance, materialProgramsOff arm, hero/importance (ADR-1097), skip shadows when nothing lit (ADR-1096), profiles (ADR-1099). Profiles not yet measured on all three scenes. |
| 3 panel | NOT STARTED |
| 4 pre-warm, minimum, recovery, priority | NOT STARTED |

## Measurements so far (measured, M2 Max, headless 1920x1080 unless said)
- Glowmere at Ultra, `--verify-candidates 4` (2 counterbalanced pairs each): scale85 -7.86 ms GPU, volumequarter
  -4.65, nomotionblur -4.19, lodbias2 -3.21; all outside the 2% floor. Estimates were 5.6-7.5, 3.4-4.9, 3.6-4.2,
  0.7-4.6.
- Liminal at 60 s: SDF tree variant compiled in 807 ms at first use (the mid-run compile Stage 4 removes).

## Next
1. Live-mode profile on the three scenes; compare with `30-live-quality/data/matrix-tables.txt`.
2. Profiles measured on the three scenes (quality/balanced/performance, pinned Ultra).
3. AI tools `performance.profile_scene`, `get_live_quality`, `set_live_quality`.
4. Stage 3 panel, Stage 4.
