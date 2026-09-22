# AV Gen — Phase D

## Autonomous Characters, World Awareness & Behavioral Motion Planning

## Mission

Build the production-grade autonomous character layer that sits above AV Gen's animation and motion systems.

Phase A established the animation foundation:

* skeleton analysis
* retargeting
* bind-pose reconciliation
* arbitrary IK chains
* contacts
* phase
* transitions
* inertialization
* reachable-contact solving
* body adaptation

Phase B established procedural adaptation and higher-quality execution.

Phase C established the reusable motion library, procedural augmentation, and classical motion matching.

Phase D now answers the higher-level question:

> **What should a character do?**

The goal is for AV Gen characters to become autonomous, environment-aware actors rather than collections of animation clips manually sequenced by the author.

A character should be capable of behaviors such as:

* wandering
* exploring
* investigating
* approaching
* avoiding
* looking at something
* following
* stopping
* idling
* reacting to events
* observing another character
* moving toward interesting environmental features
* navigating around obstacles
* interacting with objects
* reacting to the player/camera
* reacting to audio-driven world events
* fleeing
* chasing
* gathering
* inspecting
* resting
* socializing
* becoming startled
* recovering
* transitioning between activities

And critically:

**The behavior system must not directly author animation clips.**

Instead:

```text
WORLD
  ↓
Perception
  ↓
World State
  ↓
Goals / Intent
  ↓
Behavior Selection
  ↓
Motion Request
  ↓
Motion Provider
  ↓
Pose Generation
  ↓
Body Adaptation
  ↓
IK / Contacts
  ↓
FINAL CHARACTER
```

The existing animation architecture remains underneath this system.

Do not replace Phase A/B/C systems.

Do not build an LLM-driven character controller.

Do not make behavior dependent on generative AI.

The core system must be:

* deterministic
* data-driven
* debuggable
* performant
* authorable
* testable
* suitable for many simultaneous characters
* usable without any machine-learning model
* usable on the user's M2 Max
* extensible toward future learned/neural behavior systems

---

# 1. First: Inspect the Existing System

Before implementing anything, perform a repository-wide architecture audit.

Read:

* Phase A documentation
* Phase B documentation
* Phase C documentation
* all relevant ADRs
* animation runtime
* motion providers
* motion matching
* scene representation
* entity representation
* transforms
* cameras
* lights
* world effects
* procedural generators
* timeline
* audio analysis
* signal bus
* modulators
* existing event systems
* collision/geometry systems
* spatial queries
* ray/intersection systems
* object picking
* scene serialization
* project loading
* threading/task infrastructure

Find out exactly what infrastructure already exists for:

* spatial queries
* bounds
* raycasts
* collisions
* navigation
* object identity
* entity tags
* scene metadata
* animation state
* entity transforms
* velocity
* events
* audio features
* time
* deterministic simulation
* serialization

Do not duplicate an existing system.

If AV Gen already has a subsystem that can serve as the foundation, extend it instead.

Produce:

```text
docs/design/autonomous-character-architecture.md
```

and an ADR describing the final architecture before making large production changes.

---

# 2. Core Architectural Principle

Separate five concepts that are frequently incorrectly combined in character systems.

## 2.1 Perception

What does the character know?

Examples:

```text
mushroom_17 is 8.4m away
mushroom_17 is visible
mushroom_17 is glowing
alien_04 is nearby
ufo_01 is overhead
river is nearby
terrain slopes upward
sound_event_42 occurred
```

---

## 2.2 World State

What objectively exists?

Examples:

```text
Entity #17
type = mushroom
position = ...
radius = ...
tags = [flora, glowing, interactive]
```

The world state should not pretend the character necessarily knows everything.

---

## 2.3 Perception

The character receives a filtered representation of the world.

Examples:

```text
visible_objects
nearby_objects
audible_events
interesting_objects
threats
social_targets
navigation_obstacles
```

This enables limited knowledge and makes perception configurable.

---

## 2.4 Intent / Goal

What does the character want?

Examples:

```text
wander
investigate mushroom
approach alien
avoid UFO
follow animal
observe river
return home
find shelter
```

---

## 2.5 Motion Request

How should that intention currently be expressed physically?

Example:

```cpp
MotionRequest
{
    desiredDirection
    desiredSpeed
    facingDirection
    target
    urgency
    locomotionMode
    stoppingDistance
    avoidObstacles
}
```

This is passed into Phase C.

The behavior system should **never say**:

```text
play Walking animation
play TurnLeft animation
play Inspect animation
```

It should say:

```text
go toward mushroom
slow down
face mushroom
stop at 1.2m
look downward
```

The animation/motion system determines how that happens.

---

# 3. Define a First-Class Character Intent System

Introduce a stable representation for high-level intent.

For example:

```cpp
enum class CharacterIntentType
{
    Idle,
    Wander,
    MoveTo,
    Follow,
    Investigate,
    Observe,
    Flee,
    Chase,
    Avoid,
    Interact,
    Socialize,
    Rest,
    Search,
    ReturnTo,
    Custom
};
```

Do not blindly copy this exact API.

Design the system around extensibility.

A useful abstraction is:

```cpp
struct CharacterIntent
{
    IntentType type

    EntityHandle targetEntity

    glm::vec3 targetPosition

    float desiredSpeed

    float priority

    float urgency

    float stoppingDistance

    float timeout

    uint32_t flags
}
```

The actual architecture should be determined after inspecting the existing engine.

---

# 4. Do NOT Build a Giant Hard-Coded State Machine

Avoid an architecture such as:

```text
Idle
 ↓
Walk
 ↓
Investigate
 ↓
Walk
 ↓
Inspect
 ↓
Walk
 ↓
Idle
```

That approach becomes unmaintainable once characters become complex.

Instead use layered concepts:

```text
Needs / Goals
      ↓
Behavior Selection
      ↓
Intent
      ↓
Locomotion / Interaction Planning
      ↓
Motion Request
```

A finite-state-machine implementation may still be useful internally for some behaviors.

