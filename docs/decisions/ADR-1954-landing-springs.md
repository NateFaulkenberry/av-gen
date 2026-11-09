# ADR-1954: Landing springs, so the floating tiles feel a landing

- Status: Accepted (Musical Geography Phase 6, 2026-11-09), scene/musical-geography.
- Code: `TileSpringSpec`, `TileDynamics::spring`, `TileDynamics::springs`, `springsAt`, `springSeconds`
  (`src/scene/tile_dynamics.hpp`); `tileSpring` (`src/scene/tile_terrain.cpp`) and its twin `tileSpring` in
  `shaders/generator.wgsl`. Scene JSON `environment.tileDynamics.spring {depth, frequency, damping, rippleSpeed,
  rippleRadius}`.

## Context

§12 of the technical plan asks that a character's landing moves the tile it lands on, as if the tiles float, and that
the movement spreads a little to the tiles around. The ground must stay a function of the clock (ADR-1953), so that a
seek equals a play.

## Decision

A landing is a `TileEvent` (time, position, strength) on a list next to the live rings. Its effect is the closed-form
impulse response of an under-damped spring:

    h(r, t) = -depth * strength * e^(-r / rippleRadius) * e^(-zeta w a) * sin(w_d a),   a = t - t0 - r / rippleSpeed

It starts at rest, so the tile does not jump at the moment of landing. Its first motion is down. It rebounds, then
settles. The neighbours follow later and move less. A spring is dropped after `springSeconds()`, when its envelope is
below 1e-3 of the depth. At most the 8 newest springs that are still ringing reach the GPU. They are packed in
`GeneratorTileUniforms` (now 1104 B) only when the moving ground is active. Units are metres and seconds, so the
result does not depend on the tile size. The spring adds to the fine sample's height, and twice its size goes to the
step (the side ease), the same way as the other motion terms.

The springs are not saved. The composition fills them each frame from the tiles ground's landings (ADR-1955).

## Consequences

- CPU tests: starts at rest, first extremum downward, settled by `springSeconds`, envelope non-increasing, neighbour
  smaller than source, newest-8 selection. GPU test: records equal the CPU mirror, with springs alone and on a wave.
- Default `depth` 0.6 m. The first dip is about 0.45 m at strength 1 (zeta 0.18, 1.6 Hz).
