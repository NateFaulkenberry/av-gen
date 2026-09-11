# ADR-078: Mesh LODs from meshoptimizer, and what the existing decimator is still for

Status: accepted
Date: 2026-09-10

## Context

Renderer 2.0 §12 asks for a LOD chain — optimise vertex cache, optimise vertex fetch, LOD1, LOD2,
LOD3, shadow mesh — at roughly 100% / 50% / 20% / 7%, and adds *"Do not blindly use those values.
Measure visual quality."* It also says to integrate meshoptimizer rather than write a simplifier.

This engine already has a simplifier. `scene::decimateMesh` (ADR-045) is vertex-clustering
decimation: snap to a grid sized from the triangle budget, weld each occupied cell to one averaged
vertex, drop the triangles that collapse. It is what `source.meshBudget` and the imported-mesh LOD
levels in `scene::makeLodMesh` use today. §80 asks whether an existing system can be extended
before it is replaced, so the first question was not "how do I add meshoptimizer" but "does
meshoptimizer beat what is already here, and where".

## Decision

`src/assets/mesh_lod.{hpp,cpp}` builds LOD chains over meshoptimizer v1.2. It is pure,
deterministic, GPU-free and knows nothing about the renderer. Nothing is wired into
`src/rendering/`; that is Phase 3's job and this is the geometry waiting for it.

`scene::decimateMesh` stays exactly as it is. It is not superseded — see the measurements.

Three things the API insists on.

