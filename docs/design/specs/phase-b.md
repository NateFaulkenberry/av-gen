# AV GEN — PHASE B

# PROCEDURAL CHARACTER MOTION & BODY ADAPTATION

## Mission

Build the second major layer of AV Gen's autonomous character-animation system:

> **Procedural Character Motion & Body Adaptation**

Phase A established the foundational ability to:

* represent motion independently from character assets
* retarget animation
* work with arbitrary IK chains
* analyze contacts
* analyze motion phase
* represent velocity independently from facing
* perform phase-aware transitions
* perform inertialization
* process motion offline
* begin building reusable MotionPacks

Phase B now builds the machinery that allows a character to **continuously adapt its body and movement to intent, terrain, targets, objects, and changing conditions** rather than merely playing authored clips.

The ultimate target is something like:

```text
Character wants to:
    walk toward mushroom
    avoid tree
    slow down
    turn
    look at mushroom
    stop
    lean forward
    inspect mushroom
    step around it
    resume wandering
```

without requiring an animator to author one giant animation sequence for that exact situation.

This phase should NOT attempt to solve the entire autonomous-character problem.

Behavior selection comes primarily in Phase D.

Motion matching comes primarily in Phase C.

Neural motion comes primarily in Phase E.

Phase B should instead create the **procedural motion substrate** those systems will consume.

---

# 1. ABSOLUTE ARCHITECTURAL RULES

## 1.1 Build on Phase A

Before implementing anything substantial:

* inspect the actual Phase A implementation
* inspect the relevant ADRs
* inspect tests
* inspect MotionPack structures
* inspect retargeting
* inspect IK
* inspect contacts
* inspect phase
* inspect velocity/facing
* inspect inertialization
* inspect pose-layer evaluation

Do NOT duplicate functionality that Phase A already provides.

Extend it.

---

# 1.2 No monolithic "AutonomousCharacter" class

Do NOT create one enormous class containing:

```text
behavior
locomotion
IK
look-at
terrain
interaction
animation
physics
audio
navigation
```

Instead create composable systems.

The conceptual architecture should resemble:

```text
Motion Request
      ↓
Motion Controller
      ↓
Base Motion Provider
      ↓
Pose
      ↓
Procedural Motion Layers
      ↓
Body Adaptation
      ↓
Contacts / IK
      ↓
Secondary Motion
      ↓
Final Pose
```

Each layer should have a clear responsibility.

---

# 1.3 Separate intent from execution

Phase B should NOT decide high-level behavior.

For example:

```text
"Investigate mushroom"
```

is a behavior/intent problem.

Phase B should receive something closer to:

```text
desired velocity
desired facing
target position
target orientation
movement mode
attention target
interaction target
```

and figure out how the body should move.

Later Phase D will produce these requests.

---

# 1.4 No LLM dependency

Do not introduce an LLM.

Do not make character motion dependent on:

* network APIs
* cloud inference
* language models
* external AI services

Everything in Phase B should work deterministically and locally.

---

# 1.5 No neural model requirement

Do not train neural networks.

Do not introduce PyTorch as a runtime dependency.

Do not make Phase B dependent on neural inference.

Neural motion belongs to Phase E.

---

# 1.6 M2 Max is the reference machine

The implementation must remain practical on Apple Silicon.

Avoid:

* per-character expensive allocations
* repeated skeleton searches
* unnecessary matrix rebuilding
* CPU/GPU synchronization
* expensive physics for ordinary locomotion
* large per-frame data copies

Measure performance.

Do not assume performance.

---

# 2. FIRST TASK — AUDIT PHASE A

Before coding:

Review:

```text
Phase A implementation
ADR-540
ADR-541
ADR-542
ADR-543
animation system
pose system
retargeting
IK
contacts
phase
inertialization
MotionPack
```

Produce a concise internal implementation map:

```text
Existing primitive
    ↓
What Phase B can reuse
    ↓
What Phase B must add
```

Do not restart architecture from scratch.

If Phase A differs from the expected design in this prompt, adapt to the actual implementation rather than blindly recreating the conceptual structures.

