# ADR-1450: An oscillator grid couples phases on a plane

**Status:** Accepted (proto/generative-rd)
**Date:** 2026-10-17
**Brief:** `docs/prototypes/generative-rd/00-brief.md` §2 (Experiment A: coupled oscillators and phase fields).
**Implemented by:**
- **Model:** `spatial::GridMode::Oscillator`, its settings, validation, JSON and hashes, and the CPU reference
  (`oscKick`, `oscCouple`) in `src/spatial/grid_field.{hpp,cpp}`.
- **GPU:** `cs_osc_kick` and `cs_oscillate` in `shaders/simulate.wgsl`, and their dispatch and uniform lanes in
  `src/rendering/simulation.cpp`.
- **Parameters:** `grid/<name>/{coupling,frequency,spread,phaseLag,noise,crossCoupling,detune,linkWeight,persistence,speed}`
  in `src/scene/grid_params.cpp`.

**Tests:**
- `tests/unit/test_generative_grids.cpp` (`[oscillator]`):
  - validation and the JSON round trip;
  - free running at the natural frequency, with `speed` and `iterations`;
  - the synchronisation transition (K = 12: order above 0.9; K = 0: it dephases);
  - two populations with no cross-coupling lock separately and beat;
  - a kick moves only the phases it reaches, and a zero coupling mask is K = 0;
  - unit phases and bounded readouts under extreme settings, and determinism.
- `tests/rendering/test_generative_grids_gpu.cpp` (`[oscillator]`):
  - every feature at once is within 2e-3 of the CPU over 32 sub-steps;
  - with noise, two runs are bit-identical;
  - the GPU synchronises a 64² lattice.

## Context

The brief's Experiment A asks for many oscillators that interact: they synchronise, drift apart, form islands and
travelling phase waves. That needs a stateful network. The resonance field (ADR-1400) is stateless by design: it is a
function of the audio history, so it cannot hold a phase that its neighbours pulled.

The engine already steps stateful planes on the GPU (agents, ADR-1120; the excitable medium, ADR-1201). It does so
deterministically, with seek and checkpoints (ADR-1114, 1119), and any reader samples them through a `grid` field.
An oscillator network is one more such plane.

## Decision

`"mode": "oscillator"` is a plane `[x, 1, z]` up to 1024² of phase oscillators. A cell holds four floats:

| channel | value |
|---|---|
| 0 | cos θ (the default scalar reading) |
| 1 | sin θ |
| 2 | R, the local order parameter: \|Σ w e^{iθ_j}\| / Σ w over the coupling disk. 0 means the neighbours disagree (a defect, a front); 1 means they are in step. Low-passed by `persistence`. |
| 3 | activity: the strain 4 (1 − Σ w cos(θ_j − θ_i) / Σ w), plus the kicks. It is 0 in step, small across a smooth wave, and near 4 at a defect or a fresh kick. Low-passed by `persistence`, capped at 4. (Not the torque: with a phase lag the torque is K sin α everywhere, even in perfect step.) |

The phase is stored as a unit vector, so every reader (Field deformer, effector, material `field` op) interpolates
it smoothly across cells. There is no wrap seam to cross.

One sub-step (`dt = speed / simRate`) is two kernels:

1. **Kick** (`cs_osc_kick`), once. Three terms are added to θ:
   - `injectRate · max(0, injectField) · dt`: phase kicks, from onset shells or MIDI-driven spheres;
   - `2π · frequencyField · dt`: a spatial or musical retuning of the natural frequency;
   - `noise · √dt · u`: phase noise, with u uniform in ±√3 and hashed from (cell, step, seed).

   Activity gains |kick|.
2. **Couple** (`cs_oscillate`), `iterations` times, with dt / iterations each:
   `θ += dt (2π ω_i + K m_i Σ_j w_ij x_ij sin(θ_j − θ_i − α) / Σ_j w_ij)`. The terms:
   - **ω_i:** `frequency + spread · (u1 + u2 − 1)`, a triangular distribution hashed per cell, plus `detune` for the
     second population.
   - **The disk:** w_ij = exp(−d² / 2σ²) with σ = 0.6 · `couplingRadius`, over the disk d² ≤ r² + r.
   - **Links:** `links` long-range partners per cell, hashed within ±`linkRange` cells, each weighing `linkWeight` ×
     a nearest neighbour's weight. This is the hybrid, small-world network.
   - **x_ij:** 1 within a population and `crossCoupling` between populations. `populationMix` of the cells, hashed,
     form population B. This is the layered network.
   - **m_i:** the `conductivityField` (coupling mask). It shapes waveguides and regions.
   - **α:** `phaseLag`, the Sakaguchi lag. It drives travelling waves and, near π/2, spiral turbulence.

   On a clamped grid, a neighbour outside the grid is skipped, giving no-flux edges. On a wrapping grid it wraps.

The sum uses sin(θ_j − θ_i − α) = sin θ_j cos(θ_i + α) − cos θ_j sin(θ_i + α). So a neighbour costs two reads and no
trigonometry. Initial phases are `2π · seedAmount · hash(cell, seed)`.

Every kernel is a gather. The hashes are those of ADR-1201 (`simHash`), and the step index is the field block's own,
so a run is bit-exact and a seek replays exactly.

## Consequences

- **Readers need nothing new.** A `grid` field with `channel` 0-3 reads it everywhere (ADR-1201's channel
  selector). `shaders/fields.wgsl` is unchanged.
- **Cost.** About 50 neighbour reads per cell per coupling pass at radius 4. The Gaussian weights are separable,
  `g[|dx|] g[|dz|]`, which is five exponentials per cell instead of one per neighbour. Measured on an M2 Max by A/B
  (the grid on versus off): 2.6 ms of GPU for a 384² lattice at r = 4 (6.3 ms before the separable weights), and
  0.2 ms for 300 × 128. See `docs/prototypes/generative-rd/performance.md`.
- **Local coupling freezes phase vortices.** On a lattice started from random phases, local coupling leaves vortices,
  the 2D XY model's topological defects. Their global order stalls near 0.7. They are a visual feature (R marks
  them), and links or an ordered start remove them.
- **CPU and GPU agree to about 1e-3**, not bit for bit. The transcendental functions differ; the test bounds it.

## Rejected alternatives

- **A fully connected Kuramoto network.** It is O(N²), and the mean-field result is one global phase with no
  spatial structure. The brief itself says a fully connected network is not necessarily desirable.
- **Storing θ instead of (cos θ, sin θ).** Interpolating θ across the 2π wrap draws a false ridge in every reader.
- **Stuart-Landau amplitude dynamics.** They are a further model; the activity and R channels already give the
  amplitude-like readouts the visuals use.
