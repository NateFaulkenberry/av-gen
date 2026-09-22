# AV Gen — Phase E

## Learned Motion, Neural Motion Synthesis & Optional Neural Character Intelligence

## Mission

Build AV Gen's **optional learned-motion architecture**.

Phases A–D established the deterministic character stack:

```text
World
 ↓
Perception
 ↓
Attention
 ↓
Goals / Behavior
 ↓
Intent
 ↓
Navigation
 ↓
Steering
 ↓
MotionRequest
 ↓
Motion Provider
 ↓
Pose Generation
 ↓
Body Adaptation
 ↓
IK / Contacts
 ↓
Renderer
```

Phase E introduces machine-learned components into that architecture without destabilizing or replacing it.

The central principle is:

> **AI should improve motion quality and capability, not become a prerequisite for character simulation.**

A user who never trains a model should still be able to:

* create characters
* author behaviors
* use motion matching
* navigate worlds
* render offline
* use procedural animation
* use AV Gen on an M2 Max

A trained model should be an optional acceleration/quality layer.

The architecture must support neural components such as:

* learned motion matching
* neural locomotion
* learned motion synthesis
* learned motion prediction
* learned trajectory following
* scene-aware motion prediction
* optional learned high-level behavior

but they must all fit behind clean interfaces.

---

# 1. Begin With Research, Not Implementation

Before writing production code, perform a focused research pass.

Inspect:

* Phase A architecture
* Phase B architecture
* Phase C motion database
* Phase D behavior architecture
* MotionRequest
* MotionProvider
* Pose representation
* retargeting
* contacts
* IK
* motion matching
* offline MotionPack generation
* serialization
* deterministic simulation
* threading
* profiling

Then research current learned-motion approaches.

At minimum investigate:

### AI4Animation

https://github.com/sebastianstarke/AI4Animation

Study:

* phase-functioned neural networks
* motion matching
* neural state machines
* learned motion controllers
* motion editing
* IK
* trajectory prediction

Do not assume the repository's code/data can simply be embedded in AV Gen.

Treat research code, model weights and datasets as separately licensed assets.

---

### Learned Motion Matching

https://github.com/E1P3/Learned_Motion_Matching_UE5

https://github.com/E1P3/Learned_Motion_Matching_Training

Study:

* learned feature representations
* learned search
* training pipeline
* runtime architecture
* data requirements

---

### Learned Motion Matching — PyTorch

https://github.com/pau1o-hs/Learned-Motion-Matching

Study:

* model structure
* training requirements
* inference cost
* model inputs/outputs
* possible export formats

---

### SAMP

https://github.com/mohamedhassanmus/SAMP

Study:

* scene-aware motion prediction
* environmental conditioning
* interaction-aware motion
* trajectory conditioning

Also inspect:

https://github.com/mohamedhassanmus/SAMP_Training

---

### OpenMotion

https://github.com/SaxonRah/OpenMotion

Study the relationship between:

```text
motion data
retargeting
representation
learned synthesis
```

---

# 2. Research Questions

Answer these questions before committing to an implementation.

## Runtime

Can the selected model run:

* locally
* on Apple Silicon
* without CUDA
* without Python
* without PyTorch
* without a Python interpreter
* without network access

?

---

## Training

Can training happen:

* on M2 Max
* offline
* optionally
* without requiring users to train anything

?

---

## Export

Can models be exported to:

* ONNX
* Core ML
* another portable representation
* a native AV Gen representation

?

Investigate Apple's Core ML stack:

https://developer.apple.com/machine-learning/core-ml/

Investigate ONNX:

https://onnx.ai/

Investigate ONNX Runtime:

https://onnxruntime.ai/

Do not assume ONNX Runtime automatically gives optimal Apple Silicon performance.

Benchmark.

---

# 3. M2 Max Is a Hard Constraint

AV Gen is being developed and used on Apple Silicon.

Therefore:

**Do not design Phase E around NVIDIA/CUDA assumptions.**

The system must distinguish:

```text
training
```

from:

```text
inference
```

and:

```text
runtime
```

A useful architecture is:

```text
              OFFLINE
                 │
        training / generation
                 │
                 ↓
            Model Asset
                 │
                 ↓
              EXPORT
                 │
                 ↓
             AV Gen
                 │
          ┌──────┴──────┐
          ↓             ↓
      CPU inference   GPU/ML
          │             │
          └──────┬──────┘
                 ↓
          Motion Provider
```

The runtime must not require a training environment.

---

