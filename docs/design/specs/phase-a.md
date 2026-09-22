# AV Gen — Phase A Animation Foundation

## From Validated Research to Production Implementation

You have completed the initial autonomous-character-animation research and the highest-risk alien IK probe.

The research is now sufficiently mature to begin production implementation.

The authoritative research/design document is:

`docs/design/autonomous-character-animation.md`

Relevant ADRs currently include:

* ADR-540
* ADR-541
* ADR-542
* ADR-543

Use the existing repository state and those documents as the source of truth.

Do NOT restart the research from scratch.

Do NOT jump directly into motion matching or neural animation.

The immediate mission is to build the **animation foundation** that makes those later systems possible.

---

# EXECUTIVE GOAL

Transform AV Gen's current animation architecture from:

```text
animation embedded in imported character asset
+
assumed conventional skeleton hierarchy
+
limited movement state
+
basic clip transitions
```

into:

```text
reusable animation data
+
retargetable skeletons
+
generic IK/contact solving
+
phase-aware transitions
+
inertialization
+
rich movement state
+
offline motion processing
```

while preserving the existing animation system and keeping the architecture modular enough to add:

```text
procedural motion
motion matching
optional pretrained neural motion
```

later.

The first goal is NOT autonomous AI.

The first goal is to make AV Gen's animation foundation capable of supporting autonomous characters.

---

# NON-NEGOTIABLE PRINCIPLES

## 1. Do not re-export the Glowmere alien merely to satisfy current engine assumptions

The IK probe proved that the unusual alien leg can be solved correctly as an arbitrary chain.

The probe reproduced the intended solution to approximately:

`1.2e-7`

while the current shipped ancestor-chain rule was approximately:

`0.206`

off and failed to move the foot.

Therefore:

**The production engine should support the alien's valid topology.**

Do not special-case the Glowmere alien.

Do not rename joints merely to satisfy the existing solver.

Do not restructure the asset unless implementation demonstrates that the asset itself is genuinely invalid.

The preferred architecture is generic support for explicitly defined IK chains.

---

# 2. Do not trust null-request correctness

The research discovered that a null-request control arm can make the broken implementation appear perfect.

Therefore every correctness test must contain a positive/adversarial case.

A test that only says:

```text
target == current position
```

is insufficient.

Tests must include cases where:

```text
target != current position
```

and the implementation must demonstrably move toward the known expected result.

This rule applies to:

* IK
* retargeting
* transitions
* contacts
* motion matching
* trajectory processing
* procedural animation

throughout the project.

---

# 3. Do not overfit to Glowmere

Glowmere is the first production validation target.

It is NOT the architecture.

Avoid:

```text
if alien:
    special handling
```

Prefer:

```text
IKChain
RetargetProfile
SkeletonMapping
BodyCompensation
ContactConstraint
```

with Glowmere represented through data.

---

# 4. Do not build motion matching yet

The current Glowmere corpus is:

* 26 clips
* 1,712 frames
* ~57.07 seconds

and is overwhelmingly in-place.

There is currently insufficient trajectory diversity to justify a production motion-matching system.

Do not spend implementation time building a giant motion database/query architecture now.

Instead, build the data foundations that motion matching will eventually consume:

* velocity
* trajectory
* phase
* contacts
* motion metadata
* reusable motion assets

Motion matching comes after the retargeting foundation and a sufficiently large corpus exist.

---

# 5. Do not train models

Training is NOT part of this phase.

Do not introduce:

* model training
* reinforcement learning
* diffusion training
* neural locomotion training
* large PyTorch runtime dependencies

Pretrained neural models can be investigated later as optional providers.

The baseline AV Gen animation system must work without them.

---

# 6. Keep offline and runtime systems separate

Offline processing may eventually use:

* Python
* PyTorch
* ONNX
* Core ML
* heavy batch processing

The shipped runtime should not require those.

Runtime should remain primarily:

* C++
* existing AV Gen architecture
* compact motion data
* procedural animation
* IK
* contacts
* efficient animation evaluation

---

# CURRENT PHASE A

Implement the following in order.

Do not attempt to implement everything simultaneously.

---

