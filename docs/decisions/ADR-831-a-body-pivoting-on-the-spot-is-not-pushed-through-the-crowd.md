# ADR-831: A body pivoting on the spot is not pushed backwards through the crowd

**Status:** Accepted
**Date:** 2026-09-25
**Found by:** ADR-830. The terrain fix changed Rook's route in `glowmere-valley-2`, and
"the four aliens animate independently in the scene they ship in" went red: Rook travelled against
his facing on 31 of 3,826 moving frames. The limit is under half a percent, 19.
**Related:** ADR-240 (separation as a speed, not a step), ADR-829, ADR-830; Phase B §39
**Implemented by:** `separateFromCrowd`'s `stepping` argument (`src/entity/behaviors.cpp`)
**Tests:** that locomotion case. It fails with ADR-830's terrain alone and passes with this.

## What the data said

Logged per frame: Rook stood `Idle` 4.02 m from Tide, inside their combined crowd radius, and did
not move. That is right, because `Idle` does not separate. At 42.967 s he began a walk the way
every mover does, by pivoting on the spot (`Idle_turn`, facing error ~90°, travel speed 0). The
mover's crowd separation ran on that frame at its 1 m/s floor. For 0.5 s he glided 0.26 m straight
away from Tide at up to 0.78 m/s, while the turn clip rotated him through 1.3 rad with his feet
planted. The glide's direction was fixed in the world and his facing swept across it, so it read as
backwards.

## Decision

A body is not carried backwards out of an overlap faster than it is stepping.
`separateFromCrowd` takes `stepping`, a value in [0, 1]. The mover passes its facing alignment,
which is the same number that already scales its travel speed. `ground` (the `decide` path) passes
1, so its behaviour is unchanged. Only the component of the push that points **behind** the body is
scaled by `stepping`. The sideways and forward components still act at once.

**The first version scaled the whole push by `stepping`, and the suite rejected it.** Two things
failed:
- §11's "characters make room for each other" found two bodies still 0.60 m inside each other after
  the first second. The limit is 0.5.
- ADR-333's route golden changed in 91 lines.

A body that is only pivoting would never separate at all, and the rule that bodies do not stand in
each other outranks the rule that a turn does not slide. Keeping the sideways and forward push
passes both tests, and the route golden is bit-identical. What remains is a sideways drift during
a pivot. It is smaller, and it is the thing Phase B §39 should replace: separation as steering the
motion system turns into steps.
