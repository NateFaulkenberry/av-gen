# AV GEN — PHASE C

# MOTION LIBRARY, MOTION AUGMENTATION & MOTION MATCHING

## Mission

Build AV Gen's production-grade **motion library and motion matching system**.

Phase A established the animation foundation:

* retargeting
* skeleton mapping
* arbitrary IK
* contact extraction
* phase extraction
* velocity/facing representation
* inertialization
* reusable animation data

Phase B established procedural adaptation:

* acceleration/deceleration
* directional locomotion
* turning
* stride adaptation
* foot placement
* terrain adaptation
* body compensation
* look-at
* reaching
* secondary motion
* MotionRequest/MotionController infrastructure

Phase C now answers a different question:

> **Given a large library of possible motions and a character's current state plus desired future movement, which motion should AV Gen use next?**

The intended result is that a character is no longer constrained to:

```text
WalkForward
RunForward
TurnLeft
TurnRight
```

with manually authored transition graphs.

Instead:

```text
Current Motion
+
Current Pose
+
Velocity
+
Desired Velocity
+
Facing
+
Future Trajectory
+
Phase
+
Contacts
+
Motion Constraints
        ↓
   Motion Search
        ↓
Best continuation
        ↓
Procedural cleanup
        ↓
Final Pose
```

This phase should produce a **real, measurable, production-oriented motion-matching system**, not merely a research demo.

---

# 1. READ THE EXISTING SYSTEM FIRST

Before writing code:

Read and understand:

```text
docs/design/autonomous-character-animation.md
```

and all relevant Phase A / Phase B ADRs and implementation.

Inspect:

* Animation
* Skeleton
* Pose
* Retargeting
* MotionPack
* Contacts
* Phase
* MotionRequest
* MotionController
* procedural layers
* inertialization
* root motion
* existing profiling infrastructure
* serialization
* asset loading
* offline tooling

Do not recreate systems that already exist.

Do not assume the architecture described in earlier prompts exactly matches the repository.

Adapt to the actual implementation.

---

# 2. PHASE C IS NOT "ADD A NEAREST-NEIGHBOR SEARCH"

A naive implementation such as:

```text
current pose
    ↓
compare every frame
    ↓
choose lowest Euclidean distance
```

is not sufficient.

Production motion matching requires consideration of:

* current pose
* current velocity
* desired velocity
* facing
* future trajectory
* phase
* contacts
* root motion
* continuity
* locomotion mode
* motion constraints

The system should be designed around a meaningful **motion query**.

---

# 3. CORE ARCHITECTURE

Target architecture:

```text
MotionRequest
      ↓
MotionController
      ↓
MotionQuery
      ↓
MotionDatabase
      ↓
Candidate Generation
      ↓
Candidate Filtering
      ↓
Feature Search
      ↓
Cost Evaluation
      ↓
Best Motion Sample
      ↓
Continuation
      ↓
Pose Evaluation
      ↓
Phase / Contact / IK
      ↓
Procedural Adaptation
      ↓
Final Pose
```

Keep motion matching as a **MotionProvider**.

Conceptually:

```text
IMotionProvider
├── ClipMotionProvider
├── ProceduralMotionProvider
└── MotionMatchingProvider
```

Do not make MotionController itself become the motion-matching implementation.

---

# 4. MOTION DATABASE

Create a production-oriented representation of a motion database.

Conceptually:

```text
MotionDatabase
├── skeleton
├── clips
├── samples
├── features
├── trajectories
├── contacts
├── phases
├── metadata
├── search structures
└── provenance
```

Do not blindly implement every field.

Only add fields that are actually required.

The database should be:

* versioned
* serializable
* inspectable
* deterministic
* shared between character instances

---

# 5. MOTION SAMPLE

Define the atomic searchable unit.

Conceptually:

```text
MotionSample
    clip
    time
    pose reference
    phase
    contacts
    velocity
    trajectory
    feature offset
```

The sample should NOT duplicate an entire pose if unnecessary.

Prefer references/indices into shared arrays.

Memory efficiency matters.

A large motion library may contain hundreds of thousands or millions of samples.

---

# 6. DATABASE MEMORY DESIGN

Before implementing the search system, calculate expected memory.

Measure:

```text
10,000 samples
100,000 samples
1,000,000 samples
```

Estimate/measure:

* feature memory
* sample metadata
* trajectory memory
* pose data
* search structure
* total MotionPack size

Avoid:

```text
one giant C++ object per frame
```

Prefer compact contiguous data.

Think in terms of:

```text
arrays
indices
packed features
```

rather than object-heavy structures.

---

# 7. FEATURE REPRESENTATION

Research and implement a compact feature representation.

Potential feature groups:

## Current pose

Selected joint positions/orientations.

## Current velocity

Character/root velocity.

## Future trajectory

Future positions and velocities.

## Facing

Current and future facing.

## Contacts

Foot/hand contact state where useful.

## Phase

Motion cycle phase.

