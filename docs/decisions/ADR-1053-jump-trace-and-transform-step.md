# ADR-1053: Finding entities that jump: the jump trace and the `transform-step` rule

- Status: Accepted (2026-10-01), proto/liminal-space (All You Got art pass 3, §28)
- Code:
  - `src/app/jump_trace_cli.*`: `avgen --trace-jumps`.
  - The `transform-step` rule in `src/params/liveness.cpp`.
- Tests: `tests/unit/test_route_liveness.cpp` (`[adr1053]`).

## Context

The owner: "entities randomly jump upward/downward ... Investigate the source ... then either fix the underlying
behaviour, or explicitly constrain the entity so that this behaviour cannot occur."

## Investigation

`avgen --trace-jumps examples/liminal/all-you-got-pass2.json --fps 30` plays the film offline and samples every
transform parameter after each frame. It reports one-frame changes that are both larger than 3 cm and at least
four times the motion in the neighbouring frames. Changes on hidden objects, or within a frame of a camera cut, are
excluded. The results:

- **Release section (from 38 s): the armchair, plant and coffee table jump.** 36-39 jumps each, up to 14 cm.
- **Bridge 3 (from 187 s): the kettle, both chairs and the plate jump, along with the word nodes b3w83-90.**
  Up to 25 cm.
- **The cause is the same every time: a route from `grid.song.quarter` into a Y translation, with no chain.**
  - The beat-grid pulse (ADR-1045) is 1 on the beat and decays to 0. It rises in a single frame.
  - Fed straight into a position, it teleports the object up on every beat, and the object sinks back down.
  - That reads as random bouncing, not as a bob.
  - ADR-1045's guide said to use `.wave` for bobbing. Its route recipe nonetheless showed the pulse for a scale,
    and the film used the pulse for positions.
- **Not causes:**
  - Physics: none.
  - Journey anchoring: smooth.
  - Spawning: none.
  - Interpolation: the step keys found are the intro seed's construction pops and the fireworks emitters'
    relocations, which are authored and intended.

## Decision

1. **The cause is fixed in data.**
   - A bob takes the division's `.wave` (a raised cosine, continuous), or the pulse through a chain with
     `springHz` (and `springDamping`) or `attackMs`.
   - The art generator owns those routes. The exact list is in PROGRESS-eng.md.
2. **A constraint, so that this cannot pass unnoticed:** a liveness rule, `transform-step` (a hazard).
   - It fires on any route whose source is a beat-grid signal that is not `.wave` or `.phase` and whose target is
     a position or translation (`sdf/<o>/transform/position`, `sdf/<o>/node/<n>/translation`,
     `nodes/<n>/position`), when the chain has no `springHz` and no `attackMs`.
   - Every project load logs it as a warning, and `--audit-routes` reports it.
   - It is a hazard, not an error. A deliberate dislocation on a BIG CLAP, under a cut or a glitch, is
     legitimate; give such a route `attackMs` of a few milliseconds, or accept the warning.
3. **The jump trace stays as a measurement.**
   - `avgen --trace-jumps <project> [--fps 30] [--range a:b] [--threshold 0.03] [--json f]` names each jumping
     parameter, its times, and the routes or step keys that drive it.
   - It takes about 40 s for the whole film. It needs no window and no GPU.

## Consequences

- The rule catches the pattern before render. The trace catches any other cause, including timeline steps and
  route stacking, after the fact.
- Scale and size jumps are reported relative to the value. A node that grows from 0 reports huge ratios; that is
  how an appearance pop looks, and those pops are intended.
