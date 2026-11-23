# ADR-2124: a Shockwave that travels through the world

- Status: Accepted (2026-11-22), avagent/neon-dreams (Neon Dreams, iteration 14; owner msg 195 at 38.3 s: "you've got
  the right idea, the distortion looks good, but it needs to be way more visible and impactful -- the wave should
  pulsate through the entire scene / geometry / characters"; msg 204: one front per quarter note)
- Code: `world/effects/world_pulse.{hpp,cpp}` (`PulseFront`, `PulseFrame`, `buildPulseFrame`); the Shockwave's
  `travel` row and its "Through the world" rows (`kinds/shockwave_effect.cpp`: `push`, `lift`, `flashColor`, `flash`,
  `flare`, `clouds`, `refRadius`; `shockwaveShellProxies`, `shockwavePulseFronts`); `DistortionShape::Shell` /
  `DistortionField::Front` (`distortion_frame.hpp`, `shaders/distortion.wgsl` `fs_offset_ext`); `Scene::pulses`,
  `Engine` (built after the DF frame); `FrameUniforms::pulseInfo` / `pulses` (`scene_renderer.hpp/.cpp`,
  `shaders/common.wgsl`: `pulseOffset`, `pulseFlash`, `pulseCloud`); consumers in `pbr.wgsl` (`vs_entity`),
  `pbr_skinned.wgsl` (`vs_skinned`), `procedural.wgsl` (`procVertex`), `pbr_shade.wgsl` (`shadeSurface`) and
  `clouds.wgsl` (the march).
- Tests: `tests/unit/test_world_pulse.cpp` (`[adr2124]`): fronts travel at maxRadius / duration, several in the air,
  weaker with size; a pure function of the second; the world mode draws shells and the ring mode no pulse and still
  at most four rings; disabled or before the first release, an empty block.

## Context

The Shockwave (Effect Library Wave 2) is a screen-space refraction: a ring at the view ray's closest approach to a
centre. Reached across a town it is a thin circle on screen that nothing in the world answers -- the owner saw "the
right idea" at the speakers and nothing down the Quay 130 m away. The ask is a pressure wave through everything:
geometry and characters pushed as it passes, light on the surfaces it crosses, the clouds rippling, the air bending
where it is.

## Decision

A Shockwave gets a `travel` choice: `ring` (the original, the default) or `world`. In the world mode each live front
(the same TRIGGER fronts, a pure function of `age = t - t0`, now up to eight a instance) is, besides its DF proxy, a
`PulseFront` in the frame block: centre, radius, the radius a frame (1/30 s) earlier, the band's half-width
(`thickness`), the push (m), the surface band's radiance, the lift share, the cloud ripple and the emissive flare --
strength folded as `(refRadius / max(r, refRadius)) ^ radiusDecay x life x envelope`.

Per point, `u = (|p - c| - R) / h`:
- the vertex stages add `pulseOffset`: `(exp(-u^2) - 0.45 exp(-(u + 1.8)^2))` x push along the horizontal outward
  direction blended toward up by `lift` -- a shove and a smaller recoil behind it, continuous in space so a mesh never
  cracks; the previous position uses the previous radius and push, so the velocity target sees it;
- the surface adds `pulseFlash`: a sharp attack as the front arrives (`exp(-4u^2)`, u > 0) and a decay 2.2
  half-widths long behind it (`exp(-u^2/4.84)`), plus `flare` x the surface's own emission (windows, strips flare).
  The decay matters as much as the look: a band a point crosses in one frame is SAMPLED by the film -- caught bright
  on one front and half-missed on the next as the front's phase against the 30 fps frames drifts -- and the uneven
  fronts read as a separate pulse about once a second (Neon Dreams msg 218; the per-beat swing's spread across beats
  was 26 %). Keep a point lit for several frames: half-width >= speed x 2.5 frames;
- the cloud march brightens (`pulseCloud`, the band's radiance per unit density) and thickens (density x (1 + g))
  over a band 1.6 x wider;
- DF: `Shell` x `Front` -- where the scene surface lies in the band (the front meeting the town), and where the view
  ray crosses the shell in front of the surface (weighted to grazing, so the silhouette reads), the air is pushed
  across the band and stirred by curl eddies; an optional glow (`rimEmission`) along it.

Every consumer is a uniform branch on `pulseInfo.x`: a frame without a world front runs the old arithmetic.

## Consequences

- Any scene can send a pulse through itself from any point, on any TRIGGER source; the amounts are ordinary effect
  parameters (`fx/<id>/push`, ...) and so routable and keyable (Neon Dreams keys them to 0 through its held breath).
- The push is per vertex: a large flat wall with four vertices tilts rather than ripples; dense meshes (characters,
  lanterns, signs) wobble. That is the intended read ("subtle per object but unmistakable as a wave").
- Cost: a loop over at most 12 fronts per vertex and per lit fragment and cloud sample, only while fronts live.
