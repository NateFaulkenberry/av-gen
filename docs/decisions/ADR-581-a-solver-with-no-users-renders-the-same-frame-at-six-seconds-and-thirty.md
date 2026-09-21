# ADR-581: A solver with no users, and it renders the same frame at six seconds and at thirty

- Status: Accepted (2026-09-20)
- Found while surveying ADR-032's simulated grids as a candidate for ADR-580's Phase 4 tier. The
  Tornado does not currently use the grid and this record does not depend on it ever doing so.
- Extends ADR-032 (simulated fields), ADR-360 (a render must be reproducible), ADR-521 (the warm-up
  never reached the renderer that ships). Related: ADR-385 (a stated reason is not evidence),
  ADR-182 (a probe that cannot fail).

## 1. The census, which is the larger half

`spatial::GridField` + `rendering::Simulation` + `shaders/simulate.wgsl` is a **GPU semi-Lagrangian
fluid solver**: injection, back-traced advection with trilinear gather, Jacobi diffusion,
dissipation and a Gray-Scott mode, double-buffered, gather-only, up to 128^3 over eight grids, with
a CPU reference in `grid_field.cpp` that the unit tests compare against. It is a real, complete,
tested subsystem.

**No scene in this repository authors one.** Counted by parsing every `examples/**/*.json` and
walking the trees rather than by grepping:

| | |
|---|---|
| files with a non-empty `"grids"` array | **0** |
| `"kind": "grid"` occurrences | 183 |
| ...that are a simulated grid | **0** |
| ...that are `/nodes[]/procedural/distribution` | **183** |

The 183 are a *procedural point distribution* named "grid", an unrelated subsystem that happens to
share the word. This is the same collision that, two days ago, produced a removal census listing
eight files as authoring a Vortex when six of them held a `spatial::FieldKind::Vortex` driving
particles and had no atmospheric effect at all -- a `git grep` that would have deleted particle
motion from six scenes. **Parsing found both; grepping found neither.**

So the solver has shipped with zero authored users. Everything below follows from that, and is the
reason none of it has been noticed: a subsystem with no users is a subsystem nobody has validated.

`examples/labs/grid-catchup-lab.scene.json` is now the first, and writing it turned up the
authoring trap first: **the scene-level `"fields"` key is `entity::FieldDesc` (ADR-097), a
different subsystem.** The `spatial::FieldSpec` a grid's `injectField` and `velocityField` resolve
against is authored as a node of `"kind": "field"`. Both are called "fields", neither is the other,
and the first attempt at this scene parsed, loaded, logged three fields, rendered, and simulated
nothing -- which is a third instance of two subsystems sharing an English word.

## 2. The measurement

The lab injects density in a sphere at the base and carries it up and around with a spiral field,
so how far the smoke has travelled is a direct readout of how many sub-steps the grid has taken.
`simRate` 60, `maxSubSteps` 4, 48^3 scalar.

```
avgen --headless --composition examples/labs/grid-catchup-lab.scene.json \
      --render <out> --format png --range <a>:<b> --fps 30 --size 640x360
```

### 2.1 The same transport second, two ways

| arm | mean luminance |
|---|---|
| `--range 6:6` (one frame) | **113.70** |
| `--range 0:6`, its last frame | **187.21** |

**229 289 of 230 400 pixels differ -- 99.5% of the frame.** Maximum channel difference 125 of 255;
mean absolute difference 73.9. Rendered, the single frame shows a blob of smoke about a third of
the way up the volume; the sequence shows it filling most of the frame.

### 2.2 And six seconds renders identically to thirty

| arm | sequence hash |
|---|---|
| `--range 6:6` | `5496d004173787ee` |
| `--range 30:30` | `5496d004173787ee` |

**Byte-identical, twenty-four seconds apart.** This is the defect at its starkest and it is not a
coincidence: both start times reset the grid, are told they are behind, skip to `target - 240`, and
then run the same 240 sub-steps from the same zero state. The skip is to a state that *was never
simulated*, so what the two frames have in common is everything the grid contributes.

