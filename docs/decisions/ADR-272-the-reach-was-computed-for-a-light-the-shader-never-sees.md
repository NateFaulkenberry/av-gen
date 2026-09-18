# ADR-272: The reach was computed for a light the shader never sees, and the froxel look-up had no reference

**Status:** Accepted
**Date:** 2026-09-18

The Lighting Lab (lab #7 of ADR-261's suite; `docs/lighting-lab/README.md`). Three findings, one
promotion, and one thing deliberately not attempted.

---

## 1. `lightInfluenceRadius` answered a question about a different light

A local light is evaluated where the froxel pass put it, and nowhere else. That makes
`rendering::lightInfluenceRadius` a **hard edge**: past it the light is in no cluster, so whatever
it was contributing stops within the width of one froxel. The function's job is therefore to
guarantee that what stops there was already negligible.

It computed the reach from `color * intensity`. The shader is handed `colorIntensity`, which
`packLight` builds as `color * colorTemperatureToRgb(temperature, tint) * intensity` — and then,
for Rect, Disk, Tube and Sphere, multiplies again by the emitter's area, because those four kinds
carry nits over the emitter rather than candela. Two factors, both missing:

* `colorTemperatureToRgb` is normalised to **luminance** 1, so a channel is not. A 2000 K practical
  has a red channel around 1.6, and its reach was short by the square root of that.
* An emitter's area is not a correction, it is the unit conversion. `LightRig::expand` already
  divided by it (`emitterArea`, private to `light_rig.cpp`); nothing multiplied it back.

Measured over ten deliberately awkward lights, as the radiance each still carried at the reach it
was given, against the 0.004 cutoff the function is written to:

| light | radiance at its reach | over |
|---|---|---|
| white point at the origin (**control**) | 0.00400 | 1.0x |
| point at 2000 K | 0.00952 | 2.4x |
| point at 11000 K | 0.00563 | 1.4x |
| 6 x 4 m rect at 2 nits | 0.07364 | 18.4x |
| 2.5 m disk at 3 nits | 0.07126 | 17.8x |
| 1.4 m sphere at 5 nits | 0.02368 | 5.9x |
| 8 m tube at 4 nits | 0.00757 | 1.9x |

The white point light at the origin is the control and it passed throughout. Six of ten failed,
and the error is not a rounding error: an area light's radiance where its reach ran out is
`cutoff * area / pi`, which is **independent of its intensity** — the reach and the radiance scale
together — so the defect is the same size for a dim panel and a bright one, and it scales only with
how large the emitter is. It runs the other way too: an 8 cm x 5 cm rect at 40 nits was given four
times the reach it can light, spending froxel slots that then push a real light out of a crowded
cluster's thirty-two.

**Who this reached.** Not rigs — `LightRig::expand` sets `range = distance * 6` on every
non-directional light — and not the procedural ecology, which sets `range = radius * 4`. An
explicit range wins, and the shader's own window has closed to zero by the time it runs out, so
that path was always consistent. What reaches the heuristic is a light imported from glTF, where
KHR_lights_punctual's `range` is optional and usually absent, and any light built in code.

`emitterArea` is promoted out of `light_rig.cpp`'s anonymous namespace into `scene_types.hpp`,
beside `colorTemperatureToRgb`, which is the same kind of quantity in the same place: the physics
of a light rather than of a renderer, needed on both sides of the scene/rendering boundary. One
area, not two copies of it.

## 2. `packLight` refused a broken light and the cluster uniform took it anyway

`packLight` drops a light whose position, colour, intensity or range is not finite, logs the value,
and uploads black — deliberately, because a light nobody can see is a missing light somebody can
look for, and a light that quietly became a different light is a wrong picture nobody can explain.

But `SceneRenderer::updateLights` asks `lightInfluenceRadius` **separately**, on the scene light,
for the froxel uniform's radius, and that function had no such refusal. A NaN intensity produced a
NaN radius and wrote it into the pass. Nothing visible happened, because `light.w <= 0.0` is false
for a NaN and `dot(delta, delta) <= w * w` is also false, so the light was assigned to nothing —
harmless by way of every comparison failing, which is not a property to rely on and not one the new
per-light report can rely on either. The same refusal now lives in both places, in the same words.

## 3. The froxel grid had a reference for its build and none for its look-up

`assignClusters` is the CPU reference for `shaders/clusters.wgsl`, and
`tests/rendering/test_shadows_gpu.cpp` checks the pass's lists against it index for index. That
covers **which lights reach a froxel**. It says nothing about **which froxel a fragment reads**,
because the build pass is indexed by its own invocation id: a grid whose rows ran the wrong way up
would pass that comparison exactly and light the wrong half of the screen.

The screen's y runs down from the top-left; the grid's rows run bottom-up in NDC order. The flip
between them is one line of `clusterIndexFor`, and it was the one line nothing could fail on. So:

* `ClusterGrid::clusterOf(screenUv, viewDepth)` is the missing reference, term for term.
* `tests/unit/test_lighting_lab.cpp` round-trips 150 off-centre view-space points through it into
  `bounds()`, and asserts separately that all 60 samples above the view axis land in the grid's
  **upper** rows — the containment check alone cannot fail on a flip, because `bounds()` would be
  consulted for whichever froxel the look-up chose.
* `tests/rendering/test_lighting_lab_gpu.cpp` holds the clustered path to the uniform fallback for
  an **off-centre point light**. The existing clustered comparison uses a directional light, which
  never enters the grid at all — this suite's centred-box fixture (ADR-182) one pass downstream.

The grid was correct. It is now checkable, which it was not.

## 4. A per-light view of the cap, because the per-cluster one cannot name the culprit

`ClusterOccupancy` (ADR-114) answers "is the thirty-two-light cap biting". It cannot answer "which
light is paying for it", and the answer matters because **the cap is resolved in light-buffer
order**: both `assignClusters` and `cs_build` fill a cluster's list in index order and stop, so the
light dropped from a crowded froxel is the one latest in the buffer, whatever its brightness or its
distance. `rendering::lightAssignments` is that, per light: the reach it was offered with, froxels
touched, froxels admitted, froxels crowded out. It is a transposition of the same lists rather than
a second opinion about them, and the test asserts exactly that — its totals are
`ClusterOccupancy::demand` and `::dropped`.

## 5. Two overlays, because nothing drew a light

`DebugViewOptions` had twenty-three switches and not one of them drew a light. `lights` draws each
light's position, direction, the **emitter the shading pass actually integrates** — a spot's two
cone angles, a rect's quad from `areaLightCorners`, a disk's equal-area square, a tube's capsule, a
sphere's ball — and the sphere of influence, which is the hard edge of §1 and the thing that could
previously only be found by noticing a line on the floor. `lightClusters` draws the occupied
froxels coloured by how many lights reach them, white at the cap.

`lightClusters` is deliberately off in `overlaysFor(LabId::Lighting)`: it covers exactly the pixels
a lighting question is about. Both are reachable from `--debug-draw`, which is the only way into an
overlay from a headless render.

## 6. Where the lab's measurements stop, and why the boundary is in code

The first version of the GPU reach probe passed with the defect in place. Two reasons, and both are
now written into the probe:

* **The post chain was on.** AgX plus the exposure meter compressed a 0.76 step in scene-linear
  radiance into 0.08 of display luminance. A lighting probe read through a tone curve measures the
  tone curve. `--disable post` is the boundary between this lab and the HDR Lab and the probe sets
  it rather than assuming it.
* **The emitter was too small.** `cutoff * area / pi` for a 6 x 4 m softbox is 0.031, which is under
  the hemispheric ambient floor `pbr_shade.wgsl` applies when a scene has no IBL — a hardcoded
  `sky = (0.10, 0.12, 0.20)` that no scene setting can reach.

And a third, which is the one worth keeping: **the probe compares two frames rather than looking for
a step inside one.** The arm is a 40 x 28 m panel with no range; the control is the identical light
with a 600 m range, whose own window is within 0.05% of 1 everywhere in shot, so the control is what
the arm would look like if its reach were right — measured, not asserted. Worst channel difference
before the fix **48** of 255, mean over the frame 0.261; after, 1 and 0.00034. Both are in the
assertion, because a threshold on the mean alone passes with the defect in place: the far half of a
corridor seen edge-on is a small share of the image.

## 7. What was not attempted

**A cone-aware or oriented influence volume.** `clusterTouchesSphere` is the only test the grid
makes, so a 16-degree spot is assigned to every froxel within its range in every direction,
including behind it, and the shader multiplies almost all of them by a spot term of zero.
`lightAssignments` is the instrument that would size that win before anyone wrote it. It is not
taken here because a tighter assignment test that is wrong in one corner is a missing light, and
this lab's job was to stop cutting lights off rather than to start.

**A per-light isolation arm.** `--disable` has arms for passes and `--quality-arm` for behaviours;
neither can render a frame with one light. It is the single most useful thing that could be added
next, because it would make "which lights reached this pixel" answerable from a frame rather than
from a diagnostic — which is what §37 asks for.

**A known-key check in `LightRig::fromJson`.** Reported rather than fixed. The spot angle is read
from `"cone"`; a rig that writes `"coneDegrees"` gets the 45-degree default and no error, because
the rig parser ignores unknown keys. This lab's own fixture was written with the wrong key first
and only reading the parser caught it. It is ADR-225 in an authoring format and it applies to every
key in a `.rig.json`.

---

## Consequences

* Area, Tube, Sphere and non-6500 K lights **without an explicit range** now reach further, so they
  are assigned to more froxels. That is what correctness costs and it is a count rather than a
  time: a rect light's touched-froxel count scales with its reach squared and the reach now scales
  with the square root of the emitter's area. Rigs, the ecology and every light with an authored
  range are bit-identical.
* `scene::emitterArea` is public. `light_rig.cpp`'s private copy is gone rather than duplicated.
* `ClusterGrid::clusterOf` and `rendering::lightAssignments` are additive; nothing consumes them in
  the render path.
* `DebugViewOptions` gains `lights`, `lightClusters` and `lightClusterSlice`, all defaulting off.
