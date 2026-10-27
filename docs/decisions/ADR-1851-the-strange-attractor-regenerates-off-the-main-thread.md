# ADR-1851: The Strange Attractor regenerates off the main thread in the live editor

- Status: Accepted, scene/strange-attractor (Gate 2, 2026-10-27).
- Code: `Composition::updateAttractorWorld` (`attractorPending_`). Test: `[adr1851]` in
  `tests/unit/test_attractor_world.cpp`.

## Problem

A structural change (the seed, the terrain or the formation knobs) regenerates the world. That takes 1.3-3 s at
2048² and 6-12 s at 4096². ADR-1850 ran it on the main thread, so a seed change in the live editor froze the frame
for that long.

## Decision

- **In the live editor, a worker generates.** The live editor is the composition with an interactive rebuild
  budget: the engine sets that only in live mode, which is ADR-1405's existing signal.
  - The frame that asks does not wait. The old world stays on screen until the worker's world lands.
  - The new world is installed only if it is still what the knobs ask for, and its arrival is a camera cut.
  - One generation runs at a time. A newer request starts when the current one lands.
- **Offline, generation is synchronous, as before.** A render must contain the world its parameters name, on the
  frame they name it, so a wall clock never decides what a deterministic render shows.
- **The worker's world is bit-identical to a synchronous one.** The generator is a pure function, and the test
  checks it.

## Consequences

- In the live editor, a seed sweep shows the old world for one to three seconds, then cuts to the new one.
- Destroying the composition while a worker runs waits for that worker, which is bounded by one generation.