### 2.3 What each start time actually gets

`Simulation::update` grants `kCatchUpSteps = 240` sub-steps on a grid's first frame and
`maxSubSteps` thereafter.

| start | sub-steps owed | granted | |
|---|---|---|---|
| t = 1 s | 60 | 60 | 100% -- no warning, correct |
| t = 6 s | 360 | 240 | 67% |
| t = 30 s | 1800 | 240 | 13% |
| t = 90 s | 5400 | 240 | 4% |

It is loud, to its credit: `grid 'smoke' starts 360 sub-steps behind; skipping ahead`. A warning in
a run that produces a file is not a refusal, and the file is the deliverable.

## 3. This does **not** violate ADR-360, and saying why is the point

Two renders of the same range are byte-identical: `--range 6:6` twice gives **0 differing pixels**.
The skip is a pure function of `renderTime`, so the contract ADR-360 states -- a render must be
reproducible, two renders of the same range may not differ -- is kept exactly.

**What is broken is a promise nobody wrote down**: that `--range t:t` shows the scene at *t*. It is
the idiom this entire repository looks at single frames with -- the Quality Lab uses it,
`labs::reproduceCommand` emits it, every README example is one -- and for a grid-backed scene it
shows the first four seconds of the simulation with a wrong timestamp on it.

ADR-521 found exactly this shape one subsystem over: `--particle-warmup` was built, capped,
validated and never reached `RenderJob`, so every `--range t:t` render showed a sixtieth of a
second of a continuous emitter. It closes with "**for every continuous emitter in every scene**, it
was showing the first sixtieth of a second of the field rather than the field. Nobody had noticed
because a sparse particle field and a correct one both look like particles." A partly spun-up grid
and a correct one both look like smoke.

## 4. Decision

**Recorded, not fixed.** This branch's Tornado is analytic and does not use the solver, so a fix
here would be a change to a subsystem with no users made by an agent who is not using it either --
and the right fix is a decision about what `--range t:t` means, which is bigger than this record.

What is fixed is the census: there is now one authored grid scene, and it is a lab whose whole
purpose is that this defect is visible in it.

The options, so whoever takes it does not re-derive them:

1. **Raise `kCatchUpSteps`.** Cheapest, and wrong at the limit: 90 seconds at 60 Hz is 5400 steps
   and the cap exists so a stall does not explode. It moves the boundary rather than removing it.
2. **A `--grid-warmup` flag**, ADR-521's shape exactly -- including its failure mode, since ADR-521
   is the record of such a flag not reaching the renderer that ships. If it is built, it must be
   proved on the `RenderJob` path by a hash that moves.
3. **Refuse rather than warn.** A render that cannot reach the state it claims exits non-zero
   instead of writing a file. Honest, and it makes `--range 30:30` unusable on any grid scene.
4. **Make a grid's state a function of time.** Correct and expensive: it is what the analytic field
   in ADR-580 does instead, and it is why that tornado's silhouette deliberately does not live in
   a grid.

## 5. Consequences

- **Any future grid-backed effect inherits this.** ADR-580 §4 already refuses to put the Tornado's
  silhouette in a grid for this reason, arrived at from a code read; this is that reading measured.
- **`--range t:t` is not a screenshot of *t* for a stateful subsystem.** Particles (ADR-521) and
  grids are both in this class. Anything stateful added later joins it by default.
- The third case tonight of **two subsystems sharing an English word** defeating a search:
  `"kind": "grid"` here, `"kind": "vortex"` in the removal census, and `"fields"` meaning two
  different things in one scene file. A scan that does not understand its own input reports its own
  blindness as a result.

## Revisit when

- Anything ships that authors a grid. At that point one of §4's four options has to be chosen,
  because the effect will be wrong in every single-frame render taken of it.
- `--range t:t` acquires a stated meaning. That is the decision this record is really waiting on.
