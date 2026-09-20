# ADR-460: Most of the grain is the march's own jitter, and the hero shot does not contain the cyclone

- Status: Accepted (2026-09-20)
- Opens the Vortex 2.0 rebuild. Extends ADR-389 (the grain is aliasing), ADR-374 (the vortex is a
  funnel and the camera must be outside it), ADR-371 (the vortex lives in the march), ADR-388 (the
  vortex as a sampler), ADR-401 (the vortex parity failure was the parity test).
- **Corrects the premise the rebuild was commissioned on.** The proposed architecture was "the
  vortex needs its own pass at reduced resolution, where steps are cheap because pixels are few."
  Measured, that trade is 1.7x *against* you. It is not built.

## The complaint, and the four measurements that answer it

The owner reads the cosmic vortex as "procedural noise / stippled particles" and wants a
volumetric cyclone. Four measurements decide what to do about it, and three of them contradict
something this project currently believes.

### 1. The envelope is uniform in angle, so everything in the picture is noise

Before this branch, `vortexEvaluate`'s envelope was `voidMask * rim * vert`: monotone in radius,
**completely uniform in angle**, smooth in height. It contains no structure of any kind. Every
feature anybody has ever seen in this effect came out of the three fBMs underneath it.

So the complaint is structurally literal rather than a matter of taste: it *is* noise draped on a
smooth cone. No step count and no band-limit can fix that, and this also explains ADR-389's most
striking result -- clamping strictly to what 125-metre samples can carry came back "a flat teal
wash with no swirl at all" because **there was nothing underneath the noise for the clamp to
leave behind.**

### 2. You cannot trade march resolution for march steps: the exchange rate is 1.7x against you

`volume.march` pass median, minima of 3-4 interleaved repeats through `tools/gpu-lock.sh`, the
shipped Tree of Life project at 1920x1080, Realtime tier. The vortex is the whole of this pass:
the scene has `volumeDensity 0`, so with `radius` at 0 there is no `volume.march` timestamp at all.

| arm | march | vs shipped |
|---|---|---|
| half res, 32 steps (**shipped**) | 8.13 | 1.00 |
| quarter res, 32 steps | 4.39 | 0.54 |
| full res, 32 steps | 21.10 | 2.60 |
| half res, 128 steps | 27.85 | 3.43 |
| **quarter res, 128 steps** (the proposal) | **15.07** | **1.85** |
| quarter res, 256 steps | 26.21 | 3.22 |

Step ladder at half resolution, four repeats, spreads under 0.5 ms except one 21.8 outlier at 48:

| steps | 32 | 48 | 64 | 96 | 128 |
|---|---|---|---|---|---|
| march | 8.72 | 12.45 | 14.55 | 20.97 | 27.79 |

That is `2.7 ms + 0.196 ms per step`, to better than 3% at every rung.

**Quartering the pixels saves 46%. Quadrupling the steps costs 219%.** Swapping one for the other
is 1.85x more expensive than shipping, not "about the same". The reason is the 2.7 ms that does
not move with pixels: at 480x270 there are 130k pixels and an M2 Max is not saturated, so
throughput *falls* as resolution does -- 1.06 G samples/s at quarter res against 3.15 at full.
**Reducing resolution buys you the parallelism you stop using.**

This also re-confirms ADR-143's first reopening trigger is still shut: this march is not
pixel-proportional, so temporal reprojection's ceiling argument stands.

### 3. Most of the grain is the march's own step jitter, not fBM aliasing

This is the load-bearing one.

`cloudNoise` (below) sets the weight of the whole fBM stack. At 0 the density is the macro envelope
and nothing else -- an analytic, perfectly smooth field with no noise in it anywhere. Rendered at
32 steps it is **still speckled**, and the speckle is the ray-march's per-pixel start jitter, which
at 4000 m over 32 steps is +/-125 metres of random offset on a field that varies over about a
hundred.

ADR-389's metric (mean |image - 3x3 box blur|), on the band of the frame the storm occupies, on a
field with zero noise in it:

| arm | grain |
|---|---|
| macro field, 32 steps | 2.145 |
| macro field, 256 steps | 0.839 |

**A 61% fall with no noise present at all**, and the residual is the star field and the aurora,
which are real content. The two frames say it more plainly than the number: salt-and-pepper at 32,
clean at 256.

ADR-389 is not wrong -- aliasing is real and its step ladder measured it. But it measured a field
whose every feature *was* noise, so the two causes were inseparable there. Separated, jitter is the
larger share, and it has a different and much cheaper set of fixes: the jitter's amplitude is a
free parameter that nobody has ever varied, and it exists to trade banding for noise on a field
that used to have no smooth structure to band.

### 4. The hero shot does not contain the cyclone, and that is geometry

| | |
|---|---|
| camera distance to the vortex axis | **214.6 m** |
| mouth radius | **200.0 m** |
| camera pitch | 8.80 deg down, 36 deg vertical field |
| frame spans depressions of | +9.2 to **-26.8** |
| the mouth's CENTRE is at | **-28.3** -- below the bottom of the frame |
| its near rim | -82.8 -- far below it |
| its far rim | -15.5 -- the only part of the mouth in shot |