**A level reports the error it achieved.** `LodLevel` carries `achievedRatio`, `relativeError`,
`error` (the same in the mesh's own units, via `meshopt_simplifyScale`) and `reachedTarget`. Asking
for 7% and being handed 91% is not a rare pathology; on four of the eight assets measured it is
what happens. A chain that reported only the ratio it was asked for would present that as a working
LOD system.

**No level is ever empty.** Both simplifiers can return nothing — the preserving one when pruning
eats the last component, the sloppy one when its grid search cannot land near the target — and both
did so on real assets during this work. An empty level is not a small level; it is an object that
disappears at a distance and cannot be told apart from a culling bug. A level that cannot be built
falls back to the source mesh with `reachedTarget` false.

**The sloppy simplifier is a fallback from a measured stall, not a ratio threshold.** Whether the
preserving simplifier gets stuck depends on the mesh's topology, not on how far down the chain the
level is, so `sloppyFallback` fires when a level comes back more than 1.5x its target — and only
then. A hero sets it to 0 and stalls honestly instead.

## What was measured

Eight Quaternius assets, largest primitive of each, on an M-series Mac in Release. `meanErr` and
`maxErr` are the distance from every source vertex to the nearest triangle of the simplified mesh,
as a fraction of the source's bounding diagonal; `boxDrift` is how far the bounding box moved, in
the same units. The harness is `tests/unit/test_mesh_lod.cpp`, tagged `[.lodmeasure]` — it needs
the gitignored Quaternius pack, so it is hidden and skips without it.

### meshoptimizer wins clearly on connected geometry

At the ratio each actually reached, compared against `decimateMesh` asked for the same triangle
count:

| asset | ask | meshopt kept / meanErr / drift | decimateMesh kept / meanErr / drift |
|---|---|---|---|
| Fern_1 | 0.20 | 0.201 / 0.0072 / **0.0000** | 0.337 / 0.0128 / 0.0728 |
| Mushroom_Common | 0.20 | 0.198 / 0.0074 / 0.0151 | 0.376 / 0.0107 / 0.0119 |
| Rock_Medium_1 | 0.50 | 0.497 / 0.0018 / 0.0000 | 0.702 / 0.0035 / 0.0000 |
| Grass_Common_Short | 0.50 | 0.497 / **0.0019** / 0.0043 | 0.361 / **0.0204** / 0.0242 |

Half the triangles at a lower error is the usual shape of it. Grass is the extreme: clustering kept
*fewer* triangles than meshoptimizer and had **ten times** the geometric error, because a grid sized
for a 155-triangle mesh merges blades that are nowhere near each other.

The box drift column is the more damning one. On Fern_1 meshoptimizer's LODs have a bounding box
identical to the source at every level; the clustered ones move it by up to 13% of the diagonal,
which is a visible pop at the moment the level changes.

### The existing decimator does not honour its budget, and never says so

This is its real defect, and it is live in the engine today — `source.meshBudget` claims to
decimate to a budget.

| asset | asked | `decimateMesh` returned |
|---|---|---|
| Bush_Common | 50% | **100%** (nothing removed at all) |
| Bush_Common | 12% | 97% |
| Bush_Common | 4% | 78% |
| Fern_1 | 12% and 4% | 21.9% for both |
| Rock_Medium_1 | 12% and 4% | 28.1% for both |
| Grass_Common_Short | 12%, 7% and 4% | 14.2% for all three |
| UV sphere (2976 tri) | 50% | 84% |

It saturates: past a point, asking for fewer triangles returns the same mesh. The grid is sized as
`n ≈ sqrt(target / 2)`, and once a mesh's vertices are spread widely enough that each lands in its
own cell, nothing merges however small the budget. That is a defensible algorithm making a
defensible trade — and it returns the result with no indication that the budget was missed by 20x.

### meshoptimizer stalls too — and says so

| asset | preset | asked | got |
|---|---|---|---|
| CommonTree_1 | hero | 0.50, 0.20, 0.07 | 0.906 at all three |
| TwistedTree_2 | hero | 0.50, 0.20, 0.07 | 0.913 at all three |
| DeadTree_1 | hero | 0.50, 0.20, 0.07 | 0.902 at all three |
| Rock_Medium_1 | hero | 0.20 and 0.07 | 0.263 for both |

The trees are a trunk and branches carrying non-manifold junctions and attribute seams, and the
preserving simplifier will not move a vertex when doing so changes which surfaces meet there. This
is correct and useless, and the only difference between it and the clustered case above is that
`reachedTarget` is false and `achievedRatio` says 0.906.

### The sloppy fallback, and where clustering still wins

Arming `sloppyFallback` gets the trees past the stall. But at the most aggressive level it is
beaten by the decimator already in the tree:

| asset | level | meshopt sloppy kept / meanErr / drift | decimateMesh kept / meanErr / drift |
|---|---|---|---|
| CommonTree_1 | 0.04 | 0.037 / 0.0375 / 0.0921 | 0.054 / **0.0138** / **0.0050** |
| TwistedTree_2 | 0.04 | 0.033 / 0.0224 / 0.0394 | 0.054 / **0.0147** / **0.0139** |
| DeadTree_1 | 0.04 | 0.038 / 0.0242 / 0.0495 | 0.058 / **0.0171** / **0.0233** |
| CommonTree_1 | 0.12 | 0.117 / 0.0131 / 0.0228 | 0.148 / 0.0097 / 0.0033 |

Normalising for the extra triangles clustering kept, meshoptimizer's sloppy mode is still roughly
twice the error at equal triangle count. So on quality per triangle the two are the same order, and
on bounding-box stability clustering is plainly better here. **On tree-shaped geometry at the far
levels, ADR-045's vertex clustering is the better tool, and it stays.**

It was not routed into the chain, deliberately. `decimateMesh` reports no error, and a cheap error
measure for it does not exist — one-sided Hausdorff is O(vertices x triangles) and is a test-harness
luxury, not something a loader can afford. Adding a third path whose levels carry `error = 0` would
reintroduce, inside this library, precisely the silence this library exists to remove. If Phase 3
wants the clustered far level for trees it should call `scene::decimateMesh` itself and be explicit
that the level's error is unknown.

### Cost

meshoptimizer is 11-19x slower than clustering for a three-level chain:

| asset | triangles | meshopt chain | `decimateMesh` x 3 |
|---|---|---|---|
| CommonTree_1 | 4,345 | 5.9 ms | 0.31 ms |
| TwistedTree_2 | 6,790 | 6.1 ms | 0.53 ms |
| Mushroom_Common | 880 | 0.66 ms | 0.05 ms |
| Grass_Common_Short | 155 | 0.15 ms | 0.01 ms |

Milliseconds per asset is a load-time or worker cost, not a frame cost, which is what the research
note already predicted (`docs/research/assets.md` §152). It is not something to do on the main
thread during a scene swap.

## The ratios

**Heroes: 1.0 / 0.5 / 0.2 / 0.07 — the brief's numbers, measured and kept.** On the connected
assets they land within 1% of the ask, with mean error under 0.8% of the diagonal at 20% and 3.7%
at 7%. Attribute weights are 0.5 for normals and 0.1 for UVs, which is what keeps a hard edge a
hard edge. `sloppyFallback` is 0: a hero is the thing being looked at, and a hero that will not
simplify should stall and be drawn rather than be replaced by a shape that merely occupies the same
volume.

**Vegetation: 1.0 / 0.35 / 0.12 / 0.04.** More aggressive, as §12 allows, and deliberately the same
shares `scene::makeLodMesh` already uses for imported meshes — there was no reason to introduce a
second set of numbers into an engine that had one. Attribute weights are lower (0.2 / 0.05) because
the shading of a leaf at fifty metres is not what sells it and the seams cost reduction.
`sloppyFallback` is 1.5.

At 0.04 the trees read 2-4% mean error and 4-9% box drift. That is the edge of usable: it is a level
for the far band, and Phase 3 should not be selecting it inside about forty metres. The measurements
above are recorded so that judgement can be revisited against a frame rather than against an
opinion.

One caveat about the assets themselves. The current library is stylized and already low-poly —
155 to 6,790 triangles per asset — so a chain earns much less per asset than it would on
photogrammetry. What makes it pay here is instance count: the architecture audit reports 114,296
candidate instances for Glowmere, and 12% of a 900-triangle bush across tens of thousands of
instances is the whole of the saving.

## The error number is an upper bound, not a distance

`meshopt_simplify` returns the error of the collapses it performed, and `simplifyWithAttributes`
folds attribute deviation into it. On Rock_Medium_1's stalled level it reported 0.337 where the
largest measured vertex-to-surface distance was 0.066. It never understates, and it rises
monotonically with aggressiveness, which is what a distance-based selector needs; it is not a
Hausdorff distance and should not be presented as one. The header says so.

## Overdraw optimisation is available and off

meshoptimizer's recommended order is cache → overdraw → fetch. `MeshOptimiseSettings` supports it
and defaults it off, because overdraw ordering buys fewer shaded fragments at a cost to vertex-cache
efficiency, and `docs/renderer-2-architecture.md` §2 measured this engine's scene pass as vertex-
and draw-bound rather than fragment-bound at editor resolution. The trade currently runs the wrong
way. Revisit it if Phase 3 moves the bottleneck.

## Consequences

- meshoptimizer v1.2 (MIT) is a dependency of `avgen_core`. Licence verified from the fetched tree,
  not from memory: `.cache/cpm/meshoptimizer/3811/LICENSE.md` at tag `v1.2` (commit 9d9890c), MIT,
  © 2016-2026 Arseny Kapoulkine, with the same notice repeated in `src/meshoptimizer.h`. Only
  `src/*.cpp` is compiled; the demo and gltfpack are off, so nothing from the vendored `extern/`
  (cgltf, fast_obj, sdefl) enters the build. Recorded in `docs/asset-library.md`.
  **`docs/dependencies.md` still lists meshoptimizer under "Planned, not yet added" and needs its
  row** — that file was outside the scope this record was written in, and the omission is noted
  rather than left to be discovered.
- `LodChain::shadowIndices` is a position-only re-indexing of level 0's vertex buffer
  (`meshopt_generateShadowIndexBuffer`), not a second mesh. On a split-normal cube it cuts the
  distinct vertices a depth-only pass transforms by a factor of three. A *simplified* shadow mesh,
  if one is wanted, is another ratio in the chain.
- `MeshCacheStats` exists so "the optimisation pass did something" is a number rather than a
  belief; the tests assert on it.
- Nothing in `src/rendering/` changed and no scene uses this yet.

## Rejected alternatives

**Replace `scene::decimateMesh`.** The measurements say it is the better tool for the far levels of
tree-shaped geometry and roughly twice as good on bounding-box stability there, and it is 11-19x
faster. Removing it would be a regression dressed as a cleanup.

**`meshopt_SimplifyPrune` on by default.** It is what lets the preserving simplifier past a stall,
and it gets there by deleting components. On CommonTree_1 at 50% it produced twelve times the error
of the sloppy simplifier at the same triangle count, and at 7% it deleted the entire mesh. Left
available, defaulted off.

**A `sloppyBelow` ratio threshold** (the first design). Whether the preserving simplifier stalls is
a property of the mesh, not of the level index: Bush_Common reaches 4% cleanly while CommonTree_1
will not reach 50%, and a fixed threshold either switches meshes that did not need it or misses
the ones that did. The fallback triggers on the measured stall instead.

**Simplifying each level from the level above.** Errors compound and a chain rebuilt with different
ratios no longer agrees with itself. Every level is simplified from the source.

## Revisit triggers

- Phase 3 measures submitted triangles (§3 of the architecture audit says nothing does yet) and the
  ratios can be judged against a frame time rather than against a geometric error.
- The asset library gains anything above roughly 50k triangles, where the chain's value changes
  from instance-count arithmetic to per-asset density.
- A cheap error estimate for vertex clustering appears, at which point the clustered far level for
  vegetation can join the chain without going silent.
- meshoptimizer ships a simplifier that handles non-manifold junctions without the sloppy fallback.
