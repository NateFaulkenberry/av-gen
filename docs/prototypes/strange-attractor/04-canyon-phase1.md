# Canyon Phase 1: engine-native terrain conversion

This covers research, approach, baseline, measurements and verdict. The governing brief is `03-canyon-brief.md`. The
decision record is `docs/decisions/ADR-1857-the-canyon-is-engine-geometry.md`.

## 1. The source of truth (brief §3.1)

**The terrain is a single-valued height field. It has no overhangs, no multiple surface layers, and no implicit
volume.**
- `scene::generateAttractorWorld` (`src/scene/attractor_world.cpp`) bakes it on the CPU at 4096² over 16 km (3.9 m
  texels). The bake is deterministic from the seed and the structural knobs.
- What the reference drew is that bake plus three shader-side operations in `shaders/attractor/world.wgsl`:
  - `fieldSmooth`, a C2 cubic B-spline of the baked height and strata offset (its value and gradient);
  - `bandCut`, a soft metre-scale ledge cut along the strata;
  - `detailAt`, metre-scale relief within about 1 km of the camera.

  It was all ray-marched per pixel in a custom Environment pass (ADR-1850), with the pass's own sun, soft shadow, fog,
  sky and ray-marched river reflection.
- The edge artefacts the owner reported came from that ray-march: sub-texel ledges, the march step, shadow
  self-intersection and terminator aliasing (catalogued in `gate2e-artifact-catalogue.md`).
- Every one of those causes belongs to the per-pixel ray-march or to sub-texel shader geometry. A triangle mesh has
  none of them.

**Engine paths found and used:**
- **Meshes and entities.** `Composition` flattens nodes into `scene_.meshes` and `scene_.entities`. Each
  `Entity` has a `Material` with an optional procedural `program`.
- **Material programs.** `docs/procedural-materials.md` lists up to 64 ops, including `quantize`, `ramp`, `noise`
  and `swizzle`, with `worldPosition`, `uv` and `normal` inputs.
- **Engine water.** An entity with `MeshStyle::Water`, plus a `WaterSurface` in `Scene::waters` (ADR-099). It has
  screen-space reflection and a sky mirror (ADR-1650).
- **The procedural sky** (`environment.sky`), **cascaded sun shadows** (`environment.shadowRange` and
  `shadowCascades`), **height fog** (the volumetric medium), **post** (ACES, bloom), and **headless offline render**
  with `--live-profile`.

**Not used:**
- `world::generateTerrain` and the terrain node (ADR-046/090). Its chunk-grid LOD would need about 16M quads at the
  near resolution. It rebuilds the world through its own WorldMap, which would not reproduce this surface exactly,
  and its materials read a biome axis.
- Geometry clipmaps (GPU Gems 2 ch. 2). They suit an unbounded or streaming camera. This is a fixed world with a
  known path, so a static error-bounded mesh is the right tool.

## 2. Research and the choice (brief §3.2)

**Mesh extraction.**
- The source is a height field, so implicit-surface extraction (marching cubes, marching tetrahedra, dual contouring)
  is unnecessary. Those are for volumes and overhangs, which this world doesn't have.
