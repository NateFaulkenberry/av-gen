# ADR-2134: the cloud march follows the live quality ladder

- Status: Accepted (2026-11-24), eng/clouds-quality (Neon Dreams editor performance, fix 1; investigation
  `112-neon-dreams/02-ui-perf-investigation/REPORT.md`: "`CloudRenderer` never reads `QualitySettings`")
- Extends ADR-2100 (Cloud Environment v2) and the live ladder (ADR-1083, ADR-1094..1105); the same shape as ADR-1651's
  aurora lever. Keeps ADR-2131's banded march.
- Code:
  - `src/rendering/render_quality.hpp`: `QualitySettings::cloudResolutionScale`, `cloudStepScale`,
    `cloudLightStepScale`, `cloudErosionOctaves` (neutral at every tier).
  - `src/rendering/cloud_march_quality.{hpp,cpp}`: `cloudMarchQuality(clouds, quality)`, the knobs one frame uses.
  - `src/rendering/cloud_renderer.cpp`: `encode` takes the march's resolution, steps, light taps, growth and octaves
    from it, and so do ADR-2131's bands.
  - `src/app/interactive_resolution.{hpp,cpp}`: `LiveQualityRung`'s four ceilings, `kCloudMarchByLevel` applied to all
    three ladders (`withCloudMarch`), the priority ladder's `volumes` group, the levers `cloudres`, `cloudsteps`,
    `cloudlight`, `cloudoctaves` and their ceiling JSON keys.
  - `src/rendering/scene_renderer.cpp`: the same four names as quality arms (`--quality-arm`, `--ab`).
  - `src/rendering/quality_policy.hpp`: part of "a render is exactly as authored".
  - `src/app/application.cpp`: the level line names the cloud levers.
- Tests: `tests/unit/test_cloud_live_quality.cpp` (`[adr2134]`), `tests/rendering/test_clouds_live_quality_gpu.cpp`
  (`[adr2134]`).

## Context

The cloud Environment read its cost knobs -- `steps`, `lightSteps`, `resolution`, `erosionOctaves` -- from the clouds
block and nothing else. The live ladder lowered the scene's resolution, and the march (a fraction of the frame) shrank
with it, but nothing else. Neon Dreams ships its offline block (128 steps, 6 light taps, 0.5 resolution, 3 octaves), and
on the 5K canvas (3600x1914) the ladder reached Emergency in ~40 s with the clouds still ~150-190 ms of a ~270 ms GPU
frame against a 29 ms budget. The project's own `build.py --quality live` preset (40 steps, 3 taps, 0.4, 2 octaves)
took the frame to 120 ms, but nothing in the app selects it.

## Decision

1. **Four levers, neutral by default, lowered only by the live ladder.** `cloudMarchQuality` applies them to the
   block:
   - resolution x `cloudResolutionScale` (never under 1/8 of the frame);
   - view steps x `cloudStepScale`, rounded, at least 8;
   - light taps x `cloudLightStepScale`, at least 1 where any were authored (0, the analytic shadow, stays 0);
   - erosion octaves at most `cloudErosionOctaves`.

   At 1 / 3 it returns the block's own numbers, not ones recomputed through the lever, so a frame with neutral settings
   is the frame before this ADR bit for bit. No tier sets them; `assertOfflineIsUncompromised` requires them neutral.
2. **Fewer steps reach as far.** The v2 march grows its step with distance (`max(minStep, t x stepGrowth)`), so a
   march with fewer steps would end sooner and the far cloud would vanish -- the stipple ADR-2111 removed by raising
   72 to 128 steps. Its reach from where it starts is (1 + g)^N, so the growth becomes (1 + g)^(N/N') - 1 for N' steps.
   The even march (ADR-2109) divides its span by the count and needs nothing. `minStep` is left alone, so the near fade
   (two minimum steps) does not move.
3. **The rows, by level, the same on every ladder** (`kCloudMarchByLevel`):

   | level | resolution | steps | light taps | octaves | Neon Dreams (0.5, 128, 6, 3) becomes |
   |---|---|---|---|---|---|
   | Ultra | 1 | 1 | 1 | 3 | as authored |
   | High | 1 | 0.75 | 1 | 3 | 0.5, 96, 6, 3 |
   | Medium | 0.8 | 0.5 | 0.67 | 3 | 0.4, 64, 4, 3 |
   | Low | 0.8 | 0.33 | 0.5 | 2 | 0.4, 42, 3, 2 (about the `--quality live` preset) |
   | Emergency | 0.6 | 0.25 | 0.34 | 2 | 0.3, 32, 2, 2 (cheaper than the preset on every axis) |

   The priority ladder (ADR-1105) gives them up with the `volumes` group: the Medium row a little, the Low row fully.
   Not counted in `effectChanges`, like ADR-1651: a gain only where a scene has clouds.
4. **The bands follow the march it runs.** ADR-2131's `cloudMarchBands` is given the reduced steps and taps, so the
   watchdog bound holds for every level and a reduced live march is one pass where the authored one was several.

## Measured

See the evidence folder `112-neon-dreams/03-engine-perf-fixes/clouds-quality/` (README.md).

## Consequences

- Offline renders and every tier are unchanged by construction; hashed before and after on Neon Dreams, a clouds-only
  Neon Dreams and the biovalley vista and meadow shots.
- The live preview of a cloud scene at Medium and below is softer and grainier (fewer steps, a coarser march; the
  temporal resolve hides most of the grain at rest). Captures per level are in the evidence folder.
- The ladder's cost model (step ratios) learns the new steps on the way down as it does for any lever.