# STEP 1 — GENERALIZE IK CHAINS

The alien IK probe has already demonstrated the correct direction.

Promote that solution into a production-quality generic abstraction.

The solver should conceptually support:

```text
IKChain
    joints[]
    endEffector
    target
    constraints
    solver
```

Do not require the joints to be ancestors in the skeleton hierarchy if the animation representation permits a valid model-space relationship.

The exact implementation should follow existing AV Gen architecture and transform conventions.

Potential solver types may include:

```text
TwoBoneIK
FABRIK
CCD
```

but do NOT implement every solver simply because they exist.

Implement the minimum robust capability required by current and near-term AV Gen needs.

---

## IK requirements

Support:

* conventional ancestor chains
* arbitrary explicitly defined chains
* model-space solving
* correct local-space write-back
* positional targets
* optional orientation targets where useful
* unreachable targets
* stable behavior
* deterministic results

Do not hide unreachable targets.

The solver should be able to report:

```text
reachable
unreachable
error
```

where useful.

---

# STEP 2 — ADD BODY COMPENSATION FOR UNREACHABLE CONTACTS

The alien probe discovered:

* resting leg is approximately 98.5% extended
* less than 1 cm of slack exists
* a 10 cm downward foot target cannot be reached by leg IK alone
* approximately 9 cm of hip/body translation is required

This means foot IK cannot be treated as an isolated limb problem.

Do NOT implement:

```text
if GlowmereAlien:
    lowerHip()
```

Instead create a generic concept of **reachable contact solving / body compensation**.

Conceptually:

```text
Desired foot contact
        ↓
Can limb reach?
        │
     ┌──┴──┐
     │     │
    Yes    No
     │     │
     │   body compensation
     │     ↓
     │   hip/pelvis adjustment
     │     ↓
     └──→ re-solve IK
              ↓
        contact validation
```

Start with the smallest useful compensation mechanism.

Potentially:

* pelvis/hip translation
* optional torso compensation
* limits
* maximum correction
* iterative solve

Do not build a complete full-body IK framework yet.

The architecture should, however, allow it to grow later.

---

# STEP 3 — VELOCITY VECTOR

Replace or extend the current movement representation.

Current:

```text
speed
yaw
```

is insufficient for autonomous movement.

Introduce a representation capable of expressing:

```text
world-space velocity
facing
angular velocity
```

At minimum:

```text
Vec3 velocity
Vec3 facing
```

or the equivalent appropriate representation.

Ensure the change does not unnecessarily break existing systems.

Provide compatibility where practical.

The representation must support:

```text
moving north-east
while facing north
```

and:

```text
moving sideways
while facing forward
```

and:

```text
circling a target
```

This will later feed:

* steering
* animation selection
* procedural locomotion
* motion matching
* autonomous behaviors

---

# STEP 4 — CONTACT EXTRACTION

Implement the smallest reusable offline contact-analysis system.

Do not hardcode "two feet."

Represent contacts generically.

Potential metadata:

```text
ContactTrack
    joint
    start
    end
    confidence
    type
```

Possible contact types:

```text
foot
hand
body
custom
```

Start with foot contacts because they immediately benefit the alien.

Detect:

* contact start
* contact end
* planted state
* contact duration

Use physically sensible thresholds.

Document them.

Avoid fragile magic numbers.

Where appropriate, expose thresholds as analysis configuration rather than compile-time constants.

---

# STEP 5 — PHASE EXTRACTION

Add reusable motion phase metadata.

At minimum:

```text
phase ∈ [0,1)
```

with meaningful progression through cyclical locomotion.

Where contacts are available, use them to improve phase estimation.

Potential phase landmarks:

```text
left foot contact
left foot release
right foot contact
right foot release
```

Do not define phase exclusively as "walk cycle phase."

It should become general motion metadata.

---

# STEP 6 — PHASE-AWARE TRANSITIONS

Fix the current behavior where `play()` effectively causes the target animation to begin at phase zero.

The system should be able to select a compatible target phase.

For example:

```text
Walk @ 0.73
      ↓
find compatible Run phase
      ↓
Run @ compatible phase
      ↓
crossfade
```

Initially, use simple phase matching.

