# ADR-2008: A ball's impact springs the tile, glows in its own colour and sends sparks along the tile edges

- Status: Accepted (Musical Geography, CPD-R12, 2026-11-11), scene/musical-geography. Amends ADR-1954/1960 (a negative
  strength springs without the landing glow).
- Code:
  - `TileBallSpec` and `TileDynamics::balls` / `ballImpacts` (`src/scene/tile_dynamics.hpp`);
  - JSON `environment.tileDynamics.balls` (`src/scene/tile_terrain.cpp`);
  - `tileSpring` / `tileLanding` and the kernel's twin (`shaders/generator.wgsl`): `abs` for the spring, `max(0, .)`
    for the glow;
  - the frame's `ballGlow` / `ballSpark` / `ballFx` / `ballFx2` / `ballImpacts[16]` (`shaders/common.wgsl`,
    `FrameUniforms`);
  - a slab's flat `tileCentre` and `ballImpactLight` in `shaders/procedural.wgsl`;
  - the impacts' world list, gathered each frame in `Composition`.
- Tests: `[balls][adr2008]`:
  - a negative-strength landing springs a tile exactly as a positive one does and lights no landing glow;
  - the look's parameters round-trip through the scene format, are written only when changed, and an unknown key is
    refused.

## Decision

- **The spring.** A ball's impact is a landing with a negative strength. The springs use its magnitude, so the tile
  bounces like an alien's; the alien landing glow uses only positive strengths.
- **The light.** Each frame the newest 16 impacts still lit go to the frame uniforms in world x-z. A tiles slab's
  top lights its outline (`edgeBand` metres, at least a pixel) with:
  - **the glow:** `glowColor` x `glowGain`, falling to 1/e at `glowRadius` metres and over `glowLife` seconds;
  - **the sparks:** `sparkColor` x `sparkGain`, a ring running out at `sparkSpeed` m/s, `sparkWidth` metres wide,
    fading over `sparkLife` seconds and to nothing at `sparkReach` metres. About two thirds of the tiles it passes
    light (a hash per tile and impact), with varied strength, so it reads as sparks running along the edge lines,
    not as a solid ring.
- **Where it runs.** In the procedural fragment stage, after the material: only on tiles-slab tops, and only while
  an impact is lit. No new pass, no new draw.
- **Defaults** (in `environment.tileDynamics.balls`): `glowColor` [0.35, 0.8, 1.0], `glowGain` 6, `glowRadius` 4,
  `glowLife` 0.8; `sparkColor` [1.0, 0.75, 0.35], `sparkGain` 10, `sparkSpeed` 14, `sparkReach` 24, `sparkLife`
  0.9, `sparkWidth` 2.5; `edgeBand` 0.12.

## Consequences

Cost and media are in 02-requests (CPD-R12).
