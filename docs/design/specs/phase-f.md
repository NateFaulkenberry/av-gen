# AV Gen — Phase F

## Advanced Character Simulation, Generative Motion, Interaction & Cinematic Intelligence

## Mission

Phase F is the final major expansion of AV Gen's character architecture.

By this point, AV Gen should already have:

```text id="0v4h3x"
Phase A
Animation Foundation

Phase B
Procedural Adaptation / IK / Contacts / Motion Quality

Phase C
Motion Library / Augmentation / Motion Matching

Phase D
Autonomous Characters / Perception / Behavior / Navigation

Phase E
Optional Learned / Neural Motion
```

Phase F builds the higher-order systems that make those capabilities behave like a **complete production-grade character simulation environment**.

The objective is not simply:

> "Add more AI."

The objective is:

> **Allow complex characters to exist, move, react, interact, improvise, generate motion, participate in crowds, respond to the world, and remain controllable by the artist and director.**

The resulting system should support:

```text id="s5j5kn"
AUTHOR
   ↓
WORLD
   ↓
CHARACTER
   ↓
AUTONOMOUS SIMULATION
   ↓
INTERACTION
   ↓
MOTION SYNTHESIS
   ↓
SECONDARY PHYSICS
   ↓
CINEMATIC DIRECTION
   ↓
OFFLINE QUALITY
```

while preserving:

* deterministic simulation
* offline rendering
* artist control
* runtime performance
* M2 Max viability
* graceful fallbacks
* modularity
* debuggability

---

# 1. Phase F Is Deliberately Broad

Do not attempt to implement every feature simultaneously.

Phase F should be treated as a **capability platform** with several independent tracks:

```text id="5mmk71"
F1  Advanced Interaction

F2  Procedural Full-Body Motion

F3  Secondary Motion / Physics

F4  Generative Offline Motion

F5  Crowd / Multi-Agent Simulation

F6  Advanced Character Intelligence

F7  Cinematic Character Direction

F8  Character Quality / Offline Validation

F9  Production Tooling
```

Each track should have its own research and vertical slice.

Do not let one experimental subsystem block the rest.

---

# 2. Preserve the Existing Architecture

The canonical architecture remains:

```text id="yqz80b"
WORLD
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
MOTION PROVIDER
 ↓
POSE
 ↓
BODY ADAPTATION
 ↓
IK / CONTACTS
 ↓
SECONDARY MOTION
 ↓
FINAL CHARACTER
 ↓
RENDER
```

Phase F adds capabilities around this.

It does not replace it.

---

# 3. F1 — Advanced Interaction

The first major capability should be richer interaction.

Characters should be able to interact with:

* terrain
* plants
* objects
* doors
* vehicles
* creatures
* world effects
* other characters
* environmental landmarks

The existing Phase D affordance system becomes substantially more expressive.

---

# 4. Interaction Affordances

Expand the concept:

```text id="v7m3s4"
InteractionAffordance
```

to potentially describe:

```text id="0q7pzr"
target
approach position
approach orientation
contact points
required capabilities
body constraints
duration
preconditions
completion conditions
failure conditions
```

For example:

```text id="aqb1x9"
Mushroom
 └── Inspect
      ├── approach point
      ├── look target
      ├── hand target
      └── duration
```

---

# 5. Multi-Stage Interactions

Interactions should support:

```text id="6d6e36"
Approach
 ↓
Align
 ↓
Prepare
 ↓
Contact
 ↓
Action
 ↓
Observe
 ↓
Release
 ↓
Exit
```

The character does not need a manually authored animation sequence.

Each stage produces constraints and intent.

The motion system determines the actual body movement.

---

# 6. Procedural Reach

Build robust procedural reaching.

Inputs:

```text id="k8ep8f"
hand target
body position
body orientation
reach constraints
```

Output:

```text id="8q7l7f"
shoulder
upper arm
forearm
hand
```

Use IK.

Support:

* reach
* touch
* point
* grab
* hold
* release

The system should respect:

* joint limits
* reachability
* body compensation
* collision
* contact

---

# 7. Hand / Foot / Body Contact

Expand Phase A/B contact systems into a general constraint framework.

Possible contact types:

```text id="7r5k8p"
foot
hand
knee
elbow
head
body
tail
custom
```

Conceptually:

```cpp id="w9qf8b"
ContactConstraint
{
    joint
    target
    position
    orientation
    weight
    stiffness
}
```

Do not restrict the engine to humanoid assumptions.

---

# 8. Constraint Stack

Eventually support:

```text id="cbydca"
Motion
 ↓
Body Adaptation
 ↓
IK
 ↓
Contacts
 ↓
Interaction Constraints
 ↓
Secondary Motion
```

with explicit priority/weighting.

This is necessary when several constraints compete.

Example:

```text id="j0d9h7"
walk
+
look at mushroom
+
reach toward mushroom
+
keep feet planted
```

The system must resolve these together rather than producing independent competing modifications.

---

# 9. F2 — Full-Body Procedural Motion

Expand procedural animation beyond locomotion.

Support:

* leaning
* reaching
* turning
* looking
* crouching
* balancing
* stepping
* bracing
* reacting
* pointing
* grabbing
* pushing
* pulling

These should be composable layers.

---

# 10. Procedural Layer Stack