- **Triangulated irregular networks.**
  - A Delaunay TIN with greedy insertion (Garland & Heckbert 1995, "Fast Polygonal Approximation of Terrains and
    Height Fields") gives the fewest triangles for an error bound, but it is slow at 67M samples.
  - The **right-triangulated irregular network** (RTIN: Evans, Kirkpatrick & Townsend 2001, "Right-Triangulated
    Irregular Networks"; popularised by Mapbox's Martini) refines a regular grid's triangle bintree wherever a
    hypotenuse midpoint deviates beyond the tolerance. Its errors propagate up the tree, which makes one mesh
    **crack-free by construction**, with no skirts and no stitching. It is linear-time and simple.
- **Error control.**
  - The tolerance varies with distance from the flight path (a view-dependent static LOD). To keep RTIN's crack-free
    guarantee under a spatially varying tolerance, each midpoint's error is **normalised by its local tolerance
    before propagation**, and the threshold is 1.
  - The band coordinate is an interpolated **attribute**, so its interpolation error is part of the same metric
    (attribute-aware simplification, as in Garland & Heckbert's 1998 quadrics with attributes).
- **Silhouettes** are preserved because the height error near the path is about 0.3 m (under a pixel at the camera's
  closest distances), and cliffs get dense triangles automatically.
- **Normals** come from the continuous source surface, not from the triangles. Decimation therefore never shows as
  facets, and smooth shading is independent of the triangulation.
- **Watertightness.** The mesh is one triangulation partitioned afterwards by centroid. Shared border vertices are
  duplicated with identical positions and normals, so the chunks meet without gaps.
- **Winding** is made counter-clockwise from above and tested. There are no degenerate triangles (tested).

**Materials.**
- Triplanar mapping (`triplanar` op) is the standard for steep terrain that lacks UVs. This canyon's colour isn't a
  texture, though: it comes from the strata, a world-space band coordinate.
- The converter writes that coordinate, exactly the one the reference drew (band warp included), into `uv.x`. The
  program `canyonRock` turns it into per-band colour with `quantize` (per-band hash), formation `noise`, a 3-stop
  `ramp`, and bedding planes.
- Faces show the bands and flats show dust (by `normal.y`). The playa is packed into `uv.y`.
- There are no texture dependencies.

**Not needed yet:**
- Barycentric wireframes (Bærentzen et al. 2008, "Two methods for antialiased wireframe drawing with hidden line
  removal"). That's Phase 2. The converter keeps real triangle topology for it.
- Instancing for flocks. That's Phase 3.

## 3. Baseline (brief §3.3)

- **Reference:** `gate2e/00-flythrough-46s-1080p30.mp4` (commit 7fcd7e99).
  - 1920×1080, 30 fps, 46 s, 2× supersampled.
  - Seed 1, a 4096² bake over 16 km, the flight from `buildFlight` (deterministic, tested).
- **Reproducible:** yes. The flight and the bake are pure functions of the seed and knobs. `"mesh": false` reproduces
  the ray-march path in the same binary for comparison (it is kept only until this phase is accepted).
- **What in the reference is what:**
  - geometry: the bake and the B-spline;
  - sub-texel geometry: the ledge cut and near detail;
  - shading: strata colour, the soft terminator, analytic fog and sky (with cloud streaks and the Belt of Venus);
  - the river reflection: a second ray-march;
  - post: ACES and bloom.

## 4. Conversion (Milestone A)

Grid, RTIN and chunks:
- 8193² samples (1.95 m) of the B-spline surface.
- RTIN with a tolerance of `max(0.3 m, 0.004 × distance from the path)`.
- 256 chunks.

**Mesh, seed 1:**
- 4.72M vertices, 8.27M triangles, 239 MiB of vertex and index data;
- built in 4.4 s (the whole world in about 9 s);
- river: 36k triangles.

**Tolerance sweep, measured at the closest wall pass (18.5 s, native 1080p):**

| meshError | min (m) | triangles | MiB | visible difference at 18.5 s |
|---|---|---|---|---|
| 0.0006 | 0.12 | 26.6M | 741 | reference |
| 0.001 | 0.25 | 20.4M | 574 | none |
| 0.002 | 0.3 | 13.5M | 386 | none |
| **0.004** | **0.3** | **8.27M** | **239** | none (`canyon-p1/09-tolerance-0.002-vs-0.004.png`) |

**Changes from the reference, each deliberate:**
- **The ledge cut is not in the mesh.** It is metre-scale on near-vertical walls, and no height grid resolves it: it
  aliased into zigzags at 2 m, as it had into combs in the ray-march. The strata stay in the material, from the same
  band coordinate.
- **No near-field relief.** It was shader-only displacement.
- **Lighting is the engine's.**
  - A directional sun with cascaded shadows (4 cascades, 3 km), IBL from the procedural sky, height fog, ACES.
  - The sun is lowered from 12° to 9°. The engine's Lambert term lights grazing benches more than the reference's
    rough-rock terminator did. At 9° the reference's pattern of shadowed benches and sunlit upper walls returns (18.5 s);
    5° lost the lit escarpment entirely.
- **GTAO is off for this scene.** Screen-space AO at landscape scale over long wall triangles drew dark fins and a
  crosshatch, found by A/B.
- **Shadow bias is 8 m.** The cascade texels are metres across at this scale. At 1.5 m the lit walls showed acne
  stipple (`canyon-p1/11-shadow-bias-1.5-4-8-at-18.5s.png`); 8 m leaves a few sparse dots on the far wall.
- **The sky has no cloud streaks and no Belt of Venus.** The engine sky is a zenith, horizon and ground gradient with
  a sun glow.

## 5. Measurements (1920×1080, M2 Max, `--live-profile --mode headless --quality ultra`, 2400 frames from 1 s)

| | GPU p50 | GPU p95 | GPU p99 | CPU work p50 | triangles drawn |
|---|---|---|---|---|---|
| ray-march (reference) | 46.2 ms | 62.8 | 71.6 | 0.5 ms | 2 |
| mesh at 26.6M tri | 46.2 ms | 56.8 | 63.1 | 16.6 ms | 8.9M |
| mesh at 8.27M tri (final) | **40.2 ms** | 50.0 | 57.1 | 6.1 ms | 3.2M |

Neither version meets the 33 ms live budget. The mesh is about 13% faster on the GPU.

**Engine finding: 15.5 ms of the mesh's CPU time at 26.6M triangles (5.0 ms at 8.27M) is `Scene::bounds()`.**
- Called from the shadow setup every frame, it calls `MeshData::valid()` per entity, and that walks every index of
  every mesh: 25M indices.
- This affects any scene with large meshes.
- Not fixed in Phase 1, because it's shared code. Proposed: cache validity with the mesh-bounds cache (its own ADR).

## 6. Verdict

**Preserved:** silhouettes, composition, the river corridor, the cliff faces, the strata palette, the camera path, and
the scale.

**Eliminated:** the edge artefacts. There are no combs, serration, sawtooth, hair or ghosting at any checkpoint.

**Different:** the lighting model, and the overall exposure is darker than the reference.
- The violet shadows and warm faces read, but faces are flatter orange and less layered.
- The benches catch more light at some moments.
- The sky lacks the reference's cloud streaks and Belt of Venus.
- Large cast shadows across the walls are fewer than in the reference's horizon shadows.