---

# 3. DEFINE THE PROCEDURAL MOTION LAYER

Create a generic procedural motion abstraction.

Conceptually:

```cpp
class IMotionLayer
{
public:
    virtual void apply(
        Pose& pose,
        const MotionContext& context,
        float dt
    ) = 0;
};
```

The exact interface should follow AV Gen conventions.

Potential layers:

```text
LocomotionLayer
FootPlacementLayer
LookAtLayer
AimLayer
BodyOrientationLayer
ReachLayer
BalanceLayer
TerrainAdaptationLayer
SecondaryMotionLayer
```

Do NOT implement every layer immediately.

The architecture should support them.

---

# 4. DEFINE MOTION CONTEXT

Create a centralized context containing the information procedural motion needs.

Conceptually:

```text
MotionContext
├── dt
├── world transform
├── velocity
├── desired velocity
├── facing
├── desired facing
├── angular velocity
├── ground information
├── contacts
├── targets
├── environment information
├── motion phase
├── current motion
└── character configuration
```

Do not blindly copy the entire world state into every character every frame.

Prefer references, handles, compact state, or appropriately cached data.

---

# 5. PROCEDURAL LOCOMOTION

Build the first genuinely procedural locomotion layer.

It should sit on top of authored/retargeted locomotion rather than immediately replacing it.

Conceptually:

```text
Base locomotion
      ↓
speed / direction adaptation
      ↓
stride adjustment
      ↓
turn adaptation
      ↓
foot placement
      ↓
body adaptation
```

---

# 6. SPEED ADAPTATION

Characters should be able to move at speeds between authored clips.

For example:

```text
Walk clip = 1.0 m/s
Run clip  = 3.0 m/s

Desired = 1.7 m/s
```

The system should not simply snap to either walk or run.

Investigate and implement appropriate:

* playback-rate adjustment
* stride scaling
* root-motion scaling
* velocity warping
* stride warping

Do not overuse playback-rate scaling.

Large speed changes should transition between locomotion modes.

---

# 7. STRIDE WARping

Implement stride adaptation where useful.

Goals:

* match desired velocity
* preserve believable foot contacts
* reduce foot sliding
* avoid unnatural animation stretching

Conceptually:

```text
desired displacement
        ↓
available animation displacement
        ↓
stride correction
        ↓
foot contact preservation
```

This should work with Phase A contact metadata.

---

# 8. START MOTION

Build procedural/assisted starts.

Current authored animation libraries may not contain every possible start.

Support:

```text
standing
 ↓
accelerating
 ↓
walking/running
```

Use available clips where possible.

Use procedural adaptation where necessary.

The result should avoid:

```text
idle
→ instant full-speed locomotion
```

Instead create a believable acceleration period.

Parameters may include:

```text
startDuration
acceleration
strideRamp
bodyLean
footPlantBias
```

Do not expose all parameters in the UI yet.

---

# 9. STOP MOTION

Similarly:

```text
locomotion
 ↓
deceleration
 ↓
final step
 ↓
stable idle
```

Avoid simply fading locomotion into idle.

Use:

* desired velocity decay
* braking
* stride adaptation
* final contact preservation
* body deceleration
* inertialization

---

# 10. TURNING

Implement procedural turn adaptation.

The system must distinguish:

```text
movement direction
facing direction
desired facing
angular velocity
```

Support:

* gentle curves
* medium turns
* sharp turns
* turning while walking
* turning while running
* stopping and turning
* turning in place

Use authored turn clips when appropriate.

Procedurally adapt between them where practical.

Do not create an exhaustive animation graph.

---

# 11. STRAFE / NON-FACING LOCOMOTION

The velocity-vector foundation from Phase A should now become useful.

Support:

```text
facing north
velocity east
```

and:

```text
facing north
velocity north-east
```

and:

```text
facing target
velocity tangential to target
```

This is foundational for:

* circling
* backing away
* avoiding obstacles
* keeping attention on targets
* combat-like movement later
* social interaction
* cinematic character movement

Do not assume:

