# ADR-1672: A Hinge deformer turns a soft-masked part about a pivot

- Status: Accepted (scene/talking-heads, 2026-10-25)
- Code: `src/scene/procedural.{hpp,cpp}` (`DeformerKind::Hinge`, `applyHinge`), `shaders/procedural.wgsl` (`hingePoint`),
  `src/rendering/procedural_renderer.{hpp,cpp}` (packing), `src/ui/ui_logic.hpp` and `src/ui/world_panel.cpp` (panel rows).
- Tests:
  - `tests/unit/test_procedural.cpp` `[adr1672]`: a rigid turn at the mask centre; nothing moves above the slit or
    outside the mask; the ease-out along the hinge axis is monotone; amount 0 is bit-identical.
  - `tests/rendering/test_procedural_gpu.cpp` `[adr1672]`: on the GPU, amount 0 is the identity and an angle moves
    the masked part.
  - `tests/unit/test_deformer_panel.cpp`: the panel lists the kind and every control moves the point.

## Problem

The owner rejected the stop-3 Talking Heads mouth: it was a separate jaw piece that dropped, and open it read as a
cut-out. A real mouth opens because the lips part. The corners stay attached and the cheeks stretch, all on one
surface. No deformer could turn part of a mesh and blend it into the rest. Bend curves the whole span along an axis.
Twist turns about the axis by height. A rigid node turn needs a separate piece, and the seam shows.

## Decision

Add `DeformerKind::Hinge`, appended as kind 8. It turns a point by `amount` radians about `axis` through `center`, the
pivot, weighted by `w = slit x mask`:

- **The frame.**
  - `axis` is a.
  - forward d is `displacementAxis`, made perpendicular to a.
  - up is u = cross(d, a).
- **The slit.** `slit = 1 - smoothstep(phase - e, phase + e, dot(p - c, u))`, with e = 0.004 x `falloff`. Only points
  below a plane at height `phase` turn. A mesh cut along that plane, which is what the lips are, opens cleanly.
- **The mask.** It is an ellipsoid centred at m = c + d x `speed` + u x `scale`, with radii (`frequency` along a,
  `falloff` along u and d). `mask = 1 - smoothstep(0.5, 1, |q|)`. Full turn in the inner half, easing to zero at the
  rim, so the corners stay attached and the cheek stretches.
- **The rotation.** Rodrigues about a through c, by `amount x w`. The CPU twin and the shader share the formula.

The GPU packing reuses the existing slots:

| GPU slot | Holds |
|---|---|
| params | frequency, speed, phase, falloff |
| extra.xyz | forward |
| extra.w | scale |

The world-space flag moves from +8 to +16 in both the packer and `procedural.wgsl`, because kind 8 is now taken. It is
the one place the code is decoded, and the CPU and GPU change together.

## Consequences

- **Off is byte-identical.** Amount 0 returns the point untouched on the CPU (tested bit-equal) and on the GPU
  (tested). No shipped scene uses the kind.
- **The flag renumbering changes no picture.** Every world-space deformer's code changes in the packer and the decoder
  together. The existing procedural CPU and GPU suites pass unchanged.
- **The `scale` parameter is signed for this kind.** The other kinds' `scale` is a size with a hard minimum of 0.001,
  which would have moved a mouth's mask from the chin to the pivot. `tests/unit/test_procedural_params.cpp` `[adr1672]`
  holds both ranges.
- **Normals:** the rotation is rigid where w = 1, so normals there are exact. In the ease band they follow the
  renderer's existing deformed-normal path, as for Bend and Twist.
- **Talking Heads uses one Hinge for the lips and lower face.** Its eyelids are their own small meshes, posed by node
  rotation.
