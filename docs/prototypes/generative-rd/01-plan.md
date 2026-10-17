# Generative R&D: implementation plan

Branch `proto/generative-rd` (worktree `../av-gen-rd`, from main `dd27374c`). Brief: `00-brief.md`. Owner overrides
(coordinator, 2026-10-17): the live target is 1080p at 30 fps under Auto quality, measured with
`avgen --live-profile --mode live --audio <track>`; no audio file in any scene; prototype scenes go under a new index
category "Experiments"; new engine features take ADRs from 1450-1469.

## What the repository already has (the audit, 2026-10-17)

The brief asks us to reuse before building. The audit found that most of the machinery exists:

| Need | Already on main | Gap |
|---|---|---|
| GPU ping-pong grid sim, deterministic sub-steps, seek, checkpoints | `rendering::Simulation`, `shaders/simulate.wgsl`, `spatial::GridField` (ADR-032, 1114, 1119) | none |
| Reaction-diffusion | `GridMode::ReactionDiffusion`: Gray-Scott, 6-neighbour Laplacian, injection into B | volume only (128 per axis); one Euler step per sub-step at dt = 1/simRate, so a pattern needs minutes to form; one global feed/kill |
| Excitable media and agents on a plane up to 1024² | `GridMode::Excitable` (ADR-1201), `GridMode::Agents` (ADR-1120) | (they are the model for a new plane mode) |
| Oscillator network | nothing | a new grid mode |
| Reading a grid anywhere | `FieldKind::Grid` (any channel, ADR-1201): Field deformer, effectors, material `field` op, particle forces, fibre streamlines | none |
| Analytic flow fields | `Vortex`, `CurlNoise`, `Attractor`, `Repulsor`, `Spiral`, `Direction`, `WaveVector`, `Compound` (4 children, one level) | to be found by Experiment B |
| Flow representations | particles with field forces and trail ribbons (ADR-015, 040), fibres along streamlines (ADR-1180/1181), Field deformer, vector grids with self-advection, volume fog with a density field | to be found by Experiment B |
| Audio in space | `spectrum` / `onset` fields (ADR-1116), resonance (ADR-1400), `response.*` (ADR-1361), MIDI bindings to `control.*` signals | none |
| Scene authoring | `tools/sonic_vfx/kit.py` (`kit.Scene`), the Resonator generators as models | none |

So the plan is three thin engine additions plus authored scenes. There is no new renderer, no second audio
pipeline, and no simulation framework.

## Engine work (each an ADR with tests)

1. **ADR-1450: an oscillator grid.** `GridMode::Oscillator`, a plane `[x, 1, z]` up to 1024² of coupled phase
   oscillators. It uses Kuramoto-Sakaguchi coupling over a Gaussian disk of radius 1-4 cells, plus optional hashed
   long-range links (the hybrid/small-world network), two populations with a cross-coupling ratio (the layered
   network), a coupling-mask field (waveguides and regions), a natural-frequency field, an injection field (phase
   kicks from onsets), and phase noise. The cell holds (cos θ, sin θ, local coherence R, activity), so every reader
   gets smooth, interpolable channels. It is gather-only, with fixed iterations and a CPU reference.
2. **ADR-1451: reaction-diffusion as a plane, at speed.** A RD grid may be a plane `[x, 1, z]` up to 1024²
   (isotropic 9-point Laplacian on a plane); `speed` and `iterations` (Euler steps per sub-step) let a pattern form
   in seconds; a regime field blends (feed, kill) toward (feedB, killB) per cell (competing regions, local regime
   changes from music). The defaults reproduce today's RD bit for bit.
3. **Experiment B** is expected to need little or no engine code (see the table). Any gap it finds gets an ADR in
   the same range.

Shared files touched: `src/spatial/grid_field.*`, `src/rendering/simulation.*`, `shaders/simulate.wgsl`,
`src/scene/grid_params.cpp`. Not touched: `src/spatial/resonance*`, `src/params/palette*`, `shaders/fields.wgsl`
(the Grid field reader already serves any channel).

## Scenes and harnesses

- Generator `tools/generative_rd/` (one module per experiment, plus a small shared `common.py` on top of `kit`).
  It writes `examples/experiments/<id>/<id>.json` + `.scene.json`. Each experiment has a few **configurations**
  (`--config <name>`), and review variants (`--variant`) as Resonant Silk has.
- Index category **Experiments**. Scenes worth showing are live (`"live": true`, `sonic.live`, `response.*`);
  harness-only configurations are `"hidden": true`.
- The Resonator scenes are not modified.

## Order of work (brief §6 per experiment)

1. Phase 1, baseline: the Resonator numbers already measured (Resonant Silk 02-report §6, Cymatic World), plus one
   live profile of Cymatic World on today's build, for a same-day reference.
2. C, reaction-diffusion: ADR-1451, then autonomous scenes (pattern on relief, closed form, boundaries as
   geometry). Message the coordinator.
3. A, oscillators: ADR-1450, then autonomous scenes (phase-wave sculpture, sync sculpture of vanes, interference
   membrane, phase-silk ribbons). Message the coordinator.
4. B, flow: compose from the existing fields and representations (particles, fibres, deformation, density),
   filling gaps only where needed. Message the coordinator.
5. Audio and MIDI per experiment (Phase 5), with feline-footwear, trench and hyperborea, plus the repo's synthetic
   probe material if present.
6. Media in `~/Desktop/av-gen-review/63-generative-rd/{A-oscillators,B-flow,C-reaction-diffusion}`, and
   performance via `--live-profile` under `tools/gpu-lock.sh`.
7. Docs: `PROGRESS.md` (resumable), `A-oscillators.md`, `B-flow.md`, `C-reaction-diffusion.md`, `performance.md`,
   `recommendation.md`.
8. Final checks: the cpu-assets and gpu suites, `review_examples.py --only` for each experiment, and project
   integrity, all exiting 0.

## Risks

- **GPU contention.** The coordinator is rendering long videos. Keep jobs short and go through the lock.
- **Vertex-bound deformation** (the Resonant Silk lesson): read grids, which cost one trilinear fetch per vertex,
  and do not chain analytic fields.
- **A grid catch-up on seek** replays from the last checkpoint. Offline review clips start a few seconds in, which
  is cheap.
