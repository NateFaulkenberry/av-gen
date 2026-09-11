# ADR-088: An entity drives a node; it does not become one

Status: accepted
Date: 2026-09-11

## Context

The brief was a UFO over Glowmere. The thing actually needed is the layer underneath it: a way for
objects and characters to have spatial placement, autonomous motion, interaction with the
environment, audio-reactive modulation, and reusable behaviour profiles — configured in data, so
that the next project can say

```json
{ "signal": "audio.bass", "target": "parts/Lamp/emissiveGain", "depth": 3.0 }
```

about an asset nobody has imported yet, and have it work with no C++ written for it.

Most of that already existed, and the temptation was to miss it. Every registered
`params::Parameter` is already keyframeable, presettable, serialised and a legal modulation target.
A `ModRoute` already carries a signal through gain, offset, curve, clamp, threshold, asymmetric
attack/decay smoothing, an envelope follower and a remap before it reaches a property — which is
the entire "signal → mapping → smoothing → depth → target" chain, built and tested, in
`params/processor.hpp`. Building a second one beside it would have been the fifth time this
codebase grew a parallel system that nothing was wired into.

What was missing was not modulation. It was three narrower things:

1. **Something with a property surface worth pointing modulation at.** A node exposed
   `position`, `rotation`, `scale`, `visible`, `emissiveBoost` and `roughnessScale`, and that was
   all. An imported craft could be moved and dimmed as a whole, and nothing finer.
2. **Addressing.** An imported asset's materials became parts ordered by surface area
   (ADR-044) — `parts/0`, `parts/1`, `parts/2` — an ordering nobody can predict from a scene file
   and which changes the day somebody edits the model.
3. **Autonomy.** Nothing in the engine ticked. `SceneController::update` was the only per-frame
   hook and the Engine held exactly one; there was no actor, no agent, no behaviour, no entity, and
   `scene_types.hpp` says so deliberately: *"plain structs, no ECS"*.

## Decision

### An entity is a driver for a node that already exists

It is not a new `NodeKind`, not a new scene graph, and not an ECS. The scene file places the node;
the `entities` array says how it behaves. One consequence pays for the whole design: an entity can
drive an imported craft, a procedural rock or a skinned character without knowing which it is, and
nothing in the composition had to learn about behaviour to gain it.

```json
"entities": [
  { "name": "visitor", "node": "visitor", "seed": 20260911,
    "behaviors": [ {"kind": "hover", "amplitude": 0.85, "rate": 0.055} ],
    "reactions": [ {"signal": "audio.bass", "target": "parts/Light/emissiveGain", "depth": 2.6,
                    "chain": {"attackMs": 35, "decayMs": 260, "curve": "power", "curveAmount": 1.6}} ],
    "clips": { "idle": "Idle", "walk": "Walk", "run": "Run" } } ]
```

### Behaviours run between the routes and the scene

`SceneController::updateBehaviour` is called from `Engine::update` after `Modulator::applyRoutes`
and before `controller_->update`. That slot is not arbitrary; it is the only one that works.

A behaviour's own knobs — a hover's amplitude, a wander's speed — are ordinary parameters, so the
music has to be able to reach them, which means the routes must have run *before* a behaviour reads
them. A behaviour's output is an offset that a route should be able to add to, which means it must
land on the parameter finals *after* the routes wrote theirs. Running behaviours between the two
satisfies both, and a route and a behaviour then compose on the same property instead of
overwriting each other.

Behaviours write **finals**, never bases. The authored value is what the author placed, so saving a
project writes where the craft was put rather than wherever it happened to be drifting when
somebody hit save.

### Reactions are a spelling, not a second modulation system

A `reaction` compiles to an ordinary `params::ModRoute` with an ordinary `ProcessorChain`. What
this layer adds in front of it is **addressing**: a target is resolved against the entity's own
behaviour namespace, then the driven node's transform namespace, then its geometry namespace, with
`parts/<material name>` rewritten to `parts/<index>` on the way. `@` prefixes an absolute parameter
path, because the only naming scheme with no escape hatch is one that has been widened until it
means nothing.