```text
velocity == facing
```

---

# 12. LOCOMOTION MODES

Define a compact set of locomotion modes.

Potential examples:

```text
Idle
Walk
Run
Strafe
Backpedal
TurnInPlace
Start
Stop
```

Avoid hardcoding behavior-specific modes such as:

```text
InvestigateMushroom
RunFromUFO
LookAtAlien
```

Those belong to higher-level behavior.

---

# 13. FOOT PLACEMENT

Now use Phase A's generic IK/contact infrastructure for continuous foot placement.

The foot-placement system should:

* identify planted foot
* find terrain target
* test reachability
* compensate body
* solve IK
* preserve contact
* release foot naturally

Conceptually:

```text
animation foot trajectory
          ↓
terrain query
          ↓
desired foot target
          ↓
reachability
          ↓
body compensation
          ↓
IK
          ↓
contact validation
```

---

# 14. FOOT PLANTING

Implement robust contact preservation.

During a planted phase:

```text
foot should remain approximately fixed
```

even if:

* body moves
* terrain changes
* animation root moves
* character changes speed

But do not lock the foot forever.

Contact must have lifecycle:

```text
approach
plant
hold
release
swing
```

Phase A contact tracks should inform this.

---

# 15. FOOT RELEASE

Avoid the common procedural animation failure:

```text
foot locked
→ suddenly teleports
```

Use the animation phase/contact state to determine release.

The system should gracefully blend from:

```text
procedural contact
```

back to:

```text
animation trajectory
```

---

# 16. TERRAIN ADAPTATION

Implement terrain-aware body adaptation.

At minimum support:

* uneven ground
* slopes
* small steps
* depressions
* varying foot heights

The body should adapt rather than remaining rigidly aligned to a flat-plane animation.

Potential mechanisms:

```text
pelvis adjustment
foot IK
spine compensation
ankle orientation
```

Keep the first implementation modest.

---

# 17. TERRAIN QUERY ABSTRACTION

Do not couple procedural locomotion directly to the renderer.

Create an abstraction for environmental contact queries.

Conceptually:

```text
GroundQuery
    position
    normal
    distance
    material/category
    valid
```

The implementation may use existing AV Gen scene/world queries.

Procedural animation should not care whether the information came from:

* raycast
* physics query
* collision system
* generated terrain
* offline data

---

# 18. BODY BALANCE

Introduce basic procedural body balance.

The character should be able to respond to:

* foot placement
* acceleration
* deceleration
* turning
* slope
* target reach

Possible responses:

```text
pelvis shift
torso lean
spine rotation
shoulder adjustment
```

Do NOT build a full physically simulated humanoid.

Use deterministic procedural corrections.

---

# 19. MOVEMENT LEAN

Implement motion-dependent body lean.

Examples:

```text
accelerating → lean forward
braking      → lean backward
turning      → lateral lean
slope        → terrain adaptation
```

Make the effect:

* subtle
* configurable
* smooth
* additive

Avoid exaggerated game-character tilts.

The visual target is believable cinematic animation.

---

# 20. LOOK-AT SYSTEM

Build a reusable look-at layer.

The system should support targets such as:

```text
world position
entity
object
camera
moving target
```

Possible chain:

```text
eyes
head
neck
upper spine
```

Allow configurable distribution.

For example:

```text
eyes  35%
head  35%
neck  20%
spine 10%
```

Do not hardcode these values.

---

# 21. LOOK-AT LIMITS

A character should not rotate its entire spine unnaturally just to look at something.

Support:

* yaw limits
* pitch limits
* per-joint contribution
* smoothing
* activation radius
* priority
* blend weight

When a target moves outside the comfortable range:

```text
head reaches limit
       ↓
body begins turning
```

This should eventually integrate naturally with locomotion.

---

# 22. ATTENTION TARGET

Do not make LookAt itself responsible for choosing targets.

Instead support a generic:

```text
AttentionTarget
```

with:

```text
position
orientation
priority
weight
duration
```

Phase D can eventually decide:

```text
"That mushroom is interesting."
```

