# ADR-914: The water's advection is bounded

**Status:** Accepted
**Date:** 2026-09-26
**Found by:** the GV3 revision's water audit (the owner's "tearing lines" reference turned out to be
this defect, appearing by accident late in a film)
**Follows:** ADR-099 (the water surface and its three travelling ripple layers)
**Implemented by:** `flowPhases`, `flowJump`, `flowDesync` and the ripple, sparkle, foam and glow
code in `shaders/water.wgsl`; `waterHash` is now an integer hash
**Tests:** `tests/rendering/test_water_advection_gpu.cpp`:
- "the water surface at 200 s is as fine as it was at 10 s" (control: the same shader with the bound
  taken out, derived from the live file)
- "bounded water still travels and is a pure function of the second"

## Context

Everything on the water that travels -- the three ripple layers, the sparkle, the foam's break-up and
the glow -- sampled its noise at `p - v * t`: an offset that grows without limit.

The flow `v` is baked per vertex of the water mesh, and it is not continuous:
- **the direction** is the tangent of the nearest segment of the Chaikin-smoothed centreline, so it
  jumps along a ray from every node (GV3: 104 nodes, turns up to 5.2 degrees);
- **the body** is one per point, so the river's 0.41 m/s along the channel becomes the elder pool's
  0.066 m/s along (0.7, 0.7) in one grid step;
- **a still body's speed** steps to zero at its rim;
- and inside one river, **the bank shear** is a smooth `dv` across the channel.

Across a triangle whose vertices disagree, the offset differs by `|dv| * t` over the grid step
(1.2 m in GV3), so the noise there is compressed by `|dv| * t / 1.2 m` and turned parallel to the
triangle. The quad split is fixed, so the compressed bands step along the grid axes and the
anti-diagonal. At 34 s nothing shows. By 171 s the pool boundary compresses layer 1 up to 78-fold
(median 28) and every node ray 3- to 8-fold, and the smooth bank shear has doubled the median
compression of the whole river -- the "late-film streaking". The fade that keeps sub-pixel ripples
from aliasing uses the nominal frequency, so it never sees the compression: inside the pool band at
171 s, layer 1's 19 cm wavelength is about 7 mm and nothing fades it. (Magnitudes from a CPU
re-implementation of the bake, `shear_bands.py` and `shear_classes.py` in the audit.)

These are the thin, stepped seams of compressed parallel ripples in the owner's reference, and in
GV3's own early renders at 171 s. They grow with film time, sit wherever the implementation put a
node or a body boundary, alias late in the film, and nothing can place, bound or tune them.

## Decision

- **Every travelling field keeps two samples, half a period apart.** Each sample travels for one
  period (`kFlowPeriod` = 8 s at the water's own speed) and is then re-seeded, at the instant its
  weight is zero, at a new place in the noise (`flowJump`, a hash of the cycle's index). The weights
  are triangles, and they are normalised by the root of their squares rather than by their sum: two
  uncorrelated fields averaged with weights summing to one have less contrast than either, and a
  linear blend flattens the surface twice a period.
- **Each field's period is `kFlowPeriod` over its speed multiplier** (layer 2's 1.6, layer 3's 2.4,
  the sparkle's 2.1, the foam's 0.9, the glow's 0.45), so every field travels at most the same
  distance per cycle: the water's speed times 8 s. What a flow discontinuity can do to any field is
  then bounded by `|dv| * 8 s`, whatever the second.
- **The cycle is desynchronised across the world** (`flowDesync`, up to two periods over about 80 m),
  so the crossfade happens at different moments in different places and never across a whole frame
  at once. Its own gradient is a constant shear of at most 0.15 on a river as fast as Glowmere's.
- **Each field keeps its own speed and units.** The ripple layers and the glow travel in metres; the
  sparkle and the foam scroll through their noise in cells, as they always did.
- **The noise hash is an integer hash** (Wellons' lowbias32 over the integer cell). The
  `fract(sin(dot(cell, k)) * 43758)` it replaces is only as good as `sin` on a large argument: the
  sparkle runs at 135 cells a metre on Glowmere's river, so 350 m from the origin it took the sine of
  numbers near ten million, where one float step is a whole radian. Integer arithmetic is exact at any
  cell and identical on every device.
- **No legacy switch** (ADR-442). The accident's look comes back where somebody asks for it, as
  deliberate tears (ADR-916).

Rejected:
- **Making the baked flow continuous at the source** (interpolated tangents, bodies blended by
  weight). It removes the node rays and the pool switch, but not the bank shear, which is continuous
  and still grows with `t`; and its blast radius is larger, because floaters and entities read the
  same flow.
- **A shorter period.** It tightens the bound further but morphs the pattern faster; at 8 s the node
  rays are bounded at 1.28 and the smooth shear at 1.11, which no one can see.

## Consequences

- **Every frame of every scene with water changes, by design.** The ripples, the sparkle, the foam's
  break-up and the glow are new realisations of the same fields: the same sizes, speeds and
  directions, a different pattern. Nothing that was authored needs to change. Scenes: Glowmere Valley
  2 and 3, `glowmere-stylized`, `tree-of-life-ocean-world`, the QA water scene, and anything else with
  a terrain's water.
- **The accidental seams are gone.** GV3's s26 at 171.25 s, 960x540, before and after, is in the
  revision's water folder (`s26-171.25-540p` in `before/` and `A/`): the stepped lines of compressed
  ripple across the foreground are the owner's reference, and after this change the same frame has
  none. ADR-916 puts them back where they are wanted.
- **The surface no longer changes character through a film.** On the QA river, seen from above with
  the glint and the sparkle off, the water's fine structure relative to its contrast is 0.0232 at
  10-14 s and 0.0240 at 200-204 s (x1.03). The same shader with the bound taken out goes from 0.0522
  to 0.2006 (x3.8): already twice as fine as the bounded surface at 10 s, because on a 0.91 m/s river
  ten seconds of unbounded travel is already a 2.8-fold compression at the banks.
- **The cost did not measure.** `--ab water` on GV3 at 171.25 s, 1920x1080: the whole water pass is
  +0.52 ms of 28.2 ms before and +0.20 ms of 28.1 ms after, both inside a 2.6-6.2% noise floor. The
  extra work is eight noise evaluations a pixel where every field is on (ripples 3, sparkle 1, foam
  1, glow 2, the desync field 1).
- **A test re-baselined, and made to measure what it says.** "water thickness does not depend on where
  in the frame the water landed" (`tests/rendering/test_water_depth_forensics_gpu.cpp`) checked that
  the worst bed-through-water ratio over its patch was under 2.0, with the ripples on, at t = 2 s. On
  main that held at that one second only: 3.07 at 1 s, 1.90 at 2 s, 2.39 at 3 s, 2.56 at 5 s, 4.19 at
  8 s, 5.65 at 13 s; and on flat, still water it is 3.11 at any second, from one point on a steep
  stretch of bed that lands on differently placed pixels in the two views. It measured pixel
  quantisation and the ripple pattern of the day. It now holds the water flat and foam-free (its own
  premise: "the same normal"; the layers' fade is level of detail, which does depend on the pixel) and
  checks the mean signed log of the ratio across the patch, which is what a space mismatch moves:
  +0.010 now, and +0.125 with the shader's `/ cosAxis` removed -- the defect the test exists for --
  which is its new control arm. Threshold 0.06.
- **Unchanged:** every other water test (`[water]`, `[water6_2]`, `[waterfx]`), including the
  shoreline, determinism and seek tests: the fields are still pure functions of the timeline second.
