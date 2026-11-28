# ADR-2213: volumetric lighting's tiers scale its sampling only, and it is never off

- Status: Accepted (2026-11-26), eng/115-volumetrics (programme 115, Agent 2, B1; the live ladder's levers are B4)
- Amended 2026-11-28 (the tier finding below)
- Extends: ADR-035 (a tier scales sample counts, resolutions and history lengths only), ADR-139
- Code: `QualitySettings::volumetricLight{Tile,Slices,Subsamples,LocalLights}` (`render_quality.hpp`),
  `volumetricLightGrid` (`volumetric_light_medium.cpp`)

## Decision

| Tier | Tile (px per froxel) | Slices | Grid at 1080p | Subsamples (stratified, fixed) | Local lights per froxel |
|---|---|---|---|---|---|
| Preview (and the Draft view's Reduced treatment, B4) | 24 | 32 | 80x45x32 | 1 | 4 |
| Realtime | 12 | 64 | 160x90x64 | 2 | 8 |
| High | 8 | 128 | 240x135x128 | 2 | 16 |
| Offline | 8 | 128 | 240x135x128 | 4 | 32 |

The shadow tap is the same 2x2 comparison box at every tier: not a tier lever.

The grid's xy follows the frame through the tile; z is fixed per tier. Whether a light lights the air, whether the air
is shadowed, the density, colour, anisotropy and distance are the scene's and identical at every tier. No tier and no
rung turns volumetric lighting off; the bottom of the ladder is a coarse grid. Budgets (STAGE-A-PLAN §2.6): Emergency /
Draft <= 0.15 ms, Low <= 0.3 ms, Realtime <= 0.5 ms and 16 MB, High <= 2.0 ms and 64 MB, at 1080p on the M2 Max.
Amplified measurements before the change: Preview 0.11 ms, Realtime 0.25 ms or less, High 0.62 ms (including the
apply); the merge note records the re-measurement after it.

## The tier finding (full gate at 1cea2e781, 2026-11-27)

The first table varied the shadow taps by tier (1 at Preview and Realtime, 2x2 at High and Offline) and took one
sample a froxel below Offline. Lab e06 (narrow 20-degree spot cones) at 640x360 then moved 12.2% in mean luminance
between Realtime and Offline from volumetric sampling alone, and the whole-tier switch moved it 20.5% with volumetric
lighting on against 2.75% with it off: the grid amplified the tier shift. ADR-112's shadowed range is already constant
across tiers, so the cause was the sampling itself -- a coarse grid sampling a narrow cone once per froxel, and a tap
count that changed what the shadow test returned. Decision (orchestrator): two stratified samples at Realtime and High,
and one shadow tap pattern for every tier; the isolated tier test keeps its 10% bound.

## Consequences

- Memory is two RGBA16F volumes: 1.8 MB (Preview), 14.1 MB (Realtime), 63.3 MB (High and Offline) at 1080p. 4K at
  Realtime is 320x180x64, 56 MB; the live ladder will cap it (B4).
- The live ladder's ceilings for these fields arrive in B4 with the rest of the "volumes" lever group.
