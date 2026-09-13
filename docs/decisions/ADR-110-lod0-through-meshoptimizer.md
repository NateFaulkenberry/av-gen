# ADR-110: LOD0 through meshoptimizer, like every rung below it

Status: accepted
Date: 2026-09-13

## Context

ADR-078 moved the LOD ladder onto meshoptimizer and measured the calibrations. It moved levels 1, 2
and 3, and said so: "LOD0 is deliberately not routed through here — it stays exactly the mesh
`makeSourceMesh` builds — so the near field is unchanged and only levels a viewer sees at a distance
move."

That left the mesh the near field draws as the only rung of the ladder that was neither simplified
properly nor ordered for the GPU. `makeSourceMesh` applied `SourceSpec::meshBudget` with
`scene::decimateMesh`: a vertex clustering on a uniform grid, which snaps vertices into cells and
keeps whatever triangles survive. A grid has no way to *reach* a triangle count.

What Glowmere's authored budgets actually bought:

| layer | budget | LOD0 got |
|---|---|---|
| canopy (both parts) | 900 | 2,677 |
| pines (both parts) | 700 | 2,943 |
| shelf fungi | 420 | 1,490 |
| deadwood | 800 | 1,172 |
| ferns | 260 | 227 |

Over and under, never the number. And because LOD0 is the near field, it is where a scatter's drawn
triangles are: 432,271 of the 432,273 the camera submitted came from these meshes and the terrain.

The renderer is fragment-bound, and its fragment cost tracks *triangle count* rather than pixels — a
sub-pixel triangle still forces a 2×2 quad of invocations, which is why 56% of the scene pass is
resolution-independent. So a budget that is quietly not met is not a tidiness problem.

## Decision

### `assets::sourceLodMesh` builds LOD0

Given an imported mesh and a triangle budget it runs meshoptimizer's documented sequence — index
(weld), vertex cache, vertex fetch — and, when the mesh is over budget, simplifies to reach it. It is
`buildLodChain` with the ratio the budget implies, so the fallback machinery ADR-078 already measured
(the sloppy simplifier for a level that stalled, the honest reporting of a level that stopped short)
applies unchanged.

`makeSourceMesh` calls it for every `PrimitiveKind::Mesh` source. `decimateMesh` stays where the
existing fallback in `makeLodMesh` uses it.

A skinned mesh is returned untouched: vertex-fetch optimisation permutes the vertex buffer and
`MeshData::skin` is parallel to it. So is a mesh `buildLodChain` refuses — a non-finite position, an
index past the buffer — because nothing good comes of a grid clustering over one of those either.

### `lod0Settings()` is a third calibration, and each of its steps was measured

Hero attribute weights (0.5 normal, 0.1 UV), because LOD0 is looked at, with the sloppy fallback
**armed**, which the hero calibration refuses. The reason they differ: a hero has no triangle budget
to reach, and a scattered asset has one an author wrote. A budget quietly not met is the defect this
replaces, not a quality setting.

The four steps of the sequence were measured separately rather than enabled by reflex, on an M2 Max
against Glowmere at 1280×800, three interleaved pairs per arm under `tools/gpu-lock.sh`:

| arm | GPU frame | scene pass |
|---|---|---|
| the old path (`decimateMesh`) | 18.55 / 18.68 / 18.94 | 15.53 / 15.73 / 16.06 |
| **this decision** | **14.29 / 14.48 / 14.55** | **11.73 / 11.86 / 11.99** |
| weld + vertex-cache order **off** | 14.55 / 14.81 / 14.42 | 11.93 / 12.19 / 11.80 |
| overdraw pass at 1.05 | 15.20 / 15.20 / 15.01 | 12.52 / 12.58 / 12.45 |
| preserving simplifier only | refused — see below |

Camera triangles: **432,271 → 264,303**, −38.9%. GPU frame −22.5%, scene pass −24.6%. Well outside
the 2% rule, and the direction the fragment-bound diagnosis predicts.

**The reduction is the whole of the win.** Welding and vertex-cache ordering are indistinguishable
from nothing here — three pairs, the sign flips between them, every value inside the other arm's
range. They are kept because welding is what lets the simplifier work at all on an exporter-split
mesh, which is how the reduction is reached, and because neither costs anything measurable — not
because either was measured to pay.

**The overdraw pass is left off with a number against it.** meshoptimizer's documentation warns it
behaves differently on tiled GPUs, and on this one it costs 4% in every pair. The depth prepass is
already doing that job — a software early-Z arm took 1.24 ms of a 21.36 ms pass — so there is little
overdraw left for it to remove and the ACMR regression is real. Revisit on a scene with no prepass.

**Vertex fetch was not isolated.** It lives inside `compact()`, which every level needs in order to
be a memory reduction as well as a triangle reduction, so there is no arm without it. Its
contribution is unmeasured and assumed small.

**The preserving simplifier alone does not work here.** Without the sloppy fallback, `valley_canopy`'s
leaves come back at 3,937 triangles against a share of a 900 budget — five times what the grid
clustering produced — because a Quaternius tree's branch junctions are non-manifold and full of
attribute seams, exactly as ADR-078 measured. That arm was refused rather than tuned.

### What the budgets now buy

canopy 2,677 → 821 (budget 900), pines 2,943 → 678 (700), shelf fungi 1,490 → 420 (420), deadwood
1,172 → 660 (800), fungi 360 → 239 (240). Three layers go *up* — grass 56 → 79 against a budget of
80, ferns 227 → 260 against 260, pebbles 92 → 114 — because the grid had over-reduced them below what
their authors asked for. The budget is now the number in the scene file, in both directions.

Vertex counts fall with them: the canopy's leaf part goes from 5,251 vertices to 296.

Checked by eye at frame 120 of the Glowmere flythrough against the same frame before the change. The
valley reads the same; the hillside trees are if anything crisper, because their leaves are geometry
rather than grid-collapsed blobs; nothing lost its shape.

## Rejected alternatives

- **Leaving LOD0 alone and lowering the authored budgets instead.** The budgets were already right;
  it was the machinery that did not honour them. Retuning every scene file around a defect is how the
  defect becomes load-bearing.
- **Using `vegetationLodSettings()` for LOD0.** Its attribute weights (0.2 / 0.05) are chosen for a
  fern at fifty metres. LOD0 is the near field and its shading is looked at.
- **Enabling the overdraw pass because the documentation lists it in the sequence.** Measured, 4%
  slower, recorded, off.
