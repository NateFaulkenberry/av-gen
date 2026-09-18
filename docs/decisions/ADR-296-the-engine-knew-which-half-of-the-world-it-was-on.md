# ADR-296: The engine knew which half of the world it was on, and which characters could not leave

**Status:** Accepted
**Date:** 2026-09-18

`docs/character-ai-plan.md` §P7 item 2:

> 28 regions; the largest holds 9,938 of 19,584 walkable cells. Half the walkable ground of the
> shipped world is not reachable from the other half and nothing says so. `NavGridStats::regions` is
> computed at build and read by no UI; `PathStatus::Unreachable` exists and is surfaced nowhere.

**Part of that is out of date and the correction is worth more than the agreement.** ADR-197 landed
the navigation overlay after the plan was written. `NavGridStats::regions` *is* read by a UI — the
world panel's status line prints it, and "Regions" colours walkable cells by connected component.
`PathStatus::Unreachable` *is* surfaced — `NavDebug` carries `status`, the route overlay prints it
in the label, and it flags the route as failed. Neither of those needed building.

What was actually missing is three narrower things, and one of them is the case the plan's own
motivating example turns out to be.

---

## 1. A region count is not a reachability report

`regions 28, largest 9,938` tells an author the graph is in pieces. It does not tell them **how much
ground is on the wrong side**, which is the number they act on. Subtracting is one line and nobody
had written it, so nobody had seen it:

```
glowmere-valley-2: 28 regions, 9,646 of 19,584 walkable cells (49%) stranded
character lab:      3 regions,   217 of  2,517 walkable cells ( 9%) stranded
```

`NavGridStats::stranded` is that number. The build log says it at **`warn`** rather than `info` — a
world where half the standable ground cannot be reached from the other half is a thing to be told
about, not a thing to find in a log. The panel says it under the grid line. `NavGrid::regionSizes()`
and `largestRegionId()` expose the breakdown, and a test asserts the accounting closes:
`stranded + largestRegion == walkable`, because a number that does not add up is not a report.

## 2. A route overlay that says the status does not say which island you are on

The selected walker's label now carries `region 7/28 (214 cells)` when the ground is in pieces, and
`destination is NOT reachable from here` when `NavGrid::connected` says so. Both are lookups the
grid has answered since it was built, and `connected` is flood-fill labelling done with *exactly*
A*'s step rule — so "different regions" and "A\* will not find a route" are one statement and not two
that can disagree.

## 3. The case the plan's example actually is, which no `PathStatus` can reach

P4 recorded it precisely:

> `requestPath` out of the sealed pen returns `Unreachable` with 0 waypoints, `Explore`'s
> `stuckSeconds` watchdog replans silently, and **being stuck and being idle produce the same frame
> and the same log.**

Reproducing that turned out to mean reading it more carefully than it reads. A penned body that
*plans* is reported already: it asks, it is told `Unreachable`, and the overlay prints it. The body
that is invisible is the one that **never asks**. `Explore::pickGoal` filters interest points by
`homeRadius`, falls through to a stroll, and `Navigator::pickDestination` rejects every candidate in
the annulus because they are all inside the wall. No path request is ever made, so no `PathStatus` is
ever published, and the status the overlay shows is still the `Ok` of whatever errand the body
finished before it was penned.

So this is not a missing enum value. `NavDebug` grows two floats, published by `Explore`:

* `confinedFor` — seconds this body has been unable to find anywhere at all to go.
* `stuckFor` — seconds it has been walking without getting closer, which the watchdog already
  counted and threw away.

Measured by `tests/unit/test_nav_grid_trust.cpp`, twenty seconds, two identical explorers with the
same seed and the same settings, one inside a sealed wall:

```
penned: confined 18.77 s, status ok, travelled  0.00 m
free:   confined  0.00 s, status ok, travelled 11.70 m
```

The test asserts `penned.status == free.status` on purpose. That equality **is the defect**, and it
is why the fix is a new field rather than a new `PathStatus`.

*What the pen cost to build, because it is a fact about the graph and not about the test.* Two wrong
pens came first. A thin ring leaves the stroll annulus on open ground outside the wall, so the body
picks a destination there and is told `Unreachable` — the case that already worked. A ring thinner
than a grid cell is worse: at 4 m cells **the graph does not see the wall at all**, A\* routes a line
straight through it, the status reads `ok`, and the body walks into stone for two minutes with
nothing anywhere saying so. The working pen is a wall thicker than the annulus. The middle case is
worth remembering on its own: a pen smaller than a cell is a pen the planner denies exists.

## 4. What is still not reported

* **Nothing walks a whole scene and reports which characters cannot reach their home region.** The
  facts are all present now; the pass over them is not.
* **`Wander` publishes no `navDebug` at all**, so none of this reaches a farm animal. That is
  ADR-197's boundary and it is unchanged; the Character Intelligence Lab already runs `explore` on
  all four of its bodies for exactly this reason.
* **The panel is not verified visually here.** The strings are asserted where they are built; nobody
  on this branch has seen them drawn.
