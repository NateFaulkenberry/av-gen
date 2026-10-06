# ADR-1163: A scalar grid can saturate

- Status: Accepted (proto/digital-mosh)
- Amends ADR-032 (simulated grids) and ADR-1122 (a grid's behaviour is parameters).
- Found by: DIGITAL MOSH. Its contagion is a scalar grid injected at a source, and every consumer needs it as a
  0-to-1 amount: material masks, an SDF's recession, block scales.

## Problem

Injection adds `injectRate × dt × field` every step with no bound, so a source cell's value grows for as long as
the source runs. Every consumer then has to clamp the value itself, and one of them cannot:

- **A material program** can `remap` with a clamp.
- **A procedural effector** cannot. Scale × Multiply by an unbounded field makes a block 5× its size.
- **A field cannot clamp another field on the GPU.** A compound inside a compound evaluates as 0 there
  (`docs/gpu-fields.md`, "one level"), so `min(stain, 1)` over a `stain × climb` compound returned the wrong
  value. Measured in DIGITAL MOSH: the dream's tree was fully voxelised before any stain existed.

## Decision

A scalar grid has a `ceiling`. Injection cannot push a cell past it. Advection is a semi-Lagrangian gather and
diffusion an average, so neither can exceed the largest value already present, and the grid stays at or below
the ceiling everywhere.

- **The default is 0, unbounded:** every existing grid and its hash are unchanged. The ceiling is hashed and
  written to JSON only when set.
- **Reach:** the `ceiling` key in JSON, and the `grid/<name>/ceiling` parameter, a behaviour parameter under
  ADR-1122, so routes, states and MIDI reach it without reseeding.
- **GPU:** the value rides in `SimUniforms.agentDeposit.w`, a lane that only agents grids used, and they leave
  `w` at 0. So there is no new binding and no layout change. `cs_inject` clamps scalar grids.
- **CPU:** `GridField::step` clamps the same way, so the reference and the GPU agree.

## Consequences

- A contagion, a stain or a heat map is a bounded amount, and every consumer reads it directly.
- Test (`[simulation][gpu]`): a source injecting one unit per step under a ceiling of 0.6, with wind and
  diffusion. The GPU matches the CPU within 1e-3. The maximum is at most 0.6 and above 0.55, so the clamp is
  what held it. Without a ceiling, the same source exceeds 1.5.

## Rejected alternatives

- **One more compound level on the GPU** (an unrolled second level in `fields.wgsl`). The field evaluator's size
  sets the register allocation of every material program that has a `field` op (the "loop body" note in
  `material.wgsl`). A second inlined level is a measurable cost for every program, paid to fix one case.
- **A per-field ceiling** (clamping any field's result). `FieldGpu` has no spare lane, so growing it changes the
  6,192-byte field block and every shader that mirrors it.
