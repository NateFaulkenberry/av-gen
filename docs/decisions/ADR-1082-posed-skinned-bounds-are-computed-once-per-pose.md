# ADR-1082: A skinned mesh's posed bounds are computed once per pose, and the camera cull visits each entity once

**Status:** Accepted (live quality, `docs/live-quality/00-brief.md` §9.2).
**Date:** 2026-10-03

## Context

`scene::entityCullBounds` CPU-skins every vertex of a skinned entity to find its posed box, with no cache. Per
character per frame it ran up to four times: twice in the renderer's diagnostic (ADR-1081) and twice in
`Composition::cullEntityNodes`, which culled EntityWorld-driven characters in a first loop (finding each node's range
by a linear scan) and then again in a second loop over every entity. Measured on Glowmere: 18.6% of main-thread
samples, about 3 ms per frame (`evidence-live-render-path-2026-10-02.md`).

## Decision

1. **`Scene::posedMeshBounds(mesh, rig)`** returns the model-space posed box from a cache on the `Scene`, keyed by
   (rig, mesh). An entry is valid while the scene's `meshVersion` is unchanged and **the palette holds the same
   matrices** (compared with `memcmp`). `entityCullBounds` asks it instead of skinning. `Scene::clear()` empties it;
   `posedBoundsComputes()` counts misses for tests.
2. **Why the palette's contents and not `SkinnedRig::paletteVersion` alone:** `hold()` bumps the version without
   changing the pose (every culled, rate-limited character would recompute for nothing), and a rig rebuilt in place
   starts its count again, so an equal version is not proof of an equal pose. The existing culling test writes the
   palette without touching the version, which a version-keyed cache would have answered wrongly. Comparing a few
   dozen matrices is a few hundred nanoseconds; skinning is thousands of vertices times four influences.
3. **`cullEntityNodes` visits each entity once.** The EntityWorld pass marks the entities in its node ranges (keeping
   their semantics: an invalid mesh there is marked visible, water is culled), and the general pass skips them. Node
   ranges are found through a name index built once per call (first node of a name wins, `findNode`'s rule) instead of
   a `findNode` scan plus a node-list scan per character.

## Alternatives considered

- **A bound from joint spheres.** Cheaper still and correct up to a padding, but a change to the box the cull tests,
  which the brief did not ask for and the existing tests pin.
- **Cache on the rig.** The same rig poses several meshes (multi-primitive characters), so the key must include the mesh.

## Consequences

- One skinning per character per pose change, wherever it is asked from. A character that did not re-pose this frame
  (distance-rated rigs, ADR-186; culled rigs) costs a palette comparison.
- `tests/unit/test_skeleton.cpp`: three askers, one skinning; `hold()` does not recompute; a new pose with the version
  unchanged does; a mesh edit does.
- The cache is not thread-safe, like `meshBounds` beside it; both are read on the main thread only.
- **Measured** (`docs/live-quality/REPORT.md`): in this worktree Glowmere loads without its 22 rigged alien and farm
  characters (their `.glb` files are not linked here), so the A/B there is within noise. The full measurement needs
  the characters' assets linked into the worktree.