# 4. Do Not Train a Giant Model

The first Phase E model must be deliberately small.

Do not attempt:

* foundation models
* large diffusion systems
* giant transformers
* massive generative motion models
* LLM-based animation
* whole-body humanoid foundation models

The objective is to determine:

> Can a small learned model provide a measurable advantage over the Phase C deterministic motion-matching system?

If not, do not ship it.

---

# 5. Establish a Baseline First

Before introducing neural inference, capture Phase C performance and quality.

Record:

```text
motion-matching search time
transition quality
trajectory error
pose error
foot sliding
contact error
motion continuity
memory usage
```

Use this as the control.

Every neural experiment must answer:

```text
What does the neural system improve?
What does it cost?
```

If the answer is unclear, reject the experiment.

---

# 6. Neural Architecture Boundary

The core interface should remain something conceptually like:

```cpp
class IMotionProvider
{
public:
    virtual MotionResult evaluate(
        const MotionContext& context
    ) = 0;
};
```

Possible implementations:

```text
ClipMotionProvider
ProceduralMotionProvider
MotionMatchingProvider
NeuralMotionProvider
```

The neural system must be just another provider.

Do NOT create:

```text
NeuralCharacterEngine
```

that bypasses the existing system.

---

# 7. Neural Motion Provider

Design:

```text
MotionRequest
      ↓
MotionContext
      ↓
NeuralMotionProvider
      ↓
Predicted pose / motion representation
      ↓
Body adaptation
      ↓
IK
      ↓
Contacts
      ↓
Final pose
```

The provider should not own:

* world simulation
* navigation
* behavior
* rendering
* character identity
* project state

---

# 8. Define the Neural Problem Precisely

Do not begin with:

> "Let's generate animation with AI."

Define a narrow prediction problem.

Potential initial problem:

```text
Inputs:
current pose
current velocity
desired velocity
desired facing
future trajectory
contact state
motion phase
```

Output:

```text
next pose
```

or:

```text
next motion latent/state
```

or:

```text
short future pose sequence
```

Compare these architectures experimentally.

Do not commit prematurely.

---

# 9. First Neural Experiment

The first experiment should be:

> **Learn to continue locomotion from the same MotionContext that Phase C already consumes.**

Input:

```text
current pose
current velocity
desired velocity
future trajectory
facing
phase
contacts
```

Output:

```text
next pose
```

This is intentionally modest.

The purpose is to establish the complete pipeline:

```text
MotionPack
 ↓
training data
 ↓
model
 ↓
export
 ↓
native inference
 ↓
NeuralMotionProvider
 ↓
IK/contact correction
 ↓
Glowmere
```

before attempting sophisticated neural generation.

---

# 10. Training Data

Use the offline MotionPack pipeline from Phase C.

Do not invent an independent data format.

The training dataset should derive from:

```text
MotionPack
```

with appropriate preprocessing.

Potential inputs:

```text
pose features
joint velocities
root velocity
desired velocity
trajectory samples
facing
phase
contacts
terrain information
```

Potential outputs:

```text
future pose
future root motion
future trajectory
latent state
```

Document exactly which representation is used.

---

# 11. Data Splitting

Avoid training/test contamination.

Do not randomly split neighboring frames from the same sequence and call that a meaningful test.

Use splits such as:

```text
motion clip split
character split
motion style split
scenario split
```

where possible.

For example:

```text
training:
walk/run/turn clips A–Z

validation:
different clips

test:
held-out clips
```

The test set must measure generalization.

---

# 12. Start With Existing Glowmere Data

Use the existing Glowmere alien motion data to validate the plumbing.

But recognize the limitation already discovered:

```text
26 clips
~57 seconds
```

This is nowhere near enough for a compelling learned locomotion model.

Therefore:

**Do not claim success based on Glowmere-only training.**

Use it for:

* pipeline validation
* export testing
* deterministic inference
* integration
* debugging

not for demonstrating broad learned locomotion capability.

---

# 13. Introduce 100STYLE

Use the previously identified 100STYLE dataset where licensing and redistribution terms permit the intended use.

Reference:

https://github.com/ADC-Motion/100STYLE

The dataset contains substantially more locomotion diversity than the current Glowmere corpus.

But:

**Do not blindly import millions of frames.**

Start with a controlled subset.

Potential categories:

```text
walk
run
start
stop
turn
strafe
direction change
```

Build a representative training corpus.

Verify current licensing before incorporating any data into a distributed product or derived asset.

---

