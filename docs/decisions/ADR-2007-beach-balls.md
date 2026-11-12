# ADR-2007: Beach balls: light bodies that bounce on the drawn tiles, ride their waves and never settle

- Status: Accepted (Musical Geography, CPD-R12, the owner on draft9, 2026-11-11), scene/musical-geography.
- Code: the `ball` behaviour (`Ball` in `src/entity/behaviors.cpp`). It is placed with an `orb` node (its `scale` is
  the ball's size, its `material` the look).
- Tests: `[balls][adr2007]` in `tests/integration/test_beach_balls.cpp`. Three balls on a moving tiles ground, 16 s:
  - none sinks into a tile;
  - each bounces at least three times and is still rising over 2 m in the last 4 s;
  - their impacts are landings with a negative strength and reach the renderer's list;
  - a 30 fps play and a cold seek put each ball where the 60 fps play does, to a millimetre.

## Context

The owner: "bouncing beach balls that bounce off of surfaces, ramp off of waves, let's add an effect when they hit a
surface similar to the alien when it lands - but use a different color, and add a sparking explosion like effect that
also radiates out from where the ball landed ... about 10 of these dispersed throughout the cameras path - they can
act very light - take big bounces ... start with a reflective orb".

## Decision

**A body on the hop's fixed 60 Hz step** (ADR-1997), so seek equals play. Each step it:
- falls at `gravity` x `gravityScale` (default 0.35: light);
- loses `drag` (1/s);
- moves.

**The surface is the drawn tile top**: `GroundSource::drawnBy` with the landings every body has passed (ADR-2002),
the same ground the aliens stand on, springs included.

**On contact** (the ball's bottom below the top, moving into it):
- it reflects about a normal tilted by the land's slope across the neighbouring tiles (`slope`, default 1);
- `restitution` (default 0.92) applies;
- a rising tile's upward speed is added (`tileKick`, default 1), so a wave launches it and a slope ramps it.

**Never dies.** Every bounce leaves fast enough to reach `minBounce` metres (default 3).

**Stays near home.** Once further than `homeRadius` (default 30 m) from where it was placed, a bounce steers it back.
Speed is capped at `maxSpeed` (default 25 m/s).

**Tile sides.** Running into a tile whose top is above the ball's lower quarter reverses the crossing axis.

**Water.** Bounces off it like the land (allowed for now).

**Between steps** it is drawn on its ballistic arc, never below the drawn top.

**Each impact** is a landing with strength `-strength` x the impact speed / 8 (clamped to 0.2-1; `strength` default
0.5). The ground springs it like an alien's landing. ADR-2008 covers the look.

**Scene format.** On the entity's behaviour:
- placement: `radius` (match the orb's scale, default 0.6) and `velocity` (`[x, y, z]`, the start);
- motion: `gravityScale`, `drag`, `restitution`, `minBounce`, `maxSpeed`, `tileKick`, `slope`;
- home and impact: `homeRadius`, `strength`.

## Consequences

- **Order dependence.** A ball's impacts are landings, so the aliens' drawn ground and their planning surface feel
  them. The order caveat is ADR-2002's.
- **Live cost.** See ADR-2008.
