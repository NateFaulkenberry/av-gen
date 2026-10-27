# ADR-1806: The Scene caches each mesh's index validity with its bounds

- Status: Accepted (eng/scene-bounds-cache, 2026-10-27). Approved by the owner after the Strange Attractor canyon
  exposed it.
- Code: `src/scene/scene.{hpp,cpp}`: `Scene::meshValid`, `Scene::meshCacheEntry`, and `Scene::bounds()` reading the
  cache.
- Tests: `tests/unit/test_scene_mesh_cache.cpp` (`[adr1806]`).

## Problem

`Scene::bounds()` runs every frame, from the renderer's shadow setup, which sizes the cascades. For every visible lit
entity it called `MeshData::valid()`, which walks every index of the mesh to check it is below the vertex count. The
bounds were already cached per `meshVersion`; the validity check was not.

At 8.27M triangles (the Strange Attractor's engine-native canyon) this cost about 5 ms of CPU per frame, and 15.5 ms at
26.6M. The profile reports it under "render submission: lights".

## Decision

- **One cache entry per mesh: its bounds, its validity and a fingerprint.** The fingerprint is the vertex and index
  counts and their data pointers.
- **The table is rebuilt when `meshVersion` moves**, the contract every mesh edit already keeps for the renderer's
  upload.
- **Each lookup also re-checks the entry's fingerprint.** A mesh that is resized, reallocated or replaced (or appended
  to `meshes`) without a version bump is recomputed rather than answered stale. Before this ADR such an edit already
  left the bounds stale.
- **`Scene::meshValid(id)`** returns the cached `MeshData::valid()`, and false out of range. `Scene::bounds()` uses it,
  so an invalid mesh is still excluded exactly as before.
- **`meshBoundsRebuilds()` also counts per-mesh recomputes**, so a test can see a fingerprint miss.

## Consequences

- `Scene::bounds()` no longer scales with index count. On a synthetic 5.1M-triangle scene, the uncached validity scan
  takes 7.1 ms and the cached `bounds()` 0.0005 ms (unit test).
- **Profiles** (1920×1080, M2 Max, `--live-profile --mode headless --quality ultra`), "render submission: lights":
  - synthetic 1,024-chunk terrain (6.4M triangles drawn): 5.70 ms → 0.26 ms; CPU work median 8.31 → 2.71 ms;
  - Event Horizon: 0.42 ms → 0.06 ms.
- **Limitation.** An edit that overwrites index values in place, keeping the same size and allocation, without
  bumping `meshVersion` is not seen. That edit is equally invisible to the renderer's upload and to the bounds cache,
  so the existing contract (bump `meshVersion`) is the rule.