# 14. Training Pipeline

Create a separate offline training tool/workflow.

Conceptually:

```text
MotionPack
    ↓
Dataset Builder
    ↓
Feature normalization
    ↓
Train / validation / test split
    ↓
Model training
    ↓
Evaluation
    ↓
Export
    ↓
Model asset
```

Do not place training code into the runtime engine.

---

# 15. Python Is Allowed Offline

The constraint is:

> Python must not be required by AV Gen runtime.

It is acceptable to use Python for:

* model training
* dataset processing
* research experiments
* model evaluation
* export

if that materially improves the workflow.

But runtime should consume a portable model artifact.

---

# 16. Training Environment

Investigate what is practical on an M2 Max.

Benchmark:

```text
CPU training
MPS training
small models
medium models
```

Record:

```text
dataset size
training time
memory
batch size
iterations
validation loss
```

Do not spend days training a model merely because the architecture allows it.

The first objective is feasibility.

---

# 17. Model Size

Record:

```text
parameter count
model file size
activation memory
runtime memory
inference latency
```

Test several scales.

For example:

```text
Tiny
Small
Medium
```

Do not decide the winner by parameter count.

Decide based on:

```text
motion quality
latency
memory
stability
generalization
```

---

# 18. Inference Backend Investigation

Investigate:

### Core ML

Apple's native ML inference framework.

https://developer.apple.com/machine-learning/core-ml/

### ONNX

https://onnx.ai/

### ONNX Runtime

https://onnxruntime.ai/

### Metal

https://developer.apple.com/metal/

Determine which gives the best combination of:

* portability
* performance
* model support
* integration complexity
* deterministic behavior
* distribution simplicity

Do not introduce multiple inference backends initially.

Select one measured path.

---

# 19. Native Model Asset

Create an AV Gen model asset concept.

For example:

```text
.neuralmotion
```

or integrate with an existing asset system.

It should contain:

```text
model architecture
weights
input schema
output schema
normalization
version
skeleton compatibility
motion representation
training metadata
license/provenance
backend requirements
```

The exact extension should follow existing AV Gen asset conventions.

---

# 20. Model Compatibility

A model must declare what it supports.

For example:

```text
skeleton profile
joint count
joint ordering
feature layout
coordinate convention
motion representation
```

Do not accidentally feed a model trained for one skeleton into another.

Retargeting should happen at a deliberate boundary.

---

# 21. Neural Model + Retargeting

Investigate two architectures.

### Architecture A

```text
retarget
 ↓
common representation
 ↓
neural model
 ↓
retarget/output
```

### Architecture B

```text
source skeleton
 ↓
neural model
 ↓
native skeleton
```

Architecture A is likely more reusable.

Test both conceptually and determine whether a canonical skeleton representation makes sense for AV Gen.

Do not lock the engine to one humanoid skeleton unless the evidence demands it.

---

# 22. Canonical Motion Representation

Research whether AV Gen should define a canonical motion-space representation.

Potentially:

```text
root
pelvis
spine
neck
head
upper/lower limbs
hands
feet
```

with topology-independent semantic mapping.

The goal would be:

```text
Alien
Human
Creature
```

→ common learned representation

while retaining:

```text
character-specific retargeting
```

This is a major architectural decision.

Do not implement it until the research establishes whether it is useful.

---

# 23. Neural Locomotion

Once the basic prediction experiment works, investigate a proper neural locomotion model.

Potential conditioning:

```text
desired velocity
future trajectory
facing
current pose
phase
contact state
terrain
```

Output:

```text
future pose sequence
```

The model should support:

* walk
* run
* start
* stop
* turns
* direction changes
* strafing where data permits

---

# 24. Neural Model Must Not Replace IK

This is mandatory.

The neural output should pass through:

```text
Neural Pose
 ↓
Body Adaptation
 ↓
IK
 ↓
Contact Solver
 ↓
Final Pose
```

Neural prediction is not trusted as the final geometric authority.

This protects against:

* foot sliding
* terrain penetration
* unreachable targets
* numerical drift
* environment changes

---

# 25. Neural Model Must Not Replace Navigation

Navigation remains:

```text
Behavior
 ↓
Navigation
 ↓
Desired trajectory
 ↓
Neural motion
```

Not:

```text
Neural model
 ↓
figure out where to go
```

at this stage.

Keep path planning deterministic.

---

# 26. Neural Model Must Not Replace Behavior

Similarly:

```text
Behavior
 ↓
Intent
 ↓
MotionRequest
 ↓
NeuralMotionProvider
```