But the public architecture should not require every new behavior to become another giant enum and switch statement.

---

# 5. Utility / Priority-Based Behavior Selection

Implement a behavior selection system that can evaluate candidate behaviors.

Example:

```text
Wander
utility = 0.35

InvestigateInterestingObject
utility = 0.81

FollowFriend
utility = 0.62

AvoidThreat
utility = 0.98
```

The highest-priority behavior should not necessarily win solely because it has the highest raw score.

Support:

* priority
* utility
* urgency
* cooldown
* hysteresis
* interruption rules
* minimum commitment duration
* behavior prerequisites
* target validity
* distance
* visibility
* personality/configuration
* world state

This prevents characters from rapidly oscillating between behaviors.

---

# 6. Behavior Definitions Must Be Data-Driven

Behaviors should be definable without recompiling the engine.

Eventually something conceptually similar to:

```json
{
    "name": "investigate_glowing_object",

    "conditions": [
        {
            "query": "nearby_object",
            "tag": "glowing"
        }
    ],

    "utility": {
        "base": 0.4,
        "distance_weight": 0.7,
        "novelty_weight": 0.5
    },

    "actions": [
        {
            "type": "approach",
            "stopping_distance": 1.5
        },
        {
            "type": "look_at"
        },
        {
            "type": "observe",
            "duration": 3.0
        }
    ]
}
```

The exact schema should be designed after inspecting AV Gen's existing JSON conventions.

Do not create a second incompatible configuration ecosystem.

---

# 7. Build a World Query / Perception System

Characters need a safe way to ask:

> What exists around me?

Implement reusable spatial queries.

Potential queries:

```text
nearest entity
entities within radius
entities with tag
entities of type
entities visible from character
entities inside cone
entities within distance range
nearest navigable point
objects intersecting path
objects between A and B
recent world events
```

Queries should be:

* deterministic
* allocation-conscious
* cacheable where useful
* thread-safe where appropriate
* spatially accelerated
* usable by many characters

Do not scan the entire scene for every character every frame.

---

# 8. Spatial Awareness

Investigate the current scene representation and implement or reuse a spatial acceleration structure.

Possible approaches:

* uniform grid
* spatial hash
* BVH
* octree
* scene bounds hierarchy
* existing renderer culling structure

Do not automatically reuse a renderer-specific structure if its semantics are wrong.

The behavior system needs:

```text
nearby
visible
reachable
audible
obstructed
```

not merely:

```text
renderable
```

Benchmark the chosen approach.

Target:

```text
100 characters
500 characters
1,000 characters
```

where practical.

---

# 9. Character Perception

Implement configurable perception components.

At minimum:

### Vision

Support:

* range
* field of view
* line of sight
* occlusion
* target categories
* attention weighting

Conceptually:

```text
           vision cone
              /\
             /  \
            /    \
           /      \
       CHARACTER
```

A character should not magically perceive everything in the scene.

---

### Proximity

Detect:

* nearby characters
* nearby objects
* nearby landmarks
* interaction targets
* terrain features

---

### Hearing / World Events

Integrate with AV Gen's event/audio architecture.

Examples:

```text
loud_audio_event
bass_hit
world_effect_started
UFO_entered
explosion
mushroom_bloom
water_event
comet_event
character_call
```

Audio-reactive environments are a core AV Gen capability.

Characters should eventually be able to respond to world events generated by the same system.

For example:

```text
DROP occurs
    ↓
world effect intensifies
    ↓
character perceives event
    ↓
attention shifts
    ↓
character looks toward event
    ↓
behavior changes
    ↓
motion request changes
```

Do not directly tie behavior to arbitrary audio bands.

Create semantic events or signals where possible.

---

# 10. Attention System

This is critical for believable characters.

Characters should not simultaneously treat every perceived object as equally important.

Implement an attention model.

Potential factors:

```text
distance
visibility
novelty
movement
brightness
sound
semantic importance
behavior relevance
social relevance
recent attention
```

The result should be something like:

```text
AttentionTarget
{
    entity
    score
    reason
    position
    confidence
    lastSeen
}
```

This allows behavior such as:

```text
alien walking
      ↓
hears sound
      ↓
looks toward sound
      ↓
continues walking
      ↓
sees glowing mushroom
      ↓
slows
      ↓
turns
      ↓
investigates
```

without scripting every animation.

---

# 11. Attention ≠ Head Look

Separate:

```text
attention
```

from:

```text
physical gaze
```

Attention determines what the character cares about.

A later procedural layer determines:

* head rotation
* eye direction
* torso orientation
* eventual body turn

This lets the character notice something before physically turning toward it.

---

# 12. Navigation

Build a navigation abstraction.

Do not assume the current renderer/world representation is sufficient for navigation.

Define something like:

```text
INavigationProvider
```

or an equivalent engine-native abstraction.

Requirements:

* walkable surfaces
* obstacles
* slope limits
* agent radius
* agent height
* destination validation
* path generation
* path smoothing
* partial paths
* unreachable destinations
* dynamic obstacles where practical

Research:

* Recast/Detour
* navmesh generation
* hierarchical pathfinding
* flow fields
* steering behaviors
* ORCA/RVO-style crowd avoidance
* Unreal-style navigation concepts
* Unity NavMesh concepts

Do not automatically import a heavyweight dependency.

First determine whether AV Gen needs:

1. simple steering
2. graph navigation
3. navmesh
4. hierarchical navigation
5. crowd simulation

The system should be layered so simple characters don't pay for unnecessary complexity.

---

# 13. Navigation Architecture

Aim for:

```text
Goal
 ↓
Destination
 ↓
Reachability Test
 ↓
Path Planner
 ↓
Path
 ↓
Local Steering
 ↓
Motion Request
 ↓
Motion Matching
```

The motion system should not know how the path was generated.

---

# 14. Local Steering

Pathfinding alone is insufficient.

Implement local steering for:

* obstacle avoidance
* character avoidance
* slowing
* stopping
* turning
* target approach
* maintaining follow distance

