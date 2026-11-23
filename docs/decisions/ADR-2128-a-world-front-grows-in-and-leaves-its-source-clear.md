# ADR-2128: a world front grows in, and leaves its source clear

- Status: Accepted (2026-11-23), avagent/neon-dreams (Neon Dreams, iteration 19; owner msg 287 at 49.5 s: "visual bug
  from the shock wave" over the stage, and "make the screen distortion come on and off smoother; right now it feels
  like a bit of an on/off switch")
- Code: `world/effects/kinds/shockwave_effect.cpp` (rows `sourceClear`, `rampIn`; `worldLife`, the ring's `life`,
  `shockwaveSourceClear`), `world/effects/world_pulse.{hpp,cpp}` (`PulseFrame::sourceClear`),
  `rendering/scene_renderer.cpp` (`pulseInfo.y`), `shaders/common.wgsl` (`pulseSourceClear` in `pulseOffset`,
  `pulseFlash`, `pulseCloud`), `shaders/distortion.wgsl` (shape 6: `motion.w`)
- Tests: `tests/unit/test_world_pulse.cpp` (`[adr2128]`)

## Context

ADR-2124's fronts are released at a centre (Neon Dreams: the stage, on every quarter note) and were born at full
strength within 2 % of their life (two frames). The stage and everything round it therefore always sat inside a young
front's band at full strength: the floor's tiles and reflections smeared by the bend and its eddies, the players
ghosted, the cymbals shoved off their stands -- and the whole area switched on with each beat. Measured at 49.0-50.2 s
(wave on vs off, the stage region): a mean difference of ~14 levels on every frame.

## Decision

Two Shockwave rows, both off at 0 (every existing scene unchanged):

- `sourceClear` (m, world travel): every consumer weighs a point by `smoothstep(clear / 2, clear, r)`, r its distance
  from the front's centre -- the push, the surface flash and flare, the cloud ripple and density, and the DF's surface
  band; the DF's air crossing (the shell's silhouette) by the same of the front's radius, so a shell grows in as it
  leaves the source. The value travels per DF proxy in `motion.w` (free for shells) and, for the frame block, as ONE
  value in `pulseInfo.y` -- the largest of the live instances' (no free lane in `PulseFront`; scenes send their
  fronts from one source; a second source with a different clearance gets the larger).
- `rampIn` (s): a new front (world or ring) reaches full strength over this, eased (smoothstep), instead of 2 % / 3 %
  of its life.

## Consequences

- The source stays crisp; a front's effect at any point rises over the band's own passage instead of a birth pop.
  Neon Dreams (sourceClear 30, rampIn 0.6): the stage region's on-vs-off difference fell from ~14 to ~1 level.
- One clearance per frame for the pulses: a scene with two world Shockwaves from different centres and different
  clearances gets the larger for both. Promote to a per-front lane if that is ever needed.
