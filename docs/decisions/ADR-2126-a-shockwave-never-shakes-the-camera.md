# ADR-2126: a Shockwave never shakes the camera (the lens calm)

- Status: Accepted (2026-11-23), avagent/neon-dreams (Neon Dreams, iteration 17; owner msg 253: "shockwaves are still
  too violent in the way they affect the camera -- the effect is good, but it's too jarring when we get up close";
  "the shockwaves still feel like they're jerking the camera way too much")
- Extends: ADR-2124 (a Shockwave through the world). Every new row is 0 by default, and 0 is the ADR-2124 picture.
- Code: `kinds/shockwave_effect.cpp` (rows `maxShift`, `lensNear`, `lensClear`; the shell's `motion` lanes, the ring's
  `motion.xy` and `rim.w`, `PulseFront::flash.w` and `shape.w`); `shaders/distortion.wgsl` (`dfCapShift`; the
  Shock field and the Front field's calm; from inside a shell only a grazing crossing bends); `shaders/common.wgsl`
  (`pulseOffset`: no push near the lens, the push capped on screen).
- Tests: `tests/unit/test_world_pulse.cpp` `[adr2126]` (the lanes, and all zero when unset).

## Context

Nothing in Neon Dreams moves the camera with the wave -- the lens is five baked timeline tracks and no route targets
it -- yet the owner saw the camera jerk on every beat at the band. An A/B (wave on against wave off, same baked lens,
44-51 s, per-tile phase correlation over 120 px tiles; work/engine/it17/jolt) measured why: on each kick the picture
shifted by a mean of up to 22.9 px, 83 % of the frame moved more than 2 px, and the whole-frame mean vector reached
6.9 px -- a global warp, which reads as the lens moving. An attribution run (one component on at a time) found three
causes, all near the lens: the shove of the stage and the crowd (a 0.75 m push at 10-15 m is tens of pixels, and it is
coherent over everything near), the haze bend (a world displacement projected from a point 10 m away is huge on
screen; from inside the shell every ray leaves through it, so the 0.15 floor of the air term bent the whole picture),
and the speakers' 14 m rings, whose sphere held the lens.

## Decision

Three rows on the Shockwave, each 0 = off:

- `maxShift` (px at 1080p, scaled with the frame's height): no pixel is moved further by one front -- the DF offset
  is capped in pixels (`dfCapShift`, both the ring's Shock field and the shell's Front field), and the push of a
  vertex is capped at the world length that many pixels subtend at its distance from the lens
  (`dist x px x 2 tan(fovY / 2) / 1080`).
- `lensNear` (m): nothing within it of the lens bends or glows (full from twice it) and, through the world, nothing
  within it is pushed.
- `lensClear` (m): the bend and edge glow fade as the front's radius comes within it of the lens's own distance from
  the centre -- a front passing the lens is the whole frame at once. With it set, from inside a shell only a grazing
  crossing bends (the 0.15 floor is dropped there).

Neon Dreams: `maxShift` 1.5, `lensNear` 8, `lensClear` 20 on the wave; the speakers' rings 1.5 / 4 / 6.

## Consequences

- Measured (the same A/B, 210 frames): mean tile shift 3.47 -> 0.61 px, worst frame 22.9 -> 2.1 px; share of the
  frame moved over 2 px, worst 83 % -> 27 %, mean 24 % -> 6 %; whole-frame mean vector, worst 6.9 -> 1.4 px; the
  frame-to-frame change of the shift field, worst 23.3 -> 2.5 px.
- Up close the wave is light (the flash, the edge glow, the cloud ripple) and a small local bend; down the Quay, far
  from the lens, a 1.5 px cap still lets the contour bend read where it meets the town.
- The cap is per front; overlapping fronts add. Eight in the air at one pixel is the worst case.
