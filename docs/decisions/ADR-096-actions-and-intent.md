# ADR-096: Actions, schedules and interactions — the intent layer

**Status:** accepted
**Date:** 2026-09-11
**Context:** the *All You Got* cinematic world brief, §4, §7, §9, §10, §37/§38 — Group B of
`docs/cinematic-world-gap-analysis.md`
**Constrained by:** ADR-091 (two-tier simulation authority), ADR-088 (entities), ADR-086 (skeletal
animation)

## The problem

ADR-088 gave a thing a **behaviour** — something it does because of what it is. A `wander` walks
because wandering is what that entity does. Nothing in the engine could give a thing an
**intention** — something it does because somebody said so — and the brief's §4 is a whole layer
built on that distinction:

```
Character -> Action -> Target -> Animation -> Completion -> Next Action
```

The gap analysis found this to be one of the three genuinely missing clusters, and the largest of
the three that could be built without waiting on anything else.

Three things had to be true of whatever was built, and each of them rules out an obvious design.

1. **It must not know what a protagonist is.** The brief's worked example is `Wake -> WalkTo(
   Nightstand) -> PickUp(Headphones) -> Equip(Headphones) -> BeginRoutine`, and the obvious design
   is an `ActionKind` enum with `PickUp` in it. That enum grows once per prop and belongs to
   whichever scene was being built the week it grew.
2. **It must not name a clip.** ADR-088 put `EntityDesc::clips` between a behaviour and an asset
   precisely so the same `wander` drives an alien, a deer and a robot. An action system that took a
   clip name would undo that in its first field.
3. **Falling back must resume, not reset.** ADR-091 is explicit, and so is the addendum's §14:
   `Walking → Dance → Walking`, not `Walking → Dance → Idle`.

## Decision

### Eight primitives; the verbs are data

`ActionKind` has eight members — `wait`, `move`, `face`, `pose`, `interact`, `equip`, `unequip`,
`set` — and not one of them is a verb from the brief's list. The verbs (sit, open, pick up, use,
enter, sleep) are **`InteractionDesc`s authored on the prop**:

```json
{ "name": "bench", "interactions": [
    { "name": "sit", "socket": "seat", "activity": "sit", "duration": 2.0, "range": 1.2 } ] }
