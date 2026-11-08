# ADR-1959: A lattice of rigid slabs draws three quads, without the deformer chain

- Status: Accepted (Musical Geography Phase 2, 2026-11-08), scene/musical-geography.
- Code:
  - `ProceduralGeometry::slabs` (`"slabs": true`, written only when on) and `makeSlabMesh`, in
    `src/scene/procedural.{hpp,cpp}`;
  - `ensureSlabMesh`, the three slab pipelines and the eligibility rule, in `src/rendering/procedural_renderer.cpp`;
  - `slabVertex`, `vs_slab`, `vs_slab_depth` and `vs_slab_shadow`, in `shaders/procedural.wgsl`.
- Tests:
  - `[procedural][geography][adr1959]` in `tests/unit/test_procedural_slabs.cpp`;
  - `[gpu][procedural][geography][adr1959]` in `tests/rendering/test_procedural_slabs_gpu.cpp`.

## Context

The Phase 1 spike measured the general procedural path at about 0.2 µs of GPU per visible tile (scene plus depth).
At quarter resolution the time barely fell (32.9 → 29.9 ms at 512²), so the cost is the vertex stage, not the
pixels. Every 12-triangle box ran the uber vertex stage: the deformer chain, the fibre and point paths, the previous
frame's position. The shadow views drew every tile in every cascade (15 ms at 65k tiles).

## Decision

1. **Three quads, turned.** A convex box shows at most one face per axis. The slab mesh is the +x, +y and +z faces.
   The vertex stage turns each quad to the side of the box that faces the camera, and in a shadow view to the side
   that faces the light. A turn is a half rotation, never a mirror, so the winding stays CCW and back-face culling
   holds in every pass.
2. **A slim vertex stage.** `slabVertex` reads the record once, places the vertex and returns. Its depth and shadow
   entries skip the velocity.
3. **Eligibility is provable, and refusal is silent.** A box source with no bevel, no enabled deformer, no wind, no
   effect lanes and no refraction. Anything else asking for slabs draws the general way. Further LOD levels keep
   their own meshes and the general path.

## Consequences

Measured on the spike scenes, 1080p Ultra, M2 Max, under the GPU lock. A build was compiling during these runs, so
the P95s are inflated; the medians are the comparison.

| Grid | General path GPU p50 | Slab path GPU p50 | Shadow pass |
|---|---|---|---|
| 256², shadowed | 27.46 ms | 8.44 ms | 15.07 → 1.28 ms |
| 512², shadowed | 66.0 ms | 14.86 ms | 33.1 → 3.86 ms |
| 768², no shadow | 69.65 ms | 14.22 ms | — |

- Scene plus depth costs 0.033 µs per visible tile at 361k visible tiles, and 0.019 µs per tile marginal. That clears
  the Phase 2 go/no-go of 0.06 µs per tile and of 12 ms for the 256² window with shadows.
- The GPU test renders a turned, uneven lattice both ways, from above, from the side and from below, with a shadowed
  key and the prepass. Fewer than 0.5% of the pixels differ by more than 6 levels.

## Amendment (2026-11-08, CP-A): far sides shade as the land

The CP-A reveal stills showed bright and dark stripes banding across slopes in the middle distance. Each tile's side
face is a one- or two-pixel sliver there, lit by the sun or in shadow, and a slope of them aliases.

In the lit pass, a side face's normal now eases toward the top's normal:
- fully at the side's own normal while the tile is at least 8 pixels across;
- fully at the top's normal by 3 pixels.

Far steps then read by their outline, not by aliased slivers of light. Depth and shadow passes are untouched.
