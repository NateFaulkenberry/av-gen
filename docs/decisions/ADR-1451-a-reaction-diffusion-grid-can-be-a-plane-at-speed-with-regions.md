# ADR-1451: A reaction-diffusion grid can be a plane, at speed, with regions

**Status:** Accepted (proto/generative-rd)
**Date:** 2026-10-17
**Brief:** `docs/prototypes/generative-rd/00-brief.md` §4 (Experiment C: reaction-diffusion).
**Implemented by:** `GridField::{speed, iterations, regimeField, feedB, killB}` and `reactionPlane()`, the CPU step, the
validation and the JSON in `src/spatial/grid_field.{hpp,cpp}`. Also `cs_reaction` in `shaders/simulate.wgsl`; the RD lanes
and the per-sub-step dispatch count in `src/rendering/simulation.cpp`; and `grid/<name>/{feedB,killB,speed}` in
`src/scene/grid_params.cpp`.

**Tests:**
- `tests/unit/test_generative_grids.cpp` (`[rd]`):
  - a plane validates up to 1024²;
  - the defaults leave an existing grid's JSON unchanged;
  - `iterations` splits `speed` exactly;
  - the plane stencil feeds a diagonal neighbour 1/4 of an adjacent one;
  - a regime field of 1 equals the B regime;
  - a pattern forms and stays in [0, 1].
- `tests/rendering/test_generative_grids_gpu.cpp` (`[rd]`): a 64² plane with 12 iterations, a regime gradient and
  injection is within 1e-3 of the CPU, and then forms a pattern.

## Context

`GridMode::ReactionDiffusion` (ADR-032) is Gray-Scott in a volume of at most 128 per axis, with one explicit step
of dt = 1/simRate per sub-step. Three things stopped it serving as the brief's Experiment C:

- **Speed.** A Gray-Scott pattern takes thousands of unit steps to form. At one step of 1/60 per sub-step, it takes
  hours of song.
- **Detail.** A surface pattern wants 512² to 1024² cells, which is a plane, not a 128³ volume.
- **One regime.** Competing pattern regions, and music that "temporarily alters the local pattern regime" (§4.4D,
  §4.5), need feed and kill to vary per cell.

## Decision

- A RD grid with `resolution.y == 1` is a **plane**, up to `kMaxAgentGridResolution` (1024) on x and z. A plane
  steps with the isotropic 9-point Laplacian, `(4 Σ adjacent + Σ diagonal − 20 c) / 6`, so patterns do not align to
  the grid's axes. A volume keeps its 6-neighbour stencil and its 128 limit.
- **`speed`** is the model time per second of sub-steps, and **`iterations`** is the number of explicit steps each
  sub-step takes. Each step is `dt · speed / iterations`, one dispatch each.
  - A unit step per iteration is `speed = simRate · iterations`. For example, 60 × 16 = 960 gives 960 unit steps a
    second.
  - The explicit step is stable for D · dt < 0.375 on the plane. With the conventional D_A = 0.3 (Sims' Da = 1 in
    this stencil's units), a unit step is safe.
- **`regimeField`** names a scalar field. Each cell's (feed, kill) is blended toward **(`feedB`, `killB`)** by that
  field's value, clamped to [0, 1]. Regions can then hold different pattern families, and an onset, a MIDI note or
  a spectral band can change a region's regime for a while.
- **Injection** (`injectField`, into B) is unchanged. It is how music seeds new growth.
- **The defaults** (speed 1, iterations 1, no regime field) step exactly as before. None of the new keys is written
  or hashed until it is set, so an existing grid's JSON and checkpoint key are unchanged.
- **The RD uniform lanes** reuse the excitable grid's: `excite0 = (feedB, killB, speed, iterations)` and
  `exciteSlots.x` = the regime slot.

## Consequences

- **The stencil changes for a one-cell-thick volume.** A RD grid with `resolution.y == 1` now uses the 9-point
  stencil where it used the effective 5-point one. No scene or test on main had one.
- **Cost.** It grows with `iterations` × cells. A 512² plane at 16 iterations is about 4M cell updates a frame
  (measured in `docs/prototypes/generative-rd/performance.md`).
- **Regime evaluation.** The regime field is evaluated per cell per iteration. A cheap field is a plane, a
  gradient or a grid; an audio field with a long delay smoothing costs more.

## Rejected alternatives

- **Raising `simRate` instead of iterating.** Every sub-step packs a FieldBlock on the CPU (ADR-1119). At 960 Hz
  that is 16 blocks a frame of CPU work for no gain, and `maxSubSteps` (32) caps it anyway.
- **A separate "pattern" grid mode.** It would be the same state and kernel. Extending the existing mode keeps every
  reader, the seek and the checkpoints.
- **Exposing feed and kill to performers.** The brief says not to. The scenes map a few named regimes onto
  (feed, kill) pairs and route music into the regime field, not into the parameters.