A neural locomotion model should not decide:

```text
investigate mushroom
```

That belongs to Phase D.

---

# 27. Learned Motion Matching

After neural locomotion is working, investigate learned motion matching.

The architecture may become:

```text
MotionRequest
 ↓
learned embedding
 ↓
candidate retrieval
 ↓
classical continuity/contact cost
 ↓
motion selection
```

or:

```text
MotionRequest
 ↓
neural policy
 ↓
motion candidate
```

Benchmark against Phase C's classical search.

The learned approach must demonstrate measurable benefits.

---

# 28. Learned Retrieval Must Not Eliminate Deterministic Validation

Even if a neural model proposes:

```text
candidate frame = 10392
```

the runtime should validate:

* skeleton compatibility
* motion continuity
* contact consistency
* trajectory consistency
* legal state
* motion availability

Never blindly trust neural output.

---

# 29. Neural Motion Synthesis

Investigate more advanced models only after basic neural locomotion works.

Candidates:

* phase-conditioned networks
* autoregressive models
* latent motion models
* diffusion-based motion synthesis
* transformer-based motion prediction

Do not implement all of them.

Build a research comparison.

Evaluate:

```text
quality
latency
memory
training cost
stability
control
repeatability
```

---

# 30. Diffusion

Diffusion-based motion generation should initially be treated as:

> **offline content generation**

not runtime locomotion.

Potential workflow:

```text
prompt / constraints
 ↓
offline generation
 ↓
motion cleanup
 ↓
IK
 ↓
contact validation
 ↓
MotionPack
```

This could eventually be useful for:

* creating missing transitions
* generating starts/stops
* generating stylistic variants
* creating rare behaviors
* expanding a motion library

But it must not become a runtime requirement.

---

# 31. Learned Motion Augmentation

Investigate whether a learned model can augment Phase C's procedural augmentation.

Potential workflow:

```text
existing motion
 ↓
procedural augmentation
 ↓
learned refinement
 ↓
contact cleanup
 ↓
MotionPack
```

Possible applications:

* stride variation
* stylistic variation
* turn variation
* speed variation
* transition synthesis

Measure whether the learned stage actually improves results.

---

# 32. Neural Scene Awareness

After locomotion is stable, investigate scene-aware conditioning.

Potential inputs:

```text
terrain height
slope
obstacles
target position
interaction point
surface normal
```

Output:

```text
motion adapted to environment
```

The first useful scenario:

```text
character walking toward a target
on uneven terrain
```

The system should naturally adapt:

* stride
* foot placement
* body height
* orientation

while Phase A/B IK remains responsible for final contact correction.

---

# 33. Interaction-Aware Motion

Next investigate learned interaction motion.

Examples:

```text
reach toward mushroom
lean toward object
look at object
step into interaction stance
```

Do not attempt generalized human-object interaction generation immediately.

Use narrow, measurable tasks.

---

# 34. Character Behavior AI

Only after learned motion is stable should Phase E investigate learned behavior.

The interface remains:

```text
IBehaviorProvider
```

Potential:

```text
LearnedBehaviorProvider
```

Input:

```text
perception
memory
world state
current goal
personality
history
```

Output:

```text
CharacterIntent
```

The learned behavior provider must remain optional.

The deterministic provider remains the fallback.

---

# 35. No LLM in the Core

Do not introduce an LLM into:

```text
per-frame perception
motion
IK
navigation
```

If experimenting with language-driven behavior, keep it outside the simulation core.

For example:

```text
optional language layer
       ↓
high-level goal
       ↓
deterministic Phase D
```

This ensures:

* predictable behavior
* no network requirement
* no API cost
* no latency spikes
* deterministic rendering
* offline rendering compatibility

---

# 36. Model Failure Handling

Neural systems fail.

Design explicitly for:

```text
invalid model
inference failure
NaN
unexpected output
unsupported skeleton
out-of-distribution state
missing model
backend unavailable
```

Fallback:

```text
Neural
 ↓
validation failure
 ↓
Motion Matching
 ↓
Procedural / Clip
```

A neural failure must never destroy the character.

---

# 37. Confidence / Uncertainty

Investigate whether the model can provide a useful confidence signal.

For example:

```text
prediction confidence
distribution distance
embedding distance
out-of-distribution score
```

If confidence falls below a threshold:

```text
Neural
 ↓
fallback
```

Do not invent a confidence score that has no empirical meaning.

Validate it.

---

