# ADR-1149: Temper, polish and sharpness vary by region

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, iteration 4, step 6)
**Date:** 2026-10-06
**Resolves:** "per-region temper and polish" (`docs/prototypes/astral-forge/07-iteration-3.md`, retirement item 2:
production was about 30% less colourful) and "sharpness that varies over the face" (§1).
**Implemented by:** `SurfaceRegion`/`SurfaceRegions` and `Material::regions` in `src/scene/scene_types.hpp`;
`readSurfaceRegions`, `surfaceRegionsToJson`, `surfaceRegionWeight` and the `material/regions/*` parameters in
`src/scene/material_engraving.{hpp,cpp}`; `SdfDensitySource::{spread, spreadRadii}` and `density/spread` in
`src/scene/sdf_object.{hpp,cpp}`; refusals in `src/scene/procedural.cpp` and `src/scene/composition.cpp`; the region
records, the header and `SdfObjectUniforms::density3` in `src/rendering/sdf_renderer.{hpp,cpp}`; the regions loop in
`sdfEngrave`, the spread in `sdfDensityFieldOf` (`shaders/sdf_raymarch.wgsl`); `SurfaceDetail::{film, polish}` and
their use in `shaders/pbr_shade.wgsl`.
**Tests:** `tests/unit/test_astral_port.cpp` (`[regions]`: round trip, the weight as stated, refusals by name,
procedural owners refused, the spread's round trip and refusals); `tests/rendering/test_astral_port_gpu.cpp`
("regions: the film thickens and colours near a region point and not far from it").

## Context

The prototype's `featureWeight` (`latent.wgsl`) is high near the eyes and the mouth and drives four things in
`surface.wgsl`: the temper film is thicker there (`temper (330 + 90 noise + 60 fw)` nm) and keeps more of its
chroma, the metal is polished (the reflection lobe narrows from 0.058 to 0.018), and a spectral rim is added. Its
sharpness `S` is local too: `S x mix(1, clamp(1.3 - 0.55 |qc.xy / (2, 2.8)| - 0.2 |qc.z|, 0.15, 1), 0.6)`, so the
anatomy's centre is precise and its periphery stays matter. Production had one film thickness, one roughness and
one sharpness per object.

## Decision

**An SDF material may carry `regions`:**

```json
"regions": {"film": 80, "filmNoise": 30, "noiseScale": 0.9, "polish": 0.69,
            "points": [{"center": [-0.76, 0.72, 0.6], "sharpness": 4, "weight": 1},
                       {"center": [0, -1.45, 0.62], "scale": [0.8, 2, 1], "sharpness": 3, "weight": 0.6}]}
```

At the engraving domain point `q` (ADR-1154): `fw = max_k weight_k exp(-sharpness_k |(q - center_k) scale_k|^2)`.
The film gains `film x fw + filmNoise x n(q noiseScale)` nm (`n` the engraving's panel noise) on top of
`thinFilm.thickness`, and the roughness is multiplied by `1 - polish x fw`. They reach `pbr_shade` through ADR-1152's
`SurfaceDetail` record, two new fields; every other includer returns the zero record, a constant, so for them the
sum is the uniform plus 0 and the product the roughness times 1. The block rides the engraved variant (its header
carries the count and the levels, the points ride after the layer records), so an object with regions and no
layers is drawn by that variant too. Refused by name on procedural, terrain and orb owners. Parameters (only when
present): `material/regions/{film, filmNoise, polish}`.

**A density source may carry `spread`** (0..1) and `spreadRadii` (default `[2, 2.8, 5]`):
`S x mix(1, clamp(1.3 - 0.55 |p.xy / r.xy| - |p.z| / r.z, 0.15, 1), spread)` at the object-local point, in the
density variant's field only. Parameter `density/spread`.

## Consequences

- Without the blocks, nothing changes: no variant is asked for, and the code they add sits behind a constant zero
  record, a zero count or a zero uniform. Measured: ADR-1145's byte-identity table.
- Measured at the T01 formed face (1440x900, GPU p50, two runs): 110.5 ms with regions and a 0.6 spread, 113.8 ms
  without either: the spread makes the periphery cheaper to march (less of it is pulled onto the tree) by more than
  the regions cost. The colour: the T01 comparison's colourfulness is 17.8 against the prototype's 16.9 (iteration 3:
  11.3).
- Not ported: the prototype's chroma retention near features (production's film is ADR-1143's full-chroma Airy
  film, so a thicker film is the colour) and its spectral rim (`look.rimIntensity` exists, ADR-1052, but is not
  weighted by region).