Research and compare:

* seek
* arrive
* flee
* pursue
* evade
* wander
* separation
* alignment
* cohesion
* obstacle avoidance
* velocity obstacles
* ORCA/RVO

Do not implement every algorithm merely because it exists.

Implement the minimum useful subset, then benchmark behavior quality.

---

# 15. Character Movement Must Become Vector-Based

Phase A identified that the existing representation:

```text
speed
yaw
```

cannot adequately express movement relative to facing.

Phase D should fully exploit the velocity/vector foundation.

Support:

```text
desired velocity
actual velocity
facing direction
desired facing direction
angular velocity
acceleration
deceleration
```

This enables:

* strafing
* circling
* backing away
* diagonal movement
* orbiting
* avoidance
* pursuit
* smooth stops
* independent facing/movement

This is especially important for future motion matching.

---

# 16. Interaction Targets

Create a first-class concept for interaction points.

An object should expose interaction affordances rather than forcing the behavior system to understand its geometry.

Example:

```text
Mushroom
 ├─ inspect
 ├─ touch
 └─ smell

Tree
 ├─ inspect
 └─ hide_behind

UFO
 └─ observe

River
 └─ approach
```

Each affordance can specify:

```text
target position
target orientation
approach radius
required animation capability
duration
conditions
```

Do not make the character directly manipulate arbitrary transforms.

---

# 17. Interaction Planning

A character should be able to reason:

```text
I want to inspect mushroom #42

↓
Is mushroom reachable?

↓
Find interaction point

↓
Navigate there

↓
Approach

↓
Turn toward mushroom

↓
Stop

↓
Adjust body / IK

↓
Look

↓
Perform inspect behavior

↓
Exit interaction

↓
Choose next goal
```

This is the first true autonomous-character vertical slice.

---

# 18. Interaction Must Use Capabilities

Characters should expose capabilities.

For example:

```text
can_walk
can_run
can_jump
can_climb
can_inspect
can_touch
can_carry
can_sit
can_swim
```

Objects expose compatible affordances.

The planner checks compatibility.

Do not hard-code:

```text
if alien then mushroom interaction
```

Build:

```text
character capability
+
object affordance
=
valid interaction
```

This allows future:

* aliens
* animals
* humans
* robots
* creatures
* abstract entities

to share the same system.

---

# 19. Behavior Lifecycle

Every behavior should have explicit lifecycle semantics.

Conceptually:

```text
CanStart
Start
Update
CanInterrupt
Interrupt
Complete
Fail
```

Failure must be first-class.

For example:

```text
Investigate mushroom
    ↓
path generated
    ↓
obstacle appears
    ↓
path invalid
    ↓
replan
```

or:

```text
approach target
    ↓
target disappears
    ↓
behavior fails
    ↓
select new behavior
```

Do not leave characters stuck in behaviors forever.

---

# 20. Behavioral Hysteresis

This is mandatory.

Without hysteresis:

```text
Investigate = 0.61
Wander = 0.60

next frame:

Investigate = 0.59
Wander = 0.62

next frame:

Investigate = 0.61
Wander = 0.60
```

Character oscillates.

Support:

* minimum behavior duration
* switching threshold
* cooldown
* commitment
* interruption priority

Example:

```text
new utility must exceed current utility
by at least threshold X
```

Make this configurable.

---

# 21. Character Memory

Implement lightweight short-term memory.

Not LLM memory.

Deterministic runtime state.

Examples:

```text
lastSeenTarget
lastInteraction
recentEvents
recentlyVisitedLocations
recentlyInvestigatedObjects
currentAttention
behaviorHistory
```

This prevents:

```text
walk to mushroom
leave mushroom
walk back to same mushroom
repeat forever
```

and enables:

```text
I already inspected that.
```

---

# 22. Interest / Novelty

Build a reusable novelty mechanism.

A character can score objects based on:

```text
distance
novelty
visual salience
movement
sound
rarity
semantic importance
recent interaction
```

This is extremely useful for procedural worlds.

A Glowmere alien should naturally find unusual things interesting.

---

# 23. Wander Must Be Intelligent

Do not implement random XYZ wandering.

Wander should operate on the navigation surface.

Possible modes:

```text
random reachable point
area-biased wandering
landmark wandering
curiosity-driven wandering
patrol
exploration
```

For example:

```text
choose candidate locations
 ↓
filter unreachable
 ↓
score novelty
 ↓
score distance
 ↓
score recent visitation
 ↓
choose target
 ↓
navigate
```

This makes procedural worlds feel alive.

---

# 24. Social Behavior

Implement a minimal framework for character-to-character interaction.

Examples:

```text
notice
approach
follow
avoid
observe
circle
greet
idle_near
flee
```

Do not build a complete social simulation yet.

Build the abstraction that allows it.

Example:

```text
SocialTarget
{
    entity
    relationship
    distance
    visibility
    interest
}
```

Relationships can initially be data-driven labels rather than sophisticated emotional models.

---

# 25. Environmental Awareness

Characters should understand semantic world features.

Do not force them to understand raw meshes.

Introduce world semantics where practical:

```text
water
cliff
forest
open_area
path
landmark
mushroom
tree
rock
building
vehicle
world_effect
```

This can come from:

* entity tags
* scene metadata
* procedural generator metadata
* navigation metadata
* interaction affordances

This is particularly important for procedurally generated AV Gen worlds.

---

# 26. World Events

Create or extend a generic event interface.

Potential events:

```text
EntitySpawned
EntityRemoved
EntityEnteredArea
EntityExitedArea
WorldEffectStarted
WorldEffectEnded
AudioEvent
InteractionStarted
InteractionCompleted
TargetLost
CharacterSpotted
UFOEntered
Impact
```

Events should not be tightly coupled to alien behavior.

Any character or world system should be able to subscribe.

---

# 27. Event → Behavior Pipeline

Support:

```text
WORLD EVENT
    ↓
PERCEPTION
    ↓
ATTENTION
    ↓
BEHAVIOR UTILITY
    ↓
INTENT
    ↓
MOTION
```

