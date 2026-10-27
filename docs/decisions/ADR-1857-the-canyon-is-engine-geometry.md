# ADR-1857: The Strange Attractor's canyon is engine geometry

- Status: Proposed, scene/strange-attractor (canyon Phase 1, 2026-10-27). Awaits the owner's acceptance of the
  conversion. On acceptance, the ray-march path (ADR-1850's pass, `shaders/attractor/world.wgsl`, `"mesh": false`) is
  removed.
- Code:
  - `src/scene/attractor_world.{hpp,cpp}`: `SurfaceSampler`, `buildTerrainMesh`, `AttractorTerrainMesh`, the `mesh*`
    knobs and the block's `terrainProgram`, `water`, `sunLux` and `sunShadowBias`.
  - `src/scene/composition.cpp`: chunk entities, the river water entity and surface, and the sun light, steered every
    frame.
  - `src/rendering/attractor_renderer.cpp`: draws only a world without a mesh.
  - `examples/strange-attractor/strange-attractor-flythrough.scene.json`: the `canyonRock` program, sky, fog and
    shadow range.
- Tests: `[adr1857]` in `tests/unit/test_attractor_world.cpp`.
- Evidence: `docs/prototypes/strange-attractor/04-canyon-phase1.md`.

## Decision

The baked height field's rendered surface (the C2 B-spline) is sampled on a 2 m grid and triangulated as one
right-triangulated irregular network:
- Error-bounded, with the tolerance growing with distance from the flight path.
- Errors are normalised before propagation, so the result stays crack-free.
- The band coordinate is carried as an attribute.

The network is partitioned into 256 chunks, which are drawn as ordinary lit entities with a material program. The
river is a flat sheet at the water line, drawn by the engine's water pipeline with screen-space reflection. The sun is
a shadow-casting directional light that follows the look parameters.

**No engine code changed.** Everything is the scene block's use of existing systems.

## Consequences

- **Cost.** 8.27M triangles and 239 MiB for seed 1. About 4.4 s to build, on world generation.
- **Look.** The engine lighting replaces the ray-march's own: no cloud streaks or Belt of Venus in the sky, a Lambert
  terminator, and cascaded shadows. The sun is lowered to 9° to keep the reference's balance, and the shadow bias is 8 m against acne.
- **GTAO is off for this scene.** Screen-space AO drew fins on long wall triangles.
- **The ledge cut and near relief are not geometry.** No height grid resolves them on vertical walls.
- **An engine inefficiency is exposed and reported:** `Scene::bounds()` re-validates every mesh index every frame,
  costing 5 ms of CPU at 8.27M triangles (15 ms at 26.6M).
