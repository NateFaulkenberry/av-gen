# ADR-2210: volumetric lighting is a froxel grid fed by the existing lights, clusters and shadow views

- Status: Accepted (2026-11-26), eng/115-volumetrics (programme 115, Agent 2, B0-B1). Research:
  `~/Desktop/av-gen-review/115-parallel-engine/10-stage-a/02-volumetrics/RESEARCH-REPORT.md`; plan approved in
  `10-stage-a/STAGE-A-PLAN.md` §13
- Code: `rendering/volumetric_light_renderer.{hpp,cpp}`, `rendering/volumetric_light_medium.{hpp,cpp}`,
  `shaders/volumetric_light.wgsl`, `scene/volumetric_light.hpp`; the hook in `SceneRenderer::render` before the
  volume pass; the apply in `shaders/volume.wgsl` `fs_composite` (ADR-2217)
- Tests: `tests/unit/test_volumetric_light.cpp`, `tests/rendering/test_volumetric_light_gpu.cpp` (`[volumetric][light]`)

## Context

The volume march (ADR-032) lights the air with the eight frame lights and the froxel's local lights, but it has no
geometry shadowing at all ("There is no shadowing in the fog", `volume.wgsl`). So it cannot draw a shaft: the
picture the owner's spec asks for -- visible light interacting with the medium, not a fog overlay. Its cost is
pixels x steps x lights, and a shadow tap per light per step would multiply it. Stage A measured a froxel grid on
this machine (M2 Max, 1080p): 160x90x64 with eight shadowed lights about 0.1 ms, the heaviest plausible High grid
about 1.6 ms, against 7.5-14 ms for today's unshadowed march in the same scenes.

## Decision

1. **A camera-aligned froxel grid** (`VolumetricLightRenderer`), exponential in view depth from the eye out to
   `environment.volumetricLight.distance`. Two compute passes: **inject** (per froxel: the medium, ADR-2211, and the
   light it scatters toward the eye) and **integrate** (per column, front to back, Hillaire 2015's exact step
   `S (1 - e^{-sigma dz}) / sigma`). The apply is one trilinear tap in the volume composite (ADR-2217).
2. **One lighting architecture.** The inject reads the frame uniforms' eight lights with their
   `volumetricStrength`, the packed light buffer, the cluster lists, `ShadowUniforms` and the shadow atlas -- the
   surfaces' own resources, re-bound in a compute-visible group built by this renderer (the frame group's layout is
   fragment-only and is not changed). Each frame light finds its shadow view through its index in the packed buffer.
   No light list, shadow map or light type of its own (ADR-2214).
3. **What lights the air** is what the march's model says: the frame lights (directional, point, spot) by
   `volumetricStrength`, now with their shadow views; the clustered local lights by `volumeLocalLights` (unshadowed
   in B1; their shadows are B2); the particle glow (ADR-040); emission. Phase: Henyey-Greenstein at
   `volumeAnisotropy`. `volumetricLight.intensity` scales the scattered light only.
4. **The scene block** is `environment.volumetricLight {enabled, distance, intensity}`, parameters
   `scene/volumetric/*`. Off by default. Quality is the tier's (ADR-2213).
5. **Never silent.** A scene that asks and cannot be served (no air; a density or colour field, which the grid does
   not carry yet) is declined with a reason that is logged once and reported in `RenderStats::volumetricLight`;
   the march then keeps the air exactly as before.

## Consequences

- Off is byte-identical: no allocation, no pass, and the composite takes the march-only branch.
- The cluster grid (16x8x24) and the froxel grid are different grids over the same log-z law; the inject looks a
  froxel's lights up in the cluster it falls in.
- Compute timestamps are unwritten on this Dawn/Metal path, so `froxel.inject` / `froxel.integrate` read 0; budget
  the pass by amplification or frame A/B.
- Placed media (fog banks, tornado, vortex) stay in the march; their extinction does not yet enter the grid (B3), so
  a shaft is not yet blocked by a fog bank.
