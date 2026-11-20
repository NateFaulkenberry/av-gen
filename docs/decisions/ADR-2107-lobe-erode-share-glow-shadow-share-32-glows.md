# ADR-2107: A lobe's share of the erosion, a glow's share of the shadow, 32 glows; ADR-2106 far-octave fade

- Status: Accepted (2026-11-19), avagent/neon-dreams (Neon Dreams, iteration 7)
- Extends: ADR-2106 (erosion), ADR-2104 (glow shadow, 24 glows).
- Code: `src/scene/clouds.{hpp,cpp}` (`CloudLobe::erode`, `CloudGlow::shadow`, `kMaxCloudGlows`),
  `src/rendering/cloud_renderer.cpp` (lobe row `soft.y`, glow `w`), `shaders/clouds.wgsl`, `shaders/clouds_resolve.wgsl`.
- Tests: `[adr2107]` in `avgen_tests`; `[clouds]` in both suites.

## Context

ADR-2106 erodes every lobe inward. Some lobes must keep their bodies whatever the block's erosion is. The Neon Dreams
plug hides the Condensery until the crash; fully eroded, it opened half a second early. The "seat" lobes carry the
decks; fully eroded, they sank 15 to 30 m and left the stilts showing. Separately, a glow placed inside solid cloud is
almost entirely removed by the ADR-2104 shadow tap, because the cloud toward it is solid by construction. The Veil's
pulses on the journey and the Condensery's wells were in that position. Neon Dreams also needed more than 24 glows
(wells, plug and settlements). Finally, at 5 to 10 km the march's steps are longer than ADR-2106's coarse octave
(60 m), and the far towers showed diagonal aliasing stripes.

## Decision

1. `CloudLobe::erode` (JSON `"erode"`, 0..1, default 1) multiplies the block's `erosion` for that lobe. At 0 the lobe
   keeps its authored ADR-1289 shape.
2. `CloudGlow::shadow` (JSON `"shadow"`, 0..1, default 1) multiplies `glowShadow` when that glow is the strongest at a
   sample.
3. `kMaxCloudGlows` goes from 24 to 32 (the uniform's glow array is 64 vec4s in both cloud shaders).
4. ADR-2106's coarse octave fades out as the footprint (the larger of a pixel and half a step) passes 0.6 to 1.6 of
   its wavelength, as the finer octaves already did.

## Consequences

- Defaults change nothing in a scene: every lobe has erode 1 and every glow has shadow 1, and both are written only
  when they differ. The uniform grows by 8 glows (256 bytes).
- Tried and dropped: shading the scattered light inside dense cloud by the erosion's octaves (an "interior shade"),
  to give the inside of the Neon Dreams plug some structure. It changed that frame by at most 6/255. Glows placed
  beside the journey inside the plug lit the whole visible depth evenly, and the bloom made it brighter and flatter.
  Inside an opaque body only the density design gives structure (holes and tatters).
