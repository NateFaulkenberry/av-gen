# ADR-194: A body off the ground

**Status:** Accepted
**Date:** 2026-09-14

## What was missing

Every part of this entity layer assumed a walker is standing on something. `GroundFollower` exists to
guarantee it — `maxSink = 0`, "a body is never below the ground it stands on" — and the gait machine
picks between standing, walking and running from a horizontal speed. There was no way to express
*not standing on anything*.

So a character could not cross a gap it could obviously clear, and `Jumping`, `Jump_running`,
`Fall_loop` and `Landing` were four clips in the alien pack that nothing could ever ask for.

## The shape

`Activity` gains `Jump`, `Fall`, `Land`. They are **passed through** the gait machine rather than
selected by it, exactly as `React` is, and for the same reason: a body in the air is airborne
whatever its horizontal speed, and the gait underneath is remembered — so `Run → Jump → Fall → Land
→ Run` comes back as a run and not as an idle.

`entity::Airborne` is a component, not a behaviour, for the reason `GroundFollower`'s header already
gives for itself: `explore` uses it, and a future vehicle or a keyframed character can use it without
inheriting a walk cycle.

**Code-driven, not root motion — and that was measured rather than assumed.** The net root
translation of `Walking`, `Running` and `Jumping` over a full cycle is *exactly zero*: the clips are
authored in place, so there is no forward motion in them to extract. The arc is the movement and the
clip is what it looks like. `Jumping` does carry 0.87 of vertical root range — the crouch and the
extension — and that rides on top of the arc rather than duplicating it.

A hop is **committed**. While one is in progress it owns the body: steering, crowd separation, the
penetration resolve and the ground follower are all skipped until the feet are down. That is what a
jump is. On landing the `GroundFollower` is re-primed, because it has been ignored for the whole arc
and still holds the height the body left from — its own header warns about exactly that, and without
the reset the character glides down to the surface from take-off height.

## The trigger, and the finding that changed it

The first trigger was opportunistic: probe ahead along the heading, and if the ground just in front
is unwalkable and there is walkable ground within reach beyond it, that is a gap — hop it. It needs
no gap-detection machinery, only two questions the walk already asks.

**In four simulated minutes of Glowmere it fired zero times**, and the reason is worth recording:

> **A path-following walker does not meet gaps, because A\* already routed around them.**

Unwalkable cells are not in the navigation graph at all, so a path never approaches one head-on. The
probe is not wrong — it fires at a ledge the path runs beside — but it cannot be the main way a
character jumps.

Making a walker jump *a gap on its route* means the **graph** has to know the gap is jumpable: narrow
unwalkable strips recorded at grid-build time as traversable edges with a cost, so A\* can route
through them and the walker executes the hop when its leg crosses one. That is the jump-link idea
from the navmesh literature and it is a real piece of work. It is **not done here**, deliberately —
it touches `nav_grid.cpp` while other work is in flight, and it wants its own measurement.

## What is done instead, and is the thing that was actually asked for

**A hop because the music said so.** `jumpSignal` names a bus signal; the hop is edge-triggered on it
rising past a half, with the previous value remembered so a signal that sits high does not launch a
hop every frame — the same shape `spin` uses for an impulse. The body hops forward along the way it
is already going, and only onto ground the navigator says it can stand on.

Deliberately not conditional on there being a gap. This is the author's jump: a character that only
ever jumped when the terrain demanded it would be an obstacle-avoider rather than a performer, and
"a strong beat could trigger a jump" is what an audiovisual engine is for.

`jumpRange` is a **registered parameter** — how far this body will leap is a creative control that
belongs on a timeline or under a signal. The arc's *shape* (`jumpGravity`, `jumpApex`,
`landSeconds`) is read once and held, like the affinities, because it is a property of the creature
rather than of the moment. Gravity defaults to 18 rather than 9.81: 9.81 gives a floaty, slow-motion
hop at the scale these characters are built at.

**`jumpRange` defaults to 0, which is a body that does not jump**, so every scene written before this
behaves exactly as it did.

## Verified

- The arc: rises to within 10% of its apex, lands on the ground, travels the distance asked for, and
  reports `Jump` then `Fall` then `Land` — each phase actually occurring, checked rather than
  assumed.
- `Land` is `active()` but not `airborne()`, which is the distinction grounding depends on.
- Refusals: too far, too near, and a hop already in progress is not retargeted.
- Determinism: two components given the same launch produce bit-identical paths, and `reset()` really
  clears — what an offline render of the same second depends on.
- The guard: an arc that never finds ground ends anyway, rather than falling for the rest of the
  piece.
- The beat trigger: no hop without a signal, exactly one hop on a sustained beat, another on the next
  rising edge.

CPU suite 1,672 cases / 1,441,477 assertions.
