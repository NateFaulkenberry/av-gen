# ADR-1122: A grid's behaviour is parameters, and only its layout re-seeds it

- Status: Accepted (gpu/productionization, the flagship LIVE scene)
- Builds on ADR-032 (simulated grids), ADR-1119 (per-step inputs, GPU checkpoints), ADR-1120 (agents grids).
- Found by: PHONOTAXIS (`examples/phonotaxis/`), whose MIDI vocabulary needs the organism's behaviour (how far
  it looks, how sharply it turns, how fast its trails fade) under a performer's hands.

## Problem

A simulated grid was scene-file only. Its behaviour (advection, diffusion, dissipation, Gray-Scott's feed and
kill, the agents' sensing, turning, step and deposit) had no parameter, so no route, macro, scene state, MIDI
binding or OSC message could reach it. The handoff of the productionization work said so ("Grid settings are
scene-file only. Drive a grid through its fields instead").

There was a second problem underneath. The simulation decided "this grid's state is no longer valid, re-seed
it" by comparing `layoutHashOf(grids)`, which mixed `GridField::structuralHash()`, and that hash covers every
setting. So even code that changed a coefficient re-seeded the whole population: one turned knob and a
two-minute-old organism of a million agents is replaced by noise. A parameter wired to that would have been
worse than no parameter.

Driving the grid "through its fields" covers the deposit and the flow, not the behaviour. A performer
cannot make the organism swarm, wander or starve with them.

## Decision

1. **Split the hash.** `GridField::layoutHash()` covers what the state's shape and seed depend on:
   name, enabled, mode, wrap, resolution, bounds, the input fields' names, `diffuseIterations`, `simRate`,
   `maxSubSteps`, seed, `seedAmount`, and for agents the count, the species and the deposit field's name.
   `rendering::Simulation` re-seeds a grid only when this changes. `structuralHash()` is unchanged and still
   keys the checkpoints (ADR-1119), so a changed behaviour drops checkpoints taken under the old one.
2. **Behaviour is parameters**, registered by `scene/grid_params.hpp` as `grid/<name>/<leaf>`, defaulting to
   the authored values:
   - every grid: `injectRate`, `advect`, `diffusion`, `dissipation`;
   - reaction-diffusion: `feed`, `kill`, `diffusionA`, `diffusionB`;
   - agents: `sensorAngle`, `sensorDistance`, `turnAngle`, `stepSize`, `depositAmount`, `repel`.

   The finals go into the scene's grids on every frame, beside the fields' (`Composition::update`). A grid
   added after attach registers its knobs too. The editor's Intermediate layer shows `grid/`.
3. **Reach.**
   - Scene JSON: the authored values, unchanged keys.
   - Project: `parameters`, `routes`, macros, states, presets.
   - MIDI and OSC bindings; the CLI through `--project`.
   - The editor's Parameters window.
   - Offline renders.

## Consequences

- A behaviour change continues the population. The GPU test turns the agents' turn, gaze and reach mid-play:
  0 of 20,000 agents moved more than the two steps allow, while 10,278 turned differently from the unchanged
  run. A seed change re-seeds them (`test_sim_checkpoints_gpu.cpp`, `[grid-params]`).
- Exactness, stated precisely. An authored or edited base value is part of the checkpoint input key, so play
  and seek agree as before. A *modulated* behaviour (a route, a MIDI knob) is sampled once per frame and held
  for that frame's sub-steps. Its scrub equals its play only when the modulation is a pure function of time.
  That is the same rule as a node transform the engine animates (ADR-1119, "Still frame-sampled"). While a
  behaviour moves, the grid's checkpoints keep being dropped, so a seek replays from the start, bounded by the
  catch-up ceiling. A live performance does not seek, and a live input never restores (ADR-1119).
- A grid inside a nested scene takes its behaviour from that scene's parameters when the scene rebuilds, not
  per frame. No shipped scene nests grids.
- Behaviour edits in the editor no longer re-seed a grid, so the picture keeps its history while you tune. That
  is a behaviour change from before, and the point.

## Rejected alternatives

- **Register the grid's settings as parameters and leave the hash alone.** Every knob movement would re-seed
  the grid. Rejected for the reason in the Problem.
- **Make every setting live, including the resolution and the agent count.** Those change the buffers' size
  and the seeding. They are layout and stay in the file.
- **A per-sub-step behaviour (as ADR-1119 did for field inputs).** It would make a modulated behaviour seek
  exactly, at the cost of a second uniform stream. No current use needs a modulated behaviour to seek; the
  flagship's modulated grid is a live instrument. Revisit when an offline piece modulates a grid's behaviour
  and must scrub exactly.

## Revisit triggers

- An offline piece modulates a grid's behaviour and must scrub exactly: pack behaviour per sub-step.
- A nested scene's grid needs per-frame behaviour: apply the child's grid parameters in the parent's update.
