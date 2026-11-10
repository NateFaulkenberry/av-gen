# ADR-1960: The landing glow, tile edges lit by a landing

- Status: Accepted (Musical Geography CP-D r2, 2026-11-10), scene/musical-geography.
- Code:
  - `tileLanding` and `TileSample::landing` (`src/scene/tile_terrain.*`), averaged by the generator like the crest;
  - the record packing `tilePackedEmissiveW`;
  - the `tileContour` op's z and w lanes;
  - `TileDynamics::landingGain` and `landingColor`, with parameters `env/tiles/glow/landing` and
    `env/tiles/glow/landingColor` (scene JSON `tileDynamics.glow.landing` / `landingColor`);
  - the material input `landingGlow` (`FrameUniforms::landingGlow`).

## Context

The owner: *"add an edge glow effect to the tiles that are affected when a character lands."* It must read in
daylight, use the contour edges' visual language, and settle with the motion.

## Decision

**What lights.** Each tile has a landing level from 0 to 1. It is the sum, over landings still ringing, of:
- the landing's strength;
- a spatial falloff of its own, e^(−r/glowRadius), eased out between 5 and 6 radii. `spring.glowRadius` defaults to
  3 m and is capped at `rippleRadius`. A first clip with the ripple's 14 m outlined the whole field, because about 100
  landings were ringing at once;
- its spring's envelope, e^(−ζωa): the decay without the swing, so the glow does not flicker with the oscillation;
- a 60 ms rise, so the glow does not pop on.

The sum is clamped to 1. The glow therefore lights the impact tile brightest, falls off smoothly outward, follows the
ripple's arrival, and fades exactly as the tile settles. It is a function of the clock, so it is identical on a seek.

**How it reaches the tile.** The tiles kernel packs the level into the record's spare emissive lane, above the crest
and the contour side mask: crest + 2·mask + 32·level in 255ths. The `tileContour` op decodes it.

**The op's lanes.**
- x and y are unchanged: the contour coverage and the crest.
- z is now the landed tile's outline on all four sides, times the level. It uses the contour's prefilter. Its band is
  `constant.w` (local metres; 0 means the contour's band from `constant.z`), so the outline can be wider than the
  contour lines and read in daylight.
- w is the level.

The old z and w (the flagged side count, and "any side") had no reader.

**Art's controls.** A program adds `tileContour.z × landingGlow.rgb` to its emission (4 ops). `landingGlow` is the
colour × gain parameters: routable, and keyable per arc state like the other glow gains. The gain is 0 by default, so
nothing changes until art wires it.

## Consequences

- Tests:
  - CPU: the glow starts at 0, falls off from the centre outward, fades with no step and is gone by `springSeconds`;
    the packing and the op decode agree; the gain and colour round-trip as parameters.
  - GPU: the kernel's packed level matches the CPU mirror to one 255th, with 43 springs.
- `FrameUniforms` grows by 16 bytes.
