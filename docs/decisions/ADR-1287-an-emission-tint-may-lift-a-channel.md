# ADR-1287: An emission tint may lift a channel

- Status: Accepted (2026-10-09), proto/bio-valley (spec 07 §4.5)
- Code: `src/scene/procedural.cpp` (`registerProceduralParameters`, `material/emissiveColor`).
- Tests: `tests/unit/test_procedural_authored_range.cpp` (`[adr1287]`).

## Context

The valley's colourways tinted one cyan emission texture with factors above 1. For example, gold is
[4.5, 0.8, 0.06]: it lifts red and cuts blue. The render log said `material/emissiveColor.x is 4.5 and runs at 1`.

The HDR path itself was verified rather than assumed. Every step below multiplies, and none of them clamps:
- the procedural renderer packs `emissiveColor` and `emissive` unchanged into the object uniforms;
- `pbr_shade.wgsl` multiplies the emissive base by the emission texel (ADR-1284);
- bloom and AgX take scene-linear radiance.

The clamp was one step earlier, in the parameter. Procedural `material/emissiveColor` was registered with a hard
range of [0, 1], "by the colour it is", and a parameter's constructor clamps its base. So a factor that should lift a
channel could only darken. Entity parts already allow 0..64 for the same quantity.

## Decision

`material/emissiveColor` is a factor on light, not a reflectance.
- Its hard maximum is the largest authored channel, floored at 1 and capped at 64 (the entity parts' ceiling).
- Its soft range, which is what the panel draws, stays 0..1.
- A value above 64 is still moved, and reported (ADR-331).
- `baseColor` keeps [0, 1]: an albedo above 1 reflects more light than it receives.

## Consequences

- An authored tint above 1 reaches the renderer as written. A render check on the riverbank (the veilcap at
  [4, 1, 1] against [1, 1, 1]) shows red rising, and the "runs at" warning gone.
- Every object whose tint is within [0, 1] keeps exactly the range it had, so nothing shipped moves.
- The artist's recoloured colourway GLBs remain the better tool where a texture has no energy in a channel:
  multiplying a channel that is near zero still gives near zero.