Phase B simply turns that request into body motion.

---

# 23. REACH / HAND IK

Implement the foundation for reaching toward targets.

Support:

```text
hand
target position
target orientation
```

Use Phase A's arbitrary-chain IK.

Examples:

* touch mushroom
* reach toward object
* gesture toward something
* hold object
* interact with world

Do not build a full object-grasping system yet.

The goal is:

```text
target
 ↓
reach pose
 ↓
IK
```

---

# 24. REACHABILITY

Use the same reachability philosophy established for feet.

For every target:

```text
Can the limb reach?
```

If not:

```text
Can body compensate?
```

If still not:

```text
Clamp / reject / gracefully blend
```

Never silently produce broken poses.

---

# 25. BODY COMPENSATION FRAMEWORK

Generalize the Phase A hip compensation into a broader body-adaptation concept.

Potential hierarchy:

```text
Target
  ↓
Limb
  ↓
Local body compensation
  ↓
Pelvis
  ↓
Spine
```

Do not implement every level immediately.

The architecture should permit additional compensation later.

---

# 26. SECONDARY MOTION

Introduce lightweight procedural secondary motion.

Potential systems:

* antenna motion
* ear motion
* tail motion
* cloth-like appendages
* head stabilization
* subtle torso movement
* breathing
* idle sway

These should be procedural and inexpensive.

Do not build a general-purpose cloth simulator.

---

# 27. BREATHING / IDLE LIFE

Characters standing still should not look frozen.

Implement subtle idle variation.

Potential components:

```text
breathing
weight shift
head movement
micro-saccades
posture variation
```

Keep these deterministic or seeded.

A character instance should have a stable random seed so idle behavior does not change unpredictably every frame.

---

# 28. MICRO-MOTION

Consider a generic micro-motion layer.

It should operate at low amplitude and high frequency.

Potential targets:

* head
* shoulders
* torso
* hands
* fingers
* antennae

It should be:

* additive
* bounded
* seedable
* disableable
* cheap

The purpose is to eliminate the "dead mannequin" appearance.

---

# 29. ANIMATION LAYER PRIORITY

Establish explicit ordering.

A reasonable starting concept:

```text
Base animation
      ↓
Locomotion adaptation
      ↓
Root/body adaptation
      ↓
Look-at / aim
      ↓
Reach
      ↓
Foot contacts / IK
      ↓
Balance
      ↓
Secondary motion
      ↓
Inertialization / final stabilization
```

However, verify this against the actual AV Gen pose architecture.

Do not blindly adopt this order.

Some systems may need to occur earlier/later.

Document the final ordering.

---

# 30. LAYER MASKING

Procedural layers must not accidentally destroy unrelated animation.

Support masks conceptually:

```text
upper body
lower body
left arm
right arm
spine
head
```

This will become critical when characters:

```text
walk
+
look at something
+
reach
```

simultaneously.

---

# 31. PRIORITY / BLENDING

Procedural systems need controlled blending.

Every layer should have some equivalent of:

```text
weight
priority
mask
activation
```

Do not hardcode:

```text
IK always wins
```

because future systems will require:

```text
animation > procedural
procedural > animation
```

depending on context.

---

# 32. MOTION REQUEST

Use the Phase A motion-request abstraction.

Extend it only where necessary.

Potential fields:

```text
desiredVelocity
desiredFacing
desiredAngularVelocity

movementMode

lookTarget
lookWeight

reachTarget
reachWeight

groundingMode

locomotionStyle

preferredStride
```

Do not make this a giant bag of every possible behavior.

Separate high-level intent from low-level motion parameters.

---

# 33. CHARACTER MOTION CONTROLLER

Build a controller that translates motion requests into continuous motion.

Conceptually:

```text
MotionRequest
      ↓
MotionController
      ↓
locomotion state
      ↓
base animation
      ↓
procedural adaptation
      ↓
final pose
```

It should manage:

* current velocity
* desired velocity
* acceleration
* braking
* facing
* turning
* locomotion mode
* phase
* contacts

This controller should NOT decide high-level goals.

---

