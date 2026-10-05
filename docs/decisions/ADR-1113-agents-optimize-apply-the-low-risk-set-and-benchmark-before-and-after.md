# ADR-1113: Agents optimize, apply the low-risk set, and benchmark before and after

**Status:** Accepted (live optimizer, Phase 5 stage E). **Date:** 2026-10-04

The brief's 5.6. Three tools over ADR-1106's child-process hook (a scratch copy of the project, `avgen --live-profile`
headless in a child, off the main thread):

- `performance.optimize_scene` (targetFps, start, quality = ultra, heroPolicy, maxRisk, apply, critic): the search
  (ADR-1111). `apply: true` adds the chosen levers -- only if a MEASURED combination reached the target -- to the
  project's live ceilings when the answer arrives.
- `performance.apply_low_risk_optimizations`: the search at maxRisk low; the measured low-risk set becomes ceilings.
- `performance.benchmark_before_after` (levers = "project" or a list): `--compare` (ADR-1110), read-only.

"Profile scene" and "find the largest bottleneck" stay `performance.profile_scene` (its critical path and categories).
Applying happens in `DeferredResult::settle` on the main thread and only ever writes `live.overrides` (ADR-1101), never
the scene; `performance.set_live_quality clearOverrides` undoes it. The hook is now installed in every session (a
headless `--ai-script` run too), so the tools are reachable wherever the control plane is.
`examples/ai/live-optimize.ai.json` runs the whole workflow with the scripted provider.

Known limit: the ceilings written by `settle` are outside the task's transaction, so the task summary says "0 values
changed" and the AI panel's "Undo this task" does not remove them.
