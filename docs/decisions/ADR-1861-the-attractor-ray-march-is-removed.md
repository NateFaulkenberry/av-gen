# ADR-1861: The Strange Attractor's ray-march is removed

- Status: Accepted, scene/strange-attractor (canyon Phase 2 start, 2026-10-28). Supersedes ADR-1850's renderer,
  ADR-1855 and ADR-1856, all of which are specific to the ray-march.
- Asked for by the coordinator. The owner accepted Phases 1b and 1c as the foundation, and the brief says the ray-march
  path can be deleted at the start of Phase 2.

## What went

- `src/rendering/attractor_renderer.{hpp,cpp}` and `shaders/attractor/world.wgsl`, along with the renderer's place at
  the head of the Environment seam's run order.
- The `"mesh"` switch. The terrain mesh is now always built.
- The look parameters that only the ray-march read: `palette`, `ambient`, `atmosphere`, `strata`, `exposure` and
  `detail`. These are both block keys and `attractorWorld/` parameters.
- The bake products that only the ray-march read: `slopeMax` (its step bound) and `flow` (its river).
- `examples/strange-attractor/strange-attractor.scene.json`, the composed-vista scene that only the ray-march drew.

No aliases are kept (ADR-441). A scene that still names a removed key fails to load and names the key. The unit test
checks this for `"mesh"` and `look.palette`.

## What stays

- The generator: the height field, the strata offset, masks, layout, branches and flight.
- The RTIN mesh (ADR-1857), the sun light and the river water.
- `sunAzimuth`, `sunElevation` and `sunIntensity`.
- The gate2e clip (`~/Desktop/av-gen-review/101-strange-attractor/gate2e/`) is kept as the visual reference.
