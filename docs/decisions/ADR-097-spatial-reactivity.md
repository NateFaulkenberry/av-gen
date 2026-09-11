# ADR-097: A field scales the reactions an entity already has

**Status:** accepted
**Date:** 2026-09-11
**Context:** Group C of `docs/cinematic-world-gap-analysis.md` — §18 spatial triggers, §19 the music
influence field, §20 the profile library, §21 the reaction arc, §39–§43 non-character reactivity
**Constrained by:** ADR-091 (two-tier simulation authority), ADR-088 (an entity drives a node)

## The problem

The brief asks for a *music influence field*: a region of the world inside which things answer the
music more strongly, with a smooth edge rather than a line, and a reaction that a character enters,
holds, and comes back out of.

The brief also says, twice and in both documents, **do not build a second audio-reactivity system**.
Those two sentences are the whole design problem, because the obvious implementation of the first
one violates the second. A field that owned reactions — "inside this sphere, drive emissiveGain from
the bass" — would be a parallel copy of `ReactionDesc`, of `ProcessorChain`, of the addressing rules
and of the diagnostics, and would have exactly the failure mode ADR-088 was written to stop: two
places that both nearly know how a property answers a signal.

So the question is not *what a field does*. It is *where a field attaches*.

## The seam

An entity's `reactions` compile to ordinary `params::ModRoute`s (`fromEntity`), and
`Modulator::applyRoutes` computes, per route, per frame:

```
y = chain.process(signal) * amount * masterGain
```

`amount` **is** the depth the author wrote. So spatial influence is a second multiplier next to
`amount`, and nothing else:

```
y = chain.process(signal) * amount * spatialGain * masterGain
```

`ModRoute` gains two runtime fields — `spatialGain`, and `ownerEntity` so the field pass can find
its own routes after a rebuild has erased and re-added them. `compileReactions` fills `ownerEntity`.
That is the entire coupling between the field layer and the modulation layer: two floats and an
index, no new route kind, no new chain, no new addressing, no new diagnostics.

The consequence is the point of the whole feature. An author who already wrote

```json
{ "signal": "audio.bass", "target": "parts/Lamp/emissiveGain", "depth": 2.6 }
```

gets a spatial version of it by putting a field in the scene and tagging the entity. They write
nothing else, and the reaction they already tuned is the reaction that plays.

### Alternatives that were rejected

- **A field publishes a per-entity signal and the author routes from it.** Works, but it makes every
  existing reaction need a second reaction to multiply it, which is the duplication in a different
  costume — and it cannot express "all of this entity's reactions" without naming each one.
- **The field writes the parameter directly.** A second thing writing `emissiveGain`, fighting the
  route on the same property. ADR-088 settled that question once.
- **A field adds routes at runtime.** The routes are authored data that a project saves; a system
  that injects and removes them per frame makes "what is modulating this" unanswerable.

## Where it runs

`SceneController::updateFields` is a third per-frame hook, and it is on the **other side of the
routes** from `updateBehaviour`:

```
params.resetFinals()
timeline_.apply()          <- automation: where a baked actor is at this instant
controller_->updateFields() <- the field pass: resolve, query, set spatialGain      (ADR-097)
modulator_.applyRoutes()   <- the routes, now carrying the field's depth
controller_->updateBehaviour() <- behaviours, folding offsets onto the finals       (ADR-088)
controller_->update()
```

Both placements are forced, and for mirror-image reasons. A behaviour's *knobs* must have been
modulated before it reads them and its *output* must land after the routes wrote theirs, so it runs
after. A field's entire output is a gain *on* those routes, so it must run before — a field
evaluated afterwards would put every reaction in the scene one frame behind its field, which is
invisible while something drifts slowly past and is a different picture the instant anybody scrubs
to a frame instead of playing to it.

Running after `timeline_.apply()` is what makes ADR-091's promise real: a field whose source is a
baked actor reads the position that actor has **at this instant**, from the parameter finals the
automation just wrote, so the whole chain is a pure function of time.

## What a field is

A position, a radius, a falloff curve and a strength — plus a filter and an arc.

```json
{ "name": "stage", "shape": "sphere", "center": [0, 0, -12], "radius": 14,
  "falloff": "smooth", "inner": 0.25, "strength": 1.4, "tags": ["dancer"],
  "source": "singer",
  "arc": { "holdSeconds": 6, "holdJitter": 1.5, "delayJitter": 0.8, "activity": "react" } }
```

