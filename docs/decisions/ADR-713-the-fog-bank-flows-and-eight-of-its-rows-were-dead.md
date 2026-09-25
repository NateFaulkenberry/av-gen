# ADR-713: the fog bank flows, and eight of its rows were dead

**Status:** Accepted. The look of the new rows needs the owner's eye (see "For the owner").
**Date:** 2026-09-24
**Resolves:** fog brief §16 (turbulence, curl, swirl, dissipation, expansion, contraction), the
`docs/design/effect-library/tornado-fog-production-pass.md` §2 "§16 flow controls" row, and ADR-571's
and ADR-572's revisit notes.
**Implemented by:** `shaders/fog.wgsl` (`fogStructureFrame`, `fogSwell`, `fogSwellOffset`,
`fogSemiAxes`, `fogTurbulence`, `fogTurbulenceReach`, `fogShapeAt`), `src/world/fog_field.cpp` (its
twin), `src/core/noise.{hpp,cpp}` (`valueNoiseGrad`, `flowCurl`: CPU twins of the WGSL pair),
`shaders/volume.wgsl` (`mediumFogUniforms`, `mediumBoundOf`), `src/world/medium_bound.cpp`,
`src/world/effects/kinds/volumetric_fog_effect.cpp`.
**Tests:** `tests/unit/test_fog_turbulence.cpp` (new), `tests/unit/test_medium_bound.cpp` (one case
added), `tests/rendering/test_fog_parity_gpu.cpp` (one case added, harness widened),
`tests/rendering/test_fog_flow_colour_gpu.cpp` (new), `tests/unit/test_effect_registry.cpp` (count).

---

## Context

ADR-571 gave the bank a drift and ADR-572 let the drift follow the wind. §16 also asks for
turbulence, curl, swirl, dissipation, expansion and contraction. The production pass asked for the
lane audit to be run first (ADR-562 §9: grep the lane index, not the feature).

## The lane audit, and what it found

`grep -o 'mediaLane(s, [0-9]*u)' shaders/volume.wgsl`, read with the kind each reader dispatches on.
For a fog slot, a lane is free when its only readers are `mediumVortexUniforms`,
`mediumTornadoUniforms` or `mediumCapOf`'s tornado branch, since a fog slot takes none of those arms.
One reader outside `volume.wgsl` matters: `debug_visualizer.cpp` reads lane 4.y as a throat for
every kind, so lane 4 was left alone.

| lane | a fog slot reads | after ADR-713/714 |
|---|---|---|
| 0 | centre, radius | unchanged |
| 1 | x thickness (bound), w density | **z = swirl**, rad/s |
| 2 | xyz drift | w = distance-colour range (ADR-714) |
| 3 | x swell (bound only), z emission, w filaments | **x, y = swell amount, rate (now in the field)** |
| 4 | x depth (bound) | unchanged; yzw free only if the debug view learns the kind |
| 5 | x comet, y reach, z scattering | w = height-colour amount (ADR-714) |
| 6 | xyz density curve | **w = turbulence rate** |
| 7 | x detail scale, z detail amount | **y = turbulence amount, w = turbulence scale** |
| 8 | nothing | xyz distance colour, w its amount (ADR-714) |
| 9-11 | rgb depth colours | w = the height colour's r, g, b (ADR-714) |
| 12-14 | full | unchanged |
| 15 | kind tag | unchanged |

Spare after both ADRs: 1.y, and 4.yzw once the debug view is taught the kind.

**The audit found eight rows on the fog panel that packed numbers nothing read.**

- Billow, Churn, Wisps, Wisp scale and Fine detail are the vortex's noise stack. They pack into
  lanes 2 and 6, which ADR-571 gave to the drift and the density curve. Every preset set them, and
  `audio.mid` was routed to Churn. None of them reached the field.
- Drift ("how fast the whole bank turns over") packs `rotationSpeed` into lane 1.z, and Swell and
  Swell speed pack into lanes 3.x and 3.y. `fog.wgsl` read none of these lanes. Swell did reach
  `mediumBoundOf`, so it widened the ray bound and never the fog.

The file's own header said the vortex controls a bank has no use for "are simply not declared
here". Eight of them were declared. This is ADR-571's census finding again: no shipped scene contains
a fog bank, so nothing rendered the code these rows were dead in.

## Decision

### The five dead noise rows are cut (ADR-441)