Example:

```text
UFO appears
 ↓
alien sees UFO
 ↓
UFO receives high attention score
 ↓
Observe behavior becomes highly desirable
 ↓
alien turns toward UFO
 ↓
alien approaches / observes
 ↓
UFO leaves
 ↓
target lost
 ↓
behavior exits
 ↓
wander resumes
```

No hand-authored animation sequence.

---

# 28. Audio-Reactive Characters

AV Gen's unique audiovisual architecture should eventually feed this system.

But avoid:

```text
if bass > .8
    play animation
```

Instead expose semantic world signals/events.

Examples:

```text
music_section_changed
beat
drop
energy_rise
energy_fall
impact
onset
spectral_event
```

Then behaviors may react to those events.

For example:

```text
Drop
 ↓
environment changes
 ↓
character attention shifts
 ↓
behavior utility changes
 ↓
character becomes more active
```

The behavior system remains independent from the audio analyzer.

---

# 29. Character Personality

Do not implement a giant personality AI system yet.

Instead create a small parameter layer.

Examples:

```text
curiosity
sociability
caution
aggression
wander_frequency
attention_span
movement_speed
preferred_distance
event_sensitivity
```

These parameters influence behavior utility.

Thus two identical alien rigs can behave differently.

Example:

```text
Alien A:
curiosity = 0.9
caution = 0.2

Alien B:
curiosity = 0.3
caution = 0.8
```

Same world.

Different behavior.

---

# 30. Behavior Budgeting

This is a major performance requirement.

Do not evaluate every behavior for every character every render frame.

Introduce update frequencies.

For example:

```text
High-priority movement:
20–60 Hz

Navigation:
5–20 Hz

Perception:
5–15 Hz

Behavior reconsideration:
2–10 Hz

Long-term interest:
0.5–2 Hz
```

These are starting points, not requirements.

Benchmark and tune.

Characters far from the active camera may update less frequently.

But be careful not to tie simulation correctness entirely to camera visibility.

Provide deterministic simulation policies.

---

# 31. LOD for Behavior

Eventually support behavioral LOD.

Potential tiers:

### Tier 0

Full:

* perception
* navigation
* behavior
* animation
* IK

### Tier 1

Reduced:

* lower perception rate
* lower behavior rate
* simplified navigation

### Tier 2

Background:

* coarse simulation
* simplified locomotion
* no expensive perception

### Tier 3

Dormant:

* state snapshot only

This should be designed now but does not need the complete implementation in the first vertical slice.

---

# 32. Simulation vs Rendering

Keep autonomous simulation separate from rendering.

Architecture:

```text
Simulation
    ↓
CharacterState
    ↓
Animation / Motion
    ↓
Render Representation
```

The behavior system must not depend on whether a character is currently rendered.

This will matter for:

* offline rendering
* deterministic export
* multiple cameras
* editor viewport
* preview
* background simulation

---

# 33. Determinism

This is an audiovisual engine.

Behavior must be deterministic enough for:

```text
same scene
same seed
same timeline
same simulation settings
=
same result
```

Avoid uncontrolled:

* random_device
* wall-clock dependence
* thread-order-dependent decisions
* unordered iteration where ordering affects choices
* nondeterministic floating-point reductions

Use seeded deterministic RNG.

Record simulation seeds in project data where appropriate.

---

# 34. Timeline Integration

Characters must coexist with the existing AV Gen timeline.

There should eventually be three modes:

### Autonomous

Character makes decisions continuously.

### Authored

Timeline explicitly controls behavior.

### Hybrid

Timeline injects high-level goals while the character handles execution.

Example:

```text
Timeline:
"Investigate mushroom at 00:32"

Character:
finds path
approaches
turns
looks
interacts
leaves
```

This is much more powerful than manually authoring the entire animation sequence.

---

# 35. Director Integration

The existing AV Gen director should eventually be able to reason about autonomous characters.

For example:

```text
Director:
create shot showing alien investigating mushroom

Character:
autonomously decides how to investigate mushroom

Camera:
tracks resulting behavior
```

Do not couple the director directly to animation clips.

The director should target:

```text
character
goal
behavior
event
```

rather than:

```text
animation clip
```

---

# 36. Camera / Cinematic Awareness

Do not make characters camera-dependent.

However, expose optional cinematic signals.

Examples:

```text
isHero
isInShot
distanceToCamera
cameraVisibility
screenImportance
```

These can influence:

* behavior priority
* animation quality
* simulation LOD
* attention

But they should not determine the character's fundamental world state.

---

# 37. First Major Vertical Slice

The first complete Phase D demonstration should be extremely concrete.

Use the existing Glowmere alien.

Place:

```text
Alien
Mushroom
Tree
Rock
River
UFO
```

in a representative world.

Give the alien:

```text
wander
observe
investigate
approach
avoid
idle
return
```

behaviors.

Then create this scenario:

```text
ALIEN
 ↓
wanders naturally
 ↓
sees glowing mushroom
 ↓
mushroom becomes interesting
 ↓
alien chooses investigate behavior
 ↓
pathfinds
 ↓
avoids obstacle
 ↓
approaches mushroom
 ↓
slows
 ↓
faces mushroom
 ↓
stops at interaction distance
 ↓
looks at mushroom
 ↓
observes for several seconds
 ↓
mushroom loses novelty
 ↓
alien leaves
 ↓
chooses a new interesting destination
 ↓
continues wandering
```

No manually authored animation sequence.

Motion matching handles locomotion.

Phase B handles adaptation and IK.

Phase D handles the decision-making.

This is the milestone.

---

# 38. Second Vertical Slice — UFO Reaction

Then build:

```text
Alien wandering
        ↓
UFO enters world
        ↓
alien perceives UFO
        ↓
attention target changes
        ↓
Observe behavior selected
        ↓
alien turns toward UFO
        ↓
approaches appropriate observation distance
        ↓
looks upward
        ↓
reacts to tractor beam
        ↓
UFO departs
        ↓
behavior exits
        ↓
alien returns to autonomous life
```

