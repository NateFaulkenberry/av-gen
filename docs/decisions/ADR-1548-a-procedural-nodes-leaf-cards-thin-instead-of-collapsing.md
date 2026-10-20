# ADR-1548: A procedural node's leaf cards thin instead of collapsing

- Status: Accepted (art/rainbow-world)
- Amends ADR-085 (mesh-source LOD levels through meshoptimizer). Brings ADR-351's shell thinning, which imported
  gltf trees already use through `foliageLodSettings`, to procedural mesh sources.
- This change is additive. A file without the key builds exactly the levels it built before, and a node without it
  writes no key and hashes as before.
- Code:
  - `src/scene/procedural.hpp`: `LodSettings::thinning`, and a `thinning` argument to `makeLodMesh` (default off).
  - `src/scene/procedural.cpp`:
    - reads and writes the key; it is hashed only when set;
    - in `makeLodMesh`, arms `ShellThinning::fallback` at the foliage calibration (1.5);
    - on a mesh of small shells (at least 64 shells, at most 24 triangles each, the same test `buildLodChain` uses),
      also holds the shells' open edges (`lockBorder`) while simplifying.
  - `src/rendering/procedural_renderer.cpp`: passes the flag through.
- Tests: `tests/unit/test_procedural_lod_thinning.cpp` (`[adr1548]`). The fixture is a canopy of 2,000 separate
  two-triangle cards.
  - Levels 1-3 with the flag:
    - each level is a real reduction;
    - each holds only whole cards;
    - no triangle is smaller than a source card's;
    - the canopy keeps at least 80% of its leaf area at levels 1-2, and 36% at level 3, where
      `ShellThinning::maxScale` (3x) caps the growth.
  - The control: without the flag, level 3 keeps 4% of the leaf area.
  - The JSON reads, round-trips, is written only when set, and changes the structural hash.

## Problem

Rainbow World's trees are cloud-pruned pads of alpha-tested leaf cards: 4,000-4,500 cards on a Great Tree. Removing
the leaf nodes measured them at 9-17 ms of GPU at Ultra 1080p (Stage 5 report, section 5). They are the scene's
largest single cost after the land.

The procedural LOD ladder is no help as it stands. A card has only open edges, so meshoptimizer's preserving
simplifier reaches any ratio it is asked for by collapsing whole cards to nothing. It deletes them and grows none of
the survivors, so the level "succeeds" and the canopy goes bare. Shell thinning was written for exactly this
geometry (ADR-351, measured on the Tree of Life's 122,000 leaves): it removes whole cards and grows the kept ones to
hold the area. Only gltf nodes could reach it.

## Decision

A procedural node's `lod` block may carry `"thinning": true`:

```json
"lod": {"cull": true, "count": 3, "byScreenSize": false, "distance1": 40, "distance2": 85, "maxDistance": 150,
        "thinning": true}
```

Holding the shells' borders is the mechanism, not a second feature. It makes the preserving simplifier refuse the
cards instead of deleting them. `buildLodChain` reads that refusal as the overshoot that hands the level to
thinning. A mesh that is not made of small shells is left exactly as without the flag.

## Consequences

Measured on Rainbow World. Live profile at Ultra 1080p, the same frames, the leaf nodes the only difference:

| t | No LOD, leaves to 110 m | Thinned LOD, leaves to 150 m | LOD without thinning |
|---|---|---|---|
| 200 | 53.5 ms | **43.1 ms** | 43.2 ms |
| 30 | 36.0 ms | **33.6 ms** | 33.9 ms |

- Thinning costs the same as the bare ladder. What it buys is the canopy. At t=300, the unthinned ladder's middle-
  distance pads show holes, and the thinned ones read as the Stage 5 frame does
  (`~/Desktop/av-gen-review/74-rainbow-world/stage6/04-leaf-lod-thinning.png`).
- Leaf cards now reach 150 m, where before they stopped at 110 m.
