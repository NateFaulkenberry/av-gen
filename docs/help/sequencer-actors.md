---
id: sequencer/actors
title: Actors and Animation Cues
category: Sequencer
summary: Driving a node's transform from the sequence, and saying which animation clip it is playing.
order: 42
tags: actor, character, animation, clip, cue, path, spline
keywords: how do i animate a character; make something walk; animation clip; move an object over time; actor path
related: sequencer/overview, sequencer/shots, sequencer/timeline-automation
features: panel.sequence
---

# Actors and Animation Cues

An **actor** is a node in the scene that the sequence drives.

**Add Actor** asks which node. A node that carries a rig is marked `(rig)`; a node already taken by
another actor is disabled. Picking one creates an actor with a single position key at the node's
current transform, and — if it has a rig — one animation cue for the rig's first state.

## Position keys

Each key has a time, a position, an optional explicit rotation and an optional scale. Without an
explicit rotation, the actor faces its direction of travel.

**Key here** adds a key at the play-head taking the driven node's current position, which is the
practical way to author a path: scrub, move the node, key.

An actor can also follow a **spline path**, with a start and end time, a sample count and a
`faceTangent` flag. Inside the path's time range the path wins; outside it, the keys do.

Actors bake into `nodes/<node>/position`, `nodes/<node>/rotation`, `nodes/<node>/scale` and
`nodes/<node>/visible`.

## Animation cues

Labelled **Animation cues** in the inspector. A cue says: from this moment, this node is playing
this clip.

| Field | Meaning |
|---|---|
| `t` | when the cue takes effect, and the clip's phase origin |
| `clip` | the animation-state name on the node's rig; empty leaves the rig alone from here |
| `rate` | clip seconds per timeline second |
| blend | how long to cross into it; the rig's own transition time by default |

If the node has a rig, the clip field is a combo of that rig's actual state names. If it does not,
it is a free text field.

> [!NOTE]
> Animation cues are the one thing the sequencer does **not** bake into keyframes. A keyframe track
> can say where something is; it cannot say which clip it is in and since when. Cues are applied
> every frame instead, after any behaviour the entity has of its own — so a sequence that says what
> a character is doing wins over a behaviour that guessed — and before the rigs are posed.
