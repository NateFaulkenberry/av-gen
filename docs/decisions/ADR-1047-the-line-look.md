# ADR-1047: The line look: edge width in pixels, edge shape, and edge colour per surface

- Status: Accepted (2026-10-01), proto/liminal-space (All You Got art pass 2)
- `scene::SdfLook` (`edgePixels`, `edgeThreshold`, `edgeSoftness`) and `SdfObject::Surface::edge` in
  `src/scene/sdf_object.*`; the uniform `look4` and the surface record's `p2` in `src/rendering/sdf_renderer.*`;
  the mask in `shaders/sdf_raymarch.wgsl`.
- Tests: `tests/rendering/test_sdf_gpu.cpp` and `tests/unit/test_sdf_line_look.cpp` (`[adr1047]`).

## Context

Art pass 2 returns to "bright, glowing line-drawn geometry against darker environments", and the art agent's
decision is that everything is SDF with the edge light carrying the look. The edge light (ADR-1002) compared
the normal at the hit with the normal over a tetrahedron of a fixed width in tree-local units. Pass 1 found it
"CAD". Two concrete faults:

- A fixed world width is a line that is thick near the camera and sub-pixel (moire) far away. In the small
  rooms the brief now demands, near lines become bands.
- The edge colour was per object, so a second line colour cost a second SDF object (a second march).

## Decision

- `look/edge/pixels` (> 0): the tap width follows the hit distance, `pixels x (angle of one pixel) x t`, with
  the pixel's angle measured from the camera ray of the neighbouring pixel. Lines keep a constant on-screen
  width at any distance and any output size. 0 keeps the old world width (`look/edge/width`).
- `look/edge/threshold` and `look/edge/softness` shape the mask, `smoothstep(threshold, threshold + softness,
  1 - cos angle)`: a small softness draws a hard line, a large one a glow across the crease. The defaults
  (0.02, 0.28) are the old constants.
- `surface/<k>/edge`: an RGB multiplier of the object's edge colour on each surface (default 1, so nothing
  changes; 0 removes the lines from that surface). It rides in the surface record's spare `p2`, so it costs
  nothing more. It applies to compiled objects, as the surfaces do (ADR-1044).

## Consequences

- Measured: at 1.5 pixels, doubling the resolution doubles the line pixels (length only); at a world width it
  quadruples them.
- The uniform grew from 224 to 240 bytes (in its 256-byte slot); the layout guard checks both sides.
- Files write the new keys only when they differ from the defaults, so existing scenes round-trip unchanged.
- Mesh geometry (the text of ADR-1046, the figure) does not get the edge light.
