# ADR-401: The vortex parity failure was the parity test

- Status: Accepted (2026-09-20)
- Builds on ADR-388 (the vortex field and its CPU/GPU parity test), ADR-389 (the band-limit, the
  domain warp and the contrast curve), ADR-387 (a correct value is not a reached value),
  ADR-385 (a stated reason is not evidence), ADR-182, ADR-396 (one description of the air).

## Problem

`gpu.the-vortex-shader-agrees-with-core-vortex-cpp` failed 75 of 2443 assertions, all on `density`,
worst case about three times the expected value, against a margin of 1e-3. `envelope`, `radialT`
and `depthT` all passed.

The natural reading — and the one this ADR was commissioned under — is that `src/core/vortex.cpp`
and `shaders/vortex.wgsl` hold the same arithmetic and have drifted, most likely in the contrast
curve ADR-389 changed on both sides: shape right, magnitude wrong, worst where the curve is
steepest. That is ADR-396's defect one system along, and the prescribed fix was to share the
arithmetic the way `wind_field.wgsl` shares the wind's.

**That reading is wrong, and the evidence is that every candidate is byte-identical.** Read against
each other, term for term: the contrast curve, `octaveWeight`/`vortexOctaveWeight`, `fbm3`,
`fbm3Vec`, `valueNoise`, `hash01`, `pcg3d`, the octave weights, the band-limit, the billow, the
domain warp call site, and both `sampleVortex` entry points (which pass `filterWidth = 0`, so the
band-limit is inert in this test anyway). Nothing in the engine disagrees with anything.

## What it actually was

`tests/rendering/test_vortex_parity_gpu.cpp` copied the uniform block into its compute kernel
**field by field**:

```wgsl
struct Args { v0: vec4<f32>, v1: vec4<f32>, v2: vec4<f32>, v3: vec4<f32>, v4: vec4<f32>, time: vec4<f32> };
...
var v: VortexUniformsWgsl;
v.v0 = args.v0; v.v1 = args.v1; v.v2 = args.v2; v.v3 = args.v3; v.v4 = args.v4;
```

ADR-389 added a sixth vector, `v6` — `smokeWarp`, `smokeBillow`, `detail` — to `VortexUniforms` and
to `VortexUniformsWgsl`, and to that list in neither. WGSL zero-initialises a `var` with no
initialiser, so **the GPU evaluated the vortex with the domain warp, the billow and the third noise
octave all switched off, while the CPU evaluated it with them on.**

The shipped fixture leaves `smokeWarp` and `smokeBillow` at their defaults of 0, so of the three
only `detail = 0.2` was live — and dropping one octave from a weighted sum and its normalisation is
worth up to 3x in the output. One number, never delivered, and 75 failing assertions none of which
was a disagreement about arithmetic.

The signature was in the failure all along and points away from the curve, not at it: `envelope`,
`radialT` and `depthT` are computed before any noise is sampled and read none of `v6`, so they
passed. **The only quantity that could differ was the one downstream of the field that never
arrived.**

## Decision

**No engine change.** The CPU and the GPU agree and have agreed throughout; there was never a
question of which side is right, and no tuning decision is implied. Nothing the owner sees moves.

The harness carries the uniform block **by type**, not as a field list:

```wgsl
struct Args { v: VortexUniformsWgsl, time: vec4<f32> };
```
```cpp
struct Args { vortex::VortexUniforms v; glm::vec4 time; } args{u, glm::vec4(time, 0, 0, 0)};
static_assert(sizeof(Args) == sizeof(vortex::VortexUniforms) + sizeof(glm::vec4));
```

A seventh vector added to both sides is carried with no edit here; one added to a single side fails
the `sizeof` assertion or Dawn's `minBindingSize` check instead of silently reading zero. This is
the same move as ADR-396 one level up: the thing that drifted was not the arithmetic but the
**list of things to copy**, and the fix is to stop keeping a list.

## The probes, and why the obvious one is not enough

Three cases added. The second is the one worth the words.

**1. Parity with the smoke on.** Nothing in `examples/` authors `smokeWarp` or `smokeBillow` yet, so
the shipped arm — deliberately the configuration that ships — exercises neither the domain warp nor
the billow. A second arm turns them on. A parity test that covers only the paths already in use
goes red the first time somebody turns a feature on. It moves 152 of 160 samples against the
shipped field, so it is genuinely a different field and not the first arm renamed.

**2. Every number the uniform carries reaches the shader.** Each of the fifteen authored values
`vortexEvaluate` reads is perturbed in turn and the GPU's answer must move, with the control that
re-running the *same* field moves nothing (a harness returning fresh noise each call would satisfy
the rest).

Measured, with the defect deliberately reintroduced:

| probe | result with the defect present |
|---|---|
| contrast-curve perturbation (the obvious one) | **passes**, 7 assertions green |
| every field reaches the shader | **fails**: `smokeWarp` 0/160, `smokeBillow` 0/160, `detail` 0/160 |

**A probe over the transfer function cannot catch this and a probe over reachability can**, because
both sides had the same contrast — the defect was not a disagreement but an absence, and an absent
field is indistinguishable from an agreeing one from inside a comparison. ADR-387 said a correct
value is not a reached value; this is the same sentence with "every" in front of it.

**3. The comparison can still fail on the transfer function.** The margin is 1e-3 because three
octaves of value noise accumulate rounding, and a margin chosen for rounding must be shown to still
reject a real difference. The GPU runs one contrast and the CPU is asked about another, by 5%:
**122 of 127 live samples rejected**. Live samples only — most of the original spread sits outside
the funnel where both sides are zero and agree for reasons that have nothing to do with the curve.

The new spread is placed from the geometry rather than by eye, which took two corrections worth
recording: `wall` is `exp(-rel.y^2 / thickness^2)` centred at y = -70, so the dense shell is
y in [-140, 0] and **not** deep down the funnel — a first version running to y = -716 produced 37
live samples of 480. And `smokeBillow` at 0.6 maps mid-range noise toward zero, which the contrast
smoothstep then removes entirely; at 0.2 the field is dense and the probe has something to measure.
Both were fixtures that looked reasonable and measured almost nothing.

## Consequences

- The suite is green: 8 vortex cases, 3461 assertions.
- `avgen_render_tests` had not linked from 2026-09-19 19:08 until today, which is why a test broken
  by an 18:59 commit went unseen. Building it alongside `avgen` is already adopted.
- The same field-list pattern is worth a sweep: any test harness that copies a uniform block member
  by member has this defect latent in it. This one had it for a day.

## Revisit when

- A seventh vector is added to `VortexUniforms`. It should need no edit here — and if it does, this
  ADR was wrong about the fix.
