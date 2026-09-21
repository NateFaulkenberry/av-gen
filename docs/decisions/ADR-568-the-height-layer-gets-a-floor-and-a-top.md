# ADR-568: the height layer gets a floor and a top, and both had to be integrable

Status: accepted. Date: 2026-09-21. Phase D of the Fog Bank brief (§46 D, §6, §7).

## Context

§7 asks for *"a robust height-based density model with artist controls: Height, Height Falloff,
Ground Density, Upper Density, Height Curve, Horizon Density ... dense near ground → gradually
thinner → clear atmosphere, but allow custom curves. Not restricted to a simple linear gradient."*

What the engine had was one exponential: `exp(-falloff * max(0, y - height))`. Two of §7's six
controls already existed under other names -- **Height** is `fogHeight` and **Ground Density** is
`volumeDensity`, which *is* the layer's full density and does not want a second control beside it.
The exponential cannot express the other two at all:

- it never reaches zero, so there is no **clear atmosphere** above the mist, only ever less of it;
- it always reaches zero, so there is no **haze floor** -- no thin global veil that persists at any
  altitude;
- and it has one shape, so there is no **curve**.

ADR-567 found the constraint that decides how to add them: **the surface fog integrates this same
layer with a closed-form antiderivative**, so every term in the profile must have one. That rules
out the obvious spelling, `exp(-(b·d)^k)`, which has no elementary integral -- the march and the
surfaces would have parted company at exactly the setting an artist reached for, and the picture
would have been a ridge line fogged wrongly against the air in front of it.

## Decision

### Two controls, both with closed-form integrals

    profile(d) = upper + (1 - upper) * mix( exp(-b·d), max(0, 1 - b·d/2)^2, curve )

- **`fogUpperDensity`** is an affine rescale of whatever shape is underneath, so its integral is
  `upper·y + (1-upper)·∫shape`. It is the fraction of the layer's density still present at *any*
  height: 0 is clear air above the mist, 1 is a uniform atmosphere with no layer at all.
- **`fogHeightCurve`** is a **linear blend between two families that each integrate**, and because
  the blend is linear its integral is the blend of the integrals. 0 is the exponential -- a long
  soft tail, haze. 1 is a compact quadratic that reaches **exactly** zero at `2/b`: a layer with a
  definite top. They agree in value *and* in slope at `d = 0`, so no curve setting puts a corner at
  the layer's surface.

  The quadratic's antiderivative case-splits at the height where it reaches zero and saturates at
  `2/(3b)` above it. That case split is the arm a closed form gets wrong, which is why the test's
  case set includes `curve` 1 and `curve` 0.5 rather than only the ends.

This is the same shape of answer `domeShape` gives the fog bank's own lid (ADR-563) -- blend
between two analytic families rather than reach for an exponent -- and the vocabulary agreeing
across the two is worth keeping.

### The defaults are bit-identical, and that is asserted rather than argued

At `upper` 0 and `curve` 0 both functions reduce to exactly what they were: `mix(x, y, 0)` is
`x·1 + y·0` and `0 + 1·shape` is `shape`, both exact in IEEE for finite inputs. That is an
argument, so the difference is computed **on the GPU** and required to be zero.

It is the control on the whole ADR. A height model that moved every existing scene by a fraction of
a level would be caught by nothing else in the suite: the volumetric checkpoints are hashes of
frames, and nobody re-baselines a hash for a change advertised as a no-op at its defaults.

### One definition, three readers, still

ADR-567's file gains the two parameters and all three readers gain them together: the march
(`volume.wgsl`), the surface fog's analytic integral (`common.wgsl`) and the particle transmittance
estimate (`particles.wgsl`). **A reader left on the old model would be a third atmosphere in the
same frame**, and the whole point of ADR-567 was that the third reader is the one nobody remembers.

Each of the three needed two floats of uniform space and each got a **named lane** rather than the
spare zeroes of a lane that means something else -- `VolumeUniforms::heightFog`,
`FrameUniforms::fogShape` (appended last, so no `offsetof` assertion moves) and
`ParticleUniforms::fog3`. ADR-562 §9 is what reusing a lane costs when it goes wrong, and none of
these three is a packed per-kind block with a budget to defend.

## Consequences

- **`ParticleUniforms`' size assertion caught the addition** and had to be updated 37 → 38 vec4s.
  That is the guard doing its job: the WGSL struct and the C++ struct are two declarations of one
  layout, and the assertion is the only thing that holds them together.
- **The arms show the controls doing different things**, measured on the sky band of an otherwise
  identical frame: the exponential reads 201, the haze floor 231 (a veil over the background), and
  the compact curve **142** -- a definite line above which there is no fog at all and the sky is
  the colour it would be with no atmosphere. `both` sits at 217. *No setting of the exponential
  produces the 142.*
  - The first arm set was rendered at `volumeDensity` 0.0016 and came back a white wash, **the same
    over-density mistake as the fog-primitive arms, made a second time**. The generator now carries
    the arithmetic instead of the number: 4 km of march at `volumeAbsorption` 0.5 with a height
    term averaging 0.4 wants 0.0006 for an optical depth near 0.5.
- **Break demonstrations.** Wrong constant in the quadratic's antiderivative: 107.7 m against
  116.9 m of full-density air. Dropping the `upper·y` term: 117.0 against 118.0 -- deliberately
  small, and it still fails, which is what the tolerance is sized for. Swapping `exp` for the
  classic `exp2(x·log2e)`: the bit-identity case fails at 3e-8, which is the only test in the tree
  that would have noticed.
  - **And one attempted break that was not one**, worth recording because it says what the guard
    does *not* constrain: rewriting `upper + (1-upper)·shape` as `(upper + shape) - upper·shape`
    passes, because at `upper` 0 both collapse to `shape` exactly. The bit-identity case constrains
    the **default path** and nothing else. A test that only ever passes is worthless; a test whose
    exact scope you have measured is not the same thing.

## Revisit when

- **§7's Horizon Density lands.** It is the one control of the six still missing, and it is not a
  height term -- it is density rising with distance from the camera, which the march has along its
  ray and the surface pass has in `exp(-(d·density)^2)` already. Whether those two are the same
  quantity is the next question, and the answer decides whether it is a control or a duplicate.
- **§8's ground fog.** Hugging *terrain* rather than a plane needs the terrain height at a world
  position inside the march. `volumeDensityField` already binds a named scalar field into
  `volumeDensityAt`, which is the extension point to try before adding a heightmap binding.
- **A fourth reader of the layer appears.** The count is the thing to watch.