Build a generalized procedural layer architecture.

Conceptually:

```text id="8a6q9v"
Base Motion
     ↓
Locomotion
     ↓
Look
     ↓
Aim
     ↓
Reach
     ↓
Balance
     ↓
Contact
     ↓
Interaction
     ↓
Secondary Motion
```

Each layer should have:

* weight
* priority
* enable/disable
* blend mode
* target
* constraints

---

# 11. Procedural Looking

Create a reusable look-at system.

Support:

```text id="5aqqd1"
head
eyes
neck
spine
torso
```

with configurable distribution.

The character should be able to:

```text id="czr7ey"
notice object
 ↓
eyes move
 ↓
head turns
 ↓
torso follows
 ↓
body turns if necessary
```

Do not snap the entire body immediately.

---

# 12. Anticipation and Follow-Through

Investigate procedural animation principles for:

* anticipation
* overshoot
* follow-through
* settling
* weight shift

These can make procedurally generated movement feel significantly more authored.

Do not overbuild this initially.

Start with:

```text id="w0ym9b"
look
reach
turn
stop
```

and evaluate.

---

# 13. Balance

Build a balance controller.

Inputs:

```text id="9c1x6p"
center of mass
support polygon
velocity
acceleration
foot contacts
terrain
```

Outputs:

```text id="5b2h44"
body lean
step correction
foot reposition
```

This enables:

* uneven terrain
* sudden stopping
* pushing
* impacts
* reaching
* recovery

---

# 14. Dynamic Foot Placement

Expand terrain adaptation.

Support:

* slopes
* steps
* rocks
* uneven ground
* moving platforms
* dynamic terrain

Use the Phase A reachable-contact logic.

Do not allow the solver to request impossible positions without body compensation.

---

# 15. F3 — Secondary Motion / Physics

Characters should eventually support secondary motion.

Potential systems:

```text id="d5l7m2"
hair
cloth
tails
ears
antennae
tentacles
loose accessories
backpacks
organic appendages
```

Do not require a full rigid-body physics simulation for every character.

Use a hierarchy:

```text id="12wfrf"
Procedural
 ↓
Spring-Damper
 ↓
Verlet
 ↓
Constraint Solver
 ↓
Full Physics
```

Choose the cheapest method appropriate to the component.

---

# 16. Spring-Based Secondary Motion

Implement reusable spring dynamics.

Potential parameters:

```text id="82z4zq"
stiffness
damping
mass
gravity
drag
limit
```

Use stable integration.

Test under:

* fast turns
* sudden stops
* teleport/reset
* frame-rate variation
* large impulses

---

# 17. Physics Interaction

Investigate character/world interactions such as:

```text id="6x6n3b"
push
impact
stumble
fall
recover
knockback
slip
```

Do not immediately build full ragdolls.

Build a controlled physical reaction system first.

---

# 18. Ragdoll / Partial Ragdoll

Investigate partial physical simulation.

Possible states:

```text id="m58nyv"
fully animated
 ↓
physical reaction
 ↓
partial ragdoll
 ↓
full ragdoll
 ↓
recovery
```

The key challenge is recovery.

A physically simulated character must be able to return to animation cleanly.

Research:

* animation/physics blending
* active ragdoll
* pose matching
* get-up animation selection
* physical animation

---

# 19. Recovery Motion

Build recovery as a first-class behavior.

Examples:

```text id="npx2kc"
stumble
 ↓
catch balance

or

fall
 ↓
grounded
 ↓
choose recovery
 ↓
stand
 ↓
resume behavior
```

Motion matching should be able to provide appropriate recovery motion where data exists.

---

# 20. F4 — Generative Offline Motion

This is where advanced generative AI becomes genuinely useful.

Do not make generative motion a runtime requirement.

Instead:

```text id="l3r5fs"
constraints
 ↓
offline generator
 ↓
candidate motions
 ↓
retarget
 ↓
contact cleanup
 ↓
quality analysis
 ↓
MotionPack
```

The resulting motion becomes ordinary AV Gen content.

---

# 21. Generative Motion Use Cases

Prioritize:

### Transition generation

Fill gaps between:

```text id="a4b3zv"
walk
run
turn
stop
```

---

### Motion variation

Generate:

* slower
* faster
* heavier
* lighter
* cautious
* energetic

---

### Rare behaviors

Generate:

* unusual reactions
* environmental responses
* creature-specific movements

---

### Interaction motion

Generate:

* reach
* inspect
* touch
* lean
* grab

---

# 22. Diffusion Research

Research modern motion diffusion systems.

Do not assume diffusion is automatically superior.

Evaluate:

```text id="zptn1x"
control
quality
latency
generation time
training requirements
editing
determinism
```

The likely first production use is:

```text id="3ky4ns"
offline generation
```

rather than:

```text id="6t1f7r"
runtime generation
```

---

# 23. Generative Motion Must Pass AV Gen Validation

Every generated motion must pass:

```text id="5o8fqm"
skeleton validation
bone-length validation
contact validation
trajectory validation
joint limits
velocity limits
foot sliding
penetration
continuity
```

Bad generated motion should be automatically rejected.

---

# 24. Human-in-the-Loop Generation

Do not build:

```text id="9d6fqa"
Generate → automatically ship
```

Instead:

```text id="m7umfi"
Generate
 ↓
Analyze
 ↓
Rank
 ↓
Preview
 ↓
Artist chooses
 ↓
Bake into MotionPack
```

This is a production tool.

Not a magic button.

---

# 25. F5 — Crowd Simulation

Phase D supports autonomous individuals.

Phase F should investigate groups.

Examples:

```text id="c7a5tz"
10
50
100
500
1000
```

characters.

The architecture must avoid every character independently running expensive:

```text id="0hx8xv"
perception
pathfinding
motion matching
```

at full rate.

---

# 26. Crowd Layers

Use:

```text id="lx8u1k"
Group Goal
 ↓
Flow / Navigation
 ↓
Local Avoidance
 ↓
Individual Behavior
 ↓
Motion
```

This permits shared computation.

---

# 27. Group Behaviors

Support:

* flocking
* gathering
* dispersing
* following
* formation
* queueing
* fleeing
* migration
* crowd flow

Research:

* Reynolds steering
* RVO/ORCA
* flow fields
* hierarchical navigation
* crowd simulation

References:

https://www.red3d.com/cwr/steer/

https://gamma-web.iacs.umd.edu/ORCA/

https://github.com/recastnavigation/recastnavigation

---

# 28. Crowd LOD

Implement simulation LOD.

For example:

```text id="b1s9f0"
Hero:
full simulation

Nearby:
reduced simulation

Background:
coarse steering

Far:
group-level simulation
```

This must preserve visual plausibility.

---

# 29. Shared Motion

Characters should share:

* MotionPacks
* models
* motion databases
* navigation data
* behavior definitions

Do not duplicate large resources per character.

---

# 30. F6 — Advanced Character Intelligence

Only now should AV Gen explore richer learned intelligence.

The existing deterministic architecture remains the foundation.

Potential optional providers:

```text id="n9k3pl"
LearnedBehaviorProvider
LearnedGoalProvider
LearnedAttentionProvider
LearnedInteractionPlanner
```

Each must output conventional AV Gen representations.

---

# 31. Learned Goal Generation

A learned system could eventually take:

```text id="7rrt4g"
perception
memory
personality
world state
history
```

and produce:

```text id="7ps5tg"
CharacterIntent
```

Example:

```text id="b8o5c1"
Investigate strange light
```

The rest remains deterministic.

---

# 32. Language as Optional High-Level Input

If eventually desired:

```text id="j2s4z9"
language
 ↓
high-level goal
 ↓
Phase D
```

For example:

```text id="xxe7g5"
"Go investigate the strange light."
```

becomes:

```text id="b0l0fi"
Goal:
Investigate

Target:
strange light
```

The language system must not control:

* joints
* IK
* navigation internals
* renderer
* frame timing

---

# 33. Character Memory

Expand Phase D short-term memory into optional long-term memory.

Possible concepts:

```text id="wqk8s0"
locations
characters
events
objects
interactions
preferences
history
```

But keep it structured.

Do not store arbitrary text blobs as the primary representation.

Use semantic records.

---

# 34. Character Relationships

Support evolving relationships.

Examples:

```text id="f5g2tz"
trust
familiarity
avoidance
interest
group membership
```

These can influence utility.

Do not build a simulated psychology engine.

Keep the model simple and measurable.

---

# 35. Character Needs

Optional high-level needs:

```text id="w0j5gf"
exploration
rest
social
safety
curiosity
territory
```

These can generate goals.

Again:

```text id="b5d2k0"
Needs
 ↓
Goals
 ↓
Behavior
```

not:

```text id="9xqz16"
Needs
 ↓
random animation
```

---

# 36. F7 — Cinematic Character Direction

This is especially important for AV Gen.

The director should be able to specify:

```text id="8ddg4g"
Character:
Alien

Goal:
Investigate mushroom

Behavior:
Autonomous

Camera:
Follow hero

Duration:
8 seconds
```

rather than manually sequencing:

```text id="zqj3vy"
walk
turn
stop
look
idle
```

---

# 37. Director-Level Character Goals

Support timeline events such as:

```text id="j5y8ty"
CharacterGoal
CharacterBehavior
CharacterReaction
CharacterInteraction
CharacterFormation
```

These become high-level constraints.

The autonomous system executes them.

---

# 38. Cinematic Constraints

Allow director-level constraints such as:

```text id="8y1b2x"
stay in shot
face camera
enter frame
exit frame
remain near target
maintain composition
avoid blocking hero
```

These should influence behavior/motion.

Do not hard-teleport characters to satisfy framing.

---

# 39. Shot-Aware Behavior

Characters may know:

```text id="i3a2gc"
active shot
hero status
camera distance
composition importance
```

but cinematic awareness must be optional.

A character should remain physically coherent even if the camera changes.

---

# 40. Cinematic Auto-Staging

Eventually investigate:

```text id="k3tx89"
characters
+
world
+
camera
+
goal
 ↓
candidate staging
```

Potentially determine:

* where character should stand
* where to look
* where to move
* when to enter
* when to exit

This should be a director tool, not an invisible runtime behavior.

---

# 41. Character-Camera Interaction

Build explicit camera relationships:

```text id="7k0x7m"
look-at character
follow character
orbit character
lead character
anticipate movement
```

This fits AV Gen's existing camera/director architecture.

