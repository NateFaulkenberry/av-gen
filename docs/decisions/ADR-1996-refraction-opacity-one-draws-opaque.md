# ADR-1996: Refraction at opacity 1 draws opaque, and a program's opacity thins it per pixel

- Status: Accepted (Musical Geography CP-D r2, art's CPD-R2b, 2026-11-10), scene/musical-geography. Amends ADR-1603.
- Code: `Refraction::enabled` and `Refraction::authored` (`src/scene/scene_types.hpp`), the composition's authored-optics
  copy (`src/scene/composition.cpp`); `shadeOpacity` (`shaders/pbr_shade.wgsl`), read by `refractSurface`
  (`shaders/refraction.wgsl`).
- Tests: `[gpu][procedural][refraction][adr1996]` in `tests/rendering/test_refraction_gpu.cpp`: at opacity 1 no
  refractive object, no refractive draw, the plain scene's pass count and its exact pixels; with refraction opacity
  0.9, a program opacity of 0 shows the backdrop and 1 the surface's own colour.

## Context

Art made the tile clouds translucent through ADR-1603's refraction phase (strength 0.0005, opacity keyed through the
arc: day 0.55, night 1.0). Two costs:
- at night, opacity 1, the clouds still went through the phase (after the frame copy, on the general path): +1.6 ms
  for nothing, since at opacity 1 `refractSurface` returns the surface's own shaded colour exactly;
- the opacity is one constant per material, so the horizon's clouds thinned as much as the overhead ones. The owner
  only wanted the near overhead clouds less like a platform.

## Decision

1. `Refraction::enabled()` (it draws in the refraction phase) is now `transmission > 0 || (strength > 0 && opacity <
   1)`. A material at opacity 1 draws in the opaque pass with everything that implies, including the slab path when it
   is eligible. `authored()` keeps the old meaning (a strength or a transmission at any opacity), and the composition
   uses it where it copies an author's optics onto a GLB's materials, because a keyed opacity may go below 1 later.
2. In the phase, the material's refraction opacity is multiplied per pixel by its program's `opacity` output
   (`shadeOpacity`, 1 without a program). The constant stays the gate: at 1 the surface is opaque whatever the program
   says.
3. Not done: a plain translucent mode for tile layers (art's optional ask). Hypothesis, not measured pass by pass:
   most of the day cost is the clouds drawn on the general path after the copy, which a translucent mode would also
   pay; it is left until the phase's passes are split in a profile.

## Consequences

- An opacity keyed to 1 costs what an opaque material does; the change at the threshold is invisible (the phase's mix
  at opacity 1 is exactly the shaded colour).
- A refractive material whose program writes an opacity below 1 now draws more transparent than before. Only the
  clouds combine the two today.
- Cost: see `02-requests.md` (CP-D r2, engineering → art: CPD-R2b).