**The falloff is not optional.** `TriggerVolume::influenceAt` returns 1 at the centre, 0 at the
surface and something smooth between, for all three shapes, through one shared curve. A `constant`
curve exists because a *trigger* legitimately wants a hard edge — a doorway, a stage boundary — and
it is not the default because a *field* never does: binary in/out is what makes a crowd snap on at a
line in the air (addendum §13).

**The surface belongs to the outside.** Influence at exactly the boundary is exactly 0 and
`contains` is false. Any other rule makes an enter edge and an exit edge at the same point disagree
depending on which side the float landed.

**Influence combines by max, not sum.** It is a 0..1 statement about how far inside something is,
and two overlapping speakers do not make a listener more than fully inside. A field that wants to be
louder says `strength`, which may exceed 1 and which is what multiplies the depth.

**An entity no field's filter matches keeps a gain of exactly 1.** This is what makes the change
invisible to every scene that does not use it: the flag is not "fields exist", it is "a field
governs *this* entity", and an ungoverned entity never enters the broad phase at all.

## A field's knobs are parameters, and one of them cannot be modulated

ADR-088's standard is that every knob a behaviour owns is a `params::Parameter`, "so every knob a
behaviour has is keyframeable on the timeline, presettable, and a legal modulation target". A field
gets the same treatment: `<prefix>fields/<name>/strength`, `scale`, `inner`, `floorGain` and
`center`. An authored number that cannot be keyframed stops being interesting the moment a shot
needs it to change, and "the floor opens up at the drop" is the first thing anybody will ask for.

They are read from the parameter **finals**, and at the instant the field pass runs those finals are
exactly *base + automation*: `resetFinals()` has wiped the previous frame's routes and this frame's
have not run yet. So a timeline key lands, a preset lands, and the field stays a pure function of
time — which is the property the whole ADR-091 story depends on.

The cost is that **a modulation route pointed at a field knob does nothing.** It writes after the
field has been read and is wiped before the field reads again. That is a route that resolves, runs,
and has no effect — this project's signature failure — so `Composition::updateFields` scans the
routes once after an install and warns by name, telling the author to keyframe it instead. The
alternative was to run the field pass twice, or to run it after the routes and put every reaction
one frame behind its field; neither is worth being able to modulate a radius.

## The broad phase

A uniform grid over the entity positions, counting-sorted into two flat arrays and rebuilt every
frame (entities move; the rebuild is O(n) and allocates nothing once the arrays have grown). Fields
are iterated against it by their bounds. The cell is sized to the largest field's reach, so one
field's query touches at most 27 cells however large the crowd or the world is.

The grid holds *governed* entities only, and it honours the three-band behaviour LOD an entity
already carries: past `cullDistance` it is not queried at all, and past `fullDetailDistance` it is
queried on `coarseInterval` rather than every frame. Arcs are advanced for everyone regardless —
an arc that stopped being ticked because its character walked out of full detail would never end,
and the character would come back into view still dancing to a field it left a minute ago.

## The arc, and why it needed almost no code

§21 asks for `Walking → Dance → Walking`, never `Walking → Dance → Idle`. That is a *resume, not
reset* contract, and `wander` already implements it:

> Something with the character's attention has it. Travel is what a character does when nothing else
> is happening, so it yields rather than competing: the destination and the pause timer are kept,
> and the walk resumes from where it stopped.

So the arc writes `Activity::React` into the entity state **before** the behaviours run rather than
after, and the existing yield does the work. Nothing is reset, nothing is stored to be put back, and
what comes back when the arc ends is whatever the behaviours are doing at that moment. The activity
is re-asserted after the loop too, because a behaviour that ran later may have decided something
else and the arc outranks it for as long as it lasts.

Per-entity variation — delay, length and intensity — is drawn from `arcStream(entity seed, field
name, how many times this entity has entered this field)`. Deliberately **not** the entity's
behaviour stream: that one advances once per frame per behaviour, so a draw taken from it would
depend on the frame rate and on what else the entity happened to be doing. The stream used here is a
pure function of three values, so the second time entity *E* enters field *F* it always draws the
same numbers.

## Determinism, made visible (ADR-091)

