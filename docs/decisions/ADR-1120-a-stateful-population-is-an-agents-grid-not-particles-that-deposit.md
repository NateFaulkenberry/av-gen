# ADR-1120: A stateful population is an agents grid, not particles that deposit

- Status: Accepted (gpu/productionization)
- Builds on ADR-032 (grids), ADR-1119 (per-step inputs and checkpoints), ADR-1116 (audio fields).
- Evidence: the spike's Phase 3 C (Mycelium: 2M physarum agents, bit-exact play vs checkpoint seek,
  correlation 0.011 against a warm-up). Rejects the spike's literal step 3b ("particle → grid
  deposits").

## Problem

The spike's Mycelium is a population that **writes the field it reads**. Production had no such loop:

- particles can sample a grid (through fields) but never write one;
- grids inject only from analytic fields.

The spike proposed that particles deposit into grids. But particles carry ADR-360's relaxation: a
scrubbed particle frame may differ from a played one, and particle dt follows the frame. A grid fed
by particles would inherit both, and become the first grid whose scrub differs from play.

## Decision

**`GridMode::Agents`.** A grid whose plane (resolution `[x, 1, z]`, up to 1024 × 1024) holds the trails
of up to three species, plus their sum (4 floats per cell). It also holds `agentCount` agents (up to
4M), each `(x, z, heading, species)`, 16 B.

Each fixed step (ADR-1119's per-step inputs) does three things:

1. **`cs_agents_move`**, per agent:
   - sense the previous step's trail at three points ahead. Its own species attracts and the others
     count against a direction by `repel`.
   - turn toward the stronger side (Jones's rule). A tie turns by a hash of (agent, step).
   - optionally steer toward a vector field (`velocityField`, `advect`).
   - move `stepSize` cells, then wrap or turn back at the edge.
   - deposit `depositAmount × depositField` into its species' channel. The deposit field is sampled at
     the agent with `fieldElement = (species + 0.5) / 3`, so an Element-band spectrum field gives each
     species its own band: lows, mids, highs. Deposits are a **u32 fixed-point `atomicAdd`** (1/4096,
     each clamped to 64), so their order cannot change the sum.
2. **`cs_agents_resolve`**, per cell: the trail blurs (3 × 3, by `diffusion`), fades (`dissipation`),
   adds the step's deposits and clears them.
3. Seeding (`cs_agents_init`) is a hash of the agent's index and the grid's seed.

The trail is sampled like any grid, through a `grid` field, as a vector (the three species). Effectors,
deformers, particles (the spores of Phase 3), materials and volumes read it with no change. Agents are
part of the grid's checkpoints (ADR-1119). There is **no CPU reference** for agents: a chaotic
population cannot be mirrored to the last bit, so `GridField::step` does nothing for them, and agents
are not individually selectable. The grid is the unit.

`kMaxGridTableFloats` doubles to 4M floats (16 MB), so one 1024² agents trail fits the shared table.

## Consequences

- Bit-exact run to run, and play == fresh seek == restored seek
  (`tests/rendering/test_sim_checkpoints_gpu.cpp`). The spike's correlation of 0.011 against a warm-up
  becomes byte-identical.
- Particles keep ADR-360. They never deposit. Anything that must seek exactly and be stateful lives in
  `Simulation`.
- Memory: 16 B per agent, plus the trail × 4 buffers (A, B, initial, deposits); see the Mycelium
  regression benchmark.

## Rejected alternatives

- **Particle → grid deposits** (the spike's step 3b): rejected for the seek reason above.
- **Float atomics.** WGSL has none, and they would be order-dependent.
- **A CPU mirror of agents.** Turning is a float comparison of sensed trail values; one last-bit
  difference turns an agent the other way, and the population diverges. Agents are GPU-only, by design.