# 38. Neural Motion Continuity

Measure:

```text
position discontinuity
velocity discontinuity
acceleration discontinuity
angular velocity discontinuity
foot contact discontinuity
```

Neural output must not introduce visible popping.

Compare against:

```text
Phase C motion matching
```

and:

```text
Phase B inertialization
```

---

# 39. Contact Quality

Measure:

```text
foot penetration
foot sliding
contact timing
contact position error
ground height error
```

Neural systems frequently generate visually plausible motion that fails exact contacts.

Phase A/B remains the correction authority.

---

# 40. Motion Quality Metrics

Build an offline evaluator.

At minimum:

```text
trajectory error
pose error
velocity error
foot sliding
foot penetration
contact timing
root drift
facing error
joint-limit violations
```

Also create visual evaluation renders.

Automated metrics alone are insufficient.

---

# 41. A/B Evaluation

Every neural experiment should compare:

```text
Classical Motion Matching
vs
Neural Motion
```

under identical:

```text
MotionRequest
trajectory
environment
seed
character
```

Record:

```text
quality
latency
memory
failure rate
```

Do not declare the neural system better merely because it looks novel.

---

# 42. Determinism

Investigate deterministic inference.

Same:

```text
model
input
seed
runtime
```

should produce the same result to the tolerance appropriate to the backend.

If a backend is nondeterministic, document that explicitly.

For offline rendering, provide a deterministic mode where feasible.

---

# 43. Offline Rendering

Neural animation must work during:

* interactive preview
* viewport playback
* offline rendering
* frame export
* multi-camera rendering

The model must not require:

```text
internet
remote inference
```

---

# 44. Scrubbing

Support deterministic reconstruction.

If the neural model is stateful:

```text
frame N
```

may depend on:

```text
frames 0...N
```

This complicates scrubbing.

Research and implement one of:

```text
stateless inference
```

or:

```text
checkpointed neural state
```

or:

```text
deterministic replay
```

Do not ignore this.

---

# 45. Stateful vs Stateless Neural Models

Explicitly compare:

### Stateless

```text
current context
 ↓
next pose
```

Advantages:

* easy scrubbing
* deterministic
* simple integration

Disadvantages:

* less temporal memory

### Stateful

```text
current context + hidden state
 ↓
next state + pose
```

Advantages:

* temporal continuity
* potentially better motion

Disadvantages:

* replay complexity
* checkpointing
* state management

Do not choose based on novelty.

Measure the tradeoff.

---

# 46. Neural Model Caching

Investigate whether inference can be batched across characters.

For example:

```text
Character 1 context
Character 2 context
Character 3 context
...
```

→ one batched inference call.

This may significantly improve throughput.

Benchmark:

```text
1
10
50
100
500
```

characters.

---

# 47. Model Memory

Measure:

```text
weights
activations
temporary buffers
backend allocations
```

At:

```text
1 model
2 models
5 models
```

Do not load duplicate model weights for every character.

Shared model resources are mandatory.

---

# 48. Multi-Character Neural Runtime

The architecture should allow:

```text
100 aliens
```

to share:

```text
one model
one inference backend
```

with per-character:

```text
input context
state
output pose
```

where practical.

---

# 49. Model Lifecycle

Implement:

```text
load
validate
initialize
warmup
evaluate
unload
```

Model loading must not block the UI unnecessarily.

This is especially relevant given AV Gen's ongoing interactive-performance work.

Do not introduce:

```text
click character
→ UI freezes
→ model initializes
```

during ordinary interaction.

Use asynchronous loading where appropriate.

---

# 50. Hot Swapping

If practical, support:

```text
Model A
 ↓
Model B
```

without restarting the project.

This will be extremely useful during development.

Do not allow half-initialized model state into rendering.

Use:

```text
load
validate
warm
atomic swap
```

or an equivalent safe lifecycle.

---

# 51. Neural Model Versioning

Model assets need:

```text
architecture version
feature schema version
normalization version
skeleton profile
training dataset version
runtime compatibility
```

A model trained against one MotionPack schema must not silently load against an incompatible one.

---

# 52. Licensing / Provenance

Every model and training dataset must have provenance.

Record:

```text
training datasets
dataset licenses
source repositories
code licenses
model license
derived-data status
```

Do not assume:

```text
GitHub repo license
=
training dataset license
```

They are separate.

Do not ship research weights/data unless their licensing permits the intended use.

---

# 53. Security / Sandboxing

Treat model files as external assets.

Do not permit model assets to execute arbitrary code.