A field's purity follows its source, and `FieldAuthority` records which one an author actually got:

| source | authority | guarantee |
|---|---|---|
| none — the volume is where the scene put it | `Static` | pure |
| a node, or an entity with no behaviours (driven by tracks) | `Baked` | pure: scrub-safe, offline-exact |
| an entity with behaviours | `Live` | **not** scrub-exact |

It is derived, never authored, and it is said out loud in three places: one line per field in
`EntityWorld::fieldReport()`, logged at `info` at every install whether or not anything is wrong;
available to the editor, though no panel draws it yet; and `"requireScrubExact": true` turns a
`Live` resolution into a reported problem by name. ADR-091 asks that this be "stated where an author chooses, not discovered when a
render differs", and a line that is only printed when something is wrong is a line nobody learns to
look for.

Field state is live-tier state: `reset()` clears arcs, inside-flags and entry counts, the same
honest answer a behaviour gives on a seek.

## §39–§43: things that are not characters

The gap analysis was right that this needed no new path, and it needed one addition to be useful.

**The existing half.** An entity with `reactions` and no `behaviors` is already legal, drives any
node, and reaches a light, the post chain or anything else through the `@` absolute-path escape
hatch. That is tested rather than asserted.

**The addition.** `SignalBus::declare` takes a name at runtime, so a field publishes itself:
`field.<name>.occupancy` (0..1), `field.<name>.enter` and `field.<name>.exit` (events). A light, a
material or a particle system reacts to a *volume* through an ordinary reaction from an ordinary
signal, and none of them learns what a volume is. This is the same seam musical events (ADR-073)
already use, pointed at geometry.

A route naming `field.X.occupancy` binds to nothing at load, because the field has not published
itself yet, so `Composition::updateFields` re-binds once on the frame the bus grows — the same way
a new control channel is handled. Without it that reaction would resolve to nothing and say nothing,
which is the failure this project has shipped five times.

## §20: the profile library

`EntityDesc::profile` already referenced a shared file **by path**, and that stays. What §20 wanted
was for ten NPCs to share one bundle without ten path resolutions of ten copies of the same twenty
lines, so a scene may name one library and entities reference entries in it by name:

```json
"entityProfiles": "profiles/night-shift.json",
"entities": [ { "name": "dancer-01", "profile": "dancer" }, ... ]
```

One file, read once. The library is consulted first and a name it does not have is still a path, so
nothing written before this exists has to change; a miss names both the library and every profile in
it, because "no such profile" without the list costs the reader an afternoon.

The profile an entity was built from is also a **tag it did not have to be given**, so "every NPC
built from the dancer profile" is one word in a field's filter rather than a list of forty names
that goes stale the day somebody adds the forty-first.

## Consequences

**Good.** There is one reactivity system and it is the one that was already here. A field is
additive: a scene without one is byte-for-byte the scene it was. The expensive part is bounded by a
filter that is static, so a crowd of five hundred with one field tagged `dancer` pays for the forty
dancers.

**Bad.** `ModRoute` now carries two runtime fields that only one subsystem writes, and `ownerEntity`
is an index into `EntityWorld` — a coupling by position that is only safe because both are rebuilt
together in `installEntities`. If routes ever outlive an entity-set change without a recompile, that
index is wrong; it is checked against `entities_.size()` rather than trusted.

**Watch for.** A `Multiply` route whose chain rests at 1 is *muted* by a gain of 0 rather than made
quiet, because scaling depth to zero means the same thing as writing depth zero. `"spatial": false`
on a reaction is the opt-out, and it exists because the alternative was to special-case `ModOp` in
the field layer, which would be the field layer knowing about modulation semantics it has no
business knowing.

**Deliberately not done.** No editor overlay draws fields, occupancy or arc state (§45/§46); the
data is exposed (`fieldRuntime()`, `fieldReport()`, `fieldCounts()`, `triggerEvents()`) and the log
carries the part that matters, but the picture is not drawn. A field cannot yet be a source for
another field (§22's propagation) —
the data shape allows it (`source` names an entity, and an influenced entity could own a field) but
nothing evaluates the dependency order, so it is not offered. Fields do not drive events into an
action queue, because there is no action queue yet (§4, Group B); `EntityWorld::triggerEvents()` is
the list §17 will read when it lands.
