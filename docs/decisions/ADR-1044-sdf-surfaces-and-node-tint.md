# ADR-1044: SDF surfaces (a material id per node) and an opt-in node tint

- Status: Accepted (2026-09-30), proto/liminal-space
- Implemented in `src/spatial/sdf.*` (`SdfNode::material`, the compiler's `sdfSurface`),
  `src/scene/sdf_object.*` (`surfaces`, `surface/<k>/color|emission`), `src/rendering/sdf_renderer.*` and
  `shaders/sdf_raymarch.wgsl` (the lookup), `src/scene/composition.*` (`nodes/<name>/tint`).
- Tests: `tests/rendering/test_sdf_gpu.cpp` ("SDF surfaces: ..."), `tests/integration/test_liminal_journey.cpp`
  ("Node tint: ...").

## Context

The director plan needs plaster, floor, accent planes and an emissive beacon in one world, each bound to
its own palette role. An SDF object had one material, so the choice was one object per material -- and
each object is a full-screen march of the whole world's bounds, so four materials cost four marches.
It also needs the walking figure's material bound to the palette.

## Decision

- **A node `material` id** (-1 inherits, 0..7) tags a subtree. The WGSL compiler (ADR-1003) emits a
  second function, `sdfSurface(p)`, the same tree tracking which surface makes the field at p: the nearer
  child of a union, the farther of an intersection, the first child of a difference (a cut's faces are the
  solid's reveals); a node's own id overrides its subtree's. It runs once per hit pixel.
- **An object's `surfaces`**: up to 8 `{color, emission}`, parameters `surface/<k>/color` and
  `surface/<k>/emission`. They ride after the object's node records in the storage buffer; the hit's
  surface multiplies the material's base colour and emission (so an object with surfaces sets its material
  white with unit white emission, and each surface carries the real albedo and radiance --
  `tools/liminal_sdf.py:sdf_node(surfaces=...)` does this). Roughness and metallic stay per object.
- **Compiled objects only**; the interpreter shades every hit as surface 0 (tested). The ids are structural
  (part of the compile key); the colours are live uniforms.
- **A node `tint`** (`"tint": [r, g, b]` in the scene, opt-in so no other scene gains a parameter):
  `nodes/<name>/tint` multiplies the base colour of everything the node draws, captured from the asset once.

## Consequences

- The example world draws plaster, floor, stair and lamps in one march (the lamps were a second object:
  one full-screen march saved).
- A surface's emission reaches the bloom like any emission; it does not light its surroundings (put a light
  beside a beacon).