The runtime should load:

```text
data
```

not:

```text
executable Python
```

or:

```text
arbitrary scripts
```

---

# 54. Training Reproducibility

Record:

```text
dataset version
training configuration
random seed
model architecture
software version
feature schema
normalization
```

A trained model should be reproducible as far as the underlying framework permits.

---

# 55. Offline CLI

Create tooling conceptually similar to:

```text
avgen-motion-train
avgen-motion-evaluate
avgen-motion-export
avgen-motion-benchmark
```

The exact naming should follow existing AV Gen tools.

Potential commands:

```text
train
evaluate
compare
export
inspect
benchmark
```

---

# 56. Evaluation Report

Every model should produce a report:

```text
Model:
NeuralLocomotion-v1

Dataset:
100STYLE locomotion subset

Training:
...

Parameters:
...

Inference:
...

Latency:
...

Memory:
...

Trajectory error:
...

Foot sliding:
...

Contact error:
...

Failure rate:
...
```

Compare against:

```text
MotionMatching-v1
```

---

# 57. Visual Evaluation Harness

Create deterministic scenes containing:

```text
flat terrain
sloped terrain
obstacles
turns
starts
stops
direction changes
target approach
```

Render:

```text
classical motion matching
vs
neural
```

using identical camera/timing.

This becomes the visual benchmark.

---

# 58. Glowmere Neural Demonstration

The first meaningful demonstration should be:

```text
Glowmere alien
 ↓
Phase D autonomous behavior
 ↓
MotionRequest
 ↓
NeuralMotionProvider
 ↓
Body adaptation
 ↓
IK
 ↓
final animation
```

The alien should:

* wander
* accelerate
* decelerate
* turn
* approach targets
* stop
* change direction
* adapt to terrain
* investigate objects

The behavior itself remains Phase D.

Only the motion execution is neural.

---

# 59. Fallback Demonstration

Disable the neural model.

The exact same behavior should still work:

```text
Phase D
 ↓
MotionRequest
 ↓
MotionMatchingProvider
 ↓
IK
```

Then disable motion matching.

It should still degrade to:

```text
Clip / Procedural
```

This proves that neural motion is an optional enhancement rather than an architectural dependency.

---

# 60. Learned Behavior Demonstration — Optional

Only if learned behavior research succeeds, demonstrate:

```text
Perception
 ↓
LearnedBehaviorProvider
 ↓
CharacterIntent
 ↓
Phase D navigation
 ↓
NeuralMotionProvider
```

But compare this against:

```text
Perception
 ↓
Deterministic Behavior
```

and keep the deterministic path fully supported.

---

# 61. Do Not Build Everything

Phase E is a research-heavy phase.

Use explicit gates.

### Gate 1

Can a small model train?

### Gate 2

Can it export?

### Gate 3

Can AV Gen load it natively?

### Gate 4

Can inference run fast enough?

### Gate 5

Does it produce useful motion?

### Gate 6

Does it outperform Phase C on at least one meaningful metric?

### Gate 7

Does it remain stable after IK/contact correction?

Only continue if the answer is yes.

---

# 62. Phase E Vertical Slices

## E.1 — Neural Pipeline Probe

Build:

```text
dataset
 ↓
tiny model
 ↓
export
 ↓
native inference
```

No production integration.

---

## E.2 — NeuralMotionProvider

Connect:

```text
MotionRequest
 ↓
neural model
 ↓
pose
```

---

## E.3 — Correction Stack

Connect:

```text
neural
 ↓
body adaptation
 ↓
IK
 ↓
contacts
```

---

## E.4 — Benchmark

Compare:

```text
neural
vs
motion matching
```

---

## E.5 — Glowmere

Run actual autonomous alien.

---

## E.6 — 100STYLE

Train/evaluate on representative locomotion data.

---

## E.7 — Scene Awareness

Only after basic locomotion works.

---

## E.8 — Learned Retrieval

Only if evidence supports it.

---

## E.9 — Optional Learned Behavior

Only after motion is successful.

---

# 63. Testing

Add tests for:

### Model loading

* valid model
* invalid model
* incompatible schema
* incompatible skeleton
* missing model

### Inference

* finite outputs
* bounded outputs
* deterministic outputs where supported

### Motion

* trajectory
* pose
* velocity
* contacts

### Fallback

* model failure
* backend unavailable
* out-of-distribution state

must gracefully transition to:

```text
Motion Matching
```

or:

```text
Clip
```

---

