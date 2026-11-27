# ADR-2211: one medium description, read by the froxel grid, the march, the surface fog and (later) the tracer

- Status: Accepted (2026-11-26), eng/115-volumetrics (programme 115, Agent 2, B1)
- Code: `shaders/volumetric_light.wgsl` `vlDensityAt`; `rendering/volumetric_light_medium.cpp` `airDensityAt` (the CPU
  twin); `VolumeRenderer::surfaceFogStart`; `VolumeRenderer::update` (the environment-air hand-over)
- Tests: `test_volumetric_light.cpp` ("The medium on the CPU..."), `test_volumetric_light_gpu.cpp` (the analytic
  transmittance check, lab case 201; the hand-over)

## Context

ADR-705 made the march and the surface fog one law: one density, one Beer-Lambert extinction, the march out to
`volumeMaxDistance` and the surface pass beyond it, each metre counted once. A third renderer of the same air must
join that law, not add a slightly different air. The hybrid ray tracer will later sample participating media only
if the medium is evaluable on the CPU (STAGE-A-PLAN §2.2, hybrid RT plan §5.5).

## Decision

1. **The medium is the environment's existing controls**: `volumeDensity` through the shared height-fog profile
   (`height_fog.wgsl`'s `fogHeightProfile`, with `fogUpperDensity`, `fogHeightCurve`, `fogGroundFollow` and pooling
   through the frame's own lanes), ADR-705's `horizonDensity`, the noise term; extinction `density * volumeAbsorption`,
   scattering `density * volumeScattering`, emission `density * volumeEmission * fogColor`. Volumetric lighting adds no
   second density.
2. **The hand-over.** With volumetric lighting active, the grid carries the environment's air from the eye to
   `volumetricLight.distance` *along the ray* (the apply clamps its tap to that distance, so view depth and ray
   distance agree at the frame's edges); `surfaceFogStart` returns that distance, so the surface fog integrates the
   same air beyond it; the march sets its environment density to 0 and marches the placed media only.
3. **On the CPU**: `airDensityAt(env, p, distanceFromEye)` is the same profile and horizon term, without the three
   terms that need GPU data (ground follow, noise, a density field). A scene naming a density field is declined
   (ADR-2210 §5) rather than approximated.

## Consequences

- Lab case 201 (e02): the grid's transmittance at slabs 4-60 m matches `exp(-sigma_t d)` within 3%.
- The particle fog coupling reads the grid's distance as its march reach when volumetric lighting is on.
- The integration differs from the march's (exact per slice vs a Riemann sum per step), so turning volumetric
  lighting on changes a fogged frame slightly even where no light scatters; that is the more accurate answer.
- Local volumes (box and sphere `fogVolume`s, proposed ADR-2215) and the media's extinction join this description in B3.