Do not hard-code the entire sequence.

The UFO should generate world events and expose interaction/observation semantics.

---

# 39. Third Vertical Slice — Multiple Characters

Run:

```text
5 aliens
```

with different personality parameters.

Then:

```text
20 aliens
```

Then, where practical:

```text
50–100
```

Characters should not all make identical decisions.

They should:

* choose different destinations
* notice different things
* have different attention
* avoid each other
* occasionally interact
* move naturally through the environment

This becomes the first real test of whether the architecture scales.

---

# 40. Debugging / Visualization

Autonomous systems are nearly impossible to debug without visualization.

Add a debug mode showing:

### Perception

```text
vision cone
visible entities
audible events
```

### Attention

```text
current target
attention score
reason
```

### Behavior

```text
current behavior
candidate behaviors
utility scores
```

### Intent

```text
current goal
target
desired position
```

### Navigation

```text
path
waypoints
destination
local steering
```

### Motion

```text
desired velocity
actual velocity
facing
```

### Interaction

```text
interaction target
approach point
interaction radius
```

This should be available in the AV Gen workspace/canvas.

Do not build a huge polished UI yet.

A strong debug visualization is more valuable at this stage.

---

# 41. "Why Is This Character Doing That?"

Build a diagnostic representation.

For every active character, make it possible to answer:

```text
Current behavior:
Investigate

Why:
Glowing mushroom #42

Why selected:
Utility = 0.81

Factors:
Novelty       +0.31
Distance      +0.18
Visibility    +0.16
Personality   +0.12
Recent visit  -0.02

Current intent:
MoveTo mushroom #42

Navigation:
12.4m path

Motion:
Walk

Motion provider:
Motion Matching

Current target:
1.5m

Next transition:
Observe
```

This diagnostic capability is not optional.

Without it, autonomous behavior will become opaque.

---

# 42. Testing Strategy

Build deterministic tests for:

### Perception

* visible target
* occluded target
* outside FOV
* outside range

### Attention

* novelty
* distance
* salience
* competing targets

### Behavior

* utility
* priority
* interruption
* cooldown
* hysteresis

### Navigation

* reachable
* unreachable
* obstacle
* dynamic obstacle
* destination invalidation

### Steering

* obstacle avoidance
* arrival
* separation
* target following

### Interaction

* valid affordance
* invalid capability
* unreachable interaction point
* target disappearance

### Memory

* recently visited
* recently investigated
* cooldown

### Determinism

Same:

```text
scene
seed
timeline
simulation settings
```

must produce equivalent decisions.

---

# 43. Adversarial Tests

Continue the methodology discovered during Phase A.

Do not accept a test suite where:

```text
no target
```

accidentally makes broken code look correct.

Every major subsystem needs:

### Positive case

Something must happen.

### Negative case

Something must not happen.

### Adversarial case

A tempting incorrect behavior must be rejected.

Examples:

```text
visible target
→ character should investigate

occluded target
→ character should not investigate

unreachable target
→ character should not enter infinite approach state

target disappears
→ character should exit behavior

two nearly equal utilities
→ character should not oscillate

obstacle appears
→ character should replan

multiple targets
→ character should choose based on configured utility

same seed
→ identical decision sequence
```

---

# 44. Performance Instrumentation

Add explicit profiling for:

```text
perception
spatial queries
attention
behavior evaluation
navigation
pathfinding
steering
interaction planning
motion request generation
```

Benchmark:

```text
1 character
10
50
100
500
1,000
```

where practical.

Record:

```text
CPU ms/frame
allocations
bytes allocated
query counts
pathfinding calls
behavior evaluations
```

Do not optimize blindly.

---

# 45. Threading

Investigate which work can safely run asynchronously.

Potentially parallel:

```text
perception
spatial queries
behavior scoring
path generation
```

But maintain a clean deterministic boundary.

A useful conceptual architecture:

```text
WORLD SNAPSHOT
      ↓
parallel perception
      ↓
parallel behavior evaluation
      ↓
intent resolution
      ↓
navigation
      ↓
motion requests
      ↓
animation
```

Do not introduce threading merely for theoretical scalability.

Benchmark first.

---

# 46. Memory / Allocation Discipline

Avoid per-frame:

```cpp
std::vector
std::string
heap allocations
```

inside hot character loops.

Prefer:

* stable handles
* reusable scratch buffers
* contiguous arrays
* small-vector storage where appropriate
* IDs instead of strings
* interned tags
* cached query results

Profile before optimizing further.

---

# 47. Serialization

Character definitions must survive save/load.

Serialize:

```text
character definition
behavior configuration
personality
capabilities
navigation parameters
perception parameters
interaction configuration
seed
initial state
```

Do not serialize transient runtime state unless necessary.

Distinguish:

```text
authored state
```

from:

```text
simulation state
```

This matters for deterministic offline rendering.

---

# 48. Editor Integration

Do not build a giant character editor in Phase D.

However, add enough inspection to make the system usable.

At minimum:

```text
Character
 ├─ Behavior
 ├─ Personality
 ├─ Perception
 ├─ Navigation
 ├─ Capabilities
 └─ Debug
```

The exact panel architecture should follow AV Gen's existing first-class panel philosophy.

Do not create a project-specific "Alien panel."

The system should be generic.

---

# 49. Generic Character Definition

The final architecture should permit:

```text
Alien
Animal
Human
Robot
Creature
```

without engine-level branching.

The character definition should specify:

```text
skeleton
motion library
capabilities
movement constraints
perception
behaviors
personality
navigation profile
interaction profile
```

---

# 50. No LLM Dependency

Explicitly do NOT implement:

```text
LLM → character brain
```

in Phase D.

LLMs are unnecessary for the core autonomous loop.

The architecture should instead support:

```text
deterministic behavior
       ↓
future learned behavior
       ↓
future optional LLM-driven high-level goals
```

If an eventual LLM integration is desired, it should produce high-level goals such as:

```text
investigate the strange light
```

while AV Gen still handles:

```text
perception
navigation
interaction
motion
IK
```

Do not put an LLM in the frame loop.

---

# 51. Research Before Implementation

Research the following systems and papers before locking the architecture:

## Navigation

Recast & Detour:

https://github.com/recastnavigation/recastnavigation

Steering behaviors:

https://www.red3d.com/cwr/steer/

RVO / ORCA:

https://gamma-web.iacs.umd.edu/ORCA/

## Game AI / Behavior Architecture

Utility AI:

https://www.gameaipro.com/

Behavior trees:

https://www.gameaipro.com/

GOAP:

https://www.gamedeveloper.com/programming/goal-oriented-action-planning-for-games

Do not assume any one architecture is universally correct.

Compare:

```text
FSM
Behavior Tree
Utility AI
GOAP
Hierarchical State Machine
Planner + Utility
```

and select the smallest architecture that satisfies AV Gen's needs.

My expectation is that a **utility/goal layer + hierarchical behavior execution + explicit motion requests** will fit AV Gen particularly well, but verify this against the actual codebase before committing.

---

# 52. Scene-Aware Motion Research

Continue the research direction from the earlier animation work.

Study:

SAMP:

https://github.com/mohamedhassanmus/SAMP

AI4Animation:

https://github.com/sebastianstarke/AI4Animation

Orange Duck Motion Matching:

https://github.com/orangeduck/Motion-Matching

The goal is not to copy these systems.

The goal is to understand the boundary between:

```text
behavior
navigation
scene awareness
motion planning
motion synthesis
```

and ensure AV Gen maintains clean separation.

---

# 53. Learned Behavior Must Remain Optional

Do not train a neural policy.

Do not introduce Python into runtime.

Do not require PyTorch.

Design interfaces that could eventually support:

```text
LearnedBehaviorProvider
```

or:

```text
LearnedMotionPlanner
```

but implement deterministic behavior first.

A future learned system should be able to replace:

```text
Behavior Selection
```

or:

```text
Motion Planning
```

without replacing:

```text
Perception
Navigation
IK
Motion Matching
Rendering
```

---

# 54. Behavior Provider Abstraction

Consider an interface along the lines of:

```cpp
class IBehaviorProvider
{
public:
    virtual CharacterIntent evaluate(
        const CharacterContext& context
    ) = 0;
};
```

Potential implementations:

```text
RuleBasedBehaviorProvider
UtilityBehaviorProvider
ScriptedBehaviorProvider
FutureLearnedBehaviorProvider
FutureExternalBehaviorProvider
```

Do not prematurely expose unnecessary virtual dispatch in hot loops.

Use the architecture that fits AV Gen's existing style.

---

# 55. Character Controller

Create a central runtime controller responsible for coordinating:

```text
perception
memory
attention
behavior
intent
navigation
steering
motion request
```

Conceptually:

```text
CharacterController
        │
        ├── Perception
        ├── Memory
        ├── Attention
        ├── Behavior
        ├── Navigation
        ├── Steering
        └── MotionRequest
```

The controller should not own:

```text
renderer
mesh
GPU resources
```

Keep those separate.

---

# 56. Motion Boundary

The most important boundary in this entire phase:

```text
BEHAVIOR SYSTEM
        ↓
MotionRequest
```

Everything below that point belongs to the animation/motion system.

Example:

```text
CharacterIntent:
Investigate mushroom

        ↓

Navigation:
destination = mushroom interaction point

        ↓

Steering:
desiredVelocity = ...

        ↓

MotionRequest:
desiredVelocity
desiredFacing
locomotionMode
target
urgency

        ↓

Phase C:
Motion Matching

        ↓

Phase A/B:
pose
body adaptation
IK
contacts
inertialization
```

This boundary must remain clean.

---

# 57. Avoid Animation Coupling

The behavior system should never contain code such as:

```cpp
playClip("Walk")
playClip("LookAround")
playClip("InspectMushroom")
```

Likewise, do not create:

```text
AlienInvestigateAnimationController
```

The system should work for any character with an appropriate motion library.

---

# 58. Offline Authoring / Validation

Create tools where useful:

```text
avgen-character-validate
avgen-navigation-bake
avgen-behavior-test
avgen-perception-test
```

A character definition should be validated before runtime.

Catch:

```text
missing capabilities
invalid affordances
invalid navigation profile
missing motion provider
invalid behavior references
impossible interaction requirements
```

---

# 59. Error Handling

Autonomous systems must fail gracefully.

Examples:

```text
No navigation mesh
→ remain idle / choose another behavior

No motion matching database
→ fallback to clip/procedural locomotion

Target unreachable
→ abandon target

Target deleted
→ clear target

No interaction affordance
→ observe instead

Motion unavailable
→ fallback motion provider
```

Never allow:

```text
infinite loop
NaN transform
teleport
stuck behavior
unbounded path retries
```

---

# 60. Teleporting Is Not a Valid Failure Strategy

Explicitly detect:

```text
navigation failure
motion failure
contact failure
target loss
```

Do not silently teleport the character to the destination to make a test pass.

If a fallback teleport mode is useful for editor tooling, it must be explicit and never occur in normal simulation.

---

# 61. Character Spawn / Initialization

A newly spawned autonomous character should be able to:

```text
initialize
 ↓
validate navigation
 ↓
initialize perception
 ↓
choose initial goal
 ↓
request motion
```

Do not require manually authored initial animation state.

---

# 62. Offline Render Compatibility

The autonomous system must work correctly during offline rendering.

Important:

```text
frame N
```

must be able to reproduce the same character state as interactive playback when deterministic mode is enabled.

Avoid dependence on:

```text
editor frame rate
mouse movement
wall clock
render frame pacing
```

---

# 63. Scrubbing

AV Gen has a timeline/editor architecture.

Autonomous simulation must eventually support scrubbing.

At minimum, design for:

```text
simulation reset
deterministic replay
state reconstruction
```