Do NOT automatically include every skeleton joint.

Feature selection is critical.

---

# 8. FEATURE CONFIGURATION

Features should be data-driven.

Conceptually:

```text
MotionFeatureConfig
├── joints
├── trajectory samples
├── dimensions
├── weights
├── normalization
├── position/orientation choices
└── contact features
```

Do not hardcode:

```text
leftFoot
rightFoot
pelvis
head
```

into the search implementation.

Character-specific feature selection belongs in configuration.

---

# 9. FEATURE NORMALIZATION

Raw feature dimensions have different scales.

For example:

```text
position = meters
velocity = m/s
orientation = unitless
phase = 0..1
```

Implement normalization/scaling.

Measure the effect.

Document the convention.

Avoid arbitrary weights without explaining why they exist.

---

# 10. COST FUNCTION

Implement an explicit motion-matching cost.

Conceptually:

```text
cost =
    poseCost
  + trajectoryCost
  + velocityCost
  + facingCost
  + phaseCost
  + contactCost
  + transitionCost
```

The exact terms should emerge from research and experiments.

Every term should have a configurable weight.

Avoid an opaque scoring function.

---

# 11. CONTINUITY COST

One of the most important additions beyond a naive nearest-neighbor search:

Do not allow the system to constantly jump between unrelated clips simply because they happen to have similar poses.

Include continuity considerations.

Potential inputs:

* current sample
* previous sample
* source clip
* phase
* root velocity
* transition distance

The system should prefer coherent continuation.

---

# 12. TRANSITION COST

Add a transition penalty where appropriate.

For example:

```text
current motion:
walk_forward clip A

candidate:
idle_pose clip Z
```

Even if the pose looks similar, the transition may be undesirable.

A candidate from a compatible locomotion family may be preferred.

Do not hardcode clip names.

Use motion metadata.

---

# 13. MOTION TAGS / METADATA

Add metadata where useful:

```text
locomotion
walk
run
turn
start
stop
strafe
idle
jump
land
```

Potential metadata:

```text
style
speed range
direction
motion family
contact profile
root-motion mode
```

Tags should assist filtering.

They should NOT replace feature matching.

---

# 14. CANDIDATE FILTERING

Before expensive feature scoring, eliminate obviously invalid candidates.

Examples:

```text
wrong skeleton
wrong locomotion mode
incompatible motion family
invalid contact state
impossible direction
```

This can significantly reduce search cost.

Measure how much it helps.

Do not build a complicated rule engine unnecessarily.

---

# 15. SEARCH STRATEGY

Research practical search approaches.

Evaluate at least:

### Linear scan

Simple baseline.

### Spatial partitioning

Potentially:

* KD-tree
* PCA projection
* grid
* tree-based feature search

### Approximate nearest neighbor

Potential future option.

Do NOT assume an ANN library is automatically better.

Benchmark against linear search on actual motion data.

The earlier research already demonstrated that synthetic benchmark results can mislead.

Therefore:

> **Use real AV Gen motion distributions as the primary benchmark.**

---

# 16. TWO-STAGE SEARCH

Prefer investigating a structure like:

```text
candidate filtering
       ↓
cheap feature search
       ↓
top N candidates
       ↓
full cost evaluation
       ↓
best candidate
```

This allows expensive terms to be applied only to a small number of candidates.

Do not prematurely overengineer it.

---

# 17. SEARCH BENCHMARK

Benchmark:

```text
1,700 frames
10,000 frames
100,000 frames
1,000,000 frames
```

where practical.

For each:

* query latency
* average latency
* worst-case latency
* candidate count
* memory
* database load time
* build time

Measure on M2 Max.

---

# 18. REAL DATA FIRST

The current Glowmere corpus is only ~1,712 frames.

That is useful for correctness.

It is NOT sufficient to prove motion matching quality.

Use:

* Glowmere data for integration
* appropriately licensed external data for scale

100STYLE is particularly relevant because the previous research identified it as:

* large
* diverse
* locomotion-rich
* appropriately licensed under CC BY 4.0

But verify current licensing and dataset terms before packaging anything.

Do not assume third-party code/data licenses are interchangeable.

---

# 19. BUILD THE FIRST DATABASE FROM GLOWMERE

Before importing millions of frames:

Build:

```text
GlowmereMotionDatabase
```

from the existing alien corpus.

Use it to validate:

* database generation
* feature extraction
* serialization
* querying
* playback
* transitions

The database should be able to reproduce existing animation behavior.

---

# 20. 100STYLE SCALE EXPERIMENT

After the Glowmere database works:

Build a representative 100STYLE subset.

Do not start with the entire dataset.

Include:

* walk
* run
* starts
* stops
* turns
* directional movement
* varied styles

Process through the existing pipeline:

```text
100STYLE
 ↓
license/provenance
 ↓
skeleton analysis
 ↓
retarget
 ↓
contact extraction
 ↓
phase extraction
 ↓
trajectory extraction
 ↓
quality validation
 ↓
feature extraction
 ↓
MotionDatabase
```

