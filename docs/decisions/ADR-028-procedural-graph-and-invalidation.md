# ADR-028: Procedural graph as an authoring layer; structural-hash invalidation

- Status: Accepted (2026-09-09)
- Research: `docs/research/procedural-authoring.md`

## Decision
- `graph::Graph` (data): typed nodes and links; node categories generators, distributions, spatial, points, attributes, fields, effectors, deformers, sdf, materials, particles, volumes, audio, time, math, logic, output. Pins are typed (float, vec2, vec3, colour, transform, pointCloud, spline, field, mesh, sdf, material, particles, volume); links are refused when types differ.
- Evaluation **emits flat scene data**: procedural objects, fields, splines, SDFs, material programs, particle systems, routes and parameter bindings into the composition; the renderer never sees the graph. Nodes cache outputs keyed by a structural hash of their inputs' bases; only dirty nodes and their downstream re-evaluate; modulation of non-structural parameters never re-evaluates the graph (it flows through the emitted objects' parameters).
- Subgraphs are graph files with exposed parameters registered as `graph/<instance>/<param>`; recursion is a subgraph instantiating itself with `depth`, per-level transform and `maxInstances`, expanded deterministically.
- The editor is a lightweight ImGui canvas (nodes, pins, links, parameter panel); graphs serialise as JSON in scene files/projects; a library folder holds reusable graphs with metadata and generated thumbnails.

## Consequences
- Positive: authoring without C++; determinism and performance unchanged because the runtime is the flat data path; "why is it moving" is answerable from the graph plus modulation.
- Negative: two representations of a world exist (graph and emitted data) for graph-built scenes; hand-edited emitted data is overwritten on re-evaluation, so a scene is either graph-driven or flat.