# 34. ACCELERATION / DECELERATION

Do not instantly set:

```text
velocity = desiredVelocity
```

Use acceleration limits.

Parameters:

```text
maxAcceleration
maxDeceleration
turnRate
```

These should eventually be character-profile data.

This creates continuity that later behavior systems can rely upon.

---

# 35. VELOCITY TRACKING

Track:

```text
desired velocity
actual velocity
previous velocity
acceleration
```

These values should feed:

* animation selection
* lean
* stride
* balance
* turn behavior

Do not derive everything independently in every subsystem.

---

# 36. ROOT MOTION

Clarify ownership.

Determine whether each locomotion mode is:

```text
root-motion driven
```

or:

```text
velocity/controller driven
```

or:

```text
hybrid
```

Document the policy.

Do not allow multiple systems to fight over character translation.

There must be one authoritative movement result.

---

# 37. PROCEDURAL ROOT MOTION

Where useful, support adapting animation displacement to actual desired velocity.

Do not simply scale X/Z blindly.

Respect:

* character orientation
* terrain
* turning
* root-motion conventions
* foot contacts

---

# 38. CURVED TRAJECTORIES

Characters should be able to follow curved paths.

The motion system should handle:

```text
desired trajectory
```

rather than only a single instantaneous velocity.

Even if Phase C eventually provides richer future trajectories, Phase B should have a lightweight concept of:

```text
current desired direction
near-future direction
```

This helps produce natural turns.

---

# 39. OBSTACLE-RESPONSIVE MOTION

Do NOT build full navigation yet.

But allow the motion system to consume an externally supplied steering direction.

For example:

```text
desired velocity
+
avoidance correction
=
motion velocity
```

Phase D can later supply the avoidance direction.

Phase B must simply execute it naturally.

---

# 40. ENVIRONMENT-AWARE MOTION

The procedural system should eventually be capable of responding to:

```text
ground
slope
obstacle
target
interaction point
```

without directly owning world behavior.

Keep environmental sensing separate from motion execution.

---

# 41. MOTION QUALITY METRICS

Build offline validation metrics.

At minimum:

### Foot sliding

Measure:

```text
foot displacement while contact == planted
```

### Contact error

Measure:

```text
distance from desired contact target
```

### Joint-limit violations

Measure:

```text
joint rotation outside configured limits
```

### Reach error

Measure:

```text
end-effector → target distance
```

### Body correction

Measure:

```text
pelvis/spine correction magnitude
```

### Velocity error

Measure:

```text
actual velocity - desired velocity
```

These metrics should eventually feed MotionPack validation.

---

# 42. OFFLINE PROCEDURAL MOTION GENERATION

Create an offline tool capable of generating useful procedural variants.

Potential operations:

```text
clip
 ↓
speed variants
 ↓
stride variants
 ↓
turn variants
 ↓
start variants
 ↓
stop variants
 ↓
contact cleanup
 ↓
validation
```

Do not generate thousands of pointless clips.

Generate variants only where they provide meaningful coverage.

---

# 43. PROCEDURAL AUGMENTATION

Investigate and implement, where useful:

* mirroring
* time warping
* speed variation
* stride warping
* directional warping
* turn variation
* start/stop variation
* root-motion scaling
* upper-body variation

Every generated clip should retain provenance:

```text
source clip
transformation
parameters
tool version
```

---

# 44. MOTION QUALITY GATES

Offline generation must reject bad variants.

Potential rejection conditions:

```text
excessive foot sliding
excessive IK error
joint-limit violations
unstable pelvis
unreasonable acceleration
discontinuous root motion
contact inconsistencies
```

Do not flood MotionPacks with bad procedural output.

---

# 45. GLOWMERE ALIEN VALIDATION

Use the actual Glowmere alien as the primary validation character.

Build a demonstration that can show:

### Locomotion

```text
idle
→ walk
→ run
→ slow
→ stop
```

### Direction

```text
forward
backward
strafe
diagonal
curve
```

### Terrain

```text
flat
slope
uneven
small step
```

### Attention

