# ADR-058: The air and the ambient

Status: Accepted

## Context

Glowmere Valley is a night landscape whose subject is its own light: glowing fungi, drifting
spores, a hero mushroom lit from within. Rendered, it was none of those things. Measured over
three widely separated frames of the ninety-second shot:

| frame | mean | rms contrast | p01 | p99 | shadow frac | mid frac |
|---|---|---|---|---|---|---|
| 120 | 0.253 | 0.107 | 0.118 | 0.506 | 0.0000 | 0.9945 |
| 600 | 0.252 | 0.119 | 0.116 | 0.665 | 0.0000 | 0.9807 |
| 1200 | 0.273 | 0.095 | 0.123 | 0.488 | 0.0000 | 0.9990 |

Not one pixel in any frame fell below 8% luminance, and 98–99% of every frame sat in the
midtones. The valley was a single grey-teal band about two and a half stops wide. A scene whose
premise is bioluminescence had nothing dark for anything to glow against, and no depth cue but
scale: cropping the near ground and the far ridge apart gave means of 0.296 and 0.226 — seven
hundredths of luminance between the fern at the viewer's feet and the treeline four hundred
metres away.

Three separate causes, all of them structural rather than a matter of dialling a number:

**The ambient was the brightest light in the scene, and it was a shader constant.** The styled
path mixed a fixed pair of colours by the surface normal's Y. On flat ground under a moon at 22°
elevation, that constant delivered about two and a half times what the key light did, so the
landscape had no form: the lit and unlit sides of a hill were the same value, and trees cast
shadows nothing could see. Raising the rig's key by 9× did move the image (mean 0.237 → 0.462),
which is how we know the key was wired correctly and simply outgunned.

**The surface fog and the volumetric disagreed about where the air was.** The volumetric marches
a flat-topped mist layer — full density up to `fogHeight`, thinning by `fogHeightFalloff` above
it. The distance fog applied to surfaces ignored both and used a uniform slab. A ridge standing
clear of the fog bank was drawn as though buried in it.

**The fog colour and the ground were the same luminance.** `fogColor` sat at 0.083 linear against
a near field of about the same, so mixing towards it neither lifted nor deepened the distance.
Aerial perspective needs a difference to work with, and there was none.

## Decision

**Make the styled hemisphere authorable, and default it to the constants it replaces.**
`environment.styledSkyAmbient`, `styledGroundAmbient` and `styledAmbientFloor` default to the
values the shader hard-coded, so every other styled scene renders bit-for-bit as before — checked
by rendering a frame before and after the change: 0 of 2,304,000 pixels differ. The handoff gated
this on evidence that the constants blocked a required look; the table above is that evidence.

The floor stays a separate knob from the ground colour because they fix different problems.
Screen-space AO applied at full depth to a term this dominant prints its own sampling noise into
the image as a lattice on open ground; the floor is what suppresses that. Contact darkness should
come from the ground ambient instead, which is smooth by construction.

**Integrate the mist layer analytically in the surface fog.** `fogHeightAmount` blends the
geometric view distance towards the distance *through the mist*:

    G(y) = y                    for y <= 0        (inside the layer)
         = (1 - exp(-b*y)) / b  for y >  0        (above it)
    mean = (G(y1) - G(y0)) / (y1 - y0)            y measured from the layer's top
    travel = distance * mix(1, mean, amount)

`G` is the depth of air below a height, in metres of the layer's full density. Its difference
quotient along the ray is the mean density the ray passes through, because the ray climbs at a
constant rate — so one subtraction and two exponentials replace an integral. It is C1 across the
layer's surface, so a ray crossing the fog bank has no seam.

Both endpoints below the layer's top put the whole segment below it, and that case returns
`mean = 1` by a comparison rather than by the quotient. The quotient would give 1.0 only to within
rounding — its numerator and denominator are the same subtraction written twice, and the compiler
may fuse one and not the other — and a valley scene that lives inside its own fog bank should be
able to switch the integration on and see no change at all. It was a test asserting exact equality
that found this; the first version of it failed by 1.5e-4.

The surface fog borrows `fogHeight` and `fogHeightFalloff` from the volumetric rather than
declaring its own. They describe the same air. A scene whose fog bank has one height in the march
and another on the surfaces inside it does not read as one atmosphere, and the second pair of
numbers would exist only to be kept in step by hand.

## Consequences

The frame uniform grows by three vec4s (1056 → 1104 bytes), which is a single static assert and
one packing site; `sizeof(FrameUniforms)` was already the only description of that layout.

Three new scene fields, all defaulting to today's behaviour and all written back only when moved,
so an existing scene round-trips byte for byte.

`applyFog` gains a branch and, when the branch is taken, two exponentials. It is not in the
measured cost: the styled scene and its PBR control both sat at 20.8 ms GPU before this change.

The knobs are enough rope to make a scene look wrong. A ground ambient near zero with no local
lights gives a landscape whose shadowed side is black, which is not painterly, it is unlit. The
scene that motivated this pairs the darker ambient with ADR-053's ecology light field, so what
fills the shadows is the world's own glow.
