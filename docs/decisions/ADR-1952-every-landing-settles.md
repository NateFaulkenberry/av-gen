# ADR-1952: Every landing settles; the springs no longer cut off

- Status: Accepted (Musical Geography CP-D r2, 2026-11-10), scene/musical-geography. Amends ADR-1954.
- Code:
  - `kMaxTileSprings` 8 → 256, `kTileSpringReachStart` / `kTileSpringReach`, `TileDynamics::springsAt`
    (`src/scene/tile_dynamics.hpp`, `src/scene/tile_terrain.cpp`);
  - `tileGatherSprings` and `tileSpring` (`shaders/generator.wgsl`);
  - `GeneratorTileUniforms::springs`.

## Context

The owner, on CP-D draft3: *"the block animation when the aliens land ends too abruptly."*

A canary traced one landed tile's height, comparing the springs the GPU drew with every landing still ringing. Draft3's
cast hops on every beat, and up to 104 landings were ringing at once. With a cap of 8, a landing left the drawn set
about 0.6 s after it happened, once 8 newer landings had happened anywhere, and the tile snapped flat in mid-swing.
The jump was up to 0.70 m in one frame (`11-pass-cpd-r2/cpd-r2-landing-displacement-before-after.png`).

## Decision

- **Capacity.** Up to 256 landings are drawn. Below that, `springsAt` keeps every landing still ringing, so a spring
  leaves only when its envelope is under 1e-3 (`springSeconds`). Above 256 it keeps the newest.
- **Reach.** A spring reaches 6 ripple radii. Between 5 and 6 radii it eases to 0, so a tile far from a landing can
  skip it with no step at the boundary.
- **GPU cost.** Before the per-sample loop, each 8×8 workgroup of the tiles kernel gathers the springs that can reach
  its cells, widened by the seam band's coarser block. The list is built in index order, so it is deterministic. A
  sample then loops over only the few nearby landings, not every one still ringing.

The CPU mirror computes the same terms, with the same reach and the same newest-first rule.

## Consequences

- The before/after canary: the drawn and every-ringing curves are identical (largest difference 0.0). The largest
  per-frame change is the landing's own first dip (0.31 m per frame at draft3's spring), not a cutoff.
- The tail is the spring's own. Draft3's spring (1.4 Hz, ζ 0.16) is bouncy and rings about 5 s. Gentler settings
  (0.7 Hz, ζ 0.35) are shown as the third arm of `11-pass-cpd-r2/cpd-r2-landing-before-after-soft-50-70s.mp4`; they are
  art's choice.
- Tests:
  - a landed tile's displacement while 80 other landings arrive steps no more than the spring alone, and is still
    settling 2 s later;
  - every ringing spring is kept under the cap; the reach ends smoothly;
  - GPU parity with 43 springs;
  - the mid-hop seek test still passes.
