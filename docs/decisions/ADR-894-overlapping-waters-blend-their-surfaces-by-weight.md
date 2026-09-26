# ADR-894: Overlapping waters blend their surfaces by weight

**Status:** Accepted
**Date:** 2026-09-25
**Follows:** ADR-830 (each water cuts the terrain with its own weight)
**Implemented by:** `WorldMap::waterSurface` (`src/world/world_map.cpp`)
**Tests:** `tests/unit/test_path_level_continuity.cpp`:
- "a pool perched above a river meets the river's water line without a wall"
- "one water alone keeps its own surface"

Also changed: `tests/unit/test_directing_directed.cpp`, the played orders test's target (below).

## Context

The water line was the *highest* surface of any water feature reaching a point. That is right for
one water and a wall for two.

- Glowmere Valley 2's `elder-pool` sits on the river with its surface 1.2 m above the river's.
- The highest-surface rule carried the pool's surface across the river channel to the edge of the
  pool's reach, 36 m from its centre. There it dropped to the river's own surface: **2.63 m in one
  grid step, in mid-river** (`avgen_world_preview --seams`).
- The water mesh places each vertex at the surface (`buildChunkWater`), so the drop rendered as a
  wall of water across the river.

ADR-830 fixed the same shape on the terrain side, where each water now cuts with its own weight.
The water line was never given the same treatment.

## Decision

- **Where waters overlap,** each surface counts in proportion to its own feature weight. A surface
  fades out where its feature does, which is continuous, because a feature's weight falls to zero at
  its reach.
- **One water reaching a point** gives exactly its own surface, as before, so a river, a lake or a
  sea on its own is unchanged.
- **The water table** (`waterTable`, which ecology uses to place vegetation) keeps its
  nearest-water rule. It uses ADR-893's continuous level, but it does not blend between waters.
  - Blending it was tried. It fixed no visible defect, and by moving vegetation it re-routed
    Glowmere's aliens for nothing. The nearest-water rule is continuous within one water and
    changes owner only on the line where two are equally near.

## Consequences

- **The scanner** finds 0 water seams on Glowmere Valley 2 (from a 2.63 m wall).
- **The pool shows its perched surface less.** Where the river is strong, the pool's surface now
  tends toward the river's. The pool region's water line slopes gently from the pool's level to the
  river's instead of standing flat and walled.
- **Aliens re-route.** The aliens are drawn to water, so any change to where the water is changes
  where they walk. In the multicam film this re-routed Ember into a sharp turn, which exposed
  ADR-895's defect.
- **A test's control arm moved with them.** "orders, played: Rook turns to ... and reacts"
  (`tests/unit/test_directing_directed.cpp`) proves a directed `face` order works by comparing Rook
  against the same world without orders. Its control asserts that, on his own, Rook does not face the
  target between 20 and 22 s (closest > 0.5 rad).
  - **What changed:** re-routed, Rook on his own came within 0.31 rad of facing Vane. That is
    inside the gap between the test's "facing" (< 0.2 rad) and "not facing" (> 0.5 rad).
  - **Proven to be this change:** the old highest-surface rule, restored in a throwaway build,
    passes; ADR-893's switch alone does not.
  - **The fix:** the played test now faces Sage. On his own, Rook's closest to Sage in that window
    is 2.01 rad (to Tide 0.98, to Ember 0.93), so the control has the most margin. Ordered, it is
    0.03 rad.
  - **Unchanged:** the thresholds. The compile-only tests and the golden plan keep Vane, because
    they do not depend on the world.
