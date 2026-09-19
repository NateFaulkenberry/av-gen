# Current Task

## Objective
Confirm or refute a suspected **colour regression in `glowmere-valley-2-multicam`**: shot t=48.5 is
reported to have changed from dark blue to bright saturated cyan between a pre-merge and a
post-merge binary.

## Why
Raised by the agent that fixed the multicam cast defects, as *not its work*. It falls in the range
covered by the path tracer, asset LOD and water/sky merges. If it is a regression it affects the
flagship film and the owner has not seen it yet. If it is intended (the sky decoupling and fog
changes were deliberate and did alter the look) it should be recorded as intended and dropped.

## Current Status
- Reported, not investigated. No render pair has been produced by anyone to demonstrate it.
- Main is green and all contributing work is merged, so an A/B needs two *builds*, not two branches.

## In Progress
Nothing. This task has not been started.

## Remaining Work
1. Identify the merge range: `e41660d5` (water/path-tracer era) back through the asset-LOD and
   sky-decoupling merges.
2. Build `src/avgen` at a pre-range commit and at `main`. **Build the app target, not just
   `avgen_tests`** — see Known Problems.
3. Render `glowmere-valley-2-multicam` at t=48.5 from both, same camera, same size.
4. Compare. If they differ, bisect to the responsible merge.
5. Decide: regression to fix, or intended change to record in the relevant ADR.

## Known Problems
- **A render is evidence about the binary that produced it.** An agent lost an hour to byte-identical
  render pairs because it had only ever built `--target avgen_tests`, so every render ran a stale
  `src/avgen`. Build the app target explicitly.
- ADR-345 (lighting/background decoupling) and ADR-347 (fog taking its colour from the sky) both
  changed the look deliberately. A difference is not automatically a defect.
- Renders re-simulate the cast only up to **90 s** (`SeekBudget{maxSeconds = 90}`). t=48.5 is inside
  that, so the cast is valid at this time.

## Files Involved
- `examples/world/glowmere-valley-2-multicam.{json,scene.json}`
- `src/rendering/scene_renderer.cpp`, `src/scene/composition.cpp` (sky/fog/env paths)
- `docs/decisions/ADR-345-*`, `ADR-347-*` (the deliberate look changes)

## Tests
Full `~[gpu]` suite green at 2,414 cases / 3,796,327 assertions before this merge; a run covering
`3468d849` was in flight at handoff and should be re-confirmed. No test covers film colour.

## Next Action
Build `src/avgen` at `main` and at the commit immediately before the sky-decoupling merge, render
`glowmere-valley-2-multicam` at t=48.5 from each at the same size, and compare the two frames to
establish whether the cyan shift is real.

## Important Constraints
- Do not "fix" the colour by editing the film. If a merge changed the renderer, fix or accept the
  renderer change; the film's own data was not touched by those merges.
- Do not revert ADR-345 or ADR-347 without the owner — both were commissioned deliberately.

## Last Updated
2026-09-19 06:20
