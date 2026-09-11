---
id: sequencer/shots
title: Shots and Scene Cuts
category: Sequencer
summary: A shot is a span of time with a camera and a scene. What the camera can do, and how a cut works.
order: 41
tags: shot, camera, cut, transition, fade, scene slot, preset
keywords: how do i add a shot; cut between scenes; fade in; camera preset; how do i keyframe the camera; look at
related: sequencer/overview, sequencer/camera-direction, sequencer/timeline-automation
features: panel.sequence
---

# Shots and Scene Cuts

A **shot** is a span of time carrying a name, a start, a duration, a scene slot, a camera and two
transitions. Shots may not overlap.

## Scene slots

A **slot** is a node in the scene a shot can cut to. Switching between slots is a change of
visibility, so every slot stays loaded and a cut costs nothing — there is no streaming and no
preloading.

The Scenes section at the top of the inspector lists the slots, each with an `id`, the `node` it
names, and either `ok` or a red **no such node**. **Add Slot** creates another.

A shot whose scene is `(inherit)` stays on whatever the shot before it was showing.

## The camera

The `kind` combo has three settings.

**`inherit`** leaves the camera alone.

**`move`** uses a shot from the cinematic vocabulary. Seven preset buttons fill it in —
`isometric`, `follow`, `wide`, `close`, `topDown`, `tracking`, `reveal` — and you then adjust:

| Control | Meaning |
|---|---|
| `subject` | the point the move is composed around |
| `radius` | the subject's size; distances are in multiples of it |
| `focal mm` | the lens, 8 to 400 |
| `from`, `to` | start and end distance, in radii |
| `samples` | how many keys the bake writes across the shot, 2 to 256 |

Pressing a preset keeps the `look at` actor; everything else is replaced.

**`keys`** is explicit keyframing. Each key has a time relative to the shot start, a position and
an aim. **Key from viewport** adds one at the play-head, taking the camera exactly where the
viewport has it — which is the practical way to author a move: fly the camera, key it, fly, key.

With any kind but `inherit` you can set a **look at** actor, an eye height and a weight, and the
camera will aim at where that actor *is* at each moment rather than where it started.

## Transitions

`in` and `out` are each `cut`, `fade in` or `fade out`, with a duration when they are not a cut.
A fade is baked as keys on `scene/brightness`.

A cut is a millisecond wide: the last camera key of a shot that is cut away from lands 1 ms before
the boundary, so the next shot's first key is not merged with it.

## What a shot bakes into

| Target | When |
|---|---|
| `camera/mode` | a single step key at time 0, whenever any shot has a camera |
| `camera/position`, `camera/target` | per sample |
| `camera/lens/focalLength` | one key per shot — a focal length that slides through a shot is a zoom, and a zoom is something you ask for |
| `nodes/<node>/visible` | a step key per scene slot at every cut |
| `scene/brightness` | for fades |

The `camera/mode` key matters. In orbit mode the camera ignores position and target entirely, so a
sequence that did not switch it to free mode would bake perfect keys and move nothing.

Camera keys are baked with eased *values* and joined with `linear` interpolation, because handing
eased values to a smooth interpolator eases them twice.