```text
walk
→ look at mushroom
→ turn toward mushroom
→ continue walking
```

### Interaction

```text
approach mushroom
→ stop
→ reach
→ inspect
```

Do not require Phase D behavior logic.

Drive these scenarios from deterministic scripted MotionRequests.

---

# 46. THE FIRST TRUE VERTICAL SLICE

The first complete Phase B demonstration should be:

```text
Alien starts idle
      ↓
receives desired velocity
      ↓
accelerates into walk
      ↓
changes direction
      ↓
follows curved trajectory
      ↓
encounters uneven terrain
      ↓
feet adapt
      ↓
pelvis compensates
      ↓
character looks toward target
      ↓
slows down
      ↓
stops naturally
      ↓
reaches toward target
      ↓
returns to idle
      ↓
continues subtle breathing / idle motion
```

This should look like a continuous character motion system, not a collection of disconnected demos.

---

# 47. PERFORMANCE TARGETS

Profile:

* one character
* 10 characters
* 50 characters
* 100 characters

Measure:

```text
animation evaluation
IK
terrain queries
look-at
reach
secondary motion
pose blending
allocations
CPU time
```

Do not prematurely promise a particular frame time.

Establish measured baselines.

Identify:

```text
per-character cost
per-active-layer cost
shared cost
```

---

# 48. MULTI-CHARACTER ARCHITECTURE

Ensure static/shared data remains shared.

Shared:

```text
Skeleton
MotionPack
Animation clips
RetargetProfile
Character configuration
IK chain definitions
```

Per-instance:

```text
Pose
MotionController state
current velocity
phase
targets
layer state
random seed
```

Do not duplicate large animation data.

---

# 49. DETERMINISTIC RANDOMNESS

Procedural idle and secondary motion should use deterministic seeds.

For example:

```text
characterSeed
layerSeed
```

This permits:

* reproducible renders
* reproducible tests
* deterministic offline baking
* debugging

Do not use uncontrolled global randomness.

---

# 50. EDITOR / DEBUG VISUALIZATION

Do not build the final production character editor yet.

Add useful debug overlays.

Potential toggles:

```text
Show Skeleton
Show IK Chains
Show IK Targets
Show Contacts
Show Ground Probes
Show Velocity
Show Desired Velocity
Show Facing
Show Look Target
Show Reach Target
Show Body Compensation
Show Motion Phase
Show Layer Weights
```

This should make procedural animation debuggable.

---

# 51. AUTOMATED TESTING

Every procedural subsystem must have tests.

## Locomotion

Test:

* zero velocity
* acceleration
* deceleration
* direction change
* strafe
* backpedal
* curved movement

## Foot placement

Test:

* flat ground
* elevated ground
* lowered ground
* unreachable target
* contact release

## Look-at

Test:

* centered target
* left target
* right target
* above
* below
* outside limits

## Reach

Test:

* reachable target
* unreachable target
* moving target

## Body compensation

Test:

* small correction
* large correction
* max correction

## Secondary motion

Test:

* deterministic output
* bounded output
* stable output

Use positive/adversarial cases.

Never allow a null/no-op case to constitute the main correctness test.

---

# 52. VISUAL REGRESSION

Where practical, create deterministic animation fixtures and compare:

* joint positions
* end-effector positions
* contact error
* root trajectory
* pose continuity

Do not rely exclusively on screenshots.

Numerical regression is preferable where possible.

---

# 53. PROFILE BEFORE OPTIMIZING

Capture actual performance.

Look for:

```text
allocation hotspots
skeleton traversal
IK solving
terrain query count
pose copies
layer evaluation
matrix calculations
```

Optimize the measured bottlenecks.

Do not introduce complicated caching merely because it sounds fast.

---

# 54. DOCUMENT THE FINAL PIPELINE

When the architecture stabilizes, document:

