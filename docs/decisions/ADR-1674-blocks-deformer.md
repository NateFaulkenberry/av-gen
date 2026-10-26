# ADR-1674: A Blocks deformer displaces whole cells of a surface (block corruption)

- Status: Accepted (scene/talking-heads-fx, 2026-10-25)
- Code:
  - `src/scene/procedural.{hpp,cpp}`: `DeformerKind::Blocks`, `applyBlocks`, `blockHash`.
  - `shaders/procedural.wgsl`: `DEFORM_BLOCKS`, `blockHash`, `blocksPoint`.
  - `src/rendering/procedural_renderer.cpp`: packing.
  - `src/ui/ui_logic.hpp`, `src/ui/world_panel.cpp`: rows and add-defaults.
- Tests:
  - `tests/unit/test_procedural.cpp` `[adr1674]`: a share of cells moves, along the axis only and by at most the amount;
    a cell moves as one; a new epoch moves a different set; amount 0 and share 0 are bit-identical.
  - `tests/rendering/test_procedural_gpu.cpp` `[adr1674]`: amount 0 is the identity on the GPU, and a displacement
    changes the image.
  - `tests/unit/test_deformer_panel.cpp`: the panel lists the kind, and its controls move the point.

## Problem

Talking Heads' codec-collapse profile (the owner's signal-corruption brief, `17-brief-signal-corruption.md`) needs a
bust to break into rectangular regions that are displaced, persist, and reshuffle on strong events.

No deformer could cut a surface into blocks:
- Sine, Noise and Displacement are continuous, which gives waves, not blocks.
- A per-pixel material program can recolour blocks but cannot move geometry.
- The renderer's temporal mosh (ADR-1049) is full-screen and per scene, not per entity.

## Decision

Add `DeformerKind::Blocks`, appended as kind 9.
- **The cells.** Space is cut into cells: `1/frequency` tall, and `scale/frequency` wide and deep.
- **Which cells move.** A share `falloff` (0..1) of the cells moves. The choice is an integer hash of the cell, the
  `seed` and the epoch `floor(phase)`, identical on the CPU and the GPU.
- **How far.** A moved cell moves by `amount x (h x 2 - 1)` along `displacementAxis`, where h is a second hash, so
  each cell has its own distance and sign.
- **The look.** Everything in a cell moves together. Triangles that cross a cell edge stretch, which reads as a smear.
  A whole step of the epoch reshuffles the set: a reconstruction failure.
- **The GPU slots.** params = (frequency, scale, phase, falloff); extra = (displacement axis, seed bits).

## Consequences

- **Off is byte-identical.** Amount 0 or share 0 returns the point untouched. No existing scene uses the kind, and no
  other kind's packing or decoding changes.
- **What this is.** It is block displacement, not temporal datamosh: no pixel carries an earlier frame. The
  persistence comes from the share and amount decaying slowly, and from the epoch changing only on strong events.
- **Talking Heads applies it in world space** to every piece of a bust and to its face pieces (eyes, lids, mouth
  cavity). A block moves the face and what sits on it together.
