# ADR-2240: the environment map is the path tracer's environment, and the camera sees what the raster draws

- Status: Accepted (2026-11-26), eng/hybrid-rt (Hybrid RT H1; audit
  `~/Desktop/av-gen-review/115-parallel-engine/20-hybrid-rt/01-audit/AUDIT.md`, defect D1)
- Code: `pathtrace/environment.{hpp,cpp}` (new), `pathtrace/snapshot.{hpp,cpp}`, `pathtrace/path_tracer.cpp`
- Tests: `tests/unit/test_pathtrace_environment.cpp` (`[environment]`)

## Context

A scene with an HDR environment map was reported "degraded" and traced under `scene::skyRadiance`. The analytic sky
defaults to enabled and takes the key light as its sun. Neon Dreams is lit by a moon and an HDR night sky, so every
trace of it showed a daylight sky, a sun disc where the moon key points, and a city lit as if at noon. The tracer
also ignored `environmentIntensity`, `environmentRotation`, `skyIntensity` and `skyTint`, and the camera saw the
lighting sky where the raster draws the background colour (`sky.showBackground` defaults off).

## Decision

1. **Lighting.** With a loaded HDR map, the map is the environment: the map times `environmentIntensity`, turned by
   `environmentRotation` exactly as `shaders/common.wgsl` `envRotate`. With no map, the analytic sky as before, and
   with neither, the background colour as before (unchanged images for those scenes).
2. **The map is light-sampled.** A piecewise-constant distribution over texels (luminance x sin(theta)), MIS-weighted
   with the power heuristic against BSDF sampling. A scattered ray that escapes is weighted against the sampler.
   Lighting reads the nearest texel, the same function the pdf describes. `sample` re-derives its pdf and radiance
   from the direction, so both strategies estimate one integral. Scenes without a map draw no extra random numbers.
3. **What the camera sees** is `scene::skyBackgroundFor`, the raster's rule: the flat colour, the map (bilinear) at
   `skyIntensity` x `skyTint`, or the analytic sky. With no map, the analytic sky is drawn at `sky.intensity` again,
   because the raster's IBL cube already carries the intensity and the background pass multiplies by it once more;
   matched, and noted here rather than fixed in the raster.
4. **Reported as Full.** A substitution is never a degradation. A named map that is not a loaded HDR texture is
   reported as unsupported (the raster does not use it either).

## Consequences

- Neon Dreams' night sky, moon and map lighting reach the trace.
- Tests: the map is used whatever `sky.enabled` says, with the daylight sky as the control; the background rule's
  three cases; rotation against the WGSL formula; the pdf integrates to 1 and agrees with `sample`; light-only,
  BSDF-only and MIS converge on a map-lit floor.
