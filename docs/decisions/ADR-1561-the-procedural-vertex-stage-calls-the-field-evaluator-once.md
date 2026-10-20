# ADR-1561: The procedural vertex stage calls the field evaluator from one place

**Status:** Accepted, 2026-10-20 · **Range:** 1560-1569 (eng/perf-reliability)

## Context

Measured with ADR-1560's recipe on a quiet M2 Max at 416d48f1, a fully cold launch takes 305 s. Two pipelines are
most of it:

- `procedural-opaque` (vs_proc + fs_proc): 126 s;
- `procedural-depth` (vs_proc_depth + fs_proc_depth): 88 s.

A `sample` of `MTLCompilerService` during the compile shows LLVM's loop unroller and reassociation passes working on
one enormous function.

A/B experiments, each a fully cold compile with one change to a copy of the shaders (lit / depth, in seconds):

| Change | Lit | Depth |
|---|---|---|
| None | 126 | 88 |
| Material interpreter stubbed | 128 | 81 |
| Op loop bound made a runtime value | 127 | 89 |
| Fiber integration stubbed | 104 | 69 |
| Resonance field kind stubbed | 135 | 97 |
| fields.wgsl from before ADR-1501 | 140 | 100 |
| Deformer loops given unknown trip counts | 142 | 99 |
| **Field calls in the deformers stubbed** | **16** | **12** |
| Whole deformer chain stubbed | 30 | 25 |

**The cause is the number of call sites, not a single construct.**
- The Metal compiler inlines a whole copy of a function at every call site. `fields.wgsl`'s evaluator covers every
  field kind (noise, curl, audio spectrum, waves, grids, resonance), so it is large.
- `procedural.wgsl` called it, through `fieldScalar` and `fieldVector`, from:
  - the local and the world field deformer;
  - in each of `deformChain`'s two loops;
  - in each of the four chain evaluations a lit vertex makes (the point, two finite-difference neighbours, and last
    frame's point);
  - plus the point path's two `deformWorld` calls and four `fiberPull` calls in the fiber integration.
- That is about twenty copies of the evaluator in one vertex function.
- No single commit made it slow: the cost grew with `fields.wgsl`. The resonance kind and ADR-1501 were each ruled out
  by A/B.

## Decision

The evaluator and the chain are each called from one place. The arithmetic, and its order, are unchanged.

1. **`fieldDeformerSample(slot, at)`** is the field deformers' one call of the evaluator: one `fieldVector`, one
   `fieldScalar`. `applyLocalFieldDeformer` and `applyWorldFieldDeformer` take its result and do only the arithmetic
   around it.
2. **`deformRun`** is the chain as one loop over both halves:
   - the local deformers;
   - then the instance and the object;
   - then the world deformers.

   `deformChain` (the whole chain) and `deformWorld` (the world half only) are thin wrappers around it.
3. **`procVertex` evaluates its chain points in one loop.** The points are p0, the two neighbours when the normal is
   wanted, and the previous frame's point when the velocity is. The point path's two centres are also one loop.
4. **`fiberPoint` makes one call of `fiberTurn`**, whose two midpoint pulls are one call of `fiberPull`.
5. **Second step: one chain loop for every path.** Fibers, point sources and meshes set up their points and share
   `procVertex`'s one loop of `deformRun`, so the chain is compiled once per pipeline, not three times. `deformWorld`
   is gone. `fiberVertex` is split into `fiberCentreLine` (before the chain) and `fiberVertex` (after it).
6. **The material interpreter runs a program's base and its layers through one call of `materialRunOps`.** Segment
   0 is the base and segment 1 + i is layer i. The op dispatch was compiled twice into every shader that shades a
   surface.
7. **A unit test (`[adr1561]`) counts the call sites** of these functions in `procedural.wgsl`, so a new copy is a
   decision rather than an accident.

Loops with constant trip counts were checked: making their counts opaque to the compiler changed nothing (22.9 s
against 23.2 s), so they are not being unrolled into copies.

## Consequences

- **Cold compile** (lit / depth, seconds):

  | Step | Lit | Depth |
  |---|---|---|
  | 416d48f1 | 126 | 88 |
  | Steps 1-4 | 23 | 19 |
  | Steps 5-6 | 15 | 10 |

  After steps 1-4 the whole serial cold launch went from 305 s to 135 s. In the A/B, the interpreter's one call site
  made no measurable difference to `pbr-opaque` (6-7 s). Its op dispatch is simply large: stubbing the interpreter
  takes `pbr-opaque` to 1.6 s.
- **The compiled-materials variant (ADR-1493)** builds this same module. Its cold build gets the same gain, so a live
  scene sits on the interpreter for correspondingly less time.
- **The images are unchanged.** The arithmetic is the same. These GPU tests pass (72 cases):
  - procedural, fiber, field, points, wind, spline and depth;
  - material, ADR-1493's compiled-against-interpreted, and ADR-1491's depth-equals-lit invariance;
  - the fiber centre line's CPU parity.
- **Further sites cost a copy.** A new field-sampling deformer or chain call should go through the existing sites.
