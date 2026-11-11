# ADR-2002: The throw reads the drawn tile it will pop from, with the landings every body has passed

- Status: Accepted (Musical Geography, CPD-R9, 2026-11-11), scene/musical-geography. Amends ADR-1997 (the throw reads
  only the landscape) and ADR-2000 (the crouch's start).
- Code: `GroundSource::drawnBy` (`src/world/ground_source.hpp`), `TileGround::drawnBy` (`src/scene/tile_ground.*`),
  `Hop::drawnCrest` and `kDrawnLag` (`src/entity/behaviors.cpp`). A/B arm: `AVGEN_THROW_LANDSCAPE=1` (ADR-2001 alone).
- Tests:
  - `[adr2002]`: three bodies a tile apart, springing under each other's landings at art's spring. A 30 fps play, a
    60 fps play and a cold seek put every body in the same place, to a millimetre.
  - `[adr2001]`: the drawn tile's check (above).
  - The existing seek tests (`[adr1997]`, `[adr1998]`, `[adr2000]`, `HopScene`) also pass with the change.

## Context

After ADR-2001, the drawn tile under a pop still had a top 0.1 m higher within 0.7 s in 76 of 1,508 throws on art's
build. The landscape had none, so all 76 came from the landing springs (ADR-1954), which ADR-1997 kept out of the
throw so that a body's own landing could not throw it again. Art's spring is large: depth 1.1 m at strength 0.5,
1.4 Hz, with a 14 m ripple. Neighbours ring a tile by ±0.2 m (p90), and its own last landing by up to 0.6 m.

Reading the drawn surface (`at`) is not seek-safe. Bodies step their 60 Hz ticks one body at a time, catching up a
frame's worth each. Within a frame, a body earlier in the list would read landings that a later body has not made
yet. At 30 fps, at 60 fps and in a seek's 60 Hz replay, those frames cut the ticks differently.

## Decision

1. **`GroundSource::drawnBy(x, z, t, landedBy)`:** the drawn surface with only the landings made by `landedBy`. On a
   tiles ground it is `at` with the later springs filtered out.
2. **The check, once per throw, as the crouch would start.** The hop reads the drawn tile under it over the crest's
   window, with `landedBy = now - 0.1 s` (`kDrawnLag`). Every body has stepped past that instant whatever order they
   are stepped in, as long as a frame is shorter than 0.1 s. That covers the offline render (1/30 s), the seek's
   replay (1/60 s) and the live tiers.
   - If a drawn top later in the window is higher by `throwCrestMargin`, the pop moves there. The crouch then starts
     from it, so the clip's pop still lands on the launch.
   - If a drawn top earlier in the window is higher, or the tile is still climbing at the window's end, the drawn
     crest has passed. The throw is withheld.
   - The throw's speed and the landscape crest's choice (ADR-2001) are unchanged. A body's own landing still never
     arms a throw: the check only moves or withholds one.

## Consequences

On art's build (1,505 throws), a drawn top 0.1 m higher within 0.7 s of the pop: 22 throws, against 76 with ADR-2001
alone and 347 before. The rest come from landings made after the crouch began. Measured: 52 of the 76 were visible in
the springs known when the crouch started, so the remaining ~24 are not knowable without seeing the future.

Limit: in the app, a frame longer than 0.1 s (a hitch) can let a body read a landing that a later body has not yet
made. This is the same class of order dependence that the planning surface's zero-lag springs (ADR-1964) already have.
It is not met offline or in a seek.
