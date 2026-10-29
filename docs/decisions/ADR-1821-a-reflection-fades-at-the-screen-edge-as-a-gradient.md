# ADR-1821: A glossy reflection fades at the screen's edge as a gradient, not a cut

- Status: Accepted (eng/render-borrow, 2026-10-29). A review fix to ADR-1820.
- Code: `shaders/glossy_ssr.wgsl` (the screen-edge trust).

## Problem

The coordinator's review of ADR-1820's S3 still: the reflecting floor read as a lighter trapezoid painted on it. Its
left, right and bottom edges were visible, the bottom-left and bottom-right corners most of all.

## Investigation: which term draws which edge

Each pixel was coloured by the term deciding its trust (hit, edge fade, miss), and luma profiles were taken across
each edge.

- **The left and right sides and the lower corners are the screen-edge fade.** A hit within 8% of the screen's edge
  was faded to the environment over that band. On the floor the band maps to diagonal cuts at the reflection's lower
  corners.
- **The bottom edge is no trust term.** It is the mirror image of the back wall's top edge against the night sky:
  - above the line, rays hit the wall; below it, they pass over the wall and leave the top of the screen, so the
    environment stands;
  - at x = 500 the floor steps from 84 levels to 32 in one row. That is the wall's reflection (+52 levels) ending where
    the wall ends. A near-mirror floor (roughness 0.05) reflects that edge sharp, as a mirror would.
- **The reflection/environment brightness at that handoff is not the cause.** The environment's sky (0.08 x the HDRI,
  prefiltered) is about 1.6x the sky the frame draws (0.05 x the same HDRI). Both are dark: the environment adds about
  1 level to the floor below the line.
  - Tried: a miss reflecting the sky at the brightness the frame draws it (the HDRI x skyIntensity), so the
    reflection and its fallback are one picture.
  - The floor below the line went from 32 to 27, which made the step larger, not smaller. Not kept.
  - Also tried: a miss reflecting the drawn sky read from the copy where its direction is on screen. In S3 those
    directions sit at the top edge and changed rows 607-732 only. Not kept.
- **Why the region reads as a lighter shape at all** is the reflected wall's strength. The engine weights the
  environment, and so the SSR, by F_roughness(n.v) x A + B, where Filament uses F0 x A + B (the open item in ADR-1820).
  - An uncommitted experiment with Filament's weight on both: the step at the line becomes 68 to 26 instead of 84 to
    32, the reflected wall a third dimmer.
  - Not changed here, as instructed: it moves every reflective surface in every scene.
  - Image: `a1-glossy-ssr/trapezoid-fix/open-item-grazing-weight-experiment.png`.

## Decision

The screen-edge trust becomes a gradient: the distance to the nearest edge over a quarter of the screen, eased at both
ends (smootherstep). It replaces the 8% band (smoothstep of each axis). The reflection now fades toward the sides
instead of being cut, and the lower corners are no longer diagonal edges.

The bottom edge stays, because it is the reflection's content. The grazing-angle weight is left as an open item for
the owner.

## Consequences

- S3: `a1-glossy-ssr/trapezoid-fix/floor-before-after.png`.
- Flicker on the same 4 s camera move: oscillation over 32 levels attributable to the reflection 65.0 per million
  pixel-frames (66.6 before); over 16 levels 188 (195). `a1-glossy-ssr/flicker-measure.txt` and the re-encoded
  `s3-camera-move-*.mp4`.
- Reference scenes (picture_check, offline 960x540) against the capture before ADR-1820: Aurora, Event Horizon,
  Rainbow World and Talking Heads SAME, Rift within 1 level.
- Open item for the owner: the grazing-angle environment weight (ADR-1820's limitation), with the experiment image.
