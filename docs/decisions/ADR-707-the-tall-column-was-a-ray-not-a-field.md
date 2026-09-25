# ADR-707: The Tall Column was missing its middle because the march's rays were wrong, not the field

**Status:** Accepted
**Date:** 2026-09-24
**Resolves:** ADR-580 §11.3 ("Not diagnosed further, deliberately"), `docs/tornado-handoff.md` §5.2,
`docs/design/effect-library/tornado-fog-production-pass.md` §2 item 2.
**Implemented by:** `shaders/volume.wgsl` (`viewRayDirection`, used by `fs_volume`)
**Tests:** `tests/rendering/test_tornado_structure_gpu.cpp`, "the Tall Column is continuous from its
cloud to its foot"

---

## Context

`_tc-4-tall-column` rendered as a block of wall cloud, a block of foot, and nothing between
(sequence hash `eadf0dd2a8549880`, reproduced today byte for byte). ADR-580 §11.3 had ruled out
screen resolution, noise and step count, localised it to `lean` + `wobbleAmount`, and left two
named hypotheses: **a hole in the field**, or **a contrast floor** (the displaced column too thin to
read). The plan asked for a CPU probe of `tornado::evaluate` down the displaced axis to decide
between them before any fix.

## The measurement

**The CPU field has no hole, and it is not a contrast floor.** Probing `_tc-4`'s packed uniforms at
t = 6 s, Detail 0, with a 1 m step along camera rays from the review camera:

| h | peak shape | camera-ray optical depth, as authored | lean 0 | lean 0, wobble 0 |
|---|---|---|---|---|
| 0.20 | 1.776 | 6.41 | 6.41 | 6.39 |
| 0.40 | 1.773 | **6.81** | 6.82 | 7.00 |
| 0.60 | 1.818 | 8.56 | 8.58 | 8.67 |

An optical depth of 6.8 is a transmittance of 0.001: the column at h = 0.4 is opaque in the field,
the same with or without the lean. A CPU transliteration of the march at the scene's own 256 steps
(the loader clamps the authored 296) draws the whole column. So both hypotheses were wrong, and the
fault was between the field and the frame.

**Then the render said where.** Six GPU arms, each one frame at `--range 6:6`:

| arm | result |
|---|---|
| as authored | broken, `eadf0dd2a8549880` |
| `volumeJitter` 1, `volumeSteps` 128, `volumeMaxDistance` 20000 | all still broken |
| lean 0 / lean 0 + wobble 0 | `bdce04eb7412fe68` / `6270952a9ecae4bb` -- ADR-580's own hashes, and **both are pixelated into ~60 px columns too**, only less visibly |
| **the same storm and camera translated to x = 0** | **the whole column, smooth, with its striations** |

A defect that disappears when the scene is moved rigidly to the origin is a precision defect, and
the only thing in the march that depends on absolute position is how the ray is built:
`direction = normalize(worldAt(ndc, 1.0) - worldAt(ndc, 0.0))`. The composition derives the far plane
as fifty times the orbit radius (`composition.cpp`), so this camera has near 0.5 m and far 212 km. At
`z = 1` the inverse view-projection's `w` is 4.8e-6, the difference of two numbers near 1 in f32, and
its rounding error multiplies the camera's world position into the far point. Measured on the CPU in
f32 against a double reference, over the frame:

| | worst direction error | lateral miss at 4.2 km |
|---|---|---|
| `worldAt` difference, storm at x = 7857 m | 1.3e-3 rad | 5.6 m |
| `worldAt` difference, storm at x = 0 | 3.0e-5 rad | 0.13 m |
| camera basis (this ADR) | 1.4e-7 rad | 0.001 m |

On the GPU the error comes back **piecewise constant** across the screen, so a whole column of pixels
marches one ray. A 110 m funnel 4 km away is about 26 px wide at 1280x720; blocks of ~60 px either
hit it or miss it. `lean` and `wobble` mattered only because they move the column off the one screen
x where the centre block's ray happened to pass through it -- the axis displacement was the witness,
not the cause.

## Decision

`fs_volume` builds its ray direction from the camera's own orthonormal basis and the projection's
two scale factors (the lengths of the view-projection's first two rows), not from the difference of
two unprojected points. The origin is unchanged (`worldAt(ndc, 0.0)`, where `w` is well conditioned),
so the march's step positions are unchanged apart from the direction they are laid along. No step
count, interval, bound or sampling rule moved; this is not the step redistribution (plan item 3,
not this change).

## Evidence

- **Five of the seven showcase variants were affected, not one.** Before/after contact sheet
  `sheet-item2-showcase.png`: the classic cone, thick cloud, rope and cosmic storm were all visibly
  pixelated; the rope was in pieces. After, all seven are smooth and the rope is continuous at 256
  steps. **The dust devil and the wedge were not visibly affected** (a close camera, and a feature
  wider than a block). ADR-580 §8.8's "six of seven read well" and the Wedge's "intrinsic weakness"
  were both judged through this defect and may deserve a second look; this ADR does not re-judge them.
- `_tc-4` after: `dc2f5ff819d4dbee`, whole column from cloud to foot.
- **What it moves elsewhere.** `volumetric-atmosphere-lab` (near the origin): byte-identical.
  `glowmere-valley-2` at 6 s: 1,082 of 921,600 pixels differ, 90 by more than 2 levels, mean 0.0008
  levels -- samples on silhouettes landing a fraction of a degree differently. Deterministic across
  two renders.

## Test

`the Tall Column is continuous from its cloud to its foot` renders `_tc-4` through the engine with
and without the tornado and counts the rows between the cloud and the foot where the storm visibly
changes the frame. **Fixed: 100%. With the old ray reconstruction restored: 0.93%, and the case
fails.** Its control renders the same storm moved to the origin (100% both before and after), so a
future failure that is not this defect is told apart from one that is.

## Consequences

- **The same `invViewProj`-at-`z = 1` pattern is in `atmosphere.wgsl`, `skybox.wgsl`,
  `sdf_raymarch.wgsl`, `distortion.wgsl` and `post.wgsl`.** Each builds a view ray the same way and
  each will carry an error that grows with distance from the world origin when the far plane is
  large. A sky direction tolerates a milliradian; an SDF raymarch against a small object far from
  the origin may not. Not changed here -- recorded for whoever owns those passes.
- `volumeSteps` is clamped to 256 at load and again in the renderer. `make_tornado_showcase.py`
  asks for up to 512 (the rope derives 462), and ADR-580 §11.3's "512-step arm" was a 256-step arm.
  Recorded, not changed.
