# ADR-2003: A keep-out line: a body keeps a distance off the flight's route, so a near alien can run along it

- Status: Accepted (Musical Geography, CPD-R10, the owner: "need to see them running around more", 2026-11-11),
  scene/musical-geography. Extends ADR-1997/1999 (what a walker counts as water).
- Code: the hop's `avoidLine` and `avoidDistance`, `Hop::kept`, the reachable segments (`keepOut_`, found when the body
  is primed) (`src/entity/behaviors.cpp`).
- Tests: `[adr2003]` in `tests/integration/test_tile_motion.cpp`: three bodies with 24 m homes on both sides of a bent
  line, running, for 20 s. None comes nearer the line than 5 m on any frame, grounded or thrown; they run; the crests
  still throw them; a cold seek lands every body where the 30 fps play does.

## Context

CPD-R10: the hop's home is a circle (`homeRadius`). A near alien d metres off the flight line had to stay d - 4.5 m
from home to keep 4.5 m off the line. For the near band that is one or two tiles, so the near aliens idled (0.26). Art
asked for either a lane-shaped home, or a keep-out line with a minimum distance.

## Decision

**The keep-out line.** It is the cleaner of the two:
- it is one rule the walker already has a place for (what counts as water: ADR-1997, ADR-1999's banks), honoured by
  goals, paths, rests, the throw's steering and the start;
- a lane home would need a new home shape for every body, and its distance from the line would still have to be
  authored per body;
- it separates "where I like to be" (home) from "where I must never be" (the lens's path).

The hop reads two settings:
- `avoidLine`: world `[x, z]` points, the flight's route sampled.
- `avoidDistance` (m, 0 = off).

A tile whose centre is nearer the line than the distance counts as water to the body. A walk goes from tile centre
to tile centre, and the distance to a straight segment is linear along such a step, so the body itself never comes
nearer than the centres it stands on. A thrown body lands on a checked tile, along a course between two allowed
points. The check is a pure function of position, so it is seek-safe and needs nothing in a checkpoint. Each body
keeps only the segments within its reach (its home, the plan's search window, the distance), found when it is primed.

## Consequences

Measured on art's HEAD (dd3849d0, 63 bodies, the whole song, the CPD-R6 trace). The variant sets the flight's route
(the camera's x-z every 0.5 s, 422 points) with `avoidDistance` 5 m on every body, and widens the 53 near homes
(`homeRadius` 1-9 m) to 24 m:

| | as shipped | keep-out line, near homes 24 m |
|---|---|---|
| near band: idle / walk / run / jump | 0.389 / 0.129 / 0.185 / 0.248 | 0.160 / 0.217 / 0.357 / 0.221 |
| whole cast: idle / run | 0.342 / 0.222 | 0.150 / 0.366 |
| closest any body comes to the camera (3D / x-z) | 5.61 / 4.88 m | 10.05 / 6.44 m |
| grounded frames on water, landings on water | 0, 0 | 0, 0 |
| landings (throws) | 1,503 | 1,359 |

The lane home is not built. Art sets the route once for every body, and keeps the near band's homes as wide as it
likes.
