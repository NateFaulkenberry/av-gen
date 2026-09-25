# ADR-718: the fog's turbulence is band-limited by the march's step

**Status:** Accepted. What it does to scale 4 and above needs the owner's eye (see "For the owner").
**Date:** 2026-09-25
**Resolves:** ADR-713's grain finding. Turbulence 0.7 at scale 4 was grainy at the shipped 32 steps.
ADR-713 named the fix and did not build it.
**Implemented by:** `shaders/fog.wgsl` (`fogFlowLocal`, `fogTurbulenceBand`, `fogTurbulence` and
`fogShapeAt` take the step), `shaders/noise.wgsl` (`flowCurlBanded`), `shaders/volume.wgsl`
(`mediumShape` takes the step, and its three callers pass one), and the CPU twins in
`src/world/fog_field.cpp` and `src/core/noise.{hpp,cpp}`.
**Tests:** `tests/unit/test_fog_bandlimit.cpp` (new), `tests/rendering/test_fog_parity_gpu.cpp`
(one case added, harness widened to five vec4s and a step), `tests/rendering/test_fog_flow_colour_gpu.cpp`
(one case added), and one arm added to the hidden grain probe.

This is a separate ADR rather than a section of ADR-713. ADR-713 records a measurement this
changes, and the before and after are easier to read side by side here.

---

## Context

`fogTurbulence` displaces the sample by `flowCurl`, which has two octaves. In the flow's own
coordinates (the rest frame, yawed, in semi-axes) the first octave is one lattice cell and the
second is 1/2.03 of one. The turbulence scale multiplies both. ADR-713's review ellipsoid is 104 x
28 x 52 m. At scale 4 its first octave is 26 m along the long axis and 7 m vertically, and the
second octave is half that. ADR-710's schedule puts the 32 steps inside the volume's bound, roughly
10 to 17 m apart on this frame. That spacing is inferred from which octaves the band drops, not
measured directly. The march cannot carry that frequency, so the result is grain, not
structure: 0.085 at matched luminance, 3.4x the tornado hero.

## Decision

**Each flow octave is weighted by how many of its cycles one march step crosses.**

- **The step is an input to the field.** `mediumShape(s, p, t, step)` takes the step vector at the
  sample: the ray direction times the schedule's spacing there. The primary march passes
  `direction * segmentStep`, the shadow march passes `towards * dt`, and `volumeTotalDensityAt`
  passes zero. A zero step is a point sample and applies no band-limit. The vortex and the tornado
  ignore the new argument and keep `vortexFilterWidth()` as ADR-710 left it.
- **Measured along the step.** The flow's coordinates are anisotropic (the review volume's vertical
  semi-axis is a quarter of its long one), and a near-horizontal ray through a flat bank barely
  crosses the vertical frequency. `fogFlowLocal` is affine, so the local length of the step is its
  linear part applied to the step: the swirl rotation, the yaw, and the division by the semi-axes.
  The cycles per step are that length times the scale. The result does not depend on position.
- **The window.** Full weight up to the Nyquist rate (half a cycle per step), falling by smoothstep to
  zero at one cycle per step, where every sample lands on the same phase and the octave is pure
  alias. The second octave's cycles are 2.03x the first's. This is `vortexOctaveWeight`'s rule
  (ADR-389) with a hard floor.
- **The octaves fade, and the scale never changes.** Clamping the scale by the step was rejected. The
  lattice coordinate is `local * scale`, so a scale that depended on the step would move the whole
  pattern whenever a ray's spacing changed. Fading keeps every surviving octave where it was.
- **Divergence-free is kept.** `flowCurlBanded` weights each potential's octaves before the cross
  product, so it is still grad(a') x grad(b') for constant weights. The weights never exceed 1, so
  ADR-713's clamp and its bound proof are unchanged.