Measure:

* database size
* build time
* memory
* query performance
* motion diversity

---

# 21. MOTION AUGMENTATION

Use Phase B's offline procedural tools to increase coverage.

Potential augmentation:

```text
mirroring
speed variation
stride variation
directional warping
turn variation
start/stop variants
root-motion adaptation
```

Do not generate redundant samples merely to increase the number.

The goal is **coverage**, not frame count.

---

# 22. MOTION COVERAGE ANALYSIS

Create an offline analyzer capable of showing coverage.

Potential dimensions:

```text
speed
direction
acceleration
turn rate
phase
locomotion mode
```

For example:

```text
        speed
          ↑
          │       RUN
          │
          │  WALK
          │
          └────────────→ direction
```

Identify gaps.

This will help determine whether additional motion needs to be authored/generated.

---

# 23. MOTION DATABASE QUALITY ANALYZER

Create an offline quality report.

Potential metrics:

* duplicate sample percentage
* feature density
* directional coverage
* speed coverage
* turn coverage
* start coverage
* stop coverage
* contact quality
* foot sliding
* root-motion discontinuities
* joint-limit violations

Output something machine-readable and human-readable.

---

# 24. TRAJECTORY REPRESENTATION

Trajectory is one of the most important motion-matching inputs.

Represent future motion at multiple horizons.

For example:

```text
t + 0.1s
t + 0.2s
t + 0.4s
t + 0.8s
```

Exact horizons should be experimentally validated.

Store:

* future position
* future velocity
* future facing

where useful.

Do not assume every dimension improves quality.

Benchmark feature configurations.

---

# 25. TRAJECTORY PREDICTION

At runtime, generate a desired trajectory from MotionRequest.

Potential inputs:

```text
current velocity
desired velocity
desired facing
turn rate
acceleration limits
```

The prediction should be lightweight.

Do not require navigation or behavior systems.

Phase D will eventually provide richer future intent.

---

# 26. MOTION MATCHING LOOP

The runtime loop should conceptually be:

```text
MotionRequest
      ↓
predict short future trajectory
      ↓
build MotionQuery
      ↓
filter candidates
      ↓
search database
      ↓
score candidates
      ↓
select best sample
      ↓
play from sample
      ↓
procedural adaptation
      ↓
evaluate next frame
```

Avoid searching the entire database every frame if unnecessary.

---

# 27. SEARCH FREQUENCY

Investigate whether search should occur:

* every frame
* at fixed intervals
* only when trajectory changes
* only when current motion becomes invalid
* after a minimum continuation duration

Do not assume every-frame search is necessary.

Benchmark.

---

# 28. SEARCH HYSTERESIS

Avoid rapid switching:

```text
A
B
A
B
A
B
```

Introduce controlled hysteresis or continuation bias.

The current motion should have a reasonable advantage unless a significantly better candidate exists.

Do not make hysteresis so strong that the system refuses to adapt.

---

# 29. MOTION LOCK / MINIMUM CONTINUATION

Consider a minimum continuation window.

For example:

```text
selected motion
      ↓
continue for N frames
      ↓
permit new search
```

But don't hardcode a universal duration.

Use motion phase/contact information where useful.

---

# 30. CONTACT-AWARE MATCHING

The search should understand contact state.

Avoid selecting:

```text
candidate with left foot planted
```

when the current character state strongly indicates:

```text
right foot planted
```

Use contacts as a feature/filter.

This is particularly important for:

* starts
* stops
* turns
* locomotion transitions

---

# 31. PHASE-AWARE MATCHING

Use Phase A phase metadata.

A candidate with compatible phase should receive an advantage.

But do not require identical phase.

The system should be able to discover better motion when phase differs sufficiently.

---

# 32. ROOT MOTION CONTINUITY

Selected motion must not cause:

* teleportation
* root velocity spikes
* sudden direction reversals
* foot skating

Measure root displacement between:

```text
current pose
candidate continuation
```

Reject or penalize pathological transitions.

---

# 33. INTEGRATE WITH PHASE B PROCEDURAL LAYERS

Motion matching should NOT replace:

* foot IK
* terrain adaptation
* look-at
* reach
* balance
* secondary motion
* inertialization

The intended pipeline is:

```text
Motion Matching
      ↓
Base Pose
      ↓
Phase B procedural adaptation
      ↓
IK / contacts
      ↓
Final Pose
```

This is critical.

Motion matching chooses motion.

Procedural systems make the motion fit the actual world.

---

# 34. MOTION MATCHING SHOULD NOT OWN BEHAVIOR

Do not add:

```text
if mushroom:
    choose inspection motion
```

to the motion matcher.

The matcher receives motion requirements.

Behavior belongs to Phase D.

---

# 35. FALLBACK SYSTEM

Motion matching must fail gracefully.

Possible failure conditions:

* database unavailable
* no valid candidate
* invalid query
* skeleton mismatch
* all candidates filtered
* corrupted database

Fallback:

```text
MotionMatchingProvider
        ↓
no valid candidate
        ↓
ClipMotionProvider / procedural fallback
```

Never leave the character frozen because the matcher failed.

---

# 36. DATABASE VERSIONING

Motion databases must be versioned.

Track:

```text
format version
feature schema version
skeleton version
retarget profile version
source data
processing tool version
```

Changing feature definitions should invalidate incompatible search data.

Do not silently load incompatible databases.

---

# 37. OFFLINE / RUNTIME SEPARATION

Offline:

```text
Import
 ↓
Retarget
 ↓
Clean
 ↓
Contacts
 ↓
Phase
 ↓
Trajectory
 ↓
Feature extraction
 ↓
Quality analysis
 ↓
Database build
 ↓
Search structure
 ↓
MotionPack
```

Runtime:

```text
MotionPack
 ↓
MotionQuery
 ↓
Search
 ↓
Pose
 ↓
Procedural adaptation
```

Do not perform expensive feature extraction at runtime if it can be baked.

---

# 38. MOTIONPACK EXTENSION

Extend the Phase A MotionPack format only as necessary.

Potential:

```text
MotionPack
├── skeleton
├── clips
├── contacts
├── phases
├── trajectories
├── features
├── motion database
├── search index
├── provenance
└── validation report
```

Keep large arrays contiguous.

Consider memory mapping later if useful.

Do not implement memory mapping unless profiling shows value.

---

# 39. DATABASE LOADING

Asset-heavy project loading has already been identified as a UI responsiveness concern.

Do NOT load huge motion databases synchronously on the UI thread.

This phase must cooperate with AV Gen's interactive performance architecture.

Prefer:

```text
request
 ↓
background load/decode
 ↓
incremental preparation
 ↓
publish immutable database
```

The editor should remain responsive.

---

# 40. HOT-SWAP SAFETY

Where practical, allow a MotionDatabase to be built/loaded independently and then atomically published.

Avoid partially initialized databases being visible to runtime systems.

This will be valuable later for:

* editing
* database rebuilding
* live experimentation

---

# 41. MULTI-CHARACTER SHARING

The same motion database should eventually serve multiple character instances.

For example:

```text
100 aliens
      ↓
same MotionDatabase
      ↓
different runtime state
```

Do not duplicate:

* feature arrays
* samples
* clips
* search structures

per character.

---

# 42. RETARGETED DATABASE STRATEGY

Decide experimentally whether the runtime database should contain:

### Option A

Source motion + runtime retargeting

or:

### Option B

Fully retargeted target-skeleton motion

or:

### Option C

Hybrid

Research and benchmark the tradeoff.

Consider:

* memory
* load time
* runtime CPU
* database reuse
* character diversity

Do not choose based on aesthetics.

Measure.

---

# 43. CHARACTER-SPECIFIC DATABASES

Do not assume one universal humanoid database is automatically ideal.

Investigate:

```text
shared source database
       ↓
retarget
       ↓
character-specific MotionPack
```

versus:

```text
shared retargeted database
```

The final architecture should support both if practical.

---

# 44. MOTION STYLE

Motion matching should preserve stylistic identity.

The database may contain:

* natural locomotion
* exaggerated alien locomotion
* cinematic locomotion
* creature-like motion

Do not normalize everything into a generic human movement style.

Style metadata may eventually influence query cost.

Do not make style selection a Phase D behavior concern yet.

---

# 45. SEARCH WEIGHTS

Make weights configurable.

Potential configuration:

```text
poseWeight
velocityWeight
trajectoryWeight
facingWeight
phaseWeight
contactWeight
continuityWeight
transitionWeight
```

Store them in a versioned configuration.

Do not expose every experimental parameter to the artist UI immediately.

---

# 46. AUTOMATED SEARCH EVALUATION

Build an offline evaluation harness.

Given:

```text
known motion sequence
```

simulate queries and compare the selected continuation to the known ground truth.

Measure:

* selected motion continuity
* trajectory error
* velocity error
* pose error
* contact mismatch
* phase mismatch
* transition frequency

This provides an objective baseline.

---

# 47. ADVERSARIAL TESTS

Do not only test easy cases.

Include:

```text
stationary query
high-speed query
sharp turn
reverse direction
strafe
near-zero velocity
contact transition
start
stop
incompatible candidate set
empty database
single-candidate database
```

Ensure a broken matcher cannot pass because the expected answer happens to be the no-op.

---

# 48. GOLDEN MOTION TESTS

Create deterministic scenarios.

For example:

```text
Scenario:
walk north
then turn east
then stop
```

Record expected broad behavior.

Do not require exact floating-point equality for every pose unless appropriate.

Use tolerances.

---

# 49. SEARCH CORRECTNESS

Test:

* known best candidate
* obvious bad candidate
* candidate with better trajectory
* candidate with better pose
* candidate with wrong contact
* candidate with discontinuous root motion