Material *names* are what make that readable. `scene::Entity` now carries the name its source asset
gave its material, and an `AssetPart` carries the name of the material it was grouped from.
Grouping itself is untouched — still by material *value*, so two materials that shade identically
keep sharing a draw however they were named. The name is a label for addressing, never a key.

### Navigation reuses the world's own queries

There is no navmesh. `WorldMap::sample` already answers height, normal, slope, the water surface
and submersion analytically from one set of noise evaluations, and `ClearanceField` (ADR-080)
already knows the tallest thing that grows at a point and how far inside a hero a point is — the
two facts a camera needed and the same two a walker needs. A baked navmesh would be a second
description of the same ground, and the first time someone moved a hill it would be wrong in a way
nothing checked.

A walker asks for a destination every few seconds, not every frame, so sampling the real ground is
affordable: a rejection-sampled destination is 24 attempts at a handful of noise evaluations each.

### Placement is a search, not a person nudging numbers

`entity::findPlacement` chooses where a hero element goes from the frame and the world:
candidates are drawn in *screen* space and unprojected (a world grid spends most of its samples
behind the camera, and what is being chosen is a composition), then rejected against the scene's
own `Camera` matrices, the `WorldMap`'s slope and water, and the `ClearanceField`'s canopy and hero
capsules. It is seeded, so a placement can be committed to a scene file, and it reports *why*
candidates failed — which is how the right-of-centre sky over Glowmere turned out to be unusable
(77,380 of 120,000 samples on the east ridge) rather than merely unlucky.

### The seam to animation is one struct and two interfaces

`entity::LocomotionState` carries activity, position, yaw, speed, turn rate, a decaying reaction
weight, a look target, and the timeline second the decision was made at. `IPoseSink` consumes one;
`ISkeletonQuery` answers where a joint is, for sockets. Neither header includes anything from the
skinning system and the skinning system need include nothing from here.

A behaviour never names a clip. `Activity` is `Idle | Walk | Run | Turn | Observe | React`, and the
scene file maps those onto the asset's own state names, because a clip name belongs to an asset:
the same `wander` has to drive an alien, a deer and a robot.

## Consequences

**Nothing here is allowed to be silent.** An unknown behaviour kind, an entity driving a node the
scene has no node for, and a reaction that resolves to no parameter are each reported by name, with
the candidates that were tried and the material parts that were available, and they reach
`Engine::projectWarnings` alongside the timeline's unbound targets. The install path also logs what
it *did* bind — "2 entities installed, 15 reactions bound, 0 unresolved" — because "no warnings"
and "nothing happened" look identical in a log, and this project has read the second as the first.

**Two latent bugs surfaced and are fixed here**, both found because the reactivity did not appear in
a render. `params::loadProject` called `Modulator::clearRoutes()` and installed only the document's
routes, so anything a subsystem had installed was destroyed a few hundred lines after it was
installed — entity reactions, and, since ADR-028, every route a procedural graph ever emitted.
`saveProject` had the mirror of it, writing subsystem routes into the file so a project gained a
duplicate of each one every time it was saved. Load now replaces only the authored routes; save
writes only the authored ones.

**Behaviour ordering is a contract, not an implementation detail.** Behaviours run in declaration
order and each sees what the ones before it wrote. Three of the bugs found while building this were
ordering errors rather than typos: `lookAt` turning the body while `wander` walked elsewhere (which
multiplies to a standstill, because locomotion scales its pace by alignment), `interest` treating a
strong audio event as a state change (so a percussive track pinned the character in place), and an
unleashed wander being a random walk, which leaves.

**Cost is budgeted per entity in data.** `fullDetailDistance` drops an entity to `coarseInterval`
updates with the accumulated dt; `cullDistance` stops it entirely and leaves its node where the
scene put it. Entity-driven nodes now also set `Entity::cameraCulled`, which nothing but terrain
was setting, so the rig pass can skip posing a character nobody can see.

## What this does not do

There is no path planning — steering is a fan of local deviations, because this world is open
ground with scattered obstacles and A* over it would be a great deal of machinery to walk around a
tree. There is no inter-entity avoidance. Sockets resolve against the entity's own frame until an
`ISkeletonQuery` is installed. And a part is still one *material*: an asset whose lamp and whose
dome ring share a material is one addressable part, and separating them means editing the asset or
changing how parts are grouped, which would move every existing part index.