Billow, Churn, Wisps, Wisp scale and Fine detail are removed, along with their preset writes and
the `audio.mid -> churn` route. The fog's detail is `detailAmount`/`detailScale` (ADR-565), and its
turbulence is the new row below. Threads stays because lane 3.w is read.

### Drift becomes Swirl, and Swell becomes live

These three were §16's own controls under vortex names, so they are made to work rather than
duplicated beside new rows.

- **Swirl** (`swirl`, still `/vortex/rotationSpeed` in the file, rad/s). The bank's internal
  structure (detail and turbulence) turns rigidly about its vertical axis: `fogStructureFrame`
  rotates the sample by `-omega * t` about the centre. The rotation is rigid because a differential
  swirl winds without bound. At 0.02 rad/s it is twelve turns of shear after ten minutes, finer than
  the march can carry, whereas a rigid rotation is periodic in t. Swirl turns what is inside the bank,
  not its outline: turning the outline is `bankRotation` keyframed, which the timeline already does.
- **Swell** (`swell`, `swellSpeed`, rad/s): §16's expansion and contraction.
  `1 + amount * sin(rate * t)` divides the horizontal offset, for the sphere the vertical too.
  The amount is clamped below 0.9. The bound has carried `1 + amount` horizontally since ADR-566; the
  sphere's vertical bound now carries it too.
- `Vortex{}` spins at 0.035 rad/s and breathes 5% every 35 s. Those defaults were harmless while the
  fog read neither, so `applyStyle` now zeroes both. The five presets that author a swell (Valley
  Fog, Rolling Bank, Dense Cinematic, Cosmic Mist, Dream Fog) now breathe as their authors wrote.
  The `audio.bass -> swell` route reaches the outline, which its comment always claimed.

### Turbulence, and it is also the curl

ADR-572 said a flow sampled per position shears the structure, and that shearing is what turbulence
is. So turbulence and curl are one mechanism with one set of rows:

- `turbulence`, 0..1: how far, in semi-axes.
- `turbulenceScale`: folds across the bank.
- `turbulenceSpeed`: the rate at which the flow evolves in place.

The world point is displaced by `noise.wgsl`'s `flowCurl` before the **whole** field is evaluated:
the silhouette, the height profile and the detail. `flowCurl` is the cross product of two noise
gradients, so it is divergence-free and moves density rather than creating it. §17 says the same:
"flow affects movement, not basic existence".

- **Frame.** The flow is sampled in the structure's rest frame, so it drifts and swirls with the
  bank.
- **Units.** Displacement is measured in the primitive's own semi-axes, so the same number deforms a
  30 m wisp and a 2 km bank alike.
- **The bound is a proof.** The flow is clamped to length 1, so the displacement is at most
  `amount` of each semi-axis. Both bound twins grow by exactly that: horizontally by the longer
  horizontal semi-axis, vertically by the vertical one.
- **The gain.** `kFogTurbulenceGain` is 1.3, measured so that the clamp engages on under 2% of
  samples. At 0.5 the clamp never engaged, and the mean displacement was a third of what the row
  promised.
- **The early-out.** It checks the undisplaced distance against `1.35 + amount * reach / min(swell, 1)`.
  `reach` is 1 for every shape except the capsule, which measures its long axis in widths.

### Dissipation and one-shot expansion get no row

A bank that grows or clears once over a shot is Bank radius or Fog density keyframed. Those are
ordinary parameters, and the timeline drives them as a pure function of the transport second
(ADR-018, ADR-091). A static "dissipation" is Density threshold, which already erodes the thin parts
first. A new row for either would claim something two existing rows already do (ADR-421). The Swell
tooltip says where to go.

### Everything is a pure function of t, and the identity at its default

There is no accumulated state. Each new term returns its input when its row is at the default, and
it does so through a branch rather than a small number: `omega == 0`, `amount <= 0`, `swell == 1`.

## Evidence

Sheets are in the session scratchpad (`fogflow/`), 960x540, 256 steps, one placed ellipsoid in the
Tree of Life frame. Tilt-shift and bloom are off. The camera and exposure are fixed, and the island
floats, so it moves between instants.

- `sheet-turbulence.png`. At 0 the ellipsoid is a clean soft oval. At 0.35 its outline turns lumpy
  and cloud-like. At 0.7 it billows into lobes with detached wisps. At 0.7 with scale 4 the edge goes
  ragged and fibrous.
