# ADR-379: The vortex lights what floats over it

- Status: Accepted (2026-09-19)
- Phase 10, and the island's half of Phase 11. Extends ADR-371/374 (the vortex), ADR-230's ground
  illumination, whose shape this borrows.

## Decision

Two vec4s appended to `FrameUniforms` — the vortex's mouth and its radiance — and one term in
`atmosphereGroundAt`, which already does exactly this shape for a comet's ground pool. The sign of
the normal term is reversed: weighted by how much a surface faces **down**, so the island's flat top
is untouched and its underside takes nearly all of it. That is what makes it read as the vortex
lighting the rock rather than as the ambient being turned up.

Reach falls off with radial distance from the mouth's axis and switches off above it, because a
surface below the mouth plane is *inside* the funnel rather than over it.

Appended last in both `FrameUniforms` and `common.wgsl`, for the reason every block before it was:
no offset above moves, so every other pass's view of that structure is byte-identical. The
`static_assert` written as a sum is what catches the two sides drifting.

It matters more now than it would have yesterday: ADR-378's ladder halved the sky's ground fill, so
the underside is darker and has room for a light with a source.

## Two mistakes, both found by controls rather than by looking

**The first A/B was worthless.** I compared vortex-on against vortex-*off* and measured +33
luminance on the lower third — and attributed it to the spill. It was almost entirely the vortex's
own volumetrics being drawn. Switching the effect off to get a baseline moves the whole background
and swamps the thing under test. The real control is the vortex **on in both arms** with only the
spill moving, which needed `spill` to exist as its own parameter — so the measurement forced the
control surface, which is the right way round.

With that control: **+0.09** of 255 on the island. Invisible.

**And the reason was a units error.** I had scaled the surface radiance by `emission`, which is an
emissive density **per metre** integrated along a 4 km march. Multiplying a per-metre density by a
constant to get a surface irradiance is not a calibration, it is a category mistake, and it gave
0.24 where about 2.5 was wanted. A probe forcing the intensity to 8.0 moved the island by +7.31,
which is what said the plumbing was fine and the number was wrong.

`spill` is now the strength on its own, decoupled from `emission` and documented as such.

| spill | island | 
|---|---|
| 0.0 | +0.00 |
| **2.5 (shipped)** | **+0.90** |
| 5.0 | +1.70 |

Localised: the changed region is the island's own column (x 700..1417), and the underside's lower
facets take the teal while the upper rim stays the rock's own pale purple.

## Consequences

- +0.90 of 255 is subtle, which §10 asks for ("do not overdo this… the underside should remain
  primarily natural rock"), but it is close enough to the noise floor that it is worth re-checking
  at a lower camera before anyone concludes it is enough.
- Any surface within reach of the mouth catches it, including the near, low members of the
  procedural starfield. Not observed to matter at the shipped radius, and not separately measured.
- The other half of Phase 11 — particles that leave the tree and are entrained by the vortex — is
  not built. This is the static half of that connection.
