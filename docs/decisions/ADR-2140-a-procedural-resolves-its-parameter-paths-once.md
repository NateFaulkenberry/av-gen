# ADR-2140: a procedural resolves its parameter paths once

- Status: Accepted (2026-11-24), eng/proc-params (Neon Dreams editor slowdown investigation, fix 5, approved by the
  owner)
- Code: `scene/procedural.{hpp,cpp}` (`ProceduralParameterLookups`, `ProceduralParameters::lookups`,
  `ResolvedLookups`, `applyProceduralParameterValues`)
- Tests: `tests/unit/test_procedural_param_lookups.cpp` (`[adr2140]`)

## Context

`Composition::applyParameters` calls `applyProceduralParameterValues` for every procedural node and every sub-part,
every frame. The function found each of its ~190 parameters by path with `findRel`: a linear scan of the object's
`all` (~100-150 entries), comparing strings, then a `dynamic_cast`. Deformer, op and effector paths were also built
by string concatenation on every call. On Neon Dreams (1,758 procedurals) the investigation's `sample` put about
21 ms of CPU a frame in this one function (`02-ui-perf-investigation`, REPORT.md, CPU causes).

## Decision

1. **Each lookup is answered by the scan once and then from a cache on the `ProceduralParameters`.**
   `ProceduralParameterLookups` (a `mutable` member, `lookups`) holds, per lookup, the call site's path, the
   `Parameter<T>` type it was cast to, and the scan's answer (nullptr included). The unconditional lookups are keyed by
   call order; each use checks the path and the type, so a sequence that ever changed resolves again rather than read
   a neighbour's answer. The per-slot lookups (`deform/<n>/*`, `ops/<n>/*`, `effector/<n>/*`) are keyed by slot and
   field, because the slot counts come from `rest`, and a node's sub-parts apply part 0's parameters against rests
   whose slot counts differ.
2. **Invalidation is by construction, not by notification.** A `ParameterSet` owns each parameter through a
   `unique_ptr`, so registering others never moves one; the only ways a parameter dies are `remove` and `clear`, and
   the composition's unregister resets the whole `ProceduralParameters` (`node.proceduralParams = {}`) right after,
   cache included; re-registration returns a new one with an empty cache. The cache also records the `all` it was
   resolved against (`data()`, `size()`): a copy (new storage) or any change to `all` throws it away and resolves
   again. A cached pointer is therefore exactly as safe as the `all` entry it came from -- which `findRel` already
   dereferenced on every call.
3. **Values are not cached.** Every call still reads every parameter's final, so a route, a preset or an edit
   arrives the same frame, and the values written are the ones the scan would have found: offline renders are
   bit-identical (hashes below).

Not done: skipping the apply for a procedural none of whose parameters changed. Nothing records a parameter
revision (modulation rewrites every final every frame), and the composition folds the node transform into
`pg.distributionTransform` and ANDs visibility after the apply, so a skipped apply would compound the transform.
Once the lookups are pointer reads the remaining cost is small (below); the skip is not worth that risk.

## Measurements

Headless `--live-frame` at 1936x1262, base (414d47790) and this build interleaved in one GPU-lock hold, two runs
each, `sample` attached for 15 s of steady frames; cost = the function's inclusive samples / main-thread samples x the
run's median frame (the investigation's method; it measured 20 ms on its run4 sample this way). Load average 20-30 on
12 cores (other agents compiling), so absolute numbers are inflated; the ratio is the result.

| Example | `applyProceduralParameterValues` before | after | share of main thread before / after |
|---|---|---|---|
| Neon Dreams (t=25 s) | 29.1, 39.7 ms/frame | 1.7, 1.3 ms/frame | 4.3-6.0 % / 0.2-0.25 % |
| Liminal *All You Got* pass 4 (t=20 s) | 5.6, 4.5 ms/frame | 0.5, 0.4 ms/frame | 8.1-5.2 % / 0.6 % |

Liminal's `cpu(update) controller` median (the composition's per-frame update, which contains the apply) went 11.1,
11.2 -> 8.3, 8.1 ms. Neon Dreams headless is GPU-bound (the frame waits ~440 ms on the GPU), so its headless frame
time cannot show a CPU saving; an editor A/B on the CPU-bound `nd-noclouds` (`gui_ab.sh` in the evidence folder)
is prepared but was not run.

Bit-identical: offline PNG renders (960x540, 30 fps) of Neon Dreams 25-26 s and 56-57 s, Liminal pass 4 20-22 s,
Audio Response Lab 5-7 s and Cel Dream 10-12 s -- 240 frames: every PNG's md5 and every sequence hash equal between
base and this build.

## Consequences

- About 5 KB more per procedural node (the cache), filled on its first apply.
- A new lookup added to `applyProceduralParameterValues` needs nothing else: it is resolved on its first call. A new
  per-slot field needs its index and the group's field count in `ResolvedLookups::findSlot`.
- Single-threaded by assumption, as the apply already was: `applyParameters` runs the node loop on one thread.