Do not build a full motion-matching system.

The goal is to make existing transitions materially better.

---

# STEP 7 — INERTIALIZATION

Implement a compact inertialization layer.

Goals:

* remove visible transition discontinuities
* preserve motion continuity
* deterministic
* low runtime cost
* compatible with existing pose layers
* compatible with future motion matching

Do not create a giant animation graph.

Use the simplest architecture that fits the existing pose evaluation system.

---

# STEP 8 — DECOUPLE ANIMATION FROM IMPORTED CHARACTER ASSETS

This is one of the most important architectural changes.

The current importer behavior means animation is effectively trapped with the rig that loaded it.

We need to move toward:

```text
Animation
    ↓
Source Skeleton
    ↓
Retarget Profile
    ↓
Target Skeleton
```

rather than:

```text
Character GLB
 ├── skeleton
 └── duplicated animations
```

Do not immediately redesign every asset pipeline.

Implement the smallest clean separation that allows an animation to be reused across compatible skeletons.

The same animation should eventually be usable by multiple characters.

---

# STEP 9 — RETARGET PROFILE

Introduce a reusable retargeting abstraction.

Conceptually:

```text
RetargetProfile

sourceSkeleton
targetSkeleton

jointMappings
rotationOffsets
translationRules
scaleRules
rootMotionRules
contactRules
```

Adapt this to AV Gen conventions.

Do not hardcode character names.

The profile should contain character-specific information.

---

# RETARGETING REQUIREMENTS

Support at least:

* joint mapping
* missing optional joints
* different rest poses
* bone-length differences
* rotation offsets
* root motion
* translation handling
* end-effector correction
* contact correction

Start with human → Glowmere alien.

But make the abstraction generic.

---

# STEP 10 — 100STYLE SMALL-SCALE EXPERIMENT

Do NOT import all 4M+ frames initially.

Select a representative subset containing:

* walking
* running
* starts
* stops
* turns
* directional movement
* stylistic variation

Process:

```text
100STYLE
   ↓
source skeleton
   ↓
RetargetProfile
   ↓
Glowmere alien
   ↓
contact analysis
   ↓
phase analysis
   ↓
body compensation
   ↓
IK cleanup
   ↓
validation
```

Measure:

* retargeting quality
* foot placement
* foot sliding
* root trajectory
* joint deformation
* reachability
* contact preservation
* processing time
* output size

Do not scale up until this experiment works.

---

# STEP 11 — MOTIONPACK

After retargeting has demonstrated viability, establish the offline motion asset representation.

The conceptual structure is:

```text
MotionPack
├── metadata
├── skeleton
├── clips
├── contacts
├── phases
├── trajectories
├── features
├── database
├── provenance
└── optional neural model
```

Do not implement fields that are not yet needed.

Design for versioning.

Do not embed assumptions about a particular neural architecture.

---

# PROVENANCE

Every processed motion source must retain:

```text
source
source file
creator
license
license URL
attribution requirement
redistribution status
derived-data status
processing history
tool version
```

Ambiguous licensing must be marked:

```text
REQUIRES_REVIEW
```

Never silently treat ambiguous data as commercial-safe.

---

# STEP 12 — OFFLINE PROCESSING TOOL

Build an isolated command-line/offline capability.

Do not make the editor responsible for expensive batch processing.

The exact CLI is up to project conventions.

Potential operations:

```text
avgen-motion inspect
avgen-motion retarget
avgen-motion analyze
avgen-motion validate
avgen-motion benchmark
```

Keep it composable.

The offline system should eventually be capable of processing an entire character motion library without manually opening the AV Gen editor.

---

# STEP 13 — M2 MAX BENCHMARK

Use the actual development hardware as the reference platform.

Benchmark:

### Small

~1,700 frames

### Medium

~10,000 frames

### Large

~100,000 frames

### Stress

~1,000,000 frames where practical

Measure:

* import time
* retarget time
* contact analysis
* phase analysis
* trajectory extraction
* feature extraction
* MotionPack generation
* peak memory
* output size
* frames/sec

Do not fabricate estimates.

Report measured results.

The goal is to establish:

> "This is practical to run offline on an M2 Max."

