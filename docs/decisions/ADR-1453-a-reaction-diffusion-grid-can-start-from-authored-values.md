# ADR-1453: A reaction-diffusion grid can start from authored values

**Status:** Accepted (proto/generative-rd)
**Date:** 2026-10-17
**Context document:** `docs/prototypes/generative-rd/C-scene-plan.md` (RD Pins, the cold-start pre-roll).
**Implemented by:** `GridField::validate` in `src/spatial/grid_field.cpp`, which now accepts `initial` for
`reactionDiffusion`. `reset()` already used authored values whenever their length matched.
**Tests:** `tests/unit/test_generative_grids.cpp`, "A reaction-diffusion grid may start from authored values; without
them it starts as before".

## Context

A Gray-Scott pattern needs a few thousand model steps to form from seeds: about 10 s live at RD Pins' 720 steps a
second. A live scene therefore opened on bare ground with blobs. The engine-free remedies fail:
- a larger time step is unstable (D·dt is already 0.3, against a limit of 0.375);
- denser seeding is fixed by the world scale of the seed noise.

ADR-1282 already lets a scalar or vector grid start from authored values.

## Decision

A `reactionDiffusion` grid may carry `"initial"`: `components() = 2` floats per cell (A, B) in the storage order.
The rules are ADR-1282's:
- the length must match exactly, or the grid is refused;
- the values must be finite;
- the values are hashed into the layout, so changing them re-seeds the grid.

**Reset and seek.** `reset()` returns the authored state. `rendering::Simulation` uploads `reset()` on its first frame,
on a layout change, and on a backward seek with no checkpoint to restore. So every one of those paths lands
deterministically on the authored state and replays from it. A checkpoint restore is unchanged: it restores the
state the grid had stepped to from that start.

**Without authored values,** nothing changes: the canonical seed and the hashes are the same, and no new key is
written.

## Consequences

- **Size.** The authored state costs 2 floats per cell in the scene file. RD Pins' 320² plane, written to 3 decimals,
  is about 1.3 MB.
- **The generator owns it.** `tools/rd_pins/build.py` regenerates the state by running the same Gray-Scott model in
  numpy, so it is never edited by hand.