- `sheet-swirl.png` (turbulence 0.35 frozen, rate 0). With swirl 0 the outline is identical at t = 6
  and t = 16. At 0.08 rad/s the lobes have moved round the bank between the two instants.
- `sheet-swell.png` (0.3 at 0.25 rad/s). The ellipsoid is visibly larger at its peak (t = 6.28) and
  smaller at its trough (t = 18.85). With swell 0 the two instants are identical.

**Grain at matched luminance.** `PROBE the march's grain at matched luminance`, the ADR-710
estimator, 640x360, Offline tier, 32 steps, t = 6. The `ff-*` cases are added to the probe.

| arm | medium mean | grain / medium |
|---|---|---|
| base (turbulence 0) | 0.304 | 0.0096 |
| turbulence 0.35, scale 1.5 | 0.297 | 0.0168 |
| turbulence 0.7, scale 1.5 | 0.273 | 0.0349 |
| turbulence 0.7, scale 4 | 0.244 | **0.0846** |
| swirl 0.08 (turbulence 0.35 frozen) | 0.268 | 0.0212 |
| swell 0.3 | 0.358 | 0.0096 |

Turbulence raises grain, because it puts structure into the edge that 32 samples resolve less well.
At scale 4 that structure is finer than the march's spacing, and the grain is 3.4x the tornado hero's
0.025 from ADR-710. The row's soft range stops at 4. A band-limit like `vortexFilterWidth` (drop the
flow's second octave when its wavelength is under two steps) is the fix if the owner wants that end
of the range. It is not built here, because it would be a second change to the field in the same
edit.

**Byte identity at the defaults** is in ADR-714's "Evidence", which covers both ADRs' shader changes.

## Tests, and how each was shown to fail

- **CPU** (`test_fog_turbulence.cpp`).
  - Every preset packs swirl, turbulence and both colour amounts as 0, and each function returns its
    input bit for bit.
  - Swirl is a rigid rotation: `detail(c + R(w dt)(p - c), t + dt) == detail(p, t)`. The control is
    that the detail at a fixed point changes.
  - Swell scales the extent by exactly its factor. The sphere also scales vertically.
  - Turbulence moves the outline both ways within its stated bound, is carried by the drift, and is
    a pure function of t.
  - `flowCurl` is divergence-free: the worst |div|/|partials| is 0.013.
  - Every flow row reaches its lane, and the lane reaches the field.
- **Bound** (`test_medium_bound.cpp`). Every primitive's bound contains the field under turbulence
  0.9, swell 0.3 and swirl, at three instants, both with the turbulence and without it.
- **Parity** (`test_fog_parity_gpu.cpp`).
  - The harness takes the whole slot and assembles the struct as `mediumFogUniforms` does.
  - A new case compares the swell, the rest frame, the turbulence displacement, both colour
    weights, the tinted colour and the field on Bank, Sphere and Capsule.
  - Its controls count the samples that landed in the medium, the samples that moved by more than a
    metre, and the samples whose colour changed.
- **Render** (`test_fog_flow_colour_gpu.cpp`). A turbulent ball puts light in pixels whose rays miss
  the calm ball's bound cylinder. The calm ball puts light in none of them.

**Break demonstrations.** Each break was applied, the named case failed, and the file was restored
from a copy, never with `git checkout` (docs/testing.md 26).

- Sphere bound without the swell: the bound case fails.
  - The first version of that case applied turbulence 0.9 as well, and its margin hid the break.
    The case now runs each primitive both with and without turbulence.
- Bound without the turbulence growth: the bound case fails at 0.53 of peak outside.
- Swirl turning the wrong way: the swirl case fails.
- The Bank swelling vertically: the bound case fails.
- The swirl without its `omega == 0` branch: the identity case fails. Its first version used the
  presets' centre, the origin, and passed, because `c + (p - c)` is `p` when `c` is 0. The case now
  uses an off-origin centre.
- The WGSL bound without the turbulence growth: the render case fails. The CPU bound twin was right,
  and no CPU test could see this break.
- The WGSL `kFogTurbulenceGain` changed from 1.3 to 1.2: parity fails on the displacement.

## For the owner

- **Turbulence 0.7 is the look this adds.** The clean primitive becomes a cloud. Scale 4 is grainy
  at 32 steps; see the table.
- **Five presets now breathe**, and bass reaches their outline. These are the swells their authors
  wrote, which were silently inert until now.
- **Swirl turns the inside, not the outline.** On a bank with no detail and no turbulence it does
  nothing, and its tooltip says so.