```text
MotionRequest
      ↓
MotionController
      ↓
Base Motion Provider
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

Explain exactly:

* who owns translation
* who owns rotation
* who owns contacts
* who owns IK
* layer ordering
* data ownership
* offline/runtime boundaries

This documentation is required before declaring Phase B complete.

---

# 55. DO NOT IMPLEMENT PHASE C YET

Phase C will introduce:

* large motion libraries
* motion databases
* motion matching
* trajectory features
* query scoring
* runtime search
* database optimization

Phase B should expose the interfaces needed by those systems.

But do not turn Phase B into Phase C.

---

# 56. DO NOT IMPLEMENT PHASE D YET

Phase D will introduce:

* behavior
* goals
* navigation
* scene awareness
* autonomous decisions
* social behavior
* event response

For now, use scripted MotionRequests to validate Phase B.

---

# 57. DO NOT IMPLEMENT PHASE E YET

Phase E will investigate:

* neural motion
* learned locomotion
* learned motion matching
* pretrained models
* Core ML / ONNX / Metal inference

Do not add neural complexity now.

---

# 58. SUCCESS CRITERIA

Phase B is complete when AV Gen can demonstrate:

### Locomotion

A character can:

* accelerate
* decelerate
* walk
* run
* strafe
* backpedal
* turn
* curve
* stop
* transition between these naturally

### Body adaptation

A character can:

* adapt feet to terrain
* maintain planted contacts
* compensate pelvis/body position
* lean with acceleration/turning
* preserve balance

### Attention

A character can:

* look toward a target
* smoothly distribute rotation through head/neck/spine
* respect limits
* transition attention naturally

### Interaction

A character can:

* reach toward a target
* adapt body position where necessary
* gracefully handle unreachable targets

### Life

A stationary character can:

* breathe
* shift weight
* exhibit subtle micro-motion
* maintain deterministic variation

### Architecture

All of the above are:

* modular
* data-driven
* composable
* testable
* deterministic where appropriate
* reusable across characters
* independent of high-level behavior

### Runtime

No:

* Python
* PyTorch
* LLM
* cloud service
* mandatory neural model

is required.

---

# 59. DEFINITION OF "GOOD"

Do not declare Phase B complete because:

```text
the character moves
```

The actual standard is:

> The character should appear to have a body that is continuously adapting to its movement and environment.

Look specifically for:

* foot sliding
* skating
* snapping
* robotic head movement
* excessive spine rotation
* frozen idle poses
* sudden acceleration
* sudden stopping
* teleporting feet
* unnatural pelvis motion
* IK popping
* target overshoot
* layer conflicts
* discontinuities

If any of these are obvious, investigate rather than hiding them with stronger smoothing.

---

# 60. ARTISTIC QUALITY BAR

The target is **cinematic 3D character animation**, not a technical demo.

Avoid:

* robotic movement
* exaggerated game-engine procedural effects
* obvious IK snapping
* constant head tracking
* perfectly mechanical foot plants
* repetitive idle cycles
* primitive sine-wave animation everywhere

Procedural motion should be subtle enough that the viewer feels:

> "The character is responding."

rather than:

> "The engine is applying an animation algorithm."

---

# 61. DEVELOPMENT ORDER

Implement in this order:

```text
A. Phase A audit
        ↓
B. MotionContext
        ↓
C. MotionController
        ↓
D. acceleration / deceleration
        ↓
E. locomotion adaptation
        ↓
F. turn / directional movement
        ↓
G. foot placement
        ↓
H. terrain adaptation
        ↓
I. body compensation / balance
        ↓
J. look-at
        ↓
K. reach
        ↓
L. secondary motion
        ↓
M. offline procedural augmentation
        ↓
N. validation metrics
        ↓
O. performance profiling
        ↓
P. Glowmere integrated vertical slice
        ↓