The matcher must respond to meaningful feature differences.

---

# 50. PERFORMANCE TARGETING

Measure:

### Database construction

* frames/sec
* memory
* CPU

### Runtime query

* average μs/ms
* p95
* p99
* candidate count

### Database loading

* cold load
* warm load
* async load

### Multi-character

* 1
* 10
* 50
* 100

Do not claim scalability without measurements.

---

# 51. PROFILE REALISTIC SCENARIOS

At minimum benchmark:

```text
1 character / 10k samples
10 characters / 100k samples
50 characters / 100k samples
100 characters / 1M samples
```

where practical.

The exact combinations can change based on memory.

The goal is to establish realistic runtime budgets.

---

# 52. CPU / GPU BOUNDARY

Keep motion matching CPU-side initially unless there is compelling evidence for another architecture.

Do not move search to GPU simply because AV Gen has a GPU renderer.

Character motion matching involves:

* branching
* irregular memory access
* modest per-character workloads

Benchmark before considering GPU implementation.

---

# 53. CACHE BEHAVIOR

Investigate memory locality.

Prefer:

```text
contiguous feature arrays
```

over:

```text
pointer-heavy graph
```

Measure cache behavior where practical.

The system should be designed for Apple Silicon's memory architecture.

---

# 54. APPROXIMATE SEARCH

Only after establishing a strong exact-search baseline:

Investigate:

* KD-tree
* PCA
* ANN
* vector quantization
* coarse feature bins

Do not introduce approximate search unless:

1. exact search becomes too slow
2. measured data demonstrates the problem
3. quality impact is understood

---

# 55. SEARCH QUALITY VS SPEED

Create an evaluation matrix:

```text
Search method
    ↓
Latency
Memory
Recall / quality
Transition quality
Implementation complexity
```

Do not optimize latency at the expense of motion quality without measuring.

---

# 56. OFFLINE DATABASE BUILDER

Extend the offline tool from Phase A/B.

Potential commands:

```text
avgen-motion build-db
avgen-motion inspect-db
avgen-motion validate-db
avgen-motion benchmark-db
avgen-motion analyze-coverage
```

The exact CLI should follow repository conventions.

---

# 57. DATABASE INSPECTOR

Provide a developer-oriented inspector.

It should be possible to inspect:

* number of clips
* number of samples
* feature dimensions
* memory footprint
* trajectory horizons
* contact distribution
* phase distribution
* motion tags
* coverage
* search structure
* provenance

This can initially be CLI output.

Do not build a giant UI.

---

# 58. MOTION COVERAGE REPORT

The analyzer should identify statements such as:

```text
Walk:
good coverage

Run:
good coverage

High-speed left turn:
limited coverage

Reverse locomotion:
poor coverage

Start transitions:
moderate coverage

Stop transitions:
poor coverage
```

These should be measured from the actual data.

Do not invent subjective labels without defining thresholds.

---

# 59. PROCEDURAL + MOTION MATCHING

One of the most important architectural goals:

Motion matching should choose a **good source motion**, while Phase B procedural systems adapt it.

Example:

```text
Motion matcher:
    chooses approximately correct run

Procedural system:
    adapts exact velocity

Terrain system:
    adapts feet

Look-at:
    aims head

Body adaptation:
    adjusts pelvis

IK:
    preserves contacts
```

This division prevents motion matching from needing to solve every problem itself.

---

# 60. DO NOT TURN MOTION MATCHING INTO A MAGIC SYSTEM

Motion matching cannot invent motion that does not exist.

If the database has no:

```text
running backwards
```

the matcher cannot magically produce high-quality backward running.

If coverage is missing:

```text
database gap
```

should be measurable.

Then Phase B augmentation or later authored content can fill the gap.

---

# 61. DATASET STRATEGY

Continue the licensing discipline established earlier.

Potential sources may include:

* 100STYLE
* ACCAD Open Motion Project
* CMU under its specific usage terms
* other appropriately licensed motion sources

Before incorporating any dataset into a distributable MotionPack:

verify:

* exact license
* data license
* redistribution rights
* attribution
* commercial use
* derivative data rules

Do not assume the dataset is safe because a GitHub repository containing tooling is permissively licensed.

---

# 62. RESEARCH REFERENCES

Use the prior research, but validate current details when necessary.

Useful areas to investigate include:

### Motion Matching

* Simon Clavet's "Motion Matching and The Road to Next-Gen Animation"
* Ubisoft / GDC motion matching presentations
* Unreal Engine Motion Matching documentation
* Orange Duck's Motion Matching work

### Learned Motion Matching

* E1P3 Learned Motion Matching UE5
* Learned Motion Matching training implementations

### Motion Processing

* Sebastian Starke / AI4Animation
* OpenMotion

### Scene-aware motion

* SAMP

Use these as engineering references, not as permission to copy incompatible code or datasets.

---

