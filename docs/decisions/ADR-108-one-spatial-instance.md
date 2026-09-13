# ADR-108: One spatial instance, drawn once per material

Status: accepted
Date: 2026-09-13

## Context

ADR-044 splits a multi-material asset into one drawable per material, because a tree whose leaves
are drawn with the bark's colour is not a tree. The split happens in `Composition`: a scatter layer
naming `CommonTree_1.gltf` becomes `valley_canopy` and `valley_canopy_m1`, over the *same*
`spatial::PointCloud`, the same seed, the same variation and the same transform. The visitor becomes
four objects. Only the mesh, the material and the per-instance colour differ.

The renderer never knew that. Each object carried its own record buffer, its own cull dispatch chain
and its own stats slot, so one tree was classified against the frustum twice and counted twice.
Glowmere reported **116,978 logical instances over 113,923 placements** — 3,052 trees, pines and
flowers counted as two.

The duplicated arithmetic is the smaller half. The two decisions could **disagree**. The bounding
sphere each object culled against came from that object's own mesh, so:

- a trunk and its canopy could take different LOD levels at the same distance, and
- `minScreenRadius` could reject one and keep the other — a tree that loses its leaves before it
  loses its trunk, at a distance chosen by whichever part happened to be geometrically smaller.

Neither was visible in any counter. `lod=382/1417/525/5` says how many instances chose each level; it
does not say whether the parts of one asset chose the same one.

The forensics audit named this exactly — "multi-material scatter cost: confirmed, culled and counted
once per material" — and the fix it asked for is at the representation level rather than at the
counter: one spatial instance culled once, with the visible material parts emitted from the
surviving instance.

## Decision

### The representation: a part names its lead

`scene::ProceduralGeometry::partOf` holds the name of the object a part shares its placement with.
It is set where the parts are created — the terrain scatter path, the city path and the authored
procedural path in `composition.cpp` — and it is not structural: it changes nothing about what the
object generates, only how the renderer groups the work.

It survives `applyProceduralParameters` because that function edits `live` in place rather than
copying `rest` wholesale; for the authored-procedural path it is also written onto the node's
`proceduralSubRest`, which is the authored baseline every later frame derives from (the
derived-copy rule).

### The lead culls; the parts are emitted from its result

`ProceduralRenderer::update` resolves the groups before it walks the objects, then:

- the lead classifies and compacts its records exactly as before, over a bounding radius that is the
  **maximum over every part of the asset** — a trunk's radius must not cull a canopy that is still
  on screen;
- each part allocates no `lodIndex`, `blockSums` or `visible` buffers and encodes no dispatches; its
  draw groups bind the **lead's** visible lists;
- `shaders/cull.wgsl` gains a bounded fanout list on `CullParams`. Where `cs_cull_top`'s thread 0
  wrote one object's `drawIndexedIndirect` args and stats slot, it now writes the lead's and then
  one per part: the same instance count, each part's own index count.

The cull uniforms are staged and uploaded after the object loop rather than as they are built,
because a part reached later in the loop has to be able to append itself to its lead's list.

### The parts still keep their own records

The records are *not* shared. `InstanceRecord::color` and `::emissive` are derived from the object's
own material (ADR-054 hue variation is computed against the colour it will multiply), so two parts
of one asset genuinely hold different records — with identical positions, rotations and scales. The
cull pass reads only the second group, which is why one decision is enough and one buffer is not.

### A part joins its lead only when it really is the same placement

The whole claim rests on that, so it is checked rather than assumed: same record count, same object
matrix, same LOD ladder (count, flags, distances, spread, hysteresis), the lead not itself a part,
neither object running an effector pass, and room in the fanout list. Anything that fails culls
itself, which is what every part did before this existed. A part whose lead was skipped this frame —
invisible, no mesh, past `kMaxProceduralObjects` — also falls back, decided from the lead's GPU state
rather than from the scene, because that is what says whether the lists exist.

### Invalidation

A part's draw bind groups reference a buffer another object owns and grows. `ObjectState` records
the visible buffer, stride and level count its groups were built against; a mismatch rebuilds them.
That covers the lead reallocating (grow-only, on a record-set change), a part ceasing to be one, and
a part changing lead. This is state with a lifetime longer than one frame, and it has one
invalidation rule in one place.

### Counters

`stats_.instances` is now the number of **spatial** instances: a part's records are not added again.
`logicalTriangles` is deliberately *not* deduplicated — it is the geometry the objects contain, and
each part contains its own. `visibleInstances` / `culledInstances` aggregate over the leads only.
The empty-level counters still advance for every object the cull pass writes counts for, parts
included, so a part still skips its own reliably-empty far levels.

## Measurements

Glowmere, `examples/world/glowmere-stylized.scene.json`, 1280×800, 120 frames, M2 Max, same session,
under `tools/gpu-lock.sh`.

| | before | after |
|---|---|---|
| instances visible / culled / total | 2,329 / 114,283 / 116,612 | 2,161 / 111,399 / 113,560 |
| logical instances | 116,978 | 113,923 |
| camera triangles | 430,233 | 432,273 |
| cull pass | 0.33 ms | 0.26 ms |
| GPU frame | 18.55 ms | 18.55 – 19.14 ms |

Read honestly:

- **The instance counts are the result.** 3,052 duplicated records are gone from the cull, and the
  frame now reports the placements it has.
- **The cull pass moved by one timer tick.** Timestamps here quantise at about 0.065 ms, so 0.33 →
  0.26 is one step and is at the measurement floor. It is consistent across three runs and the
  mechanism is plausible — three objects' worth of dispatches, twelve of about a hundred, are gone —
  but 2.6% fewer records cannot account for 20% and this is not claimed as a 20% win.
- **The frame did not move**, which was expected and stated in advance: the scene pass is
  fragment-bound and this changes neither the fragments nor the draws.
- **Camera triangles went up 0.5%.** That is the LOD-coherence fix showing: a part no longer drops to
  a coarser level ahead of its lead, so the parts of an asset are drawn at one level. It is a
  correctness change that costs half a percent of triangles.

In Glowmere only three scatter layers carry a second material and the visitor's four parts are one
instance each, so 2.6% of the records were duplicated. The saving scales with how multi-material the
world is; a city kit, where nearly every piece has two or three materials, is the case this was
built for.

## Rejected alternatives

- **Sharing the instance buffer as well as the cull.** The records differ in colour, so they would
  have to be recomputed per part on the GPU or the per-instance hue variation would be lost. A
  second buffer of 96-byte records is cheaper than either.
- **A separate fanout compute pass** copying each lead's instance counts into its parts' indirect
  slots. Unbounded in the number of parts, and clean, but it adds a pipeline, a buffer and a
  dispatch to every frame to do what four u32 stores inside an existing thread do for nothing.
  Revisit if an asset ever needs more than eight materials.
- **Widening the fanout limit instead of falling back.** Eight materials covers every asset in the
  library; a ninth part culling itself is exactly the old behaviour, which is a safe floor rather
  than a cliff.
- **Deduplicating in the counters only.** The numbers would have been right and the duplicated work
  and the disagreeing LOD decisions would both still be there. The audit asked for the
  representation, and the LOD incoherence is only reachable from there.
