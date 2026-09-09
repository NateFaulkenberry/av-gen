# ADR-045: Vertex-clustering decimation for imported meshes

## Status
Accepted, 2026-09-09.

## Context
ADR-044 made an imported glTF asset the source mesh of a procedural object, which means one asset
is drawn hundreds of times. Production assets do not arrive at a density suited to that: a scanned
cliff is routinely over a million triangles, authored for a hero shot in which it appears once and
fills the frame. Instanced across a hillside it is a frame-rate cliff for detail nobody can see.

The LOD ladder (ADR-029) already coarsens *generated* primitives by rebuilding them at lower
segment counts, but an imported mesh has no generator to re-run. Its levels have to come from the
mesh itself.

## Decision
`decimateMesh(mesh, targetTriangles)` reduces a mesh by vertex clustering: the bounding box is
divided into a grid sized so that roughly two triangles land in each occupied cell, every vertex
snaps to its cell, each cell collapses to one averaged vertex, and triangles whose corners land in
the same cell are dropped.

`SourceSpec::meshBudget` applies it once, at resolve time, so the budget is a property of the
source and not of any particular frame. `makeLodMesh` applies a decreasing share of that budget per
LOD level, floored at 24 triangles, so an imported asset gets the same four-level ladder a
generated primitive does.

## Consequences
Vertex clustering is deterministic and linear in the input, which matters because it runs at load
on assets that may be very large, and because scenes must be reproducible.

It is chosen for what it is good at and is genuinely bad at other things. It preserves silhouette
and volume and destroys topology: seams open, UV continuity is not maintained, and a sharp crease
is rounded off by the cell average. That suits scanned organic geometry -- rock, bark, foliage,
fungus -- where the silhouette is the whole read. It is the wrong tool for hard-surface assets with
deliberate creases, where a quadric error metric would be worth its cost. If such assets arrive,
the answer is a second algorithm behind the same call, not a tuned version of this one.

The budget is a triangle count rather than a screen-space error because it is authored per asset in
the scene file, where the author knows how many of the thing there will be and does not know how
large it will be on screen.