not:

> "A theoretical Apple Silicon implementation should be fast."

---

# STEP 14 — TESTING

Every new subsystem must have automated tests.

## IK

Test:

* conventional chain
* arbitrary chain
* positive movement
* unreachable target
* exact target
* model/local conversion
* write-back

The alien regression test MUST be a positive case.

It must actually move the foot.

A null-request test is not sufficient.

---

## Body compensation

Test:

* reachable target without compensation
* slightly unreachable target
* significant unreachable target
* maximum correction
* impossible target

---

## Retargeting

Test:

* identical skeleton
* different bone lengths
* different rest pose
* rotation offset
* missing optional joint
* root motion

---

## Contacts

Test:

* clear planted contact
* moving foot
* ambiguous contact
* contact start/end

---

## Phase

Test:

* stable cyclic motion
* phase continuity
* contact landmarks

---

## Transitions

Test:

* same phase
* different phase
* walk → run
* run → walk
* transition continuity

---

# STEP 15 — VISUAL VALIDATION

Do not rely solely on numerical tests.

Create a minimal debug visualization capable of showing:

* skeleton
* IK target
* IK chain
* foot contact
* hip compensation
* velocity vector
* facing direction
* animation phase

This can be a temporary developer/debug surface.

Do not build the final animation editor yet.

---

# STEP 16 — PERFORMANCE

Profile the implementation.

Pay particular attention to:

* per-frame allocations
* repeated skeleton traversal
* string-based joint lookup
* unnecessary matrix reconstruction
* pose copying
* repeated retarget calculations
* unnecessary CPU/GPU synchronization

Shared animation data must remain shared.

Do not create one copy of the motion database per character.

A character instance should primarily hold dynamic state.

---

# STEP 17 — MULTI-CHARACTER READINESS

Do not optimize prematurely for 1,000 characters.

But do not architect the system around a single character either.

Shared:

```text
Skeleton
Animation
MotionPack
RetargetProfile
Feature data
```

should be reusable.

Per-instance:

```text
current pose
motion request
provider state
current phase
parameters
```

should remain lightweight.

---

# STEP 18 — DO NOT BUILD THE FINAL ANIMATION UI YET

The research found there is currently no proper animation UI.

Eventually AV Gen should have professional animation/rig tooling.

But do not build the entire UI before the data model stabilizes.

For now, provide enough debug tooling to validate the foundation.

Later we can build:

### Rig

* skeleton
* joint hierarchy
* retarget profile
* mapping status

### Animation

* clips
* duration
* phase
* contacts
* root motion

### Motion Library

* MotionPacks
* clips
* frames
* tags
* source
* licensing
* validation

### Character Motion

* current provider
* desired velocity
* actual velocity
* facing
* active clip
* phase
* contacts
* IK

---

# ARCHITECTURAL BOUNDARY FOR FUTURE SYSTEMS

The implementation should leave room for:

```text
IMotionProvider
├── ClipMotionProvider
├── ProceduralMotionProvider
├── MotionMatchingProvider
└── NeuralMotionProvider
```

Do not necessarily implement all four now.

The first production implementation may primarily use:

```text
ClipMotionProvider
ProceduralMotionProvider
```

with the data structures needed for later motion matching.

The interface must not force neural inference.

---

# FUTURE MOTION MATCHING

When sufficient motion data exists, we will eventually add:

```text
current pose
current velocity
desired velocity
future trajectory
facing
phase
contacts
```

→ database query

→ best continuation

Do not implement that now unless a small amount is genuinely necessary for validating the foundation.

---

# FUTURE NEURAL MOTION

Neural motion remains optional.

The architecture should permit:

```text
NeuralMotionProvider
```

without making it mandatory.

If a suitable pretrained model is found later, evaluate:

* license
* model size
* inference speed
* Apple Silicon support
* ONNX/Core ML compatibility
* runtime memory
* deterministic behavior
* generated-motion licensing

Do not train a model as part of this implementation.

---

# DEVELOPMENT WORKFLOW

Use this sequence:

