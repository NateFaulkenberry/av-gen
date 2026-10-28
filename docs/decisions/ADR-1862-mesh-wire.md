# ADR-1862: Mesh wire: a mesh entity's own triangle edges, read in place and revealed by music

- Status: Proposed, scene/strange-attractor (canyon Phase 2, 2026-10-28).
- Extends ADR-1073 (wire lines on procedural nodes) to mesh entities. ADR-1073 left this open: "Mesh entities do not
  get lines yet: ObjectUniforms is full."
- Code:
  - `scene::MeshWire` with its file block and parameters: `src/scene/mesh_wire.{hpp,cpp}`. `Scene::meshWire`.
  - The Composition parses the top-level `"meshWire"` key, registers `meshWire/<key>` parameters, applies their finals
    every frame, and saves the authored block.
  - `rendering::MeshWireRenderer`: `src/rendering/mesh_wire_renderer.{hpp,cpp}`.
  - `shaders/mesh_wire.wgsl`, which includes ADR-1073's `wire.wgsl` quad.
  - `SceneRenderer` creates mesh vertex and index buffers with `Storage` usage as well, and draws the wire in pass 1's
    blended section after the ribbons.
  - Scene-local glue, not engine: the attractor composition feeds the terrain program's contour and glow strengths
    through the chunks' material emission (`attractorWorld/contour`, `attractorWorld/glow`). The scene file and
    `examples/strange-attractor/build_project.py` hold the routes.
- Tests: `tests/unit/test_mesh_wire.cpp` (`[mesh-wire][adr1862]`) and `tests/rendering/test_mesh_wire_gpu.cpp`
  (`[gpu][mesh-wire][adr1862]`). The GPU tests check three things:
  - the gate (byte-identical, no draw);
  - the target's edges are drawn, and only the target's;
  - lines are depth-tested behind a wall.
- Evidence: `docs/prototypes/strange-attractor/07-canyon-phase2.md`.

## Context

The canyon brief's signature effect (§5) needs "thin luminous lines" that "trace the underlying triangle mesh" and
"reuse the terrain's actual triangle topology". The terrain is 8.27M triangles in 256 chunk entities.

ADR-1073 extracts an edge list on the CPU and draws four 56-byte vertices per edge. For this terrain that would be
12.4M edges, about 3 GB. The ADR also says WGSL has no barycentric builtin, and a de-indexed copy would double every
mesh that wants lines.

## Decision

**No edge list.** The wire reads the mesh where it already lives. The scene renderer's mesh buffers gain `Storage`
usage, and a vertex shader reads each triangle from them directly. There is one instance per triangle and 18 vertices
per instance: three edge quads, built with ADR-1073's `wireExpand` and `wireCoverage`.
- **No doubling.** An interior edge is walked in opposite directions by its two triangles. Only the triangle that walks
  it from the lexicographically smaller endpoint draws it. That holds across chunk seams, because the comparison is in
  world space.
- **Structure weight.** Each edge gets the larger of two measures of how much the surface turns there:
  - the angle between its endpoints' smooth normals;
  - twice the angle between the face and those normals.

  This is smoothstepped between `creaseLow` and `creaseHigh`. `reveal` lowers the threshold: 0 shows structure only, 1
  shows every triangle.
- **Restraint (brief §5.3).**
  - A distance fade (`fadeNear`..`fadeFar`). Nothing beyond `fadeFar` is drawn: chunks are culled on the CPU, edges
    collapsed in the shader.
  - A density fade: a triangle smaller on screen than `minPixels` (the side of a square of equal area) fades its edges
    out, so distant or sliver-thin geometry never piles into a luminous mass.
  - `clipBelow` with a 3 m soft edge, for a mesh under water.
  - Lines are added light, fogged like the ground behind them.
  - They are depth-tested and pulled toward the eye by ADR-1073's rule, and never write depth.
- **The wave (Mode C, localised dissolution).**
  - `wave` is an envelope, meant to be driven by a route with a latching linear fall.
  - A ring centred `waveAhead` metres in front of the camera has radius `(1 - wave) x waveRadius` and width
    `waveWidth`.
  - Every edge the ring crosses lights at `waveGain`, structure or not, and the rock returns as the ring passes.
  - The renderer keeps no state: the front is a pure function of the parameter, so it is seekable and independent of
    frame rate.
- **Gate.** `MeshWire::active()` is false without targets, or with both `intensity` and the wave at 0. When it is
  false the renderer returns before binding or uploading anything.

## Consequences

- Cost on the Strange Attractor at 1080p ultra, M2 Max: the scene pass goes from 45.0 to 48.4 ms with the wire fully on
  (structure lines on, a wave mid-flight). That is about 3.4 ms for up to 3.2M visible triangle instances. The
  composition only fills the parameters.
- Every mesh's vertex and index buffers now carry `Storage` usage. No memory is added (the buffers are the same) and no
  draw changes. The GPU suite's scene hashes are unchanged.
- Any scene can now put lines on any mesh entity by name prefix. Skinned meshes are not supported: the wire reads the
  bind pose.