```

An actor says `{"kind": "interact", "target": "bench.sit"}`. Adding a verb is a line of scene JSON.
The chair knows how to be sat on; the character knows nothing about chairs; and the abstraction is
symmetric, so a deer and a robot use the same bench with no code anywhere knowing which is which.

This is the decision the rest of the design hangs off, and the one most worth defending: the
alternative — a verb enum — is easier for exactly one scene and wrong for every scene after it.

### An action names an activity, never a clip

`ActionDesc::activity` and `InteractionDesc::activity` hold an **activity name** — "sit", "sleep",
"pickUp" — which `EntityDesc::clips` maps onto whatever the asset shipped. `Entity::clipFor` gained
a string overload with the same `idle` fallback the enum one has, so an actor whose asset has no
sit clip stands rather than freezing in its bind pose.

This is why a *prop* may name an animation state at all without breaking rule 2: it is naming the
**actor's own vocabulary**, not the actor's asset. `LocomotionState` carries the activity name
alongside the gait's `Activity`, and the composition's `AnimationSink` prefers it — so the seam in
`locomotion.hpp` still carries everything the animation layer needs and still names no clip.

### The queue is a stack of authority tiers, not a list

ADR-091's hierarchy is read top-down, so the queue is three lists, one per tier
(`Routine`, `Action`, `Director`), and the highest live tier drives. That single structural choice
is what makes every resumption requirement fall out rather than be implemented:

- A director override goes on the `Director` tier. The routine is not consumed, not cancelled and
  not rewound; it is simply not on top. When the override drains, it is on top again, in the same
  action, **with the seconds it had already spent** — because only the driving tier's `elapsed`
  advances.
- Pausing a routine *holds* its tier rather than cancelling it. A held tier neither ticks nor
  counts as driving, so the entity falls through to its own behaviours — which is what "pause the
  routine" should look like — and unholding restores the exact action and its elapsed seconds.
- `resumable: false` on an action opts out, replaying it from the start instead. Resumption is a
  choice the data makes, which is the only way to know the engine is making it deliberately.

The tier below the action layer is a **behaviour**, and the same rule reaches it: `EntityState`
gained a `driven` flag, and the two behaviours that own travel and facing (`wander`, `lookAt`)
yield by *keeping everything* — the destination, the pause timer, the target. A wanderer preempted
mid-walk carries on to the same place. `interest` deliberately does not fully yield: a directed
character may still glance at a window and flinch at a drum hit; it may not stop walking.

### Navigation is a three-call interface

`IPathProvider` — `route`, `steer`, `groundHeight` — with a `PathStatus` of `Ready | Pending |
Unreachable`. `Pending` exists so an asynchronous planner can say "not yet" without the character
either freezing forever or setting off blind.

`NavigatorPath` implements it against what exists today: `entity::Navigator` samples walkability
analytically and steers locally, but has **no graph and no search**. So its route is the straight
line, and the only destination it refuses is one a walker could not stand on. A character gets
around a tree; it does not get around a lake. That limitation is written into the header rather
than discovered in a scene. §6's planner implements the same interface and nothing in the action
layer changes.

A `move` that makes no progress for four seconds fails with `"stuck"` rather than walking on the
spot for the rest of the render.

### State is an ordinary parameter

`EntityDesc::properties` declares named numbers, registered as `params::Parameter<float>` under
`entity/<name>/state/<property>`. `equip` sets one. That makes "the headphones are on" a legal
reaction target, modulation target, keyframe target and save-file value **with no new plumbing** —
`{"signal": "...", "target": "state/headphones"}` resolves through the addressing ADR-088 already
built. Writing the *base* rather than the final is what makes it survive the modulation pass and a
save.

Setting a property the entity did not declare is refused rather than invented: a property invented
at runtime is a property nothing could have bound to, which is this project's recurring failure in
miniature.

### The gait is a separate, tiny state machine

`Activity` and gait-selection-by-speed already existed; what was missing was that selecting on one
threshold flickers. `Gait` selects with **two kinds of hysteresis**: separate enter and exit
thresholds (a band in speed) and a minimum dwell (a band in time, which the speed band cannot
provide — an accelerating body crosses a 0.8 m/s band in a fifth of a second). `GaitSettings` is
per entity and authored in the scene file, because a deer, a robot and a person break into a run at
different speeds. `Gait::approach` is the acceleration model, integrated against the real dt.

A proposal the gait has no opinion about — `Observe`, `React` — passes straight through and leaves
the remembered gait alone, which is `Walking → React → Walking` at the gait's own scale.

### An entity under orders is not culled

Behaviour LOD's far band ("do not update at all") is refused for an entity with a pending action or
a running routine. A background pedestrian losing its wander off camera costs nothing; a character
that stopped walking to the nightstand because the camera looked away is a bug nobody can
reproduce. The *coarse* band still applies, with accumulated dt, so the cost stays bounded — 500
entities under orders cost 0.037 ms/frame, about 74 ns each.

## Consequences

**Good.** Nothing scene-specific entered the engine; the brief's worked example is entirely scene
JSON. `SocketDesc`, `AttachmentDesc`, `EntityDesc::clips`, `EntityDesc::profile`, behaviour LOD,
`AnimationPlayer`'s cross-fade and the parameter/reaction addressing were all reused rather than
rebuilt — the equip action is roughly thirty lines because §37/§38 already existed. Actions are
deterministic from (start time, seed): no wall clock, no frame index, dt-integrated throughout, so
the sequence replays identically at 24 Hz and 120 Hz.

**Bad.** There are now two things that can move a character — a behaviour and an action — and an
author has to know which is driving to predict a frame. The `driven` flag makes that explicit in
code but it is invisible in the editor until §45's overlays land. And an action tier held forever
(a `pose` with no duration, which is the honest encoding of "sleep until told otherwise") is
indistinguishable from a stuck one without looking at the queue.

**Watch for.** The temptation to add a ninth primitive. Every verb the next scene needs should be
an `InteractionDesc`; if it genuinely cannot be, that is a real signal, and it should be argued for
rather than added. The second temptation is an action that walks to its own target — `interact`
deliberately *fails* when out of range rather than moving, because an interaction that walked would
make "why did my character walk off" unanswerable.

## What this does not do

- **Pathfinding.** The seam is defined and implemented against straight-line steering. Group A owns
  the search.
- **Retargeting.** Unchanged from ADR-088: the `clips` indirection is the architecture; actual
  cross-skeleton retargeting is still a multi-week feature nobody has started.
- **Playback-rate matching is off by default.** `Gait::playbackRate` is computed and published, and
  `GaitSettings::matchRate` turns it on, but it ships off: it wants a look over real assets before
  it changes how every existing walk cycle plays.
