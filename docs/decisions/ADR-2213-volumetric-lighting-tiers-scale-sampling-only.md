# ADR-2213: volumetric lighting's tiers scale its sampling only, and it is never off

- Status: Accepted (2026-11-26), eng/115-volumetrics (programme 115, Agent 2, B1; the live ladder's levers are B4)
- Extends: ADR-035 (a tier scales sample counts, resolutions and history lengths only), ADR-139
- Code: `QualitySettings::volumetricLight{Tile,Slices,ShadowTaps,Subsamples,LocalLights}` (`render_quality.hpp`),
  `volumetricLightGrid` (`volumetric_light_medium.cpp`)

## Decision

| Tier | Tile (px per froxel) | Slices | Grid at 1080p | Shadow taps | Subsamples | Local lights per froxel |
|---|---|---|---|---|---|---|
| Preview (and the Draft view's Reduced treatment, B4) | 24 | 32 | 80x45x32 | 1 | 1 | 4 |
| Realtime | 12 | 64 | 160x90x64 | 1 | 1 | 8 |
| High | 8 | 128 | 240x135x128 | 2x2 | 1 | 16 |
| Offline | 8 | 128 | 240x135x128 | 2x2 | 4 (fixed offsets) | 32 |

The grid's xy follows the frame through the tile; z is fixed per tier. Whether a light lights the air, whether the air
is shadowed, the density, colour, anisotropy and distance are the scene's and identical at every tier. No tier and no
rung turns volumetric lighting off; the bottom of the ladder is a coarse grid. Budgets (STAGE-A-PLAN §2.6): Emergency /
Draft <= 0.15 ms, Low <= 0.3 ms, Realtime <= 0.5 ms and 16 MB, High <= 2.0 ms and 64 MB, at 1080p on the M2 Max.

## Consequences

- Memory is two RGBA16F volumes: 1.8 MB (Preview), 14.1 MB (Realtime), 63.3 MB (High and Offline) at 1080p. 4K at
  Realtime is 320x180x64, 56 MB; the live ladder will cap it (B4).
- The live ladder's ceilings for these fields arrive in B4 with the rest of the "volumes" lever group.
