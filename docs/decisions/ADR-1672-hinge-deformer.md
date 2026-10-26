# ADR-1672: A Hinge deformer turns a soft-masked part about a pivot

- Status: Accepted (scene/talking-heads, 2026-10-25)
- Code: `src/scene/procedural.{hpp,cpp}` (`DeformerKind::Hinge`, `applyHinge`), `shaders/procedural.wgsl` (`hingePoint`),
  `src/rendering/procedural_renderer.{hpp,cpp}` (packing), `src/ui/ui_logic.hpp` and `src/ui/world_panel.cpp` (panel rows).
- Tests:
  - `tests/unit/test_procedural.cpp` `[adr1672]`:
    - a rigid turn at the mask centre;
    - nothing moves above the slit or outside the mask;
    - the mask's ease-out is monotone;
    - the lips part in a lens (widest in the middle, the corners attached, nothing past them at the slit's height);
    - past the corners the turn grows smoothly toward the chin;
    - amount 0 is bit-identical.
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
pivot, weighted by `w = slit x lip x mask`:

- **The frame.**
  - `axis` is a.
  - forward d is `displacementAxis`, made perpendicular to a.
  - up is u = cross(d, a).
  - m = c + d x `speed` + u x `scale` is the mask's centre (the chin).
  - dy = dot(p - c, u) - `phase` is the height above the slit.
  - lat = |dot(p - m, a)|; r = `falloff`; cw = `frequency`, the cut's half-width (the mouth's corners).
- **The slit.** `slit = 1 - smoothstep(-e, e, dy)`, with e = 0.004 r: a step, hidden in the mesh's own cut (the lips).
- **The lips.** `lip = taper + (1 - taper) x smoothstep(0, r/4, -dy)`, with `taper = 1 - min(lat/cw, 1)^2`.
  - Just below the slit, the turn tapers to nothing at the corners, so the lips part in a lens.
  - Past the corners, where the surface is not cut, nothing at the slit's height moves.
  - A quarter radius below the slit, the whole width turns (the chin), so the cheeks stretch rather than tear.
- **The mask.** `mask = 1 - smoothstep(0.5, 1, |p - m| / r)`: rigid inside half the sphere, easing to nothing at its
  shell.
- **The rotation.** Rodrigues about a through c, by `amount x w`. The CPU twin and the shader share the formula.

The first version had no `lip` term, an ellipsoid mask, and `frequency` as the mask's half-width. In the first
renders, points past the corners just below the slit's plane turned while the connected points just above it did not,
so the chin folded. The opening was also uniform out to the corners, so it read as a notch. The `lip` term replaced it
before anything shipped.

**The purse** (`tension`, added after the owner's stop-4 note: "reshape fully open mouth to more of an 'oh' sound /
look rather than a half smile"). 0 is off and skipped, so a hinge without one is byte-identical. With purse > 0:
- **The lens rounds toward an ellipse.** `taper` becomes `mix(1 - u^2, sqrt(1 - u^2), min(purse, 1))`, giving steep
  sides and a round bottom.
- **The lips move, in proportion to the angle t = amount x purse** (so shut is still sealed, and half open is half the
  pursing). With near = 1 - smoothstep(0, r/4, |dy|):
  - **drawn in:** along a, `-sign x min(lat, cw) x 0.5 t x near`, easing out from cw to 2 cw;
  - **upper lip arched:** above the slit only, along u, `0.20 t r x taper x (1 - smoothstep(0, r/4, dy))`;
  - **pushed forward:** along d, `0.10 t r x near`, easing out to 2 cw.
- **On the GPU,** it travels as the forward vector's length past 1, because every slot of the uniform was already
  used. The shader normalises forward anyway, and a purse of 0 packs the vector unchanged.
- **Tests.** `test_procedural.cpp` `[adr1672]` checks drawn in, arched, forward, linear in the angle, still sealed
  shut, nothing far away moving, and purse 0 bit-equal to none. `test_procedural_gpu.cpp` checks that a purse
  changes the image.

The GPU packing reuses the existing slots:

| GPU slot | Holds |
|---|---|
| params | frequency, speed, phase, falloff |
| extra.xyz | forward |
| extra.w | scale |

The world-space flag moves from +8 to +16 in both the packer and `procedural.wgsl`, because kind 8 is now taken. It is
the one place the code is decoded, and the CPU and GPU change together.

**The mask's ease** (fx1, the owner: "I can still see visible seams under the chins").
- The mask was rigid inside half its radius and eased over the outer half. In profile, the turned lower face met the
  still cheek at a visible crease.
- It now eases over everything past a quarter of the radius: `smoothstep(0.25, 1, |p - m| / r)`.
- Talking Heads' mask is now smaller and higher (`make_bust.py` `HINGE_DOWN`, `HINGE_RADIUS`), so it ends above the
  head piece's lower edge and never turns the head where the still neck meets it.

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