# 63. RESEARCH BEFORE OPTIMIZATION

For every major search strategy:

1. Read relevant research
2. Implement simple baseline
3. Test on real data
4. Benchmark
5. Compare quality
6. Decide whether complexity is justified

Do not implement five competing search algorithms before one strong baseline exists.

---

# 64. FIRST VERTICAL SLICE

The first true Phase C vertical slice should be:

```text
100STYLE subset
      ↓
retarget
      ↓
contact extraction
      ↓
phase extraction
      ↓
trajectory extraction
      ↓
feature extraction
      ↓
MotionDatabase
      ↓
runtime MotionMatchingProvider
      ↓
Glowmere alien
      ↓
desired trajectory
      ↓
search
      ↓
selected motion
      ↓
Phase B procedural adaptation
      ↓
IK/contact
      ↓
inertialization
      ↓
playable character
```

This is the milestone that matters.

---

# 65. GLOWMERE DEMONSTRATION

Create a deterministic test scene in which the alien:

```text
idle
 ↓
walk
 ↓
accelerate
 ↓
curve left
 ↓
curve right
 ↓
run
 ↓
slow
 ↓
turn
 ↓
stop
 ↓
strafe
 ↓
walk again
```

Drive it through MotionRequests.

Do not create a manually authored sequence of clips.

The point is to demonstrate the matcher selecting appropriate continuations.

---

# 66. PHASE B INTEGRATION

The final stack should resemble:

```text
MotionRequest
      ↓
MotionController
      ↓
MotionMatchingProvider
      ↓
Base Pose
      ↓
Locomotion Adaptation
      ↓
Body Adaptation
      ↓
Look / Reach
      ↓
Contacts
      ↓
IK
      ↓
Secondary Motion
      ↓
Inertialization
      ↓
Final Pose
```

Do not bypass Phase B.

---

# 67. MULTI-CHARACTER DEMONSTRATION

Once one character works:

Test:

```text
10 aliens
```

with different:

* desired velocities
* facing
* phases
* target directions
* terrain positions

Then:

```text
50
```

and, where practical:

```text
100
```

The characters should share the database.

Measure CPU cost.

---

# 68. MOTION MATCHING DEBUG VISUALIZATION

Add developer-only diagnostics:

```text
current sample
selected sample
query trajectory
selected trajectory
candidate count
search cost
feature cost
transition cost
phase
contact state
```

For example:

```text
Current:
100STYLE_run_032 @ 1.82s

Selected:
100STYLE_run_417 @ 0.73s

Cost:
Pose      0.21
Trajectory 0.08
Velocity   0.04
Contact    0.02
Transition 0.07

Total:
0.42
```

The exact UI is not important.

Debuggability is.

---

# 69. WHY DID IT CHOOSE THIS MOTION?

Build enough diagnostics that an engineer can answer:

> Why did the matcher select this sample?

This is essential.

If the system makes a bad choice, we need to know whether the problem is:

* features
* weights
* filtering
* trajectory
* database coverage
* continuity
* contact state
* search approximation

Do not create an opaque black box.

---

# 70. FAILURE CASE ANALYSIS

When motion matching produces poor output:

Do not immediately add smoothing.

Classify the failure:

```text
A. Bad database
B. Bad retarget
C. Bad features
D. Bad weights
E. Bad trajectory
F. Bad candidate filtering
G. Bad transition
H. Procedural adaptation problem
I. Search approximation problem
```

Fix the correct layer.

---

# 71. MOTION MATCHING + ROOT MOTION POLICY

Establish a clear policy for how selected motion affects world translation.

There must be one authoritative movement result.

Avoid:

```text
motion matching moves root
+
MotionController moves root
+
physics moves root
```

all simultaneously.

Document the ownership model.

---

# 72. OFFLINE BAKING

Where useful, allow selected/generated motion sequences to be baked.

For example:

```text
motion matching session
      ↓
selected samples
      ↓
continuous clip
      ↓
baked animation
```

This could eventually be useful for:

* deterministic final renders
* editing
* debugging
* export
* cinematic authoring

Do not make baking a prerequisite for runtime matching.

---

# 73. CINEMATIC DETERMINISM

AV Gen is an audiovisual engine and needs reproducible renders.

Motion matching should be deterministic given:

```text
same MotionPack
same query sequence
same configuration
same initial state
same random seed
```

Do not use uncontrolled nondeterminism.

This is especially important for offline rendering.

---

# 74. THREADING

Investigate threading carefully.

Potentially:

```text
character queries
```

can be evaluated in parallel.

But avoid unnecessary synchronization.

Design shared MotionDatabase as immutable during runtime.

Per-character query state should remain independent.

---

# 75. MEMORY OWNERSHIP

Prefer:

```text
immutable MotionDatabase
```

shared across characters.

Per-character:

```text
MotionMatchState
```

should be compact.

Avoid allocations during normal matching.

---

# 76. HOT RELOAD / EDITOR

If practical, support replacing a database without restarting AV Gen.