# 64. Adversarial Testing

Continue the Phase A lesson.

Never rely solely on null/default inputs.

Test:

```text
high-speed turn
sudden stop
reverse direction
extreme trajectory
terrain discontinuity
target suddenly disappears
model unavailable
invalid feature normalization
wrong skeleton
NaN input
very large input
very small input
```

The model must not produce:

```text
NaN
infinite transform
exploding joints
teleport
unbounded velocity
```

---

# 65. Out-of-Distribution Testing

A neural system should be deliberately tested outside its training distribution.

Examples:

```text
unusual speed
sharp turn
steep slope
unexpected obstacle
unusual character scale
missing contact
```

Measure degradation.

Do not hide failures by constraining the test to ideal conditions.

---

# 66. Runtime Guardrails

Implement output validation.

Check:

```text
finite
joint limits
root velocity
position bounds
rotation validity
bone length
contact plausibility
```

If invalid:

```text
reject output
fallback
```

The renderer must never receive obviously corrupt neural state.

---

# 67. Neural Quality Gates

A model is not production-ready merely because:

```text
loss decreases
```

Require:

```text
visual quality
motion continuity
contact quality
control responsiveness
latency
memory
failure rate
```

---

# 68. Performance Targets

Do not invent absolute thresholds before measuring the baseline.

Instead establish:

```text
Phase C baseline
```

then require neural inference to meet a clearly documented budget.

Measure:

```text
single-character latency
batch latency
CPU
GPU
memory
startup
model load
```

Test:

```text
1
10
50
100
500
```

characters where practical.

---

# 69. UI Responsiveness

Given AV Gen's ongoing interactive-performance work, neural systems must not regress editor responsiveness.

Model operations should not cause:

```text
UI stalls
timeline stalls
scrubbing stalls
canvas stalls
```

Separate:

```text
model loading
model initialization
inference
```

from:

```text
UI thread
```

where technically appropriate.

---

# 70. Offline Rendering Budget

A neural model can be slower in offline rendering than interactive preview if it materially improves quality.

Therefore distinguish:

```text
interactive quality
offline quality
```

Allow an offline-quality setting if justified.

But do not use this as an excuse for an unusably slow model.

---

# 71. Future Generative Motion

Document but do not necessarily implement:

```text
text / constraints
 ↓
offline generative motion
 ↓
retarget
 ↓
contacts
 ↓
MotionPack
```

Potential future uses:

* fill motion gaps
* create transition libraries
* generate stylized movement
* generate rare interactions
* generate creature locomotion
* produce artist-editable motion assets

The generated result should become ordinary AV Gen motion data.

That is preferable to requiring a model to run forever.

---

# 72. Artist Control

AI must not remove author control.

The artist should still be able to specify:

```text
speed
style
energy
direction
trajectory
facing
intent
timing
personality
```

The neural system interprets constraints.

It does not own the creative decision.

---

# 73. Neural Style Conditioning

Investigate whether motion style can be represented separately from locomotion intent.

Potential dimensions:

```text
energy
weight
urgency
personality
style
stride
posture
```

Example:

```text
same desired velocity
+
different style
=
different motion
```

Do not implement a giant style embedding system unless the data supports it.

---

# 74. Creature Generalization

Do not assume all learned motion must be human-like.

Investigate whether the architecture supports:

```text
alien
animal
robot
humanoid
```

The learned model may initially target humanoid locomotion.

Document this limitation.

Do not fake generality.

---

# 75. Research Outcome Categories

At the end of each experiment classify it:

```text
PROMOTE
```

Useful enough for production.

```text
EXPERIMENTAL
```

Interesting but not production-ready.

```text
REJECT
```

Does not provide sufficient benefit.

This prevents "AI for AI's sake."

---

# 76. Definition of Done

Phase E is complete when AV Gen has:

### Architecture

* optional neural motion provider
* clean model asset representation
* offline training/export pipeline
* native inference path
* model validation
* fallback system

### Motion

A neural model can consume:

```text
MotionRequest
+
MotionContext
```

and produce useful motion.

### Integration

```text
Phase D
 ↓
MotionRequest
 ↓
NeuralMotionProvider
 ↓
Phase A/B correction
```

works in real AV Gen playback.

### Benchmark

There is a measured comparison against:

```text
Phase C MotionMatchingProvider
```

### Robustness

Neural failure gracefully falls back.

### Performance

M2 Max performance is documented.

### Determinism

Offline rendering/replay behavior is documented and supported to the extent the backend permits.

