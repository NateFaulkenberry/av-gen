# ADR-1970: Contour edges on the tile sides a level drops across

- Status: Accepted (Musical Geography Phase 3, 2026-11-09). This is the art agent's request 1970, built by engineering.
- Code:
  - `cs_tile_contours`, in `shaders/generator.wgsl`;
  - `TileContour` (`tileContour`), in `src/scene/material_program.{hpp,cpp}`, `src/scene/material_compiler.cpp` and
    `shaders/material.wgsl`;
  - `TileDynamics::contourInterval` / `contourOffset`, and the parameters `env/tiles/contour/*`.

## Decision

A second pass over each ring's window runs when `contour.interval > 0`. It reads the four neighbours' tops (the first
pass writes each top into `scale.w`) and flags each side across which `floor((top − offset) / interval)` drops. The
flags are bits for +x, −x, +z and −z.

The mask joins the crest in the emissive's spare lane as `crest + 2·mask`. The `tileContour` op decodes it and draws
tileBorder's band, never thinner than N pixels and prefiltered under 4 pixels, on the flagged sides only. The band
sits on the top face's border, not a chamfer, because the tiles have sharp edges. The op also returns the crest.

The contours follow the moving ground. A neighbour outside the window or in the ring's hole is not an edge.

## Consequences

- The GPU test checks the flags against the CPU mirror's tops, with 0 wrong. The CPU test checks the op.
- The pass reads records only, with no extra terrain samples. Its cost is one more dispatch per ring.