```text
RESEARCH
   ↓
PROBE
   ↓
IMPLEMENT
   ↓
TEST
   ↓
VISUAL VALIDATION
   ↓
BENCHMARK
   ↓
DOCUMENT
   ↓
NEXT LAYER
```

Do not skip the probe/test stage when changing a foundational transform or animation assumption.

---

# ADR DISCIPLINE

ADR-543 is currently Proposed.

Once the production implementation lands and the regression test proves the arbitrary-chain solution, update the ADR to Accepted.

Do the same for other Proposed ADRs when their implementation is validated.

If implementation disproves an existing architectural assumption:

1. Record the finding
2. Explain why
3. Update the ADR appropriately
4. Do not silently rewrite history

The synthetic benchmark/control-arm issue should remain documented as an engineering lesson.

---

# FIRST VERTICAL SLICE

The first major success should be:

```text
Actual external animation
        ↓
Retarget Profile
        ↓
Glowmere alien
        ↓
Retargeted pose
        ↓
Contact extraction
        ↓
Phase extraction
        ↓
Reachability test
        ↓
Hip/body compensation
        ↓
Foot IK
        ↓
Phase-aware transition
        ↓
Inertialization
        ↓
Playable alien
```

This should use the actual Glowmere asset.

Do not make a toy humanoid the primary success case.

---

# DEFINITION OF DONE FOR PHASE A

Phase A is complete when:

## Skeleton / IK

* Glowmere alien works without re-export
* arbitrary IK chains are supported generically
* positive regression tests exist
* unreachable contacts can trigger body compensation

## Motion state

* velocity is represented as a vector
* facing is represented independently
* autonomous movement can represent strafing/circling/etc.

## Animation analysis

* contacts can be extracted
* phase can be extracted
* metadata can be stored

## Transitions

* transitions can preserve compatible phase
* inertialization reduces discontinuities

## Retargeting

* external animation can be retargeted to Glowmere
* bone-length differences are handled
* rest-pose differences are handled
* root motion is handled
* contact cleanup works

## Offline

* processing is isolated
* MotionPack direction is established
* provenance exists
* M2 Max measurements exist

## Runtime

* no Python dependency
* no PyTorch dependency
* no model-training dependency
* no giant third-party animation framework
* existing AV Gen animation functionality remains intact

---

# IMPORTANT: KEEP THE SYSTEM SMALL

Do not interpret this prompt as permission to create a giant framework.

The goal is a **small number of strong primitives** that compose:

```text
Skeleton
Animation
RetargetProfile
IKChain
ContactTrack
PhaseTrack
MotionRequest
MotionProvider
MotionPack
```

These primitives should become the foundation for the much larger autonomous-character system later.

Prefer composition over inheritance-heavy animation architecture.

Prefer data over hardcoded character behavior.

Prefer offline processing over runtime complexity where possible.

Prefer deterministic algorithms when they provide equivalent quality.

Prefer measured results over theoretical assumptions.

---

# START NOW

Begin by:

1. Re-reading the completed research document
2. Reviewing ADR-540 through ADR-543
3. Reviewing the existing IK probe
4. Reviewing the actual Glowmere alien skeleton
5. Reviewing the current animation/pose architecture
6. Implementing the arbitrary-chain IK fix
7. Adding the positive Glowmere regression case
8. Implementing reachable-contact/body compensation
9. Implementing velocity-vector state
10. Implementing contact/phase extraction
11. Improving transitions with phase matching
12. Adding inertialization
13. Beginning the reusable retargeting foundation
14. Running the first small 100STYLE retarget experiment
15. Benchmarking the offline pipeline on the M2 Max
16. Documenting results and updating ADRs

After each major subsystem, run the relevant tests and visual validation before moving forward.

Do not declare success based on compilation alone.

Do not declare success based on a null-request control arm.

Do not optimize based on synthetic benchmarks alone.

Do not introduce neural models merely because they are available.

Do not build motion matching merely because the architecture now permits it.

Build the foundation first.

The ultimate goal is:

**An AV Gen character should eventually be able to receive a high-level intent such as "walk over to that mushroom, inspect it, avoid the tree, then wander away" and produce convincing continuous body motion without requiring the artist to author every transition manually.**

This phase builds the machinery that makes that future possible.
