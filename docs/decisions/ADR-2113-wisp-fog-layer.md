# ADR-2113: the wisp layer -- thin, tattered cloud-fog low over the sea, parted by walking figures

- Status: Accepted (2026-11-20), avagent/neon-dreams (Neon Dreams, iteration 9; owner msg 73: "fog layers ... wispy and
  cloud-like, not just a layer of grey ... above the 'ocean' that slightly covers the streets and wisps around aliens as
  they walk"; msg 78: wrap the floating islands' rims)
- Code: `scene::CloudWisps`, `CloudWispZone` (`src/scene/clouds.{hpp,cpp}`); `wispDensity`, `wispNext` in
  `shaders/clouds.wgsl`; `Composition::updateCloudWakes`.
- Tests: `tests/rendering/test_clouds_it9_gpu.cpp` (`[adr2113]`).

## Decision

The wisps are part of the cloud march, not a second medium: they are lit by the key's shadow taps, the ambient, the
glows (the city's neon) and the beams exactly as the cloud is, and composite with it.

- Region: an optional height band `[base, top]` (none unless top > base) plus up to 24 `zones`: `{name, centre [x, z],
  radii [a, b], yaw, floor, below, above, density, bob}`, a soft plan ellipse (fading over its outer 30 %) in which the
  layer peaks a metre under `floor` and thins to nothing `above` metres over it and `below` metres under it (fog lying
  on a deck and pooling in the gaps beneath). A zone may ride a formation (ADR-2112).
- Density: the shape volume's Worley and Perlin channels at `scale` metres, squashed vertically by `stretch` into
  sheets, domain-warped by a coarser tap, cut by `coverage` and torn at the edges by a finer octave (`tatter`), drifting
  on its own wind (`drift`, a pure function of the second). `density` scales it against the block's extinction.
- Wakes: `wakes` names scene nodes (a trailing `*` matches a prefix). Each frame the composition writes up to 16 of them
  (nearest the camera) with their velocity (from the last frame; zero after a seek or a jump). Round each, within
  `wakeRadius` and up to `wakeHeight` above its feet, the wisps are thinned by `wakeStrength`, twisted about the vertical
  by `wakeSwirl` radians and dragged back along the velocity; the parting extends back along the last `wakeTrail`
  seconds of the walker's path, widening and refilling toward the tail.
- The march's span includes the band and the zones, and an empty-air jump never crosses into them.
- Parameters: `clouds/wisps/density`, `coverage`, `tatter`. Off unless the block has an enabled `wisps` object.

## Measured (Neon Dreams, 1080p offline, art iteration 9 data, A/B with the layer off)

| Still | GPU frame off -> on | cloud pass off -> on | mean abs luma change (top half) |
|---|---|---|---|
| 27 s | 243 -> 257 ms | 108 -> 125 ms | 1.9 (0.2) |
| 35.75 s | 183 -> 205 ms | 44 -> 63 ms | 2.7 (0.02) |
| 44.5 s | 235 -> 253 ms | 110 -> 127 ms | 6.5 (0.14) |

(density 2.2 there; the committed setting is 1.8.) The near-city cloud above the decks is unchanged.

## Consequences

- Cost is three to four extra volume taps per sample inside the band or a zone (two in a light tap with no erosion).
- The wake velocity depends on the previous frame, so the first frame after a seek has no drag (the parting and twist
  are a function of position only).