Do not pretend arbitrary long-horizon autonomous behavior can automatically be scrubbed correctly without state management.

Research whether AV Gen needs:

* periodic simulation checkpoints
* deterministic replay
* event replay
* state snapshots

Do not overbuild this in the first slice, but document the architecture.

---

# 64. Offline Deterministic Simulation

Add a test mode:

```text
simulate scene for N seconds
```

and produce a behavior trace:

```text
0.00  Wander
4.21  Observe Mushroom#42
5.04  MoveTo Mushroom#42
8.31  Observe
12.00 Wander
18.32  Observe UFO#1
...
```

Running the same simulation twice should produce the same trace.

This becomes one of the strongest regression tests for Phase D.

---

# 65. Behavior Trace Logging

Create structured diagnostics.

Example:

```text
Character 17

00:12.42
Behavior changed:
Wander → Investigate

Target:
Entity 42

Reason:
glowing + novel + visible

Utility:
0.82

Previous:
0.47

Navigation:
reachable

Motion:
Walk

Provider:
MotionMatching
```

Allow traces to be disabled in production.

---

# 66. Glowmere Integration

Once the generic system works, use Glowmere as the proving ground.

Add semantic metadata to:

* mushrooms
* trees
* rocks
* water
* UFO
* aliens
* environmental effects

Then allow aliens to discover these through generic perception.

Do not add:

```text
if scene == glowmere
```

to the engine.

Glowmere should be an ordinary project using generic systems.

---

# 67. Glowmere Demonstration Behaviors

Implement:

### Wander

Explore nearby navigable environment.

### Curiosity

Notice unusual glowing objects.

### Investigate

Approach and observe.

### Avoid

Move away from threatening/undesired objects.

### Observe UFO

Notice, approach/position, look upward, observe.

### Return

Return toward a designated region after exploration.

### Idle

Perform contextual idle behavior while maintaining awareness.

---

# 68. Contextual Idle

Do not make idle equivalent to:

```text
play idle clip forever
```

Idle can include:

* looking around
* shifting weight
* checking nearby objects
* changing attention
* small procedural movement
* occasional repositioning

Phase C/B should determine actual body motion.

Phase D determines the behavioral context.

---

# 69. Interaction with World Effects

World effects should be able to expose semantic information.

Examples:

```text
Aurora
Comet
Vortex
Cosmic Ocean
UFO Beam
Mushroom Ripple
```

A character may perceive them as:

```text
visual event
world event
landmark
attention target
```

Do not expose raw renderer internals to the character system.

---

# 70. Audio / World / Character Architecture

The eventual AV Gen architecture should resemble:

```text
                    AUDIO
                      │
                      ↓
                Audio Analysis
                      │
                      ↓
                 Signal Bus
                      │
                      ↓
               World / Events
                      │
            ┌─────────┴─────────┐
            ↓                   ↓
       World State         Character Perception
                                │
                                ↓
                           Attention
                                │
                                ↓
                         Behavior / Goals
                                │
                                ↓
                              Intent
                                │
                                ↓
                           Navigation
                                │
                                ↓
                            Steering
                                │
                                ↓
                         Motion Request
                                │
                                ↓
                         Motion Provider
                                │
                                ↓
                         Pose Generation
                                │
                                ↓
                      Body Adaptation / IK
                                │
                                ↓
                             RENDER
```

This is the architectural direction.

---

# 71. Future AI Boundary

The eventual system should permit:

```text
                   OPTIONAL AI
                       │
             high-level goals only
                       ↓
               CHARACTER INTENT
                       ↓
        deterministic AV Gen systems
                       ↓
                 final behavior
```

Potential future components:

```text
LearnedBehaviorProvider
NeuralMotionProvider
LearnedNavigation
LLMGoalProvider
```

All optional.

None should be required for the core product.

---

# 72. Research Deliverables

Before implementation is considered complete, produce:

```text
docs/design/autonomous-character-architecture.md
docs/design/character-behavior-model.md
docs/design/navigation-architecture.md
docs/design/character-perception.md
docs/design/interaction-affordances.md
```

and appropriate ADRs.

Include:

* alternatives considered
* rejected approaches
* performance implications
* determinism implications
* threading implications
* serialization
* future neural integration
* editor integration
* runtime cost
* limitations

---

# 73. Implementation Order

Do NOT implement everything simultaneously.

Use this sequence.

## Phase D.0 — Architecture

* inspect codebase
* inspect Phase A/B/C
* research
* define boundaries
* ADRs

No major runtime implementation yet.

---

## Phase D.1 — Character State

Build:

* CharacterDefinition
* CharacterState
* capabilities
* personality
* deterministic seed
* runtime controller

---

## Phase D.2 — World Queries

Build:

* spatial queries
* nearby entity queries
* semantic filtering
* visibility queries
* reusable query infrastructure

---

## Phase D.3 — Perception

Build:

* vision
* proximity
* world event perception
* perception memory

---

## Phase D.4 — Attention

Build:

* target scoring
* novelty
* salience
* attention persistence
* attention decay

---

## Phase D.5 — Behavior Selection

Build:

* goals
* utility
* priority
* hysteresis
* interruption
* cooldown
* behavior lifecycle

---

## Phase D.6 — Navigation

Build:

* navigation abstraction
* reachable targets
* pathfinding
* path smoothing
* re-planning

---

## Phase D.7 — Steering

Build:

* seek
* arrive
* avoid
* separation
* target following

---

## Phase D.8 — Motion Integration

Connect:

```text
behavior
→ intent
→ navigation
→ steering
→ MotionRequest
→ Phase C
```

This is the first true integration milestone.

---

## Phase D.9 — Interaction

Build:

* affordances
* interaction targets
* approach points
* capability matching
* interaction lifecycle

---

## Phase D.10 — Glowmere

Build the complete autonomous alien demonstration.

---

## Phase D.11 — Multi-Agent

Test:

```text
5
20
50
100
```

characters.

---

## Phase D.12 — Performance / LOD

Only after behavior quality is correct:

* update budgets
* caching
* spatial acceleration
* asynchronous work
* simulation LOD
* memory optimization

---

# 74. Definition of Done

Phase D is complete only when:

### Architecture

* behavior is separated from animation
* navigation is separated from motion
* perception is separated from world state
* interaction uses capabilities/affordances
* no LLM dependency exists

### Autonomous behavior

A character can:

* perceive
* notice
* choose
* navigate
* avoid
* approach
* interact
* lose targets
* recover
* choose another behavior

### Motion

Behavior produces high-level MotionRequests.

Phase C produces locomotion.

Phase A/B produces final physical adaptation.

### World

Characters can react to:

* objects
* characters
* world effects
* events

### Determinism

Same:

```text
scene
seed
timeline
settings
```

produces the same behavior trace.

### Performance

Character behavior is measurable and bounded.

No accidental:

```text
O(characters × entire scene)
```

perception loop.

### Debuggability

The developer can answer:

> Why is this character doing this?

without reading source code.

### Genericity

The system is not Glowmere-specific.

At least one non-alien test fixture should exercise the generic architecture.

### Resilience

Characters do not:

* get stuck forever
* teleport unexpectedly
* NaN
* endlessly replan
* oscillate between behaviors
* chase deleted entities
* select impossible interactions

---

# 75. Required Final Report

At the end of the phase produce:

```text
docs/reports/autonomous-character-phase-d-report.md
```

Include:

## Architecture

What was built.

## Behavior model

How decisions are made.

## Perception

How characters perceive the world.

## Navigation

How paths are generated.

## Interaction

How affordances work.

## Motion integration

How MotionRequest crosses into Phase C.

## Determinism

How reproducibility is guaranteed.

## Performance

Provide measured results for:

```text
1
10
50
100
500
1000
```

where practical.

Report:

```text
CPU ms
memory
allocations
query count
pathfinding count
behavior evaluations
```

## Glowmere

Show the autonomous alien scenario working.

## Limitations

Explicitly document:

* what is not solved
* what is simplified
* what requires future work

## Future neural integration

Describe exactly where a learned system could eventually plug in without redesigning the engine.

---

# 76. Critical Constraints

Throughout the entire phase:

### DO

* extend existing AV Gen architecture
* reuse Phase A/B/C
* keep systems generic
* make behavior deterministic
* make behavior inspectable
* make world queries reusable
* use semantic world metadata
* benchmark
* test adversarially
* keep runtime C++
* target M2 Max
* support many characters
* keep AI optional

### DO NOT

* introduce LLMs into the frame loop
* require Python
* require PyTorch
* require CUDA
* build a neural behavior model
* hard-code Glowmere behavior into engine code
* hard-code animation clips into behaviors
* build a giant FSM
* scan the entire scene every frame
* teleport characters to solve navigation failures
* hide behavior decisions from diagnostics
* build a giant character UI before the runtime model is stable
* replace the existing animation system
* replace Phase C motion matching
* prematurely optimize without measurements

---

# 77. The Most Important Architectural Test

At the end, delete the Glowmere-specific behavior configuration and create a second character.

For example:

```text
Creature B
```

with:

```text
different skeleton
different motion library
different capabilities
different personality
```

Place it in a new test world.

If the same autonomous framework works with only data/configuration changes, the architecture is succeeding.

If the implementation requires:

```cpp
if (alien)
if (glowmere)
if (mushroom)
if (ufo)
```

throughout the runtime, stop and refactor.

---

# 78. Final Target Architecture

The target architecture after Phase D should be approximately:

```text
                        WORLD
                          │
              ┌───────────┴───────────┐
              │                       │
        WORLD STATE              WORLD EVENTS
              │                       │
              └───────────┬───────────┘
                          ↓
                     PERCEPTION
                          ↓
                     ATTENTION
                          ↓
                 MEMORY / NOVELTY
                          ↓
                 GOALS / BEHAVIORS
                          ↓
                  CHARACTER INTENT
                          ↓
                     NAVIGATION
                          ↓
                      STEERING
                          ↓
                  MOTION REQUEST
                          ↓
             ┌────────────┴────────────┐
             │                         │
           CLIP                  MOTION MATCH
             │                         │
             └────────────┬────────────┘
                          ↓
                    POSE GENERATION
                          ↓
                  BODY ADAPTATION
                          ↓
                    IK / CONTACTS
                          ↓
                     FINAL POSE
                          ↓
                       RENDER
```

And the offline side:

```text
MOTION DATA
    ↓
RETARGET
    ↓
CLEAN
    ↓
CONTACT / PHASE
    ↓
TRAJECTORY
    ↓
AUGMENTATION
    ↓
MOTION DATABASE
    ↓
RUNTIME MOTION PROVIDERS
```

The key architectural boundary is:

```text
                    WHAT?
                     │
            Behavior / Goals
                     │
                  Intent
                     │
                     ▼
                    HOW?
             Navigation / Motion
                     │
                     ▼
                   BODY
              Motion / IK / Pose
```

That separation is what allows AV Gen to eventually have characters that **actually appear to live inside the worlds you create**, rather than characters that merely execute increasingly sophisticated animation sequences.

---

# Phase D Success Demonstration

The final demonstration should be something you can watch for several minutes without manually controlling the alien.

Start playback.

The alien:

1. wanders
2. notices a glowing mushroom
3. decides it is interesting
4. navigates toward it
5. avoids a rock
6. approaches naturally
7. slows down
8. turns toward the mushroom
9. looks down
10. observes it
11. loses interest
12. leaves
13. notices another event
14. reacts differently because of its personality
15. encounters another alien
16. avoids or approaches it
17. notices the UFO
18. looks upward
19. responds to the event
20. returns to autonomous exploration

And none of that should require the author to specify:

```text
Walk clip at 12.3 seconds
Turn animation at 14.1
Look animation at 15.0
Idle animation at 18.0
```

The author specifies the **world, character, capabilities, goals, behaviors and artistic constraints**.

AV Gen determines the physical execution.

That is the point of Phase D.