The hero camera stands **14.6 metres outside its own funnel's lip**, level with the mouth plane.
An eye, an eye wall and spiral bands are features of the *horizontal* plane; from a camera 8.8
degrees above that plane they are edge-on or out of frame. What the shot contains is the far lip
of the funnel seen edge on, which is exactly the "band of atmospheric depth" ADR-371 described and
ADR-374 recorded as an edit decision.

**So the brief's §0 image -- "an enormous physical volume of atmosphere rotating beneath me, and
if I fall into that eye I may disappear into it" -- is not reachable from the shipped camera by any
amount of work on the field.** Worse, the island's radius is about 85 m against a 200 m mouth, so
from any camera high enough to see the eye, the island hangs directly over it.

## Decision

**1. The macro structure goes in the envelope, and it is analytic.**

§7-§11 as trigonometry and smoothsteps, above the early-out and below any noise -- the brief's
hierarchy written as control flow. An eye (`innerVoid` given a wall, not a second radius),
an eye wall (`eyeWallWidth`, `eyeWallGain`), and logarithmic spiral bands at three nested scales
from the closed form `theta - ln(r)/b`, with `b` packed as the cotangent of an artist-typed pitch
angle. Band-limited by construction, so it survives 125-metre samples; free, so it does not move
the measurement above.

`cloudNoise` is the weight of the whole fBM stack against a flat field of the same mean: §5's
diagnostic and §53's failure test are the same control, which is why it is one parameter and not a
code path.

**2. No separate pass at reduced resolution.** Measurement 2. If that pass is ever built it must be
justified by something other than the step budget, because reduced resolution does not buy steps
here.

**3. The next lever is the jitter's amplitude, then the tier's step count -- not a new architecture.**
In that order, because measurement 3 says the jitter is the larger share and its amplitude is free.
A march over a smooth macro field has banding to trade *back*: full-step jitter was the right
default when the field had no smooth structure in it and is not obviously right now.

If that is not enough, `volumeStepScale` is already a tier setting and the deliverable is a render:
Offline can afford steps that Realtime cannot, and `2.7 + 0.196 * steps` says exactly what they cost.
Making each sample cheaper -- a density cache in a 3D texture, §59 -- is the lever after that, and
it is the one that moves `0.196` rather than the step count. **ADR-032's grid simulation is not that
cache**: it is a semi-Lagrangian *simulator* with its own sub-step determinism model, sampled through
a storage-buffer table with software trilinear and an 8 MB budget for the whole scene, and it is
unreachable (nothing can create a grid). It is worth reaching for later, for §20's "temporal
evolution that is not a rotating texture", which advection genuinely is -- not as a bake.

**4. The camera is a decision for the owner and it cannot be engineered around.** Measurement 4.
Either the film cuts to a shot that looks down into the funnel, or the vortex stops being a
cyclone-from-above and becomes something a level camera can read. Both are legitimate; neither is
this branch's to choose, and building detail before it is chosen is building for a frame nobody
may render.

## Consequences

- **`packVortex` had exactly one caller: its own parity test.** `volume_renderer.cpp` kept a
  hand-written copy of the same clamps, so the bytes the shipped frame marched were produced by a
  path nothing tested. The parity test proved the CPU and the shader agree; nothing proved either
  agreed with the renderer. The geometry slots come from `packVortex` now. This is ADR-401's shape
  one file over, and the pair of them is a pattern: a test can be green about a path nobody renders.
- **`sampleVortex`'s velocity has no consumer at all.** ADR-388 built it so particles could stop
  approximating the funnel with ADR-380's attractor-plus-orbit, and they still do. §18/§19/§34's
  "one flow field, two consumers" currently has zero.
- **The first eye design made `innerVoid` a dead slider**, and the parity test's per-field
  reachability probe reported it as `changing innerVoid moved 0 of 160 GPU samples` before anybody
  looked at a picture. That probe is ADR-401's and it has now earned its keep twice.
- **`eyeWallGain` raises the field's mean on purpose and is not normalised away.** A storm with an
  eye wall holds more air there; normalising it would make the slider unable to change the picture's
  weight, which is half of what it is for. `density` is re-tuned beside it. The *band* term is
  written `1 + depth * cos(...)` so its mean over angle is exactly 1 at any depth -- ADR-389's
  family rule, obeyed where it applies and declined where it does not.
- **Main's working tree is 3.78% of pixels away from main's HEAD** on the hero frame, because five
  branches are mid-merge there and the changes are staged rather than committed. A branch cut from
  the commit cannot see them, and comparing against a build of that working tree is not a control.
  This cost one wrong conclusion here before the real baseline was built.

## Revisit when

- The jitter amplitude has been measured. This ADR predicts a large fall in grain at zero cost and
  should be held to it; if the prediction fails, measurement 3's attribution is wrong and the
  conclusions that rest on it go with it.
- The camera question is answered, at which point the detail stages (§12-§17, §26-§29) have a frame
  to be judged in.
