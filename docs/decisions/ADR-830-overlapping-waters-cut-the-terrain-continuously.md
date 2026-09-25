# ADR-830: Overlapping waters cut the terrain continuously

**Status:** Accepted
**Date:** 2026-09-25
**Answers:** the owner's ruling "fix Ember's foot jump on the steep bank" (found by ADR-829's
all-alien check): at 16.85-17.03 s in the multicam film Ember's foot jumped 0.40 m in one posed
frame, with body compensation at its 0.30 m cap and a foot unreachable
**Related:** ADR-829 (stride warp), ADR-551 (per-foot ground), ADR-544 (body compensation)
**Implemented by:** `WorldMap::heightUncached` (`src/world/world_map.cpp`)
**Tests:** `tests/unit/test_water_cut_continuity.cpp` (`[adr830]`): a synthetic river and pool, the
Glowmere bank itself, and the multicam film's five aliens over 40 s. All three fail on the old code
(1.687 m, 0.872 m, and Ember 0.396 m) and pass now.

## What the data said

It was not the foot IK. Logged per frame, the ground under Ember (the entity's own surface
sample, and each foot's terrain sample) rose 0.47 m between 16.933 and 16.950 s while she moved
5 cm. So the terrain has a step there. Walked with `TerrainQuery::at` along her line at 1.3 cm
spacing, the height went 3.388 -> 4.263 m between two adjacent samples. That is an 0.875 m cliff
on an otherwise ~12° slope. It was the same with the height cache and the path cutoff turned off,
so it was not either optimisation. The IK and compensation were doing their job on a surface with
a step in it: they pulled the body down to its cap, then the foot's target jumped.

Removing features one at a time found it. The step disappears without `glowmere-run-2` (the river)
or without `elder-pool` (a flat water), and with nothing else removed.

## Cause

Every water feature cuts the terrain down to its bed. The height function kept **one** target, the
lowest of the waters' beds, and **one** weight, the largest of their weights, and applied
`mix(h, min(h, target), weight)`. Where two waters overlap, those two come from different features.
Where the pool's influence begins (weight 0+) inside the river's shoulder (weight ~0.6), the pool's
lower bed arrived at the river's weight in one step: a cliff along the whole edge of the pool's
reach, and a crease where the weights cross.

## Decision

Each water cuts with its own weight, `mix(h, min(h, target_i), weight_i)`, and the terrain takes
the deepest of those cuts. Each cut is continuous in its own weight, and the minimum of continuous
functions is continuous. A point under one water is exactly what it was. Up to 16 cuts are kept per
point (no point in any scene has more than two). Past that, the last slot folds the rest the old
way rather than dropping one.

## Consequences

- The Glowmere map changes on 1,192 of 409,600 m² (1 m grid), all of it in the river/pool overlap
  west of the elder pool. It is identical elsewhere. The cliff and crease are gone (review file
  12).
- A terrain change moves navigation, so Ember no longer takes that line at 17 s. The film test
  measures every alien, not one route. The other four aliens' worst foot steps are unchanged or lower (Sage 0.074 -> 0.043 m).
  Vane's remaining 0.10 m at 16.48 s is a fast walk down a steep bank, where compensation bobs with
  the stride. There is no step in it.
