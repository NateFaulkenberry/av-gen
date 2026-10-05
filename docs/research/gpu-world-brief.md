# AV Gen GPU World Architecture — Gated Research Spike

(The owner's brief, verbatim, 2026-10-04.)

## Mission

Investigate whether AV Gen would materially benefit from introducing a GPU-resident / GPU-driven procedural world execution model.

This is a **research spike, not an architecture refactor**.

The goal is to determine, with measured evidence, whether there is a sufficiently compelling case to pursue this architecture further.

Do NOT modify AV Gen's production scene architecture unless explicitly authorized by a later phase.

Do NOT attempt to redesign the engine.

Do NOT build a generalized GPU VM, GPU scripting language, new scene graph, or speculative abstraction.

Start with a small isolated experimental subsystem using established GPU techniques:

* compute shaders
* storage buffers
* GPU-generated instance data
* indirect drawing where appropriate
* GPU-side animation
* GPU-side culling
* GPU-resident simulation state
* procedural generation

The central hypothesis is:

> AV Gen may be able to represent certain audiovisual worlds as compact procedural descriptions whose expensive population, animation, culling, and simulation happen primarily on the GPU, reducing CPU scene/entity overhead while enabling visual systems that are impractical with conventional CPU-managed entities.

This hypothesis must be tested rather than assumed.

---

# Non-negotiable research rules

## 1. Evidence over architecture

Do not implement an architectural change because it sounds elegant.

Every major conclusion must be supported by measurements, screenshots, rendered output, profiling data, or direct comparison.

## 2. Preserve the existing engine

The production AV Gen architecture is the control group.

Do not refactor it merely to make the experiment easier.

The experimental implementation should be isolated and disposable.

## 3. Every phase has a gate

At the end of each phase, explicitly classify the result:

* PASS
* CONDITIONAL PASS
* FAIL

A failed gate means STOP.

Do not continue merely because the next phase sounds interesting.

The final research document must clearly state:

> "The hypothesis should be pursued"

or

> "The hypothesis should be abandoned"

based on the evidence.

## 4. Update the research artifact continuously

Create:

`docs/research/gpu-world-architecture-spike.md`

Update it after every phase.

Do not wait until the end.

The document should contain:

* hypothesis
* experiment design
* baseline measurements
* implementation details
* results
* graphs/tables where useful
* screenshots/render captures where useful
* profiling observations
* failures
* surprises
* gate decision
* recommendation for next phase

If the experiment changes direction, document why.

## 5. Prefer the smallest possible experiment

If a 500-line prototype can answer the question, do not build a 5,000-line subsystem.

Avoid premature abstractions.

---

# PHASE 0 — Reconnaissance

## Objective

Understand the current AV Gen rendering architecture sufficiently to design a fair experiment.

Inspect:

* scene representation
* entity representation
* scene flattening
* render preparation
* transform updates
* instance generation
* procedural generators
* GPU buffer uploads
* draw submission
* current compute shader usage
* audio signal propagation
* frame timing/profiling infrastructure
* existing performance instrumentation

Pay particular attention to known problem areas such as:

* scene flattening cost
* CPU entity expansion
* procedural expansion
* CPU → GPU synchronization
* large populations
* audio-reactive parameter evaluation

Do not change production code during this phase.

## Deliverable

Add a "Phase 0 — Architecture Reconnaissance" section to the research artifact.

Document:

1. Current execution path.
2. Where CPU work grows with scene complexity.
3. Where data crosses CPU/GPU boundaries.
4. Existing GPU-driven techniques already present.
5. Candidate workloads for the experiment.
6. Any existing AV Gen feature that is an especially good control case.

## Gate 0

PASS only if you can identify at least one workload where the hypothesis has a plausible measurable advantage.

If no such workload can be identified:

**STOP — abandon the experiment.**

---

# PHASE 1 — Controlled GPU Population Benchmark

## Objective

Determine whether GPU-resident population/animation can substantially reduce CPU overhead for a workload that resembles AV Gen.

This is the most important gate.

If this phase does not demonstrate meaningful value, do not proceed to the more ambitious experiments.

## Build

Create an isolated prototype.

It should render a large population of a simple but visually representative AV Gen asset.

Good candidates include:

* mushroom
* firefly
* grass blade
* rock
* simple alien/creature
* simple geometric audiovisual element

Use an existing AV Gen asset where practical.

Create two implementations.

### Control A — CPU-driven

Conceptually:

```text
CPU:
create/maintain N entities
update transforms
evaluate animation
prepare instance data
upload/render data

GPU:
render
```

### Experimental B — GPU-driven

Conceptually:

```text
CPU:
upload compact parameters

GPU:
generate/maintain transforms
animate population
perform relevant culling
generate/render instances
```

Do not build a general abstraction.

Hard-code the prototype if necessary.

## Population scaling

Test at minimum:

```text
1,000
10,000
100,000
1,000,000
```

If 1,000,000 is unreasonable for the selected asset, choose a larger but still meaningful range and explain why.

## Measure

For each population size record:

* total frame time
* CPU frame time
* GPU frame time
* CPU scene/update time
* CPU render preparation time
* CPU → GPU upload time
* GPU compute time
* GPU rendering time
* memory usage
* number of CPU entities
* number of draw calls
* relevant dispatch counts
* frame rate

Measure enough frames to avoid single-frame noise.

Use release builds.

Run identical camera/view conditions.

Use the same machine and graphics settings.

## Important

Do not merely compare FPS.

The key question is whether CPU work scales differently.

Produce a table and, where useful, graphs showing:

```text
population size → CPU cost
population size → GPU cost
population size → total frame cost
```

## Gate 1 — KILL GATE

The phase PASSES only if the GPU-driven approach demonstrates a **material advantage in a workload relevant to AV Gen**.

As a target rather than an absolute requirement, look for one or more of:

* ≥2× reduction in CPU work at meaningful population sizes
* dramatically better scaling as population increases
* substantially reduced CPU/GPU synchronization
* materially larger feasible population
* materially reduced memory/transfer overhead

A tiny improvement such as 5–15% is NOT sufficient by itself to justify an architectural direction.

If the GPU implementation merely moves work from CPU to GPU while producing no meaningful end-to-end benefit:

**FAIL. STOP.**

If GPU overhead makes the experimental approach substantially worse:

**FAIL. STOP.**

If the benefit is significant but highly workload-specific:

**CONDITIONAL PASS. Continue only if the workload is representative of an important AV Gen use case.**

Update the research artifact with the decision before proceeding.

---

# PHASE 2 — GPU-Resident Audio-Reactive Population

## Only run this phase if Phase 1 passes.

## Objective

Determine whether the architecture provides a particularly strong advantage for AV Gen's defining characteristic: audiovisual reactivity.

Build a population where individual elements respond to:

* beat
* bass
* spectral energy
* time
* deterministic per-instance phase/randomness

Example:

```text
bass → scale
kick → impulse
mids → movement
highs → emission
beat phase → animation phase
```

The CPU should provide compact signal data.

The GPU should derive individual element behavior.

Test at large populations.

Compare against a CPU-driven equivalent.

## Question

Does GPU-resident audiovisual behavior provide:

1. meaningful performance advantages, OR
2. dramatically greater population/detail than practical CPU-driven evaluation?

## Gate 2

PASS if there is a clear advantage relevant to real AV Gen scenes.

FAIL if it is merely an alternate implementation with no meaningful benefit.

If FAIL:

**STOP.**

---

# PHASE 3 — New-Art Experiment

## Only run this phase if Phase 2 passes.

This phase intentionally changes the question.

We are no longer asking:

> "Can this be faster?"

We are asking:

> "Does this architecture enable a category of visual system that would be impractical or unnatural to build with conventional AV Gen entities?"

Create at least three experimental visual systems.

Potential candidates:

### A. Million-element audiovisual field

Millions of independently animated elements responding to audio.

### B. Procedural infinite environment

A world generated around the camera rather than instantiated as a conventional finite scene.

Examples:

* endless hallway
* procedural rooms
* procedural landscape
* generated forest
* generated alien environment

### C. GPU audiovisual organism

A large population where behavior depends on:

* audio
* neighboring elements
* procedural fields
* previous-frame state
* camera position

The point is not technical novelty for its own sake.

The output should be aesthetically useful.

Render actual examples.

Capture screenshots and short clips.

## Gate 3

PASS if at least one experiment demonstrates a compelling visual capability that would be:

* impractical
* prohibitively expensive
* excessively cumbersome
* or architecturally unnatural

using conventional AV Gen entities.

If the result is merely:

> "This is a faster way to render some particles"

then this phase does not pass.

---

# PHASE 4 — Procedural World Representation Experiment

## Only run this phase if Phase 3 passes.

Now test the more ambitious hypothesis.

Can a conceptual world remain compact rather than being expanded into thousands/millions of ordinary entities?

Example:

```text
Forest(
    seed,
    bounds,
    density,
    species,
    wind
)
```

rather than:

```text
Tree
Tree
Tree
Tree
Tree
...
```

Implement one narrow procedural world.

Do NOT build a generalized scene IR yet.

The prototype may be ugly internally.

Measure:

* authoring representation size
* CPU memory
* GPU memory
* CPU entity count
* flattening time
* render preparation time
* frame time
* scaling behavior

Compare against an equivalent conventional scene.

## Gate 4

PASS only if the compact procedural representation produces a meaningful architectural advantage.

Examples:

* avoids expensive flattening
* avoids large entity populations
* dramatically reduces CPU state
* enables much larger worlds
* enables useful dynamic generation
* provides meaningful new optimization opportunities

Otherwise:

**STOP.**

---

# PHASE 5 — Architecture Proposal

## Only run this phase if all previous gates pass.

Now, and only now, design how the successful techniques might become part of AV Gen.

Do not immediately refactor.

Produce an architecture proposal first.

Investigate a hybrid model such as:

```text
AV Gen Scene
│
├── Conventional Objects
│
├── GPU Populations
│
├── GPU Simulations
│
└── Procedural Worlds
```

Determine:

* ownership of state
* CPU/GPU synchronization
* serialization
* editor representation
* debugging
* determinism
* timeline integration
* audio integration
* camera interaction
* picking/selection
* offline rendering
* screenshots/AOVs
* quality scaling
* fallback behavior
* lifecycle management
* memory management
* shader versioning
* WebGPU/Dawn constraints
* Metal behavior
* testing strategy

Explicitly identify what should **not** move to the GPU.

## Critical requirement

Do not propose a total replacement of AV Gen's existing scene architecture unless the experimental evidence makes that unavoidable.

Prefer an additive architecture.

---

# Final Research Decision

At the end of the research artifact, provide a blunt recommendation:

## GREEN — Pursue

Use this only if the experiments demonstrate both:

1. significant measurable technical benefits, and/or
2. genuinely valuable new audiovisual capabilities.

Recommend the smallest production architecture needed to exploit those benefits.

## YELLOW — Targeted adoption

Use this if the general architecture is not justified but specific techniques are clearly valuable.

For example:

> Do not create GPU Worlds, but introduce GPU populations and GPU audio-reactive simulation.

This is a perfectly successful outcome.

## RED — Abandon

Use this if the experiments fail to demonstrate meaningful benefits.

Do not rationalize continuing because the technology is interesting.

---

# Research Discipline

Throughout the experiment, maintain a section called:

## "Reasons This Might Be A Bad Idea"

Update it continuously.

Actively try to falsify the hypothesis.

Record:

* GPU bottlenecks
* synchronization problems
* debugging difficulties
* awkward data access
* increased shader complexity
* poor scalability
* Metal/WebGPU limitations
* visual artifacts
* nondeterminism
* editor integration problems
* offline rendering problems
* cases where CPU implementation is clearly superior

The goal is not to prove the architecture is good.

The goal is to determine whether it deserves to exist.

---

# Final Deliverables

At completion, produce:

1. `docs/research/gpu-world-architecture-spike.md`
2. isolated prototype source
3. benchmark data
4. profiling captures/results
5. representative screenshots
6. short rendered demonstrations where useful
7. final GREEN/YELLOW/RED recommendation

The research document should be understandable by another engineer without reading the entire conversation that led to this experiment.

---

# Important implementation constraint

Do not allow scope creep.

If a phase requires building infrastructure that isn't necessary to answer its question, simplify the experiment.

If a phase fails its gate, stop immediately.

Do not continue into later phases "for completeness."

The most valuable result may be:

> "We tested this rigorously and determined that AV Gen should NOT pursue it."

That is a successful research outcome.
