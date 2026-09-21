# ADR-567: one height layer, three readers, and the test that they are a function and its integral

Status: accepted. Date: 2026-09-21. The first step of Phase D of the Fog Bank brief (§46 D, §6, §7).

## Context

Phase D is the brief's height, ground and distance distribution -- §7's *Height, Height Falloff,
Ground Density, Upper Density, Height Curve, Horizon Density*. Before adding any of them I went
looking for what the engine's height model already is, and found a constraint that decides the
shape of the whole phase.

**The global height layer is read by two passes that must agree, and one of them reads its
integral.** ADR-058: the volumetric march multiplies its density by a height term, and the surface
fog replaces its geometric distance with the distance *through that same layer*, using a closed-form
antiderivative --

> "a scene whose fog bank has one height in the march and another on the surfaces inside it does
> not read as one atmosphere."

That is two expressions, in two files, that have to be a function and its integral:

| | where | what |
|---|---|---|
| the march | `shaders/volume.wgsl`, `volumeDensityAt` | `exp(-max(0, y - fogHeight) * falloff)` |
| the surfaces | `shaders/common.wgsl`, `fogHeightIntegral` | `y` below the top, `(1 - exp(-b y)) / b` above |

**Nothing checked the relationship.** `test_volume_gpu.cpp` checks the *sign* -- a surface standing
clear of the layer is fogged less than the same surface buried in it -- and the *degenerate case* --
a ray entirely inside the layer is bit-identical with the integration on or off. Both pass against
any monotone `F`. The one property that makes the two passes describe one atmosphere had no test on
it: the same hole ADR-566 found in the march's ray bound, and the general form is
`docs/testing.md` 24.

**And it is about to matter much more than it does today.** §7's *Height Curve* is the control that
breaks it. A profile of `exp(-(b d)^k)` has no elementary antiderivative for general `k`, so the
surface pass could not integrate it and the two atmospheres would silently part company at exactly
the setting an artist reached for. **Phase D's height model is constrained to curve families whose
antiderivative is closed-form**, and that constraint is worth knowing before writing the control
rather than after.

## Decision

### `shaders/height_fog.wgsl`: one definition each, with no bindings

The profile and its antiderivative move into their own file, unchanged, and both take their
parameters as arguments. `common.wgsl` includes it; `volume.wgsl` reaches it through its existing
include of `common.wgsl` and calls `fogHeightProfile` instead of restating the expression;
`particles.wgsl` includes neither and so includes this file directly.

**There were three readers, not two.** The particle pass estimates its own transmittance with four
midpoint samples of the same layer and had the expression written out inline. Nothing in this ADR's
first draft knew that -- it was found by grepping for the expression rather than for the feature,
which is the audit ADR-562 §9 prescribes and the reason the rule says *grep the lane, not the
name*.

**No bindings, deliberately.** A test can compile this file alone with no uniform buffers to
assemble, which is what makes the check below cheap enough to exist. The rule that comes with it:
**included by `common.wgsl` and by nothing else.** The include directive does not de-duplicate
(ADR-360), so a second include would put two copies of these functions into one module -- and WGSL
compiles at *load*, so a duplicate function is invisible until something renders a frame, which is
one of `agent/tornado`'s two `docs/testing.md` entries.

### `test_height_fog_gpu.cpp`: integrate the profile and compare

The closed form is compared against a 4096-slice midpoint integral **of the shader's own profile
function**, over spans that sit wholly inside the layer, wholly above it, straddle the seam, start
exactly on it, and descend rather than climb.

It runs on the GPU although the maths is trivial, and that is the point: **the maths is not what
could be wrong.** What could be wrong is that the shader the frame runs disagrees with the model
anyone reasoned about -- a lost `max`, a falloff read from the wrong slot, an edit to one of the
two expressions and not the other. So the numbers come from the shipped WGSL.

Two controls, because a set of spans that never crosses the join tests one branch of a piecewise
function and a set that never reaches the uniform interior tests only the exponential -- and both
would pass against a shader that had lost its `max`. A second case checks the *instrument*: the
whole span must equal the sum of its halves for both the closed form and the numerical integral,
so the agreement is not being read off a number with its own error.

## Consequences

- **The refactor is bit-identical** and has to be: `max(x) * b` and `b * max(x)` are the same
  float multiply, and the full GPU suite's determinism and checkpoint cases are what says so
  rather than the argument.
- **Phase D's height model has a stated constraint**: every term in the profile must have a
  closed-form antiderivative, because the surface pass integrates it. `groundDensity` and
  `upperDensity` are an affine rescale and survive it trivially; a *curve* control has to be a
  linear blend between two families that each integrate -- the exponential it already has, and a
  compact quadratic that reaches zero at a definite height. That is the same vocabulary the fog
  bank's own `domeShape` uses (ADR-563), which is worth keeping.
- **Break demonstrations.** Dropping the `max` from the profile -- so the layer's interior grows
  exponentially downward instead of being uniform -- fails at 110 m against 143.8 m of
  full-density air. Changing the antiderivative's `1/b` to `1/(b + 0.01)` fails at 21.4 m against
  75.0 m. Both are reported in metres of air, which is the unit the defect would have been argued
  about in.

## Revisit when

- **The curve control lands.** `fogHeightProfile` and `fogHeightIntegral` gain `upper` and `curve`
  together or not at all, and this test is what holds them together.
- **A fourth reader appears.** There were three, not two, and the third was found by looking
  rather than by assuming: `shaders/particles.wgsl`'s `fogTransmittance` wrote
  `exp(-max(0.0, y - params.fog.y) * params.fog.z)` out for itself, four midpoint samples of the
  same layer, fed from its own copy of `fogHeight`/`fogHeightFalloff` in the particle frame. It
  calls `fogHeightProfile` now. That is ADR-562 §9 again -- **every reader of a shared model is a
  call site to audit** -- and the count is the thing to keep watching: a model with three readers
  and one definition is fine; a model with three readers and three definitions is three
  atmospheres that happen to agree.
