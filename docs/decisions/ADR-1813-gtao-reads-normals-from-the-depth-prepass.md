# ADR-1813: GTAO reads surface normals written by the depth prepass

- Status: Accepted (eng/render-improvements, 2026-10-28). Render audit prototype P3 (gap G6), approved by the owner.
- Code:
  - `shaders/pbr.wgsl`: `fs_depth_normal` (and `depthPrepassClip`, the discards `fs_depth` and it share);
    pbr_skinned.wgsl includes it.
  - `shaders/gtao.wgsl`: binding 4 `prepassNormal`, `surfaceNormal`.
  - `src/rendering/scene_renderer.{hpp,cpp}`: `prepassNormal_` (RGBA16Float), `depthNormalPipeline_`
    (`createDepthOnlyPipeline(module, true)`), the split prepass.
  - `src/rendering/skinning.{hpp,cpp}`: `depthNormalPipeline()`.
  - `src/rendering/ao_renderer.{hpp,cpp}`: the fifth binding; `update(..., prepassNormal, ...)`.
  - `src/rendering/render_quality.hpp`, `quality_policy.hpp`, the `aodepthnormals` arm.
- Test: `tests/rendering/test_ao_prepass_normals_gpu.cpp` (`[adr1813]`).

## Problem

GTAO (ADR-034) runs before the lit pass writes its normal target, so it rebuilt each pixel's normal from depth
differences (`depthNormal`). On long, thin, nearly edge-on triangles at landscape scale the differences across one
pixel mix two facets, the normal flips between them, and the horizon search reads a facet edge as an occluder: dark
fins along the triangles. The Strange Attractor canyon switched GTAO off for this (ADR-1857/1859, on its branch). The lit
pass shades with the interpolated vertex normal, which has no facet edges. Godot writes normal-roughness in its depth
prepass for exactly this reason.

## Decision

- **With GTAO on, the depth prepass is two passes.**
  1. Entities, skinned meshes and meshed SDFs (every surface with interpolated vertex normals) draw with a depth+normal
     variant into the depth buffer and an RGBA16Float target: rg = the octahedral world normal (the vertex normal,
     turned to face the camera on a back face), b = the axial view depth it was written at, a = 1.
  2. Procedurals and raymarched SDFs draw depth-only, loading the depth, as before.

  This keeps the heavy procedural and SDF pipelines as they are: none of them needs a normal-writing variant.
- **GTAO uses the prepass normal where it belongs to the visible surface**: where a = 1 and its stored depth matches the
  linear depth to f16 precision (0.2%), and it faces the camera. Everywhere else (a procedural or an SDF drew over the
  entity, or nothing wrote one) it falls back to `depthNormal`, unchanged.
- **The occluder threshold rises with the facet's tilt.** The depth buffer still holds the facets, so a sample on the
  pixel's own facet stands above the smooth normal's tangent plane by its distance times the sine of the angle between
  the vertex normal and the facet normal. The first version, a plain swap of normals, read that as occlusion: on the
  test wall it made GTAO darker and grainier than before (mean darkness 0.024 -> 0.052). So where the prepass normal is
  used, a sample must clear `occluderBias + dist * |n_vertex x n_facet|`. Where the two normals agree, nothing changes;
  without a prepass normal the term is 0 and the code is the old code.
- **The `aodepthnormals` arm** (`QualitySettings::aoDepthNormals`) restores the previous frame exactly: one depth-only
  prepass, normals from depth.
- With GTAO off, nothing changes: one depth-only prepass, no normal target written.

## Consequences

- **The fixture** (`[adr1813]`): a wall 3 km long of 5 m strips, one triangle pair each along the whole length, a few
  metres of undulating relief, smooth analytic vertex normals, seen along its length; the occlusion debug view.
  Depth-derived normals draw hard bands along the strip edges and grain on the strips; prepass normals give a smooth
  term. Mean darkness on the wall 0.024 -> 0.013; pixels under 75% visibility 392 -> 73. What remains is the wall's real
  relief and a few strip-edge pixels where GTAO falls back. In the lit frame the difference is subtle at the default AO
  strength; the occlusion view is where it shows.
- **The reference scenes** (picture comparison, ADR-1810, offline, 960x540): Aurora, Rainbow World and Talking Heads
  SAME, Event Horizon WITHIN (max 2 levels), **Rift DIFFERS by at most 3 levels on 0.13-0.26% of pixels**, all on its
  ground. The difference map is triangle-shaped: the old term varied facet by facet (the fin mechanism at small
  amplitude) and the new one does not. Rainbow World has GTAO off at this tier, so nothing changes there.
- **Cost** (`--ab aodepthnormals`, 1080p, interleaved): Talking Heads 10.85 against 10.77 ms GPU (+0.07 ms, inside the
  2% floor); its depth passes 2.03-2.07 ms against 1.98 (about +0.06 ms for the normal target). Rainbow World does not
  run GTAO at this tier: +0.38 ms is noise (NOT A RESULT). Rift's run drifted (VOID); its depth passes read 2.17-2.32
  against 2.43-2.44 ms, no measurable growth. The audit's bar was +0.3 ms or less.
- Memory: one RGBA16Float target at full resolution (16.6 MB at 1080p). The canyon itself is on another branch, so the
  shot the defect was reported on could not be rendered here; the fixture is its geometry.
- Review images: `~/Desktop/av-gen-review/103-render-improvements/p3-ao-normals/`.
