# ADR-027: SDF architecture — a data tree, sphere tracing on the GPU, surface nets on the CPU

- Status: Accepted (2026-09-09)
- Research: `docs/research/sdf-and-implicit-geometry.md`

## Decision
- `spatial::SdfTree` (data): nodes {kind: sphere, box, roundedBox, cylinder, capsule, torus, plane, cone, union, intersection, difference, smoothUnion/Intersection/Difference, translate/rotate/scale, twist, bend, repeat, polarRepeat, mirror, displaceNoise/Fbm/Voronoi/Wave; params; children}. `evaluate(tree, p, t)` on the CPU; `sdf.wgsl` interprets the same tree from a uniform node array (≤ 64 nodes, post-order with an explicit stack of ≤ 8 distances).
- Scene-level `Scene::sdfs` (`SdfObject`: tree, transform, material, render mode raymarch|mesh, resolution, bounds); composition node kind `sdf`; parameters `sdf/<name>/node/<i>/…`.
- GPU path: `rendering::SdfRenderer` sphere-traces each object inside its bounding box in the lit pass, writes depth (so meshes and particles compose), shades with the shared PBR + material program, normals by tetrahedron differences.
- CPU path: naive surface nets over the object's bounds at `resolution` → `MeshData`, cached by the tree's structural hash; used when render mode is `mesh` (cheaper for static forms, and the path to SDF-as-a-source for instancing).
- Displacement and audio: node parameters are parameters; displacement amplitude from `audio.*` is an ordinary route.

## Consequences
- Positive: CSG worlds, domain repetition and warping without meshes; deterministic on both paths.
- Negative: raymarch cost scales with screen coverage and node count; meshes from surface nets are coarse at low resolutions; no GPU meshing yet.
