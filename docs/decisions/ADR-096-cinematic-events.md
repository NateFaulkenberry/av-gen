# ADR-096: Cinematic events — three tiers, and where the bake stops

**Status:** accepted
**Date:** 2026-09-11
**Context:** section 17 of the *All You Got* cinematic world brief, and sections 3, 14, 34 and 35
**Depends on:** ADR-089 (the sequence bakes), ADR-091 (two-tier simulation authority)
**Constrains:** the action system, trigger volumes, and anything else that wants to be triggered

## The problem

The brief asks for an event system: *when X happens, do Y*, where **when** is an absolute time, a
beat, a bar, a musical section, a shot start or end, an animation or interaction completing, a
timeline cue, or an entity crossing a volume — and **what** is an action on an entity, a parameter
change, a camera change, a clip, a light/material/particle change, an overlay, or a scene
transition.

The engine has no such thing. `seq::Marker` is "authoring context, not executable events" — the
header says so deliberately — and tracks fire values, not verbs.

**The difficulty is not building one. It is that a naive one would destroy the property the whole
sequencer exists for.** ADR-089's entire determinism story is that a sequence *bakes*: shots,
actors, transitions and overlay cues become `params::Track`s, and from then on a frame is
`Track::evaluate(t)` and nothing else. Scrubbing, replay and offline rendering are correct by
construction because there is no accumulated state to be wrong about.

