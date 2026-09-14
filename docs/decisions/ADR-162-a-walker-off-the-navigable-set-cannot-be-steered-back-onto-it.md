# ADR-162: A walker that steps off the navigable set cannot be steered back onto it

**Status:** Accepted
**Date:** 2026-09-14

## The report

*"He does have a tendency to get stuck places in the map in different spots and will stay there."*

A bug described as "sometimes, somewhere" cannot be confirmed or refuted by looking at a frame, so the
first work was a probe: walk the real Glowmere wanderer for twenty simulated minutes and measure the
longest interval in which it moves less than a metre. **942.5 seconds.** Sixteen minutes standing
still. The report was exact.

## The probe was wrong first, in the way this project keeps being wrong

Its first three runs returned **643.5 s at exactly the same coordinates across three different
attempted fixes.** Identical to the decimal is not a result, it is a tell. The camera had been left
wherever the scene put it, the wanderer was past its entity `cullDistance` of 320 m, and it was not
being simulated at all. A culled entity's position is frozen to the decimal, which is
indistinguishable from stranded.

ADR-151's rule, again, and from someone who had cited it twice that day: **a probe must prove it
established the state it claims to measure.** With the camera following the wanderer the stall became
942.5 s at a different place — and started varying with the code, which is what a live measurement
does.

## What is actually happening

Every component behaves correctly. That is why it survived.

1. The walker ends up just outside the navigable set — a step down, the edge of the water, a slope
   that tipped over the limit on the way down it. Measured at the wedge point: `navigable = false`.
2. `pathClear` samples navigability **from the body's own position outward**, so from there every
   ray is blocked whichever way it points. Measured: all eight compass directions blocked at 0.5 m.
3. The steering fan therefore finds nothing, and `steer` returns zero.
4. The behaviour stops the feet and its watchdog re-plans after 2.5 s.
5. The planner **snaps an unwalkable start to the nearest walkable cell** and returns a perfectly
   good route — from a place the body is not. Status `ok`, failures `0`.
6. The walk resumes into the same spot. Measured: the blocked branch taken on every frame, 121
   frames per two seconds, for the rest of the piece.

Nobody ever told the body where the planner snapped to.

## Three fixes that were measured and are not the fix

Each was plausible, each was implemented, each was run against the corrected probe, and each left the
stall at 942.5 s:

* **Widening the steering fan past 120°** so a walker can turn round. The ±2.1 rad fan does leave a
  120° blind cone behind the walker, and that is a real limitation — but the directions were never
  the problem.
* **Resolving penetration before giving up.** The guarantee that a body never ends a frame inside a
  solid was genuinely unreachable from the blocked branch, which returns early. Fixing that moved the
  body 0.094 m once and never again, because it is not inside anything.
* **Retrying the fan at a shorter reach.** An eight-metre lookahead in a clearing smaller than eight
  metres does fail everywhere, and the reach ought to be an ambition rather than a requirement — but
  it is not what was happening here.

All three are arguable improvements and none of them is this bug. They are recorded because the next
person to look at this will think of all three, and now they cost nothing to skip.

## The decision

When the steering fan finds nothing **and** `navigable` is false at the body's own position, walk the
body toward the nearest point that the navigator — not the grid — calls navigable, found by an
outward spiral, clamped to a walking step so it reads as picking its way back onto the path rather
than as a teleport.

The predicate matters and is the second half of the bug. The first attempt targeted
`NavGrid::nearestWalkable`, which is what the planner itself uses. It moved the body one metre onto
(-14, -242) — a cell centre, exactly — where `navigable` is still false, and it re-stalled there.
**The grid is baked once from one sample per cell and the obstacle set moves afterwards**, so
grid-walkable is a claim about build time and `navigable` is a claim about now. An escape that
satisfies the wrong predicate escapes to somewhere it is still stuck.

This is the same guarantee the penetration resolve makes for solids — a body may not end a frame
inside a rock — extended to the case that actually occurs: a body ending a frame off the walkable set
entirely.

## Also tried and dropped

Pruning connected regions smaller than eight cells at nav-grid build time. Glowmere's grid comes out
as 24 disconnected pieces, 21,694 cells in the valley and 49 across the rest, and those 21 pockets
are places a walker would be genuinely trapped. Pruning them took the grid to 3 regions — and
**changed the measured stall by nothing at all**, 10 s either way. The escape above handles an islet
anyway, because it spirals to a navigable point across the gap. Dropped rather than kept as insurance:
an unmeasured change that duplicates a measured one is how a system accumulates two mechanisms for one
problem and loses track of which is load-bearing.

## Evidence

`tests/unit/test_entity_action.cpp`, tagged `[.probe]` because twenty simulated minutes is seconds of
wall clock but not something the default suite should pay for on every run. **942.5 s → 10 s**, and
10 s is an authored observe dwell rather than a stall. The threshold is 120 s, deliberately generous:
the explore behaviour idles, observes and dwells by design, and the longest of those is measured in
seconds, so a character that has not moved a metre in two minutes is not resting.
