# ADR-1958: The hop behaviour, a body that walks the small steps and hops the rest

- Status: Accepted (Musical Geography Phase 6, 2026-11-09), scene/musical-geography.
- Code: `Hop` in `src/entity/behaviors.cpp`, behaviour kind `"hop"`.

## Decision

A body on a ground source (ADR-1955) repeats one cycle:

1. **Pause.** It rides its tile (waves, breathing, a neighbour's spring) for 1 to 3.5 s.
2. **Pick a goal.** It samples 16 tiles 4 to 12 tiles away, scored by the crowns, the sea slabs and the path
   (`preferCrowns`, `preferWater`, `preferPath`) plus noise. It stays within `homeRadius` of where it started.
3. **Plan.** It plans a route with TileNav (ADR-1957).
4. **Move.** It walks the walk edges between tile centres. It steps up at once (never inside the tile) and eases down
   over 80 ms. It hops the jump edges, and with `hopChance` it also hops a walk edge for joy.
5. **Hop.** The arc runs from tile centre to tile centre: constant horizontal speed over `airTime`, a parabola
   `apex` metres high (plus half of any rise), aimed at the landing tile's height as it moves.
6. **Land.** It lands on the tile's top as it is at that moment, tells the ground (which springs, ADR-1954), and
   recovers for `landTime`.

It writes travel, yaw, speed, the activity (Idle, Walk, Run, Jump, Fall, Land) and airborne, so the gait picks the
clips. With no ground, it stays where the scene placed it. All randomness comes from the entity's own stream, so it
is deterministic. Live parameters: `walkSpeed`, `runSpeed`, `airTime`, `apex`, `hopChance`, `strength`.

## Consequences

- Tests: when standing or landing it sits exactly on its moving, sprung tile, with no jitter. It travels and hops,
  and the ground records the landings. Two runs are identical. With no ground it does not move.
