# ADR-1073: Wire lines: a procedural surface's edges as screen-space quads, by edge extraction

- **Status:** Accepted (2026-10-02), proto/sonic-garden (stylized engineering for the abstract direction; the Neon
  Vector look)
- **Code:**
  - `scene::WireLines` and `Material::wire` (`src/scene/scene_types.hpp`); the file block and the parameters
    (`src/scene/wire_lines.*`, called from `src/scene/procedural.cpp`);
  - the edge list: `scene::buildWireEdges` (`src/scene/wire_edges.*`);
  - the draw: `ProceduralRenderer::drawWire`, `ensureWireMesh`, `createWirePipeline`, the `wireArgs` copy after the
    cull pass, and the `fill` skip in `drawImpl` (`src/rendering/procedural_renderer.cpp`); called from the scene pass
    after the water;
  - the shaders: `shaders/wire.wgsl` (the quad), `vs_proc_wire` / `fs_proc_wire` and `ProceduralUniforms::wire`
    (`shaders/procedural.wgsl`).
- **Tests:** `tests/unit/test_wire_edges.cpp` (`[wire][adr1073]`), `tests/rendering/test_toon_gpu.cpp`
  (`[gpu][wire][adr1073]`).

## Context

The Neon Vector direction draws geometry as glowing lines. SDFs have their edge light (ADR-1002, ADR-1047); meshes and
procedural instances had nothing. The brief left the method open: barycentrics in the fragment stage, or extracted
edges.

## Decision: extracted edges, drawn by the surface's own shader module

WGSL has no barycentric builtin. Faking one needs a de-indexed copy of every mesh that wants lines and a second vertex
attribute in every pipeline that draws it -- the lit pass, the depth prepass and every shadow view -- and it can only
mark triangle edges, not choose feature edges by angle; it also cannot show a hidden edge or draw lines without the
surface. Extraction costs nothing to a draw that does not ask for lines.

- `buildWireEdges` welds vertices by position (a flat-shaded cube's split corners, a UV seam), builds the edge
  adjacency and keeps, in mode 1, the boundary edges and those whose faces meet at more than `crease` degrees; in mode
  2, every edge. Each edge is a quad: four vertices carrying their own endpoint and the other one.
- `vs_proc_wire` is an entry point in `procedural.wgsl` with the procedural bind groups, so each endpoint runs the
  same steps as the surface's vertex -- source transform, deformer chain, instance, object, world deformers, wind,
  FXL displacement -- and the quad is expanded across the projected segment, `width` pixels at 1080 lines plus one
  pixel of antialiasing, lengthened at each end so joins overlap. Both ends are pulled towards the eye by 0.3% of
  their distance (+2 mm), so a line on its surface's own edge passes the depth test while one behind a solid stays
  hidden.
- `fs_proc_wire` blends `color x intensity` at `opacity` x coverage into the colour AND the emission target (it
  blooms); the normal, velocity and identifier targets keep the surface's.
- Instancing: a direct draw uses the object's instance count. A culled (`lod.cull`) object draws the camera list's
  LOD 0 instances through its own indirect args: the edge list's index count, and the instance count the cull pass
  wrote for level 0, copied on the GPU each frame.
- `fill` 0 skips the surface in every pass (no depth, no shadow): lines alone. `occlude` 0 draws through everything.

| parameter (`procedural/<node>/`) | file key (`material.wire`) | range | default |
|---|---|---|---|
| `wire/mode` | `mode` (0/1/2 or "off"/"feature"/"all") | 0..2 | 0 (off) |
| `wire/crease` | `crease` | 0..180 degrees | 30 |
| `wire/color` | `color` | colour | (0.2, 1, 0.9) |
| `wire/intensity` | `intensity` | 0..1000 | 2 |
| `wire/opacity` | `opacity` | 0..1 | 1 |
| `wire/width` | `width` | 0..64 px at 1080 lines | 1.5 |
| `wire/fill` | `fill` | 0/1 | 1 |
| `wire/occlude` | `occlude` | 0/1 | 1 |

All are routes and timeline targets (mode and crease rebuild the cached edge list on change). UI: World panel, select
the procedural, Inspector section **wire**; Parameters panel, the object's group, section `wire`.

## Consequences

- `ProceduralUniforms` grew 752 -> 768 bytes (`wire`), filling the slot it was already padded to: no stride change.
- Only procedural nodes in this ADR (their sources include imported meshes). Mesh entities (glTF nodes) and skinned
  meshes do not get lines yet: `ObjectUniforms` is full (ADR-1071), so they need a uniform of their own.
- A culled object's lines are LOD 0's instances only; impostor LODs draw none. Point sources draw none.
- Lines do not cast shadows or write depth, so the outline pass (ADR-1072) does not see them.
