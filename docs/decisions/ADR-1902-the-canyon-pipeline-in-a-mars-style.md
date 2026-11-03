# ADR-1902: The canyon's terrain pipeline in a Mars style

- Status: Proposed (Art Pass 2, M3d), scene/strange-attractor.
- Code: `AttractorGen::mars`, `mesaCover`, `yardangs`, `wind`, `dunes` and the `mars` JSON block
  (`src/scene/attractor_world.*`).
- Tests: `[attractor-world][pass2][adr1902]`.

## Context

The owner, on M3c: the canyon looks amazing, and Mars looks crude by comparison. Rebuild Mars with the canyon's own
procedural pipeline rather than hand-placed primitives (the lunar generator's mesa stamps), retuned for Mars:
wind-eroded mesas and buttes in layered sediment, yardangs, dune fields. Reuse, don't duplicate. The ocean floor got
the same note: replace its box-like mesas with the canyon's approach.

## Decision

The canyon generator gains a style. With `"mars": {...}` in `attractorWorld`:
- **The plateau becomes mesas and buttes.** The same domain-warped erosion front, biased by `mesaCover` so that share
  of the land stands up. The valley, the camera's promontory, the composed masses and the far ramp are not applied.
  The metric cliffs, the talus aprons, the strata, the terraces and the erosion gullies are applied as before.
- **No gorge, no river, no side canyons.**
- **The floor's wind forms.**
  - Yardangs: a ridged noise stretched 12:1 along `wind`, `yardangs` metres deep, in patches.
  - Transverse dunes: an asymmetric wave across the wind (a long windward slope, a short slip face), `dunes` metres
    high, crests wandering, in fields where the yardangs are not.
- The style is hashed; absent, the hash and the canyon are unchanged.

Mars and the deep ocean are now `attractorWorld`s with the canyon's rock program (Mars rust-tinted with a photoscanned
sand near the camera; the reef cooled to blue-greens). Their layouts are carried onto the generated ground: a search of
the height field found a wall with open floor in front of it, and pads level the pyramids', the rover's and the base's
footings (ADR-1897).

## Consequences

- One terrain system serves the canyon, Mars and the seafloor.
- The lunar generator still serves the Moon.