An event system is the opposite shape. It is imperative, it is naturally stateful ("has this fired
yet?"), and the obvious implementation — walk the list every frame and fire whatever the playhead
just crossed — makes the frame depend on how the playhead got there. A scrub from 10 s to 45 s
would fire thirty-five seconds of events in one frame, or none at all, depending on which side of a
comparison somebody wrote. That is exactly the history dependence ADR-089 refused, and it would
arrive through the back door of a feature that sounds unrelated.

So the question this ADR has to answer is not *how do events fire*. It is **how much of an event can
stop being an event**.

## The decision

**Three tiers, and the tier is a property of the event that an author can read before running
anything.**

```
        when is knowable?          what is a value over time?
Tier 1  yes  (time, beat, bar,     yes  (parameter, shake, clip,      ->  BAKED
             section, shot edge,         overlay, scene transition)       (it stops existing)
             cue, bounded clip)
Tier 2  yes                        no   (entity action, notify)       ->  SCHEDULED
Tier 3  no   (volume, completion)  either                             ->  LIVE
```

The rule in one line: **a trigger decides whether an event can be scheduled; an action decides
whether it can be baked.** Both halves must be knowable for tier 1. `seq::triggerIsScheduled()` and
`seq::actionIsBaked()` are pure predicates over the enums, so an editor can colour an event row
without evaluating anything, and the bake reports the split per sequence.

### Tier 1 — baked. The event stops existing.

A time-known trigger with a value-shaped action becomes keys, and there is no event left to fire.
"Set the fog to 3 at 0:12" is a Step key at 12 s. **Baking converts *fire once* into *hold from
here*** — and a value that holds cannot be skipped by a scrub or delivered twice by one, because
there is no delivery, only an evaluation. "Fires exactly once" stops being a runtime invariant
somebody has to maintain and becomes a fact about a sorted list produced by a pure function.

This is where the great majority of a piece's events live, and it is larger than it first looks:

- **Beats, bars and sections are precomputed.** The analysis is offline and deterministic; a
  sequence already carries beat and section markers (ADR-089 spec 19/20). So "shake the camera on
  every eighth bar" is not a runtime subscription — it is a list of times, known at bake.
- **A light, a material, a particle rate and the fog are all parameter paths.** They are one action
  kind (`SetParameter`), not four. Inventing `SetLightIntensity`, `SetMaterialColor` and
  `SetParticleRate` would be the duplication the brief's section 48 warns about, and every property
  added to the engine afterwards would have needed a fourth verb.
- **A clip is already solved.** ADR-089 established that an animation state needs a *time origin*
  and that a track cannot carry one, so clip cues are evaluated by `animationAt()` rather than
  baked. A `PlayClip` event therefore produces a `ScheduledClip`, which is the same object an
  authored `ClipCue` produces. The two lists merge and the later of the two wins.
- **An overlay is a layer's parameters.** A `MoveOverlay` writes `layers/<id>/position` through the
  same `LayerSink` seam the overlay cues already use.

### Tier 2 — scheduled. A known time, an imperative effect.

"At the start of shot 4, tell the elder to walk to the door" has an exact time. But a track cannot
carry *ask the action system to walk to the door*, and the action system is stateful by nature
(ADR-091's live tier). Pretending this is scrub-safe would be the lie.

What a seek does instead is **restore the standing intent**. On a forward step the dispatcher
delivers every crossed dispatch exactly once, in schedule order. On a jump — backwards, or forwards
by more than `continuitySeconds` — it delivers *the latest dispatch at or before the new playhead,
one per (action kind, target)*, marked `restored`, and drops everything else. Playing 0 → 60 s and
jumping to 60 s therefore leave the same standing intents.

This is not a compromise invented here; it is ADR-091's **resume-rather-than-reset** rule applied to
direction. An entity cannot perform nine queued actions in one frame, and the nine it "performed"
would be in the wrong order relative to a world that never ran. The last thing each target was told
to do is the only part of that history that is still true.

### Tier 3 — live. Posted, never polled.

An entity crossing a volume, an action completing, a rig finishing a clip whose length only the
asset knows. Nobody can compute these from the playhead, because the entity is in ADR-091's live
tier and its position at *t* depends on how it got there.

These are **posted** by whoever already owns the fact — the trigger-volume system computes an
enter/exit edge anyway, the action system knows when a queue drains — and dispatched by a hash
lookup on `(kind, name)`. Nothing in `seq/` visits an entity, and nothing in it walks the event list
per frame. A signal naming a volume nobody listens to costs one failed lookup and no allocation.

A seek clears pending live firings: a live event belongs to the moment it happened, and the moment
is gone.

### The boundary is visible

A guarantee nobody can see is a guarantee nobody relies on (ADR-091). So:

- `BakeResult::events` reports the three lists by name and size.
- A bake with a non-empty scheduled tier emits a warning saying what a seek will do to it.
- A `ClipEnd` trigger the sequence cannot bound warns that it has dropped to the live tier, naming
  the actor and the second, rather than guessing at a clip length that belongs to an asset.
- A `Replace`-mode parameter event warns that its value holds backwards from t = 0 unless something
  else states the value first — because a track holds its first key's value backwards forever, and
  "the fog was always 3" is a genuinely surprising reading of "set the fog at 0:12".

### `Add` and `Multiply` are how an event means *a change*

The last point deserves its own line, because it turned out to be the most useful thing in the
design. `params::TrackMode` already exists. An event authored in `Add` or `Multiply` mode has a
**known identity** — 0 or 1 — so the bake can key a baseline at t = 0 without knowing anything about
the scene, and the event contributes nothing until it fires and its delta afterwards, whatever the
author set the property to. That also makes `holdSeconds` exact: returning to the identity needs no
knowledge of what was there before, so "flash the lamp for a beat and put it back" is four keys and
is correct under any scrub. `Replace` remains right for a property the event owns outright.

## Alternatives considered

**A. One runtime evaluator for everything.** Keep an ordered event list, remember the last playhead,
fire what was crossed. Simple, and it is what most engines do. Rejected because it makes determinism
a property of the evaluator's discipline rather than of the architecture — the same reason ADR-089
rejected a per-frame sequence evaluator — and because it would have made the *majority* of events
(which have perfectly knowable times and value-shaped effects) as fragile as the minority that
genuinely cannot be known.

**B. Bake everything by recording a simulation.** Run the world once, record what fired, replay the
recording. Perfectly deterministic; it is ADR-091's option 1, and it was rejected there for the same
reason it is rejected here: the world stops being live, and "the city continues when the camera
looks away" becomes a tape.

**C. Checkpoint the event runtime and re-simulate on a seek.** ADR-091's option 2. Still available,
still only for the live tier, still not worth blocking this on. Nothing here spends that option: the
baked tier has no state to checkpoint, and the scheduled tier's restore rule is what a checkpoint
system would have to produce anyway.

**D. Events as a second sequencer.** A parallel list with its own time base, its own serialisation
and its own editor. Rejected on the brief's own section 48 grounds and on ADR-089's: it would have
needed everything the timeline already has, twice, and the two would have drifted.

## The other four items this decision carried

**Camera shake (section 14).** `app/cinematic.hpp` says "there is no shake in this file and there is
not going to be one". It was right about what it was refusing — a director that shakes on every beat
— and wrong only if that is read as a refusal of the *capability*. A shake is now a camera-space
offset built from four ordinary parameters (`camera/shake/amplitude`, `frequency`, `decay`,
`rotation`), so it is keyframeable and a beat can drive it through an ordinary `ModRoute` with no
new mechanism.

The subtle part is `camera/shake/start`. A decay needs "how long since the impulse", and the obvious
implementation is a timer — accumulated state, and the whole problem again. So the impulse's origin
is **itself a parameter**, Step-keyed by the event that fired it, and the engine evaluates
`now - start`. That is precisely the trick ADR-089 used for a clip cue's phase origin, for precisely
the same reason. The envelope is finite (quadratic to exactly zero) rather than exponential, so a
shake provably ends and a shake that is over costs nothing.

**Match cut (section 35).** Crossfade was evaluated a second time and the answer did not change:
blending two 3D scenes needs both drawn into separate targets, which is a full second scene render —
the most expensive thing in the engine — for one transition, and a dip is what a cutter actually
reaches for. It stays unimplemented and stays honestly named.

Match cut, though, turned out to be nearly free, and it is a real editorial verb rather than a
consolation prize. A match cut is a hard cut whose two frames *rhyme*: the incoming subject lands at
the same apparent size and the same place in frame as the outgoing one. `app::Shot` already states a
subject's radius, a distance in radii, a focal length and a framing offset, and
`subjectCoverageAt()` already computes the apparent size — so the match is one inversion of that
expression, decided at bake, costing the renderer nothing. `preferredDistance` can bound it, and the
bake says so when it does.

**Shot-driven quality (section 34).** A shot's `Spotlight` now raises its subject's level-of-detail
floor for the length of the shot, as **Multiply** tracks. That is the whole trick: every knob
involved (`lod/minScreenRadius`, `lod/distance1..3`) treats zero as *no limit*, so multiplying by
`1 - emphasis` pins a fully spotlit subject at its best level and restores the author's own values
afterwards — without the bake ever having to know, or store, what those values were. A `Replace`
track would have had to guess them.

**The fourth-wall cursor (brief section 3).** There is no cursor in `src/`. A scripted pointer is an
overlay cue of kind Shape, two `Overlay` events moving `position.x`/`position.y`, one more for the
click, and a `Notify` for the selection. The storyboard's "a cursor appears and clicks a character"
is *the sequence can show and move a pointer and fire a selection event*, which is the generic
statement the addendum's section 26 requires.

## The two seams this needs from elsewhere

Both are deliberately narrow: one struct and one call each. Nothing in `seq/` knows what a volume or
an action *is*, and neither system has to know what an event is.

### From the trigger-volume system (`agent/cw-react`, brief section 18)

Trigger volumes are that agent's primitive and this system's event source; nothing like one is built
here. What is needed is a call on the **edge**, which that system computes anyway:

```cpp
dispatcher.post(seq::TriggerSignal{
    .kind        = seq::TriggerKind::VolumeEnter,  // or VolumeExit
    .name        = "<volume id>",                  // as authored
    .subject     = "<entity name>",                // whose edge it was
    .timeSeconds = <the timeline second>,          // not a wall clock, not a source time
});
```

Requirements on the caller, all of which a grid-based enter/exit implementation satisfies already:

- **Edges, not states.** Post once when the entity crosses, never per frame while it is inside.
- **Timeline seconds.** The engine's `timelineClock_.seconds`, so a multi-audio piece and a delayed
  event agree about when "now" is (brief section 25).
- **Stable ids.** The volume's authored id and the entity's `EntityDesc::name`. An event naming a
  volume nobody posts is a failed hash lookup and nothing else, so a typo is cheap — but it is also
  invisible, which is why the editor should list the volume ids in force.
- **Nothing on a seek.** The volume system's own reset should not synthesise exit edges; the
  dispatcher clears pending live firings on a seek regardless.

The reverse direction is optional and worth having: an event with a `SetParameter` action on a live
trigger is applied by the host from `Engine::firedEvents()`, so a music influence field could equally
consume the same list.

### From the action system (`agent/cw-intent`, brief section 4)

Two directions, both one call.

**Events trigger actions.** A fired `EventActionKind::EntityAction` carries:

| field | meaning |
|---|---|
| `what.target` | the entity's name |
| `what.value` | the verb — `walkTo`, `react`, `equip`, `interact` |
| `what.argument` | the verb's own object: a node name, an interaction id, a destination |
| `what.amount` | numeric arguments (a position, a duration, an intensity) |
| `FiredEvent::restored` | **true means this is a seek restoring a standing intent, not a new beat.** Apply it as a state — put the character where the action would have left it — rather than re-running its entry. This is the one field the action system must not ignore. |

The host drains `Engine::firedEvents()` each frame and routes these; the engine deliberately does
not interpret them, because inventing a meaning for `walkTo` in `app/` would be the second action
system this design exists to avoid.

**Actions trigger events.** When an action or interaction finishes, post the completion:

```cpp
dispatcher.post({seq::TriggerKind::ActionComplete,      "<action id>",      entity, now});
dispatcher.post({seq::TriggerKind::InteractionComplete, "<interaction id>", entity, now});
```

`ClipEnd` is the third of these and is the one with a split: when the *sequence* bounds a clip (a
following cue states when it stops), the trigger is scheduled at bake and no post is needed. When it
does not — the actor's last cue, or a rig driven by a behaviour — only the animation layer knows,
and it should post `TriggerKind::ClipEnd` with the clip's name and the actor's id. The bake already
warns, per cue, which of the two a given event ended up in.

## Consequences

**Good.** The sequencer's guarantees are untouched, and the majority of what the brief calls an
"event system" inherits them rather than eroding them. Nothing polls. One action kind covers
lights, materials, particles and everything added later. The two tiers that cannot be pure are named
as such, in the bake's own output, where an author will see them.

**Bad.** There are three answers to "what will a scrub do to this event", and an author has to know
which tier an event is in. That cost is paid in the editor, which must show the tier — the same debt
ADR-091 took on and for the same reason. `Replace` mode has a genuinely surprising backwards-holding
behaviour that only a warning mitigates.

**Watch for.** Events drifting into the scheduled tier because an author reached for `EntityAction`
where a parameter change would have done. The baked tier is the one with the guarantees, and an
event that could have been a value should be one.

## Revisit triggers

- A piece that needs a *scrub-exact* entity action — then ADR-091's option 2, checkpointing, for the
  live tier, and the restore rule here becomes the fallback rather than the answer.
- A second author asking for a crossfade rather than a dip, which is ADR-089's own revisit trigger
  and is still the thing that would justify the renderer change.
- A `ClipEnd` that needs asset clip lengths at bake time — which would mean the sequencer learning
  what a clip is, and is a bigger decision than it sounds.