Q. documentation / ADR updates
```

Do not implement all of these simultaneously.

Each stage must be validated before the next major layer is added.

---

# 62. IMPLEMENTATION DISCIPLINE

For every major feature:

1. Inspect existing implementation
2. Identify reusable primitives
3. Design the smallest addition
4. Implement
5. Add automated tests
6. Run tests
7. Run visual validation
8. Profile if runtime-related
9. Document the result
10. Continue

If a design assumption proves wrong:

* stop
* record the finding
* update the ADR
* adjust the implementation
* add a regression test

Do not silently work around architectural problems.

---

# 63. IMPORTANT: KEEP PROCEDURAL MOTION REVERSIBLE

A procedural layer should be possible to disable.

This is extremely useful for debugging.

For example:

```text
Base animation
Base + locomotion
Base + locomotion + IK
Base + locomotion + IK + look
...
```

This allows us to identify exactly which layer introduces an artifact.

---

# 64. IMPORTANT: DO NOT HIDE FAILURES

If:

```text
foot target unreachable
```

show/report it.

If:

```text
look target outside limits
```

handle it gracefully and expose diagnostic information.

If:

```text
reach target impossible
```

do not create an impossible pose.

If:

```text
terrain query invalid
```

fall back predictably.

The system should degrade gracefully.

---

# 65. PHASE B MILESTONE REPORT

At the end, produce:

```text
docs/design/procedural-character-motion.md
```

containing:

* final architecture
* layer ordering
* data flow
* MotionContext
* MotionController
* procedural layers
* terrain abstraction
* IK interaction
* body compensation
* look-at
* reach
* secondary motion
* offline augmentation
* performance measurements
* testing strategy
* known limitations
* future Phase C integration points

Also update relevant ADRs.

---

# 66. FINAL DEMONSTRATION

The final demonstration should show the Glowmere alien performing a scripted sequence such as:

```text
IDLE
 ↓
notice target
 ↓
turn
 ↓
accelerate
 ↓
walk
 ↓
curve around obstacle
 ↓
terrain changes
 ↓
feet adapt
 ↓
look toward target
 ↓
slow
 ↓
stop
 ↓
shift weight
 ↓
reach
 ↓
inspect
 ↓
turn away
 ↓
accelerate
 ↓
resume wandering
```

The sequence should be driven primarily by **MotionRequests**, not a hand-authored animation sequence.

The purpose is to prove that the motion system can continuously synthesize/adapt movement.

---

# 67. THE LONG-TERM ARCHITECTURE

Keep this eventual architecture in mind:

```text
                    CHARACTER GOAL
                          ↓
                    BEHAVIOR SYSTEM
                          ↓
                    MOTION REQUEST
                          ↓
                  MOTION CONTROLLER
                          ↓
              ┌───────────┴───────────┐
              │                       │
        Motion Provider         Procedural Motion
              │                       │
              └───────────┬───────────┘
                          ↓
                     BASE POSE
                          ↓
                  BODY ADAPTATION
                          ↓
             ┌────────────┼────────────┐
             ↓            ↓            ↓
           LOOK         REACH        CONTACT
             │            │            │
             └────────────┼────────────┘
                          ↓
                         IK
                          ↓
                     SECONDARY
                       MOTION
                          ↓
                    INERTIALIZATION
                          ↓
                      FINAL POSE
                          ↓
                      CHARACTER
```

Later:

```text
Phase C
Motion Matching
      ↓
Motion Provider
```

and:

```text
Phase E
Neural Motion
      ↓
Motion Provider
```

and:

```text
Phase D
Behavior
      ↓
Motion Request
```

The architecture you build now should make those additions straightforward.

---

# START

Begin by auditing the actual Phase A implementation.

Then implement the smallest vertical slice:

```text
MotionRequest
    ↓
MotionController
    ↓
desired velocity
    ↓
accelerate
    ↓
walk
    ↓
turn
    ↓
stop
    ↓
foot placement
    ↓
terrain adaptation
```

Use the Glowmere alien.

Do not start with a generic toy humanoid.

Once that works, add:

```text
look-at
```

then:

```text
reach
```

then:

```text
secondary motion
```

then:

```text
offline procedural augmentation
```

Finally integrate everything into the complete scripted Glowmere demonstration.

Do not move on to Phase C until the Phase B foundation has been:

* tested
* visually validated
* profiled
* documented

The goal is not to produce a large amount of animation code.

The goal is to produce a **small, coherent procedural motion architecture capable of making AV Gen characters feel physically responsive and continuously alive.**