### Licensing

Training/model provenance is documented.

### Artist control

Neural motion remains controlled by AV Gen's existing intent and motion interfaces.

---

# 77. What Phase E Must NOT Become

Do not let this phase turn into:

```text
"Build an AI character"
```

as an amorphous goal.

The actual architecture should remain:

```text
                CHARACTER
                    │
                BEHAVIOR
                    │
                 INTENT
                    │
              NAVIGATION
                    │
            MOTION REQUEST
                    │
        ┌───────────┴───────────┐
        │                       │
 CLASSICAL                 NEURAL
 MOTION                     MOTION
 MATCHING                   PROVIDER
        │                       │
        └───────────┬───────────┘
                    ↓
             BODY ADAPTATION
                    ↓
                 IK
                    ↓
               CONTACTS
                    ↓
                 POSE
```

The neural system is one branch.

It is not the engine.

---

# 78. Final Target Architecture

After Phase E:

```text
                         WORLD
                           │
                 ┌─────────┴─────────┐
                 ↓                   ↓
             EVENTS              STATE
                 │                   │
                 └─────────┬─────────┘
                           ↓
                      PERCEPTION
                           ↓
                       ATTENTION
                           ↓
                    GOALS / BEHAVIOR
                           ↓
                         INTENT
                           ↓
                      NAVIGATION
                           ↓
                       STEERING
                           ↓
                    MOTION REQUEST
                           ↓
             ┌─────────────┼─────────────┐
             ↓             ↓             ↓
           CLIP       MOTION MATCH     NEURAL
             │             │             │
             └─────────────┼─────────────┘
                           ↓
                    POSE GENERATION
                           ↓
                    BODY ADAPTATION
                           ↓
                      IK / CONTACT
                           ↓
                       FINAL POSE
                           ↓
                        RENDER
```

And offline:

```text
RAW MOTION
    ↓
RETARGET
    ↓
CLEAN
    ↓
CONTACT / PHASE
    ↓
TRAJECTORY
    ↓
AUGMENT
    ↓
MOTIONPACK
    ↓
 ┌───────────────┬────────────────┐
 ↓               ↓                ↓
CLASSICAL MM   TRAINING DATA   OFFLINE AI
                 ↓                ↓
               MODEL          GENERATED MOTION
                 ↓                ↓
                 └───────┬────────┘
                         ↓
                    AV GEN ASSET
```

---

# 79. Final Demonstration

The strongest Phase E demonstration is not a flashy AI demo.

It is this:

Start with the exact autonomous Glowmere alien from Phase D.

Run:

```text
Alien
 ↓
Perception
 ↓
Behavior
 ↓
Investigate mushroom
 ↓
Navigation
 ↓
MotionRequest
```

Then toggle:

```text
Motion Provider:
Motion Matching
```

Observe.

Then:

```text
Motion Provider:
Neural
```

Observe.

The behavior should remain fundamentally identical.

The difference should be in the **quality, continuity, responsiveness and adaptability of the physical motion**.

Then deliberately disable the model.

The character should continue functioning through the classical fallback.

That proves the architecture is correct.

---

# 80. Ultimate Objective

The long-term AV Gen character stack should eventually look like:

```text
                   WORLD
                     ↓
                PERCEPTION
                     ↓
             MEMORY / ATTENTION
                     ↓
              GOALS / BEHAVIOR
                     ↓
                  INTENT
                     ↓
                NAVIGATION
                     ↓
                 STEERING
                     ↓
              MOTION REQUEST
                     ↓
          ┌──────────┼──────────┐
          │          │          │
        CLIP     MOTION MATCH  NEURAL
          │          │          │
          └──────────┼──────────┘
                     ↓
              POSE GENERATION
                     ↓
              BODY ADAPTATION
                     ↓
                IK / CONTACT
                     ↓
                  RENDER
```

With offline intelligence:

```text
MOTION DATA
     ↓
RETARGET
     ↓
ANALYZE
     ↓
AUGMENT
     ↓
MOTIONPACK
     ↓
┌────┴───────────────┐
│                    │
CLASSICAL           AI
SEARCH               │
                     ├── learned motion
                     ├── learned retrieval
                     ├── scene-aware synthesis
                     └── generative offline tools
```

The guiding rule for the entire phase is:

> **Use machine learning where it demonstrably creates a capability that is difficult or expensive to achieve deterministically. Keep everything else deterministic.**

The result should not feel like "an AI character."

It should simply feel like **a character that moves better**.
