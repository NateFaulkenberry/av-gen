# ADR-1665: A light ball that looks like a fireball

- Status: Accepted (scene/aurora-borealis)
- Extends ADR-1663 (the light ball). Uses the ball's free `WaveGpu::extra` lane, so no struct grows.
- Asked for by the Aurora Borealis owner's pass-5 notes: "make the fireball look more like a realistic fireball - deep
  research on how to do this". The research is `docs/prototypes/aurora-borealis/41-research-pass5-fireball.md`.
- Code:
  - `src/world/wave_effect.{hpp,cpp}`: `BallShape::look` (0, 1 or 2), `smoke` and `embers` (0..1), with validation.
    Packing into `extra` = (look, smoke x envelope, embers, a seed from the launch second) happens only when `look` is
    not 0. `ResolvedWave::launchSecond` is new.
  - `src/world/effects/kinds/wave_rows.hpp`: the three rows (Ball > Look, Smoke, Embers). Each is written only when
    set.
  - `shaders/common.wgsl`: `fireHash3`, `fireNoise3`, `fireFbm3`, `fireRamp` and `waveBallFire`. `waveBallGlow`
    hands looks 1 and 2 to `waveBallFire`. `wavesBallSky` now returns the transmittance in `.a`.
  - `shaders/atmosphere_fx.wgsl`: the sky's aurora and comets are multiplied by that transmittance, so the soot dims
    them.
- Tests:
  - `tests/unit/test_wave_path.cpp` (`[adr1665]`): look 0 packs byte for byte as ADR-1663's ball, whatever its smoke
    and embers are set to, and writes no new rows. Look 1 packs its lane and round-trips. Out-of-range values are
    refused.
  - `tests/rendering/test_wave_path_gpu.cpp` (`[adr1665]`): both looks draw a white-hot heart in a warm body. With no
    ball, the frame hash is unchanged.

## Context

ADR-1663's ball is a Gaussian core with a halo, a flickering corona and a glowing trail: a lamp, not a fire. A
realistic fireball needs three things.

- **Colour from temperature.** Flame light is incandescent soot, so its colour follows a blackbody ramp:
  - white-yellow at the heart;
  - orange;
  - deep red;
  - then cool soot that only absorbs, which gives the dark edge.
- **Turbulent, boiling shape.** This comes from domain-warped fbm, advected back along the flight.
- **A trail of smoke and embers.**

## Decision

Two looks, both procedural, chosen per effect.

- **Look 1, a flame billboard.**
  - It is drawn in the plane through the ball, across the ray, with the noise in the ball's own 3D frame.
  - Its temperature falls off with radius and is pushed out into tongues by fbm(p + fbm(p)). That temperature drives
    `fireRamp`.
  - A sooty fringe on the cool, trailing side absorbs light.
- **Look 2, volumetric-lite.**
  - The ray takes ten emission-absorption steps through a sphere holding a teardrop of the same noise.
  - Cool samples absorb more, which gives the dark edge.

Both looks share:

- the white-hot heart;
- a trail drawn as a string of six soft puffs, flame near the ball cooling to soot further back;
- ten embers shed from the trail.

Everything is a function of the launch second, the transport second and the authored values, so seek equals play.

Look 0 takes the ADR-1663 code path untouched. `extra` stays 0, and the sky's transmittance is exactly 1.0, so a
look-0 frame is ADR-1663's byte for byte.

Aurora Borealis uses look 1. Of the three variants compared, it read most as fire: ragged tongues and a sooty fringe
(`~/Desktop/av-gen-review/aurora-borealis/pass5/`).

## Rejected

- **Flipbooks.** This is what shipped games mostly use, rendered in EmberGen or Houdini. It needs baked sequences,
  which means textures this procedural, asset-free scene does not have, and a visible loop.
- **Heat shimmer.** This is a screen-space refraction of the frame behind the fire. It needs a distortion pass after
  the opaque pass, which is beyond a narrow change, and the fireballs fly against a dark sky where it would barely
  read.
- **Keying the noise to the ray's offset in the screen plane.** The first take did this. Where the flight heads
  straight away from the camera, the scrolling direction spins with the view, and the noise fanned out into radial
  rays. The trail keyed to the ray's offset from its axis did the same end-on. Both are now keyed in world or ball
  space.
- **Darkening lit surfaces with the soot.** Only the sky is dimmed. The balls fly above the land, and threading a
  transmittance through every surface shader is not narrow.
