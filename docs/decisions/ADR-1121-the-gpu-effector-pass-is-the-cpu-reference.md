# ADR-1121: The GPU effector pass is the CPU reference, for every op and blend

- Status: Accepted (gpu/productionization; the owner ruled 2026-10-05: "fix it now, don't worry about the
  shipped scenes")
- Corrects ADR-025's implementation. It does not change ADR-025's decision: "the same list runs in
  `points.wgsl`", with `spatial::applyEffectors` as the reference and "GPU-identical evaluation".
- Found by ADR-1116's per-record audio test.

## Problem

ADR-025 defines effectors once, on the CPU (`spatial::applyEffectorsToRecords`, with `blendValue` and
`blendRotation`), and promises the GPU pass evaluates the same thing. It did not. A test that runs every
op the GPU executes, with every blend, as the only effector on its own object
(`tests/rendering/test_effector_blend_parity_gpu.cpp`) found **17 of 36 (op, blend) pairs disagreeing**
before this fix:

- **Scale + Add**, the default blend. The GPU added the target scale to the existing one
  (`scale + scale·(1 + s·k)`); the reference returns the target (`scale·(1 + s·k)`, "scale multiply"
  in ADR-025). Scale + Multiply disagreed the same way.
- **Position offset ignored its blend**: always `p + raw`.
- **Rotation**: Replace and Mix rotations differed.
- **Colour, emission and density**: their Multiply, Replace, Min, Max and Mix blends differed in part.

The old parity test gave Scale a Mix blend and everything else Add. Those are the pairs where the two
sides happened to agree, so nothing caught it.

## Decision

`shaders/points.wgsl` `cs_effectors` now implements the reference operation for operation:

- the same raw and natural values per op;
- `blend(existing, natural, raw, weight)` as `blendValue` for every vector and scalar lane;
- `blendRotation` for rotations, including glm's slerp, transcribed;
- a vector field's direction rotated into object space before use, as the reference's `sampleAt` does.

There is no compatibility shim and no "legacy blend" flag (ADR-441).

## Consequences

- **The new test fails before the fix (17 of 36) and passes after (0 of 36)**, within 1e-4. The existing
  points, fields, audio-field and generator tests still pass.
- **Visual change in four shipped scenes.** They author `scale` effectors with `add` (the default) or
  `multiply`, and were tuned on the GPU's old reading:

  | scene | effectors | change |
  |---|---|---|
  | `examples/infinite/infinite.scene.json` | scale/add ×4, scale/multiply ×1 | effected objects draw at the authored scale × (1 + s·k) instead of about twice that |
  | `examples/machine/machine.scene.json` | scale/add ×2 | same |
  | `examples/reassembly/reassembly.scene.json` | scale/add ×1 | same |
  | `examples/hyperspace/hyperspace.scene.json` | scale/multiply ×1 | scale = existing × s·k·axis (was existing × target) |

  No shipped scene uses any other pair that changed. Their art may want re-tuning (for example, a
  scale-effected layer's source scale doubled); that is left to the owner.
- `spatial::applyEffectorsToRecords` is now a reference that is actually checked for every pair the GPU
  runs.

## Rejected alternative

- **Change the CPU to the GPU's reading.** Rejected: the GPU's Scale + Add doubles a scale that the field
  does not touch (s = 0), which contradicts ADR-025's "scale multiply". The CPU reading is what ADR-025
  defines, what its unit tests assert, and what `graph/builtin_nodes.cpp` already produces.