---

# 42. F8 — Character Quality Analyzer

Build an offline character-quality analyzer.

This should be one of the most useful Phase F tools.

Analyze:

```text id="1f7a8z"
foot sliding
foot penetration
hand penetration
joint-limit violations
bone stretching
velocity discontinuity
root popping
contact errors
trajectory errors
motion matching transitions
neural failures
behavior stalls
navigation failures
```

---

# 43. Behavioral Quality

Analyze:

```text id="e5f9g7"
stuck duration
replanning frequency
behavior oscillation
target churn
unreachable goals
repeated destinations
idle percentage
interaction failure
```

This can expose subtle autonomous-system problems.

---

# 44. Motion Quality Score

Do not create a single arbitrary "AI quality score."

Instead expose individual metrics:

```text id="9e8o2j"
Foot Slide:
...

Contact Error:
...

Trajectory Error:
...

Behavior Oscillation:
...

Navigation Failure:
...
```

Let artists/developers understand the actual problem.

---

# 45. Visual Regression

Generate deterministic evaluation renders.

Compare:

```text id="f9d7qn"
baseline
vs
new build
```

for:

* motion
* contacts
* interactions
* behavior
* camera staging

This should integrate with AV Gen's broader offline quality-analysis ambitions.

---

# 46. F9 — Production Tooling

Build production-oriented tooling around the character system.

Potential tools:

```text id="z8c1xv"
Character Validator
Motion Validator
Behavior Simulator
Navigation Visualizer
Interaction Preview
Motion Database Inspector
Neural Model Inspector
Character Performance Analyzer
Crowd Simulator
```

Do not make each tool a separate disconnected application.

Reuse the existing AV Gen tool architecture.

---

# 47. Character Sandbox

Create a dedicated test scene / lab.

It should allow:

```text id="l1kq72"
character
terrain
targets
obstacles
objects
ramps
stairs
moving objects
world events
```

and controls for:

```text id="0as8tw"
behavior
intent
motion provider
navigation
personality
physics
```

This becomes the primary development environment for character systems.

---

# 48. Automated Scenario Testing

Create scenarios such as:

```text id="5d4m3x"
Scenario:
Investigate object

Scenario:
Avoid obstacle

Scenario:
Follow character

Scenario:
React to event

Scenario:
Interact with object

Scenario:
Recover from stumble

Scenario:
Navigate uneven terrain

Scenario:
Crowd crossing

Scenario:
UFO encounter
```

Each should produce deterministic traces.

---

# 49. Character Replay

Add replay support.

Record:

```text id="g0td2p"
seed
behavior decisions
intent
navigation
motion provider
major events
```

Then replay.

This is invaluable for debugging autonomous behavior.

---

# 50. Simulation Recording

Support:

```text id="2om0ut"
Record
 ↓
Simulation Trace
 ↓
Replay
```

Potentially:

```text id="7f1b7g"
frame
character
behavior
intent
target
motion
```

Do not record every pose by default.

Use event/state checkpoints.

---

# 51. Simulation Checkpoints

For long autonomous sequences:

```text id="7mq4ph"
checkpoint
```

every N frames/seconds.

This helps:

* scrubbing
* debugging
* replay
* offline rendering
* failure reproduction

---

# 52. Deterministic Offline Rendering

Phase F should make autonomous character rendering production-grade.

Requirements:

```text id="8qlv78"
same scene
same seed
same simulation configuration
same output
```

as far as numerical/backend constraints permit.

---

# 53. Frame-Accurate Rendering

Support:

```text id="1m2v8y"
render frame N
```

without requiring the artist to play from frame 0 interactively.

This may require:

* checkpoints
* deterministic replay
* simulation state serialization

Research and implement the appropriate solution.

---

# 54. Multi-Camera

Characters should simulate independently of active camera.

One simulation should drive:

```text id="w7y17c"
Camera A
Camera B
Camera C
```

This is particularly important for AV Gen's multicam director architecture.

---

# 55. Character State Export

Allow offline tools to export:

```text id="4v6l5d"
behavior trace
motion trace
interaction events
navigation path
```

This enables external analysis.

---

# 56. Performance Architecture

By Phase F, the character system must support substantial populations.

Profile:

```text id="a3d9wu"
1
10
50
100
500
1000
5000
```

where practical.

Separate costs:

```text id="t3j0vw"
perception
behavior
navigation
motion matching
neural inference
IK
secondary physics
render preparation
```

---

# 57. Hierarchical Update Frequencies

Use appropriate update rates.

For example:

```text id="qg2d4x"
Physics:
60 Hz

Motion:
30–60 Hz

Perception:
5–15 Hz

Behavior:
2–10 Hz

Long-term planning:
0.5–2 Hz
```

These are starting hypotheses.

Measure.

---

# 58. Multithreading

Identify safe parallelism.

Potential:

```text id="a2s4bz"
character perception
behavior evaluation
pathfinding
motion feature evaluation
neural inference
secondary physics
```

Use job systems where AV Gen already has them.

Do not create a second threading architecture.

---

# 59. Batched Neural Inference

If Phase E neural models are used:

```text id="5j0cxy"
Character contexts
 ↓
batch
 ↓
one inference
 ↓
individual outputs
```

Benchmark.

---

# 60. Shared Resources

Ensure:

```text id="2j5x9v"
MotionPack
MotionDatabase
NeuralModel
NavigationData
```

are shared.

Do not create per-character copies.

---

# 61. Memory Budget

Profile:

```text id="j2tq8x"
per-character state
behavior memory
perception memory
navigation state
motion state
physics state
neural state
```

Provide estimates at:

```text id="b3zq15"
100 characters
500 characters
1000 characters
```

---

# 62. Failure Containment

Any subsystem must be able to fail without destroying the character.

For example:

```text id="m5v9a4"
Neural failure
→ Motion Matching

Motion Matching failure
→ Clip

Navigation failure
→ behavior failure

Physics failure
→ animation

Interaction failure
→ fallback behavior
```

No subsystem should become a single point of catastrophic failure.

---

# 63. Asset / Project Compatibility

Character systems must remain compatible with:

* existing `.json`
* `.scene.json`
* project loading
* offline rendering
* old projects

Provide migrations where necessary.

Do not silently alter existing character behavior when loading old projects.

---

# 64. Versioning

Version:

```text id="n3k8s2"
CharacterDefinition
BehaviorDefinition
NavigationData
MotionPack
MotionDatabase
NeuralModel
InteractionDefinition
```

independently where practical.

---

# 65. Artist Override

Every autonomous subsystem needs an override mechanism.

Examples:

```text id="r1f4yn"
force goal
force target
disable behavior
force motion provider
lock attention
override navigation
override pose layer
```

This is essential for production.

Autonomy must never trap the artist.

---

# 66. Authoritative vs Autonomous State

Distinguish:

```text id="d9b2i6"
AUTHORED
```

from:

```text id="w1q3at"
AUTONOMOUS
```

and:

```text id="q9m7sl"
GENERATED
```

This should be visible in diagnostics.

---

# 67. Hybrid Control

Support:

```text id="f7z1wq"
Director
 ↓
high-level constraint
 ↓
autonomous character
 ↓
physical execution
```

Examples:

```text id="q7k1x5"
"Investigate mushroom."

"Stay near the river."

"Look toward UFO."

"Enter frame during the drop."

"Leave after 5 seconds."
```

The character decides how.

---

# 68. Procedural World Integration

This is particularly important for AV Gen.

Characters should be able to operate in worlds they have never seen before.

Procedural generators should expose:

```text id="y4s5kg"
semantic tags
navigation hints
interaction affordances
landmarks
regions
```

Then characters can automatically adapt.

This is how AV Gen moves toward:

> worlds that populate themselves with believable life.

---

# 69. Character Spawn System

Eventually support:

```text id="x3x8fk"
Spawn Region
 ↓
Character Definition
 ↓
Personality variation
 ↓
Seed
 ↓
Navigation initialization
 ↓
Autonomous behavior
```

This makes procedural population possible.

---

# 70. Population Director

Build an optional population system.

For example:

```text id="f0k8v2"
Forest:
20 creatures

River:
8 creatures

Open field:
15 creatures

Landmark:
3 observers
```

The system distributes characters according to:

* region
* density
* species
* behavior
* time
* world state

---

# 71. Ecological / World Simulation

Do not build a full biological simulation.

Instead create lightweight semantic relationships.

For example:

```text id="9f4t6m"
creature
likes forest

creature
avoids UFO

creature
interested in mushroom

creature
follows group
```

These become behavior inputs.

---

# 72. Time-of-Day Behavior

AV Gen already supports world cycles.

Expose semantic signals:

```text id="2d6q3w"
day
night
sunrise
sunset
weather
world-energy
```

Characters may alter behavior.

For example:

```text id="0q7v2e"
night
 ↓
different activity distribution
```

Do not hard-code biological claims.

Make it authorable.

---

# 73. World Event Ecosystem

By Phase F, world effects and characters should communicate through a common semantic event architecture.

For example:

```text id="y8x5cf"
UFO appears
 ↓
World Event
 ↓
Perception
 ↓
Attention
 ↓
Behavior
 ↓
Motion
 ↓
Camera Director
```

This is one of the most important AV Gen-specific capabilities.

---

# 74. Audio → Character → Camera

The complete audiovisual loop should eventually support:

```text id="3y7q8v"
AUDIO
 ↓
ANALYSIS
 ↓
WORLD EVENT
 ↓
CHARACTER BEHAVIOR
 ↓
MOTION
 ↓
CAMERA
 ↓
RENDER
```

This allows the music itself to influence the emergent visual choreography.

---

# 75. Audio-Reactive Crowd Behavior

Experiment with:

```text id="p4x0sw"
energy rise
 ↓
group becomes active

drop
 ↓
group movement changes

quiet section
 ↓
group slows

beat
 ↓
small synchronized reaction
```

Avoid direct animation triggering.

Use semantic signals.

---

# 76. Synchronization

Investigate controlled synchronization between autonomous characters.

Support:

```text id="4t3k9q"
individual timing
group timing
event synchronization
beat synchronization
```

The system should allow:

```text id="h5f2y1"
characters remain autonomous
+
director imposes synchronized event
```

rather than forcing every character into identical animation.

---

# 77. Emergent Choreography

This should become a major AV Gen capability.

Example:

```text id="5q8x2c"
music changes
 ↓
world effect changes
 ↓
characters notice
 ↓
some investigate
 ↓
others follow
 ↓
crowd forms
 ↓
camera finds interesting interaction
```

No single animation sequence explicitly authored this.

The scene produces choreography through interacting systems.

---

# 78. Cinematic Director Integration

The director can observe:

```text id="b3m9w4"
character events
behavior changes
interesting interactions
group formations
```

and use them as shot candidates.

For example:

```text id="m4t2y6"
character began unusual interaction
        ↓
director marks event
        ↓
candidate camera shot
```

Do not automatically replace authored cinematography.

Provide candidate generation.

---

# 79. Auto-Director Compatibility

The existing AV Gen Auto Director should eventually understand semantic events such as:

```text id="3d7z9k"
CharacterEntered
CharacterExited
InteractionStarted
InteractionCompleted
RareBehavior
GroupFormed
WorldEvent
CharacterReaction
```

These are much more useful than raw animation clip events.

---

# 80. Advanced Offline Quality Analyzer

Integrate character analysis with AV Gen's broader render-quality analysis.

Generate reports such as:

```text id="7j4m2q"
Character:
Alien #4

Motion:
OK

Foot sliding:
0.7 cm

Contact error:
1.2 cm

Navigation:
OK

Behavior:
3 transitions

Stuck time:
0.0 sec

Interaction:
Success

Neural:
Confidence stable
```

This should be machine-readable.

---

# 81. Golden Scenes

Create permanent regression scenes.

At minimum:

```text id="f4y7j0"
Character Lab
Glowmere Autonomous
UFO Encounter
Uneven Terrain
Crowd
Interaction Lab
Neural Motion Lab
Physics Recovery
```

Every major engine change can render/execute these.

---

# 82. Regression Policy

A change should not be considered safe merely because:

```text id="l7d0r1"
unit tests pass
```

Character systems require:

* unit tests
* simulation tests
* behavior traces
* visual regression
* performance benchmarks

---

# 83. Research Deliverables

Produce:

```text id="v4f0cq"
docs/design/advanced-character-simulation.md
docs/design/procedural-interaction.md
docs/design/secondary-motion.md
docs/design/generative-motion.md
docs/design/crowd-simulation.md
docs/design/cinematic-character-direction.md
```

and appropriate ADRs.

Document:

* alternatives
* rejected architectures
* performance
* licensing
* determinism
* threading
* M2 Max constraints
* runtime vs offline
* limitations

---

# 84. Research Sources

At minimum investigate:

### AI4Animation

https://github.com/sebastianstarke/AI4Animation

### Motion Matching

https://github.com/orangeduck/Motion-Matching

### OpenMotion

https://github.com/SaxonRah/OpenMotion

### SAMP

https://github.com/mohamedhassanmus/SAMP

### 100STYLE

https://github.com/ADC-Motion/100STYLE

### Recast Navigation

https://github.com/recastnavigation/recastnavigation

### Steering

https://www.red3d.com/cwr/steer/

### ORCA

https://gamma-web.iacs.umd.edu/ORCA/

### Core ML

https://developer.apple.com/machine-learning/core-ml/

### ONNX

https://onnx.ai/

### ONNX Runtime

https://onnxruntime.ai/

Also research current academic work on:

* neural motion synthesis
* motion diffusion
* scene-aware motion
* active ragdolls
* learned interaction
* crowd simulation
* procedural animation
* neural trajectory prediction

Use primary papers/repositories where possible.

---

# 85. Licensing

Continue the strict licensing policy established in previous phases.

For every:

* dataset
* model
* code library
* pretrained weight
* generated asset

record:

```text id="gr4m0h"
source
license
attribution
redistribution rights
commercial-use rights
derived-data restrictions
```

Do not assume model licenses cover training data.

Do not assume dataset licenses cover generated derivatives.

When uncertain:

```text id="b7j3f1"
DO NOT SHIP
```

until verified.

---

# 86. M2 Max Constraints

Do not build a system that assumes:

```text id="f4t8yw"
multiple high-end NVIDIA GPUs
```

for normal development.

Offline heavy AI may be optional.

Runtime must remain practical.

Potentially acceptable:

```text id="n5w7z2"
overnight offline generation
```

Potentially unacceptable:

```text id="s3d9k4"
30-second runtime inference per character
```

Measure everything.

---

# 87. Runtime / Offline Split

The final architecture should clearly separate:

```text id="g2p6v9"
RUNTIME
```

from:

```text id="h8s4q1"
OFFLINE
```

Runtime:

* behavior
* navigation
* motion
* neural inference
* IK
* secondary motion

Offline:

* training
* generative motion
* database generation
* motion synthesis
* quality analysis
* population baking
* expensive simulation

---

# 88. Generated Content Becomes Ordinary Content

One of the most important principles:

If an AI system generates motion offline:

```text id="x9s5w2"
AI
 ↓
motion
 ↓
cleanup
 ↓
MotionPack
```

the final MotionPack should not require the AI system.

This gives AV Gen:

* reproducibility
* portability
* predictable rendering
* simpler deployment
* lower runtime cost

---

# 89. Human Author Remains the Director

Autonomy should be layered under artistic control.

The artist can:

```text id="q8m2j1"
define world
define characters
define goals
define constraints
define personality
define population
define events
define camera
define timing
```

