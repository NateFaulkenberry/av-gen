# ADR-1546: A chunked surface's mesh LOD can hold its border

- Status: Accepted (art/rainbow-world)
- Amends ADR-029 (procedural culling and LOD) and ADR-085 (mesh-source levels through meshoptimizer). This change is
  additive. A file without the key builds exactly the levels it built before, and a node without it writes no key
  and hashes as before, so every existing save and cache key is unchanged.
- Code:
  - `src/scene/procedural.hpp`: `LodSettings::lockBorder`, and a `lockBorder` argument to `makeLodMesh` (default off).
  - `src/scene/procedural.cpp`: reads and writes the key. `makeLodMesh` simplifies a mesh source's levels with
    `LodChainSettings::lockBorder` set and the sloppy fallback off. A level that will not simplify returns the
    source instead of the clustering fallback. `structuralHash` includes the flag only when it is set.
  - `src/rendering/procedural_renderer.cpp`: passes the flag through.
- Tests: `tests/unit/test_procedural_lock_border.cpp` (`[adr1546]`).
  - A rolling 48x48 grid is simplified at levels 1-3 with the flag. Every level has fewer triangles than the source,
    and every one of the 192 border vertices is still present, in the same place.
  - The control: without the flag, level 3 loses border vertices.
  - The JSON reads, round-trips and is written only when set, a non-boolean is refused, and the flag changes the
    structural hash, so the renderer's mesh cache cannot hand one ladder out for the other.

## Problem

Rainbow World's land is one closed band 5.7 km long. It is exported in chunks so the culler can drop what is behind
the camera. A chunk is about 50,000 triangles at 3 m spacing.

Beyond a few hundred metres those triangles are sub-pixel, and the scene pass pays a quad per triangle.

Procedural mesh sources already had a LOD ladder (ADR-085). But its simplifier treats a mesh's open edges like any
other edge, and its two fallbacks (meshoptimizer's sloppy simplifier and vertex clustering) ignore topology
altogether. Two neighbouring chunks at different levels would no longer share their seam, and a crack in the land
shows the sky through it. That is a visible edge of the world, which the scene's rules forbid.

## Decision

A procedural node's `lod` block may carry `"lockBorder": true`:

```json
"lod": {"cull": true, "count": 4, "byScreenSize": false, "distance1": 320, "distance2": 800, "distance3": 1700,
        "lockBorder": true}
```

- It applies to mesh sources' simplified levels (1-3). LOD0 is the source as before.
- It sets meshoptimizer's `LockBorder`, which `LodChainSettings` already carried for exactly this case ("right for a
  mesh that is one piece of a larger surface (a terrain chunk)"), but which nothing could reach from a scene.
- Both topology-blind fallbacks are disabled with it. A level that cannot simplify with its border held is the
  source. A level that is too dense is a cost, but a cracked one is a defect.
- It is structural, because it changes the meshes: it is hashed when set.

## Consequences

- Rainbow World's 48 band chunks, centred on their own origins, take a four-rung ladder.
  - Measured on the live profile at Ultra 1080p (five ride times): shadows fell from 11.5-12.9 to 6.5-8.8 ms, and
    visible triangles from 4.3-4.7 M to 2.4-3.2 M.
  - No seam opened in the review stills.
- Centring the chunks mattered as much as the flag. The procedural culler's bound is the mesh's extent about its
  origin, so a chunk exported in world space was never culled.