But do not compromise runtime safety.

A safe architecture is:

```text
old database
       ↓
new database loads asynchronously
       ↓
validated
       ↓
atomic swap
       ↓
existing instances migrate/fallback
```

This should integrate with the broader Interactive Performance Architecture work.

---

# 77. TESTING MATRIX

Create tests for:

## Database

* serialization
* deserialization
* version mismatch
* corrupt data
* empty database
* one-sample database

## Features

* deterministic extraction
* normalization
* dimensions
* missing joints

## Search

* obvious best match
* trajectory preference
* velocity preference
* contact preference
* phase preference
* continuity preference

## Runtime

* no candidate
* fallback
* database swap
* multiple characters

---

# 78. ADVERSARIAL SEARCH TEST

Construct a test where:

```text
Candidate A:
excellent current pose
terrible future trajectory

Candidate B:
slightly worse current pose
excellent future trajectory
```

The matcher should choose according to configured weighting.

This verifies that trajectory features actually matter.

Likewise test:

```text
Candidate A:
good pose
wrong contact

Candidate B:
slightly worse pose
correct contact
```

The system must demonstrate that contact weighting works.

---

# 79. GOLDEN DATASET

Create a small deterministic database specifically for testing.

Do not rely exclusively on a huge real-world dataset.

It should contain intentionally distinguishable motions.

This lets tests verify:

* feature extraction
* weighting
* candidate selection
* continuity

without requiring large assets.

But remember the earlier lesson:

**synthetic data validates implementation mechanics, not real-world performance.**

Use both synthetic tests and real motion benchmarks.

---

# 80. PERFORMANCE OPTIMIZATION ORDER

Optimize in this order:

```text
1. allocations
2. memory layout
3. feature extraction
4. candidate filtering
5. search
6. scoring
7. threading
8. approximate search
```

Do not jump to SIMD/ANN/GPU work before measuring.

Apple Silicon vectorization can be investigated later if profiling demonstrates the need.

---

# 81. UI RESPONSIVENESS

Because AV Gen already has known UI responsiveness problems with:

* asset-heavy project loading
* sequence timeline interaction
* scrubbing

do not introduce a motion database system that makes those problems worse.

Never:

```text
timeline scrub
    ↓
rebuild million-frame database
    ↓
UI stalls
```

Use:

* immutable runtime databases
* background processing
* cached feature data
* incremental loading where appropriate
* asynchronous database construction

The editor must remain interactive.

---

# 82. OFFLINE BUILD CACHE

Consider content-addressed caching.

If:

```text
same source
+
same retarget profile
+
same processing configuration
```

is processed again, the system should ideally be able to reuse the previous result.

Do not implement a complex cache until the pipeline is stable.

But design the MotionPack metadata so caching can eventually work.

---

# 83. MOTION DATABASE DIFFING

Consider a developer tool that can report:

```text
Database A:
100,421 samples

Database B:
103,822 samples

Added:
4,211

Removed:
810

Feature schema:
unchanged
```

Optional.

Only implement if it naturally fits the tooling architecture.

---

# 84. QUALITY BAR

The final character should not look like:

```text
clip
→ snap
→ clip
→ snap
→ clip
```

It should feel like:

```text
continuous movement
```

Look specifically for:

* foot skating
* sudden phase changes
* root velocity spikes
* torso pops
* repeated motion
* implausible turns
* transition discontinuities
* unnatural braking
* contact errors

Motion matching should make these better, not merely move the artifacts around.

---

# 85. PHASE C DEFINITION OF DONE

Phase C is complete when:

## Motion Library

* large MotionPacks can be built
* provenance is retained
* contacts/phases/trajectories/features are stored
* databases are versioned

## Motion Search

* production-quality query exists
* feature weights are configurable
* candidate filtering works
* search is deterministic
* continuity is handled
* fallback works

## Runtime

* MotionMatchingProvider works
* database is shared
* no per-frame allocation
* asynchronous loading is supported where appropriate
* Phase B systems remain downstream

## Data

* Glowmere database works
* representative 100STYLE database works
* coverage can be analyzed

## Performance

* M2 Max benchmarks exist
* realistic data is benchmarked
* multi-character performance is measured
* memory usage is known

## Quality

* transitions are visually coherent
* trajectory matching works
* contacts remain stable
* foot IK still works
* terrain adaptation still works
* deterministic playback works

---

# 86. DO NOT MOVE TO PHASE D UNTIL THE FOLLOWING WORKS

Before Phase D begins, AV Gen should be capable of:

```text
High-level MotionRequest
        ↓
desired trajectory
        ↓
MotionMatchingProvider
        ↓
appropriate motion selected
        ↓
continuous locomotion
        ↓
procedural body adaptation
        ↓
IK/contact correction
```

without an animator explicitly specifying:

```text
play Walk
play TurnLeft
play Run
play Stop
```

for every transition.