The engine handles:

```text id="a4v8r3"
navigation
motion
IK
adaptation
interaction
secondary motion
```

Optional AI handles:

```text id="h7c1n6"
motion generation
motion refinement
learned prediction
optional high-level suggestions
```

---

# 90. First Phase F Vertical Slice

Do not start with crowds or diffusion.

The first vertical slice should be:

```text id="k3p7m0"
AUTONOMOUS CHARACTER
        ↓
SEES OBJECT
        ↓
DECIDES TO INVESTIGATE
        ↓
NAVIGATES
        ↓
APPROACHES
        ↓
LOOKS
        ↓
REACHES
        ↓
TOUCHES
        ↓
OBJECT REACTS
        ↓
CHARACTER REACTS
        ↓
CHARACTER RECOVERS
        ↓
LEAVES
```

This demonstrates:

```text id="s2v8n4"
Phase D behavior
+
Phase C motion
+
Phase E neural option
+
Phase F interaction
+
Phase A/B IK/contact
```

---

# 91. Second Vertical Slice — Physical Reaction

Build:

```text id="j5q9v3"
character walking
 ↓
object moves into path
 ↓
character reacts
 ↓
body loses balance
 ↓
foot correction
 ↓
stumble
 ↓
recovery
 ↓
continue original goal
```

This demonstrates the full animation/physics stack.

---

# 92. Third Vertical Slice — Generative Motion

Take a known motion gap:

```text id="v7k2d9"
Run
 ↓
???
 ↓
Stop
```

Generate candidates offline.

Then:

```text id="u4p8n1"
generate
 ↓
validate
 ↓
rank
 ↓
artist chooses
 ↓
MotionPack
```

The runtime should then use it like ordinary motion data.

---

# 93. Fourth Vertical Slice — Crowd

Create:

```text id="q6r1s5"
100 autonomous characters
```

with:

* different personality
* different targets
* shared navigation
* local avoidance
* shared MotionPack

Demonstrate:

```text id="x4n7k8"
group gathers
 ↓
some characters investigate
 ↓
others continue walking
 ↓
group disperses
```

---

# 94. Fifth Vertical Slice — Audiovisual Emergence

This should be the ultimate AV Gen demonstration.

Create a musical sequence.

Then:

```text id="m7p3c2"
AUDIO
 ↓
section change
 ↓
world effect
 ↓
character attention
 ↓
behavior
 ↓
motion
 ↓
group interaction
 ↓
camera director
 ↓
cinematic result
```

The user should be able to change the music and see the world react.

---

# 95. Phase F Performance Matrix

Create a benchmark matrix:

```text id="0y4r7s"
Characters:
1
10
50
100
500
1000
5000

Motion:
Clip
Motion Matching
Neural

Behavior:
Simple
Complex

Physics:
Off
Secondary
Full/Partial

Crowd:
Off
On
```

Measure:

```text id="q2s6d8"
CPU
GPU
memory
allocations
simulation time
motion time
IK time
physics time
neural inference
navigation
behavior
```

---

# 96. Performance Budgets

Do not establish arbitrary numbers before measurement.

Instead derive budgets from:

```text id="r5t8y1"
interactive playback
offline rendering
```

and define:

```text id="p3k7w2"
hero character budget
background character budget
crowd character budget
```

---

# 97. Debugging

The final debug system should allow inspection of:

```text id="w8f2k6"
World
 ↓
Perception
 ↓
Attention
 ↓
Behavior
 ↓
Intent
 ↓
Navigation
 ↓
Steering
 ↓
Motion Provider
 ↓
Pose
 ↓
IK
 ↓
Physics
```

A developer should be able to inspect any character and understand its entire decision chain.

---

# 98. "Why Did This Character Do That?"

The answer should be inspectable as:

```text id="a7n4k9"
EVENT:
UFO entered perception range

ATTENTION:
UFO score = 0.91

BEHAVIOR:
Observe UFO = 0.84

CURRENT GOAL:
Observe UFO

NAVIGATION:
Reachable

STEERING:
Desired velocity = ...

MOTION:
NeuralMotionProvider

BODY:
Look-at active

CONTACT:
Feet planted

PHYSICS:
Stable
```

This should be a first-class debugging capability.

---

# 99. No Magic

Do not allow subsystems to contain mysterious heuristics such as:

```text id="g1m5r7"
if distance < 3.4
    maybe do thing
```

without:

* documentation
* configuration
* tests
* rationale

The character system should remain understandable.

---

# 100. Final Definition of Done

Phase F is complete when AV Gen has a coherent advanced character platform supporting:

## Animation

* motion matching
* neural motion
* procedural adaptation
* IK
* contacts
* full-body constraints
* secondary motion

## Intelligence

* perception
* attention
* behavior
* goals
* memory
* personality
* optional learned providers

## World

* navigation
* interaction affordances
* world events
* semantic environments
* procedural population

## Physics

* balance
* secondary dynamics
* physical reactions
* partial ragdoll where justified
* recovery

## Generation

* offline motion synthesis
* optional generative AI
* automatic validation
* MotionPack baking

## Crowds

* group behavior
* local avoidance
* population systems
* simulation LOD

## Cinematics

* character goals
* director constraints
* staging
* event-aware cinematography
* multicam compatibility

## Production

