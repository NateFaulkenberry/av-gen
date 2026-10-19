# ADR-1500: An effector's axis can be the point's own

- Status: Accepted (art/generative-pass)
- Amends ADR-025 (effectors) and ADR-1121 (the GPU's effector blends). Additive: an effector without the flag
  serialises, hashes, packs and runs exactly as before.
- Found by: the Generative pass (`docs/prototypes/generative-pass/00-brief.md`, Part 4). Phase Hall's blades move from
  four flat walls to the inside of a sphere: each blade is placed with its own orientation (a `points` distribution,
  ADR-1118), its long axis along its meridian and its face toward the centre.

## Problem

A scalar field's PositionOffset moves every point along one `axis` in the object's frame, and a Rotation turns every
point about one `axis` in the object's frame (`rotation = angleAxis(axis, s k) * rotation`). On a flat wall every blade
shares its hinge, so that was enough. On a sphere no two blades share a hinge or a normal: a swing about the object's
+Y turns a blade at 50 degrees latitude on a cone instead of about its own long edge, and a displacement along +Y
cannot push a membrane outward everywhere. A vector field gives a direction per point but its magnitude is the
direction's, so it cannot carry a travelling wave's amplitude along a radial axis without a second field and a
compound (two of the sixteen GPU field slots for every such displacement).

## Decision

`effectors[].local: true` reads a scalar field's `axis` in each point's own frame:

```text
PositionOffset : raw = rotate(rotation, axis) * s * strength      (blend as before)
Rotation       : natural = rotation * angleAxis(axis, s * strength)  (the point turns about its own axis)
```

Vector fields ignore it (their vector is the axis), and so does every other op. The CPU reference
(`spatial::applyEffectors`, `applyEffectorsToRecords`) and `shaders/points.wgsl` implement it together; the flag
rides in `EffectorGpu::scaleAxisPad.w`, which was padding. JSON writes `"local": true` only when set, and the
structural hash includes it only when set.

## Consequences

- A membrane of panels can be displaced along each panel's normal by one scalar field, and blades placed in any
  orientation can swing about their own hinges. Phase Hall's sphere uses both.
- Order matters, as it always did: a local offset after a local rotation moves along the turned axis.
- Tests: `A local axis is the point's own: offset along it, turn about it (ADR-1500)` (`[spatial][effectors]`: the
  offset, the turn, vector fields unchanged, JSON, hash, packing) and `Local-axis effectors agree with the CPU
  reference under every blend` (`[gpu][points][effectors]`: every blend, varied point rotations, CPU against GPU).
