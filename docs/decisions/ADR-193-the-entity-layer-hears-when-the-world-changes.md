# ADR-193: The entity layer hears when the world changes

**Status:** Accepted
**Date:** 2026-09-14

`Composition::rebuild()` opens by throwing the obstacle field away and building a fresh one, with a
comment stating exactly why:

> The obstacle set describes the world this rebuild is about to produce, so it starts empty here
> rather than being patched: **a stale solid is a character walking round nothing, and a missing one
> is a character walking through a tree.**

That sentence was true of the bridge and false of the characters.

## Two defects, one shape

**The navigator was installed once and never again.** `EntityWorld::setNavigator` takes a `Navigator`
**by value**, and a `Navigator` holds `shared_ptr`s to the obstacle field and to a `NavGrid` baked
from it. It is installed from `installEntities()`, which `rebuild()` does not call. So after any
rebuild the walkers were pathing against the previous world, while `TerrainQuery` — rebuilt on every
call — saw the new one. The two halves of the same question, disagreeing.

**And a hero was not an obstacle until something else happened to rebuild.** `setHeroes` validated,
stored, bumped a revision — and did not mark the composition dirty. `obstaclesFromHeroes` runs only
inside `rebuild()`, so a hero declared on its own was a solid the navigation layer never heard about:
the director framed it, the editor drew its ring, and walkers strolled through it.

## The fix

`rebuild()` re-installs the navigator at the end, once the obstacle field is built — and only when
there is somebody to walk, so a scene with no entities pays nothing. `setHeroes` marks the
composition dirty.

Measured on Glowmere: the nav grid is **170 ms against a 1676 ms flatten**. Ten per cent of an
operation that is already the expensive one, which is the right price for the entity layer agreeing
with the world it is standing in. (A second flatten is much cheaper — 226 ms — because the terrain
products are reused; the grid is then most of it, and that is worth knowing before anyone puts this
on a per-frame path.)

## The test, and the control that was not one

The first version of the test used `TerrainQuery::isOccupied` as its control: the shared query sees
the hero, so if the navigator does not, the navigator is stale. **That control proves nothing.**
`terrainQuery()` is constructed fresh on every call and reads `heroes_` directly, so it sees a hero
whether or not anything rebuilt — it passed before the fix and after, and would have passed with no
rebuild at all.

The control has to be something only a rebuild can produce. It is the **blocked-cell count of the
grid**: a twelve-metre solid takes cells out of the walkable set (838 → 868 on Glowmere), and that
number can only move if the grid — the thing `EntityWorld` was holding a stale pointer to — was
built again.

Same rule as ADR-182, found in this project's own test rather than in its renderer: an arm can be
non-vacuous and still attribute nothing.

## And a knob nobody could turn

`navCellSize_` had a field, a default of 4 m, and a documented escape hatch — "0 disables
pathfinding, which leaves the straight-line steering that was here before ADR-093" — and **no setter
and no scene key**. The escape hatch was unreachable and the default was unchangeable. It is now
`setNavCellSize` plus a top-level `navCellSize` in the scene file, validated rather than silently
clamped: a scene asking for a 0.1 m grid over a kilometre of world is asking for a hundred million
cells, and finding that out as an error beats finding it as a stall. The key is emitted only when it
differs from the default, so an untouched scene round-trips byte-identical.