- **The identities are branches.** Turbulence 0 never reaches the flow (ADR-713's own branch).
  Weights (1, 1) go through `flowCurlBanded`'s branch to `flowCurl` itself. A moderate setting whose
  octaves the step can carry therefore evaluates exactly what it did before.
- **ADR-091.** A pure function of position, time and the step. There is no state, and the step is a
  function of the pixel's ray and the bound.

### Two stricter windows, measured and rejected

| window (cycles per step, full to zero) | 0.35 @ 1.5 | 0.7 @ 1.5 | 0.7 @ 4 | 1.0 @ 6 |
|---|---|---|---|---|
| 0.25 to 0.5 | 0.0159 | 0.0255 (medium mean 0.273 -> 0.293) | 0.0202, flow gone | 0.0238, flow gone |
| 0.35 to 0.7 | 0.0165 | 0.0295 (0.277) | 0.0203, flow gone | 0.0238, flow gone |
| **0.5 to 1.0 (chosen)** | see below | | | |

Both stricter windows changed turbulence 0.7 at scale 1.5, which the brief asked to leave alone, and
both removed scale 4's flow entirely at 32 steps.

## Evidence

**Grain at matched luminance.** Measured with `PROBE the march's grain at matched luminance`
(ADR-710's estimator: 640x360, Offline tier, the shipped 32 steps, t = 6) on ADR-713's review
ellipsoid. The `ff-turb-1.0-s6-32` arm is new. The before column was measured on this branch before
the change, and it reproduces ADR-713's table exactly.

| arm | medium mean, before -> after | grain / medium, before | grain / medium, after |
|---|---|---|---|
| default (turbulence 0) | 0.3041 -> 0.3041 | 0.0096 | 0.0096 |
| 0.35 @ 1.5 | 0.2969 -> 0.2970 | 0.0168 | 0.0168 |
| 0.7 @ 1.5 | 0.2728 -> 0.2730 | 0.0349 | 0.0349 |
| 0.7 @ 4 | 0.2438 -> 0.2730 | **0.0846** | **0.0398** |
| 1.0 @ 6 | 0.2174 -> 0.3069 | **0.1436** | **0.0238** |

- The moderate settings are unchanged to the probe's four places.
- At 0.7 @ 4 the second octave is gone and most of the first stays. The grain is 0.040, near
  0.7 @ 1.5's 0.035. The medium mean matches 0.7 @ 1.5's too, which fits a flow that is now mostly
  its first octave.
- At 1.0 @ 6 on this frame both octaves are over one cycle a step, so the flow is gone. The medium
  mean (0.3069) and the pixel count (23,058 against 23,050) are the calm ellipsoid's. This is the
  grain drop, and it is not a cleaner version of the same look: at 32 steps there is no flow left.
  The matched-luminance rule is why this shows: the grain fell because the thing that was grainy
  was removed.

**Byte identity at turbulence 0.** Rendered with a baseline binary and shaders built from this
branch's parent (`b401ac72`) and with the change, under `tools/gpu-lock.sh`, every frame hashed. All
ten cases are identical frame for frame: Glowmere Valley 2 (t = 60..60.25, 960x540, 15 frames), the
Tree of Life hero (t = 6, 640x360), the six fog-lab primitives with every pre-ADR-713 control off
its default, and the two ADR-713/714 review bases (flow and colour). Turbulence 0 never reaches the
new code: ADR-713's branch returns before the flow.

**The moderate settings, pixel by pixel** (960x540, before against after):

| arm | 32 steps: pixels changed / changed by more than 8 levels / largest | 256 steps: the same |
|---|---|---|
| 0.35 @ 1.5 | 14,496 / 0 / 2 | 14,040 / 0 / 1 |
| 0.7 @ 1.5 | 32,081 / 1 / 10 | 27,131 / 0 / 3 |
| 0.7 @ 4 | 77,667 / 36,178 / 120 | 33,749 / 0 / 5 |
| 1.0 @ 6 | 87,691 / 58,092 / 160 | 41,484 / 1 / 9 |

At 256 steps the step is eight times finer. At scale 1.5, even a ray straight down through the
28 m axis puts the finer octave well under a quarter cycle a step. The band is (1, 1) there, and
`flowCurlBanded` returns `flowCurl`, so the one-to-three-level differences at 0.35 and 0.7 @ 1.5 are
not the band-limit. (At scale 4 and 6, steep rays can still fade the finer octave a little at 256
steps, which fits their largest changes of 5 and 9 levels.) They are ADR-714's finding again:
the flow now reaches `flowCurl` through a new function and a refactored `fogFlowLocal`, and the Metal
compiler contracts that path differently. They were not chased to zero, because the brief's
byte-identity bar is turbulence 0, and that holds. At 32 steps, 0.7 @ 1.5 has one pixel over 8
levels: a grazing ray whose step is long enough to fade the second octave.

**The sheets** (session scratchpad `bandlimit/`, copied numbered to
`~/Desktop/av-gen-review/13-fog-flow/bandlimit/`). They are crops of the 960x540 review frame at
t = 6, before on the left and after on the right, five settings top to bottom.

- `1-before-after-32-steps.png`, the shipped step count. Default, 0.35 @ 1.5 and 0.7 @ 1.5 look the
  same before and after. At 0.7 @ 4 the fibrous, speckled edge becomes softer lobes with a little
  speckle left on the lower rim. At 1.0 @ 6 the speckled cloud becomes the plain ellipsoid.
- `2-before-after-256-steps.png`. Every row looks the same before and after, including 0.7 @ 4 and
  1.0 @ 6, whose fine, flaky structure is fully there. The band follows the step, not the scale.

## Tests, and how each was shown to fail

- **CPU** (`test_fog_bandlimit.cpp`, `[bandlimit]`).
  - The window: a zero step gives (1, 1). The first octave is whole at Nyquist and gone past one
    cycle a step. The weights are monotone in the step, the finer octave never keeps more than the
    coarser one, and the fade is a ramp. Measured along the step: a step straight down through the
    flat volume keeps less than the same length along it.
  - Resolved means untouched: at 0.35 and 0.7 @ 1.5, a step that carries both octaves gives
    `fogShapeAt` and `fogTurbulence` bit for bit equal to the point sample. The control is that a
    coarse step changes the field.
  - Unresolved means no flow: at 0.7 @ 4 with a step of half the long semi-axis, the displacement is
    zero and the field is the calm primitive's bit for bit. The control is that the point-sampled
    flow moves the field.
  - Turbulence 0 ignores the step. `flowCurlBanded(1, 1)` is `flowCurl`. The banded flow is
    divergence-free (worst finite-difference ratio 0.023, against 0.98 for a control flow scaled by
    `1 + 0.5 x`, which has a real divergence).
- **Parity** (`test_fog_parity_gpu.cpp`). A new case compares both octave weights, the displacement
  and the field at three step vectors, on the Bank, the Sphere and the Capsule, with swirl on. Its
  controls count samples with a weight strictly between 0 and 1, samples with the first octave
  silenced, samples with both whole, and samples displaced by more than a metre. ADR-713's three
  cases now pass a zero step, so they still compare the point-sampled field.
- **Render** (`test_fog_flow_colour_gpu.cpp`). Turbulence 0.7 at scale 20 on a 100 m sphere moves the
  32-step frame's luminance by 0.95% relative to the calm sphere (the check is under 3%), and
  moves the 256-step frame by 9.1% (the check is over 5%). Only a render can show that the march
  hands the field its step.

**Break demonstrations.** Each break was applied, the named case failed, and the file was restored
from a copy.

- CPU `fogTurbulence` ignoring the band (weights forced to 1): the "no flow" case fails on all 2,000
  displacement comparisons and 194 field comparisons, and the resolved case's control fails.
- CPU band measured isotropically, as the step length over the smallest semi-axis: the window case
  fails five checks, including along the bank keeping its first octave (0 == 1).
- `fs_volume` passing a zero step to `mediumShape`: the render case fails. The 32-step change is 0.11,
  the full point-sampled flow. The parity case passed under this break, and no CPU test reads the
  shader.
- WGSL `kFogBandZero` changed from 1.0 to 1.2 (the CPU twin left at 1.0): the parity case fails on
  both weights, the displacement and the field (about 9,500 failed checks).

## For the owner

- **Scale 4 and above now depends on the step count.** At 32 steps, 0.7 @ 4 keeps its lobes and
  loses its finest fibres. 1.0 @ 6 on this frame is a plain ellipsoid. At 256 steps both look as
  they did. That is what a band-limit is: the march cannot show that structure at 32 steps, and what
  it showed before was grain. If a high scale has to read at the shipped step count, the options
  are more steps or a tighter bound (ADR-710: the steps go inside the bound, so a smaller bound
  means finer steps). ADR-710 already names the fog bank's bound as the next candidate.
- **The row's soft range** still stops at 4, and at 4 it now reads as a lobed cloud rather than
  speckle. Nothing about the range was changed.
- **The shadow march** passes its own step (`towards * dt`). With `volumeShadowSteps` at 4, a
  turbulent bank's shadow sees a smoother flow than the camera does. That is the right trade for a
  shadow, but it has not been looked at, because no review arm has shadow steps on.

## Owner ruling (2026-09-25)

- **At the default 32 march steps, a very turbulent bank reads as plain.** With turbulence 1.0 at
  scale 6, the band-limit removes detail finer than 32 steps can resolve, and the bank draws as a
  plain shape. The detail returns at 256 steps. The owner accepted this. The slider is not capped
  at low step counts, even though that makes it a control over nothing at 32 steps (ADR-421's
  risk), accepted knowingly.