The artist should be describing **what kind of movement is desired**, while the motion system determines **which available movement best satisfies that request**.

---

# 87. WHAT PHASE D WILL ADD

Do NOT implement these now, but preserve clean integration points for:

```text
Behavior
Goals
Navigation
Scene awareness
World interaction
Event response
Social behavior
Character personality
```

Eventually:

```text
Behavior
   ↓
MotionRequest
   ↓
MotionMatchingProvider
   ↓
Procedural Motion
```

Phase C should make that future connection straightforward.

---

# 88. WHAT PHASE E WILL ADD

Do NOT implement now:

```text
Learned Motion Matching
Neural Locomotion
Neural Pose Generation
Generative Motion
```

Those should eventually plug into:

```text
IMotionProvider
```

without replacing the existing system.

---

# 89. REQUIRED FINAL REPORT

At completion produce:

```text
docs/design/motion-matching.md
```

Include:

* architecture
* MotionDatabase
* MotionSample
* feature representation
* trajectory representation
* query construction
* candidate filtering
* cost function
* search strategy
* continuity
* phase/contact integration
* fallback
* MotionPack integration
* offline/runtime split
* memory layout
* performance measurements
* quality measurements
* known limitations
* Phase D integration points
* Phase E integration points

Update relevant ADRs.

---

# 90. DEVELOPMENT ORDER

Implement in this exact broad order:

```text
A. Phase A/B audit
        ↓
B. MotionDatabase representation
        ↓
C. MotionSample representation
        ↓
D. feature extraction
        ↓
E. trajectory extraction
        ↓
F. offline database builder
        ↓
G. database serialization
        ↓
H. simple linear search baseline
        ↓
I. MotionQuery
        ↓
J. runtime MotionMatchingProvider
        ↓
K. continuity / transition costs
        ↓
L. phase/contact integration
        ↓
M. Phase B integration
        ↓
N. Glowmere validation
        ↓
O. 100STYLE subset
        ↓
P. coverage analyzer
        ↓
Q. performance benchmarks
        ↓
R. search optimization
        ↓
S. multi-character validation
        ↓
T. documentation / ADRs
```

Do not optimize search before the baseline works.

---

# 91. FIRST IMPLEMENTATION MILESTONE

The first milestone should be intentionally small:

```text
10–100 representative clips
        ↓
MotionDatabase
        ↓
feature extraction
        ↓
linear search
        ↓
MotionMatchingProvider
        ↓
Glowmere alien
```

The alien should successfully choose different continuation samples as:

* desired velocity changes
* direction changes
* speed changes
* phase changes

Only after this works should the database grow.

---

# 92. SECOND MILESTONE

Then:

```text
100STYLE subset
        ↓
retarget
        ↓
analyze
        ↓
database
        ↓
motion matching
        ↓
Glowmere
```

The important question is:

> Does this architecture actually scale beyond the tiny hand-authored Glowmere corpus?

That is the critical Phase C validation.

---

# 93. THIRD MILESTONE

Then optimize.

Compare:

```text
linear search
vs
optimized exact search
vs
optional approximate search
```

using actual AV Gen motion distributions.

Report:

```text
latency
quality
memory
complexity
```

Do not choose an algorithm because it is fashionable.

---

# 94. FINAL ARTISTIC DEMONSTRATION

Create a polished demonstration in the Glowmere world.

The alien should:

```text
walk
→ accelerate
→ run
→ curve
→ turn
→ slow
→ strafe
→ stop
→ resume
```

while the system automatically chooses appropriate motion.

Layer on:

* terrain adaptation
* foot placement
* look-at
* body adaptation
* secondary motion

from Phase B.

The viewer should not need to know that motion matching is happening.

The desired result is simply:

> **The character looks like it knows how to move.**

---

# 95. FINAL PRINCIPLE

Do not confuse **more animation frames** with **better motion matching**.

The purpose of Phase C is not:

```text
4 million frames!
```

The purpose is:

```text
large, diverse, clean motion coverage
+
meaningful features
+
good trajectory representation
+
intelligent candidate selection
+
continuous transitions
+
procedural adaptation
```

A 100,000-frame high-quality database can be more useful than a 4-million-frame database full of duplicates and poor coverage.

Optimize for **motion coverage and quality**, not headline dataset size.

---

# START NOW

Begin with the Phase A/B audit.

Then build the smallest complete motion-database/search vertical slice.

Do not start by importing millions of frames.

Do not start with ANN.

Do not start with GPU search.

Do not start with neural motion.

Do not start with a giant editor UI.

Start with:

```text
real motion
 ↓
clean MotionDatabase
 ↓
features
 ↓
trajectory
 ↓
linear search
 ↓
MotionMatchingProvider
 ↓
Glowmere alien
```

Prove that.

Then scale it.

Then optimize it.

Then make it production-grade.

The end state of Phase C should be a system where **motion is selected from a large reusable library based on what the character is currently doing and where it needs to go**, rather than an artist having to manually author every possible transition.
