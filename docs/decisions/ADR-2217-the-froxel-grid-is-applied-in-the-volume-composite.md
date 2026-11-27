# ADR-2217: the froxel grid is applied in the volume composite

- Status: Accepted (2026-11-26), eng/115-volumetrics (programme 115, Agent 2, B1). The separable premultiplied layer for
  the hybrid tracer (H3) is the same apply at a supplied depth, B5.
- Code: `shaders/volume.wgsl` (`froxelComposite`, `marchComposite`, `fs_composite`; `VolumeUniforms.froxel`,
  `froxelGrid`; bindings 6 and 7), `VolumeRenderer::update`

## Decision

1. No new full-screen pass. On a tile-based GPU a full-screen pass costs a tile load and store of the frame whatever
   its shader does; the volume composite already pays it. `fs_composite` takes one trilinear tap of the integrated grid
   at the pixel's depth and composites `air + T_air * (march + T_march * hdr)` through the existing
   `One, SrcAlpha` blend. With the grid off the shader takes the march-only branch, which is the composite it was.
2. The tap is clamped to the hand-over distance along the ray, and inside the first slice the stored integral is
   scaled by the fraction of the slice in front of the pixel, so the air from the eye on is counted once.
3. The composite always binds a grid: the renderer's integrated volume, or a 1x1x1 placeholder it never samples.
4. With the grid on and no placed media, the march pass is skipped; the composite still runs for the grid.
