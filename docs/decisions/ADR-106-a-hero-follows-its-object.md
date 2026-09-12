# ADR-106: A hero follows the object it describes

Status: accepted
Date: 2026-09-12

## Context

ADR-074 defines a hero as "a description of what has already been placed by the scene's own nodes",
and gives it a `position`. ADR-104 made that position something the editor measures from the object
when the object is starred.

Nothing then kept the two together. The position was a snapshot taken at designation, so moving the
object left the hero where it had been: the director framed the space the object used to occupy, the
clearance field cleared undergrowth around nothing, and the editor's own hero mark stayed on the
ground twenty metres below the thing it was drawn for. That is a value living in two places and
written in one — the trap this repository has paid for twice already, and the reason
`syncNodeTransform` exists on the transform side.

## Decision

### The hero is translated by the object's delta, once a frame

`Composition::syncHeroesToNodes` runs after the parameters are applied — a node's position *is* a
parameter, so where the object is has only just been decided for this frame — and moves each hero by
however far its node has moved since the last sync. Each hero keeps an anchor: where its node stood
when the two were last in step.

### A delta, not a re-measurement

Re-measuring the bounds each frame would be both more expensive and wrong. A hero's position is
allowed to be somewhere other than the middle of its object: Glowmere's elder sits below its crown
deliberately, so that the camera arrives looking at the trunk rather than the cap. Re-measuring would
silently discard that authorship the first time anybody nudged the object.

`radius` and `height` are not followed either. Scaling an object is a rarer and more deliberate act
than moving it, and those two numbers may have been chosen rather than measured. Undeclaring and
declaring again is how to take a fresh measurement, and it is one click.

### The world follows immediately; the director waits for the object to stop

Everything that reads a hero's position — the editor's mark, the clearance field, the obstacles
entities walk around — is correct the same frame, because it reads the hero.

`heroRevision` is what ADR-105 re-cuts the directed shot on, and a re-cut folds the whole track. A
drag is sixty positions a second; sixty re-cuts a second is not a feature. So the revision moves once,
a quarter of a second after the motion stops. The debounce is on time rather than on a drag-ended
signal from the editor, because the editor is not the only thing that moves a node — automation, the
AI control plane and a procedural rebuild all do — and a notification every mover has to remember to
send is a notification one of them will forget.

### A hero naming an assembly is left alone

There is no single object whose movement would be the assembly's. An anchor is only taken when a node
of the hero's own name exists, and a hero that later gains one adopts its position without moving —
otherwise an undone deletion would teleport the hero by the whole distance between them.

## Rejected alternatives

- **Resolving the position at every read instead of storing it.** `HeroPoint` is passed by value and
  by span to the director, the obstacle builder, the clearance field and the terrain query; making
  each of them able to resolve a name against a composition is a much larger change than keeping one
  vector in step, and it would leave a hero with no node — an assembly — with no position at all.
- **Re-measuring the bounds on settle.** Cheap enough, and it throws away authored offsets. See above.
- **A dirty flag set by the editor's move command.** Same objection as ADR-105's: every mover has to
  remember, and the one that forgets is a stale hero nobody can explain.
- **Moving the object when the hero moves.** The inverse relationship, and the wrong way round: a
  hero describes an object, so the object is the source.

## Consequences

- Moving a hero's object now re-cuts a directed shot a quarter-second after you let go, which is the
  behaviour ADR-105 listed as a revisit trigger.
- A hero whose object is *scaled* keeps its old radius and height until it is declared again. That is
  a deliberate limit, and the one most likely to want revisiting.
- `setHeroes` re-anchors, so declaring a hero at the same moment its object is placed does not read
  as a move.

## Revisit triggers

- Heroes whose size needs to follow a scaled object often enough that re-declaring is a chore.
- A hero that should describe a *moving* object continuously — a vehicle, a character — where the
  quarter-second settle would never elapse and the shot would never re-cut.
