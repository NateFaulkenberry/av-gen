# ADR-1069: A Voronoi edge material op (Worley F2 - F1)

- **Status:** Accepted (2026-10-02), proto/sonic-garden (VFX expansion; the art agent's request)
- **Code:**
  - `MaterialOpKind::VoronoiEdge` (appended: the enum order is the wire format);
  - the CPU twin in `src/scene/material_program.cpp`;
  - `MAT_OP_VORONOI_EDGE` in `shaders/material.wgsl`, on noise.wgsl's existing `worleyF1F2`.
- **Tests:** a CPU/GPU parity probe in `tests/rendering/test_material_gpu.cpp`, and `[adr1069]` (names).

## Decision

`{"op": "voronoiEdge", "a": <reg>, "value": <frequency>, "constant": [x, y, z, _], "seed": n}` writes:
- `vec4(F2 - F1, F1, cellHash, F2)` of `a.xyz x value + constant.xyz`;
- **x** is 0 on the boundary between two cells, so a `smoothstep` or `threshold` on it draws cell walls;
- **z** gives each cell its own value.

The plain `voronoi` op gives F1 only, which reads as blobs on a plane. Salt polygons, cracked mud and ice cracks need
the edge. The lattice, jitter and hashes are `voronoiF1`'s, so the two ops line up.