* deterministic simulation
* replay
* checkpoints
* offline rendering
* quality analyzer
* performance analyzer
* debug visualization
* asset/version validation

---

# 101. Final Architecture

The mature AV Gen character architecture should look approximately like:

```text id="4x8m1q"
                           AUDIO
                             │
                             ↓
                       AUDIO ANALYSIS
                             │
                             ↓
                        SIGNAL BUS
                             │
                             ↓
                     WORLD / EVENTS
                             │
               ┌─────────────┴─────────────┐
               ↓                           ↓
          WORLD STATE                 CHARACTERS
                                           │
                                      PERCEPTION
                                           │
                                      ATTENTION
                                           │
                                   MEMORY / PERSONALITY
                                           │
                                      GOALS / NEEDS
                                           │
                                       BEHAVIOR
                                           │
                                         INTENT
                                           │
                                      NAVIGATION
                                           │
                                       STEERING
                                           │
                                    MOTION REQUEST
                                           │
                        ┌──────────────────┼──────────────────┐
                        ↓                  ↓                  ↓
                      CLIP          MOTION MATCHING       NEURAL
                        │                  │                  │
                        └──────────────────┼──────────────────┘
                                           ↓
                                    POSE GENERATION
                                           ↓
                                    BODY ADAPTATION
                                           ↓
                                  FULL BODY CONSTRAINTS
                                           ↓
                                       IK / CONTACT
                                           ↓
                                   SECONDARY PHYSICS
                                           ↓
                                   FINAL CHARACTER
                                           │
                         ┌─────────────────┴─────────────────┐
                         ↓                                   ↓
                      CAMERA                             RENDER
                         │                                   │
                         └─────────────────┬─────────────────┘
                                           ↓
                                        OUTPUT
```

Offline:

```text id="8h4k1m"
                         RAW MOTION
                              │
                              ↓
                       RETARGET / CLEAN
                              │
                              ↓
                    CONTACT / PHASE / TRAJECTORY
                              │
                              ↓
                         AUGMENTATION
                              │
                    ┌─────────┴──────────┐
                    ↓                    ↓
              CLASSICAL DB          AI TRAINING
                                         │
                                         ↓
                                      MODEL
                                         │
                           ┌─────────────┴────────────┐
                           ↓                          ↓
                    NEURAL MOTION              GENERATIVE AI
                           │                          │
                           └─────────────┬────────────┘
                                         ↓
                                  GENERATED MOTION
                                         ↓
                                  QUALITY ANALYSIS
                                         ↓
                                  ARTIST REVIEW
                                         ↓
                                    MOTIONPACK
```

---

# 102. The Ultimate AV Gen Character Loop

The end-state should make this possible:

```text id="m2n7v5"
MUSIC
  ↓
WORLD CHANGES
  ↓
CHARACTER PERCEIVES
  ↓
CHARACTER DECIDES
  ↓
CHARACTER NAVIGATES
  ↓
CHARACTER MOVES
  ↓
CHARACTER INTERACTS
  ↓
WORLD REACTS
  ↓
OTHER CHARACTERS PERCEIVE
  ↓
BEHAVIOR PROPAGATES
  ↓
GROUP FORMS / CHANGES
  ↓
DIRECTOR OBSERVES
  ↓
CAMERA RESPONDS
  ↓
RENDER
```

This is the point where AV Gen stops being merely a system that **animates characters** and becomes a system that can **generate emergent audiovisual performances**.

---

# 103. Final Principle

The entire six-phase architecture should preserve one rule:

> **The higher-level system describes intent. The lower-level system figures out how to physically realize it.**

Therefore:

```text id="x9q3b7"
Director
    ↓
Goal

Behavior
    ↓
Intent

Navigation
    ↓
Trajectory

Motion Matching / Neural
    ↓
Motion

IK / Contacts
    ↓
Physical Pose

Physics
    ↓
Secondary Response

Renderer
    ↓
Image
```

No single subsystem should attempt to own the entire chain.

That separation is what allows AV Gen to evolve from:

```text id="n4r6w8"
animation engine
```

into:

```text id="p7x2c5"
autonomous audiovisual world engine
```

while remaining controllable, debuggable, deterministic and performant.

---

# 104. Final Phase F Acceptance Test

Create one final production scene.

It should contain:

```text
river
forest
mushrooms
rocks
terrain variation
UFO
world effects
20–100 autonomous characters
```

Give the characters:

* different personalities
* different capabilities
* different interests
* shared motion libraries

Then play a complete musical track.

The scene should be able to produce:

```text
characters wandering
        ↓
objects discovered
        ↓
characters investigate
        ↓
characters interact
        ↓
world effects occur
        ↓
characters react
        ↓
groups form
        ↓
characters separate
        ↓
UFO arrives
        ↓
some characters investigate
        ↓
others ignore it
        ↓
physical reactions occur
        ↓
characters recover
        ↓
music changes
        ↓
world changes
        ↓
behavior changes
        ↓
camera director discovers interesting moments
```

Then render the sequence offline.

The result should be reproducible from:

```text
project
+
assets
+
MotionPacks
+
models
+
seed
+
timeline
```

without requiring:

* manual animation sequencing
* an LLM
* network access
* CUDA
* interactive human intervention
* model training at render time

That is the final target for the character architecture.
