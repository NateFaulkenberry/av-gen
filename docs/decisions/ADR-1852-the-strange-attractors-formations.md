# ADR-1852: The Strange Attractor's formations, branching network and band cut

- Status: Accepted, scene/strange-attractor (Gate 2, 2026-10-27).
- Code:
  - `src/scene/attractor_formations.{hpp,cpp}`: the integrator, resampling and the tube volume.
  - `src/scene/attractor_world.cpp`: pass 1c (the network), `bandProfile`, `placeFormations` and `attractorCamera`.
  - `shaders/attractor/formations.wgsl`; in `shaders/attractor/world.wgsl`: `sceneDist`, `bandCut`, `gradientSmooth`.
- Tests: `[adr1852]` in `tests/unit/test_attractor_world.cpp`.

## Decision

**The attractor ribbon.**
- **Integration.**
  - Rössler (a = b = 0.2, c = 5.7), or Thomas (b = 0.19) as the fallback.
  - Fixed-step RK4: dt 0.01 for Rössler, 0.05 for Thomas.
  - A seeded transient of 4,000-7,000 steps is discarded, and about 2,700 steps are kept (about four turns of the
    Rössler spiral and its fold).
  - A non-finite or diverging state (|s| > 1e4) stops the integration rather than overflowing.
- **Placement.**
  - The spiral is stood up: its x runs across the view, turned 35-50 degrees; its y is vertical; the fold sweeps
    toward or away from the camera at 0.55 scale.
  - It is scaled to 2R across, with R = 380 m × formationScale × terrainScale × (0.6 + 0.4 influence).
  - 32% of it is buried, so the loops become nested arches over the gorge.
- **The tube.** The curve is resampled to 640 equal segments; its radius tapers from 1.25 to 0.7 × 0.06R with height.
- **The bake.**
  - A 160³ RG32F volume holds the distance and the nearest segment, computed with per-block candidate culling.
  - The shader samples it trilinearly, then refines exactly over ±6 segments within three voxels of the surface.

**The recursive landmarks.** Both are analytic signed distances, evaluated only inside a bounding sphere.
- **The butte tower.** Each tier repeats one outline (the same noise in the tier's normalised frame) at 0.55 the
  radius and 0.8 the height, rotated and offset, with a talus flare at its foot.
- **The arch fin.** A fin pierced by an arch, and inside that arch a 0.38-scale fin pierced by its own arch, to
  `recursiveDepth` levels.

**Composition and filleting.**
- The landmarks and the ribbon are placed in the composed view by a search: attractor centre, tower one third, fin
  the other third.
- Each placement checks the camera can see the base.
- Formations use their own random stream, so formation knobs never move the terrain or the camera (tested).
- The pass marches one scene distance: the terrain's clearance, smooth-min filleted (22 m) into the formations.
  Formations cast shadows through the same soft-shadow march.

**The branching network (pass 1c).**
- Tributary gorges grow off the inner gorge's centreline, which is solved from the meander warp by fixed-point
  iteration. Junctions sit at uneven spacing and mostly alternate sides.
- Each branch is a random walk with a slow curl. It tapers and stops at the talus.
- A second level grows off the first. At most 48 branches.
- Branches are carved by splatting segments, never below the gorge floor. A network-distance field is kept for the
  audio stage's travelling light.

**The streak fix, which is part of this pass.** Cause, from the code:
- the shading normal came from finite differences of the bilinear surface;
- a bilinear patch's contours turn at every texel edge;
- so each texel column of a steep face faced a different way, which drew curtains one texel wide.

Fix:
- the normal now comes from the gradient of a cubic B-spline over 4×4 texels (C2);
- the cavity mask, a 2D image that is meaningless on a face, is faded out on steep slopes;
- gullies narrower than 7 texels are no longer cut.

The bands read as paint because a face spans less than a texel, so the generator's terraces could not show. The
generator now bakes the strata offset (tilt, warp and fold) beside the height. The shader cuts the same band profile
again on steep faces, at sub-texel scale: hard bands become cliffs with a ledge, soft bands keep their slope. The
colour bands are keyed to the same band index.

**Camera drift.** `attractorCamera(world, drift, seconds)` is a pure function of time, with a 150 s period, up to
140 m lateral and 60 m of dolly. The aim moves 35% as far as the eye.

## Consequences

- Generation costs about 1-2 s more (the 160³ bake).
- The march evaluates the formations at every step inside their bounds. The cost is unmeasured until Stage 6.
- The tower's seed comes from its slot, not from `AttractorLandmark::seed`. It is deterministic, and the record's
  seed is unused by the shader.
