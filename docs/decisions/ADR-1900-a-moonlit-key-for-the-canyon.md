# ADR-1900: A moonlit key for the canyon

- Status: Proposed (Art Pass 2, M3b), scene/strange-attractor.
- Code: `AttractorLook::sunWarmth`, `attractorSunColor` (`src/scene/attractor_world.*`); the key light's colour in
  `Composition::updateAttractorWorld` (`src/scene/composition.cpp`).
- Tests: `[attractor-world][pass2][adr1900]`.

## Context

The owner asked for a real night pass: moonlight with cast shadows. The canyon's key light is the generator's own
directional sun, steered by the look (azimuth, elevation, intensity). Its colour was fixed, a warm (1, 0.82, 0.62):
at night it lit the sandstone a dull orange, and the night read as a dim dusk however the sky was graded.

## Decision

A look key, `sunWarmth` (0 to 1, default 1): the key light's colour runs from a cold moon (0.62, 0.72, 1.0) at 0 to
the warm sun at 1. It is a look parameter like the others (`attractorWorld/sunWarmth`), so a look sets it per
crossing and a route could move it. It is saved only when it is not 1, so existing scenes save as before.

## Consequences

- The night look sets 0 and raises the key: the moon casts the figures', the rocks' and the craft's shadows in a
  cold light, and the fire is the warm key at the gathering.
- The dawn look sets 1.
