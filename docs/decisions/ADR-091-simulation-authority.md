# ADR-091: Two-tier simulation authority

**Status:** accepted
**Date:** 2026-09-11
**Context:** the *All You Got* cinematic world brief and its Night Shift addendum
**Supersedes nothing. Constrains:** every system added under `docs/cinematic-world-gap-analysis.md`

## The problem

This engine has two ways of making something move, and they have opposite determinism properties.

**The sequencer is pure.** `seq::Sequence::bake()` turns shots, actors, paths and cues into
ordinary `params::Track`s, and from then on a frame is `Track::evaluate(t)` and nothing else
(ADR-089). Scrubbing, replay, and offline-render-matches-realtime are correct *by construction*,
because there is no accumulated state to be wrong about. 10s → 45s → 3s → 30s is four pure
evaluations.

**The entity layer is stateful.** A behaviour integrates: `wander` picks a destination and walks
toward it, and where it is at *t* depends on how it got there. `IBehavior::reset()` exists and is
called on a timeline seek, which means a scrubbed frame is not the frame you would have reached by
playing to that time — it is the **reset** frame. That is honest and it is invisible for ambient
motion, but it is not what the brief's §22 (scrubbing) and §23 (replay) ask for.

Every system the brief adds inherits one of these. A navigating character accumulates. A music
influence field whose source is a **baked actor** is a pure function of time; the same field whose
source is an **autonomous character** is not. So this is not a decision per system — it is one
decision that determines whether the whole simulation layer is scrub-safe, and it has to be made
before Phase 3 rather than discovered during it.

## The options

1. **Bake the simulation.** Run it once, record the result as actor keys, play those back. Perfect
   determinism, cheapest runtime — but the world stops being live in the editor, and "the city
   continues to exist when the camera is not looking at it" becomes a recording.
2. **Fixed-step catch-up with checkpoints.** Simulation on its own fixed step; a seek restores the
   nearest checkpoint and re-simulates forward. Live *and* deterministic, at the cost of checkpoint
   memory and a seek that is no longer instant — and it has to be finished before anything can
   depend on it.
3. **Two-tier authority.** Director-owned actors bake; ambient population simulates live and is
   documented as not frame-accurate under scrub.

## Decision

**Two-tier authority.** An entity is under exactly one of two authorities at any instant:

```
Director Override          <- a shot says exactly what happens; baked to tracks
      |
Cinematic Action           <- an action queue the director filled; deterministic from its cues
      |
Behavior                   <- autonomous; live; stateful
      |
Navigation                 <- serves whichever of the above is driving
```

The hierarchy is the brief's own (§9 of the addendum), and authority is read top-down: the highest
tier with something to say wins, and when it finishes the entity falls back to the tier below,
resuming rather than resetting.

**Baked tier.** Anything the director names in a shot. Its motion is `Track::evaluate(t)`, so it
keeps every guarantee the sequencer has today: instant seek, identical offline render, identical
replay. The protagonist's morning routine — the part the audience actually watches — lives here.

**Live tier.** Ambient population, props, background vehicles. Stateful, reset on seek, and
**explicitly not frame-accurate under scrub**. A background pedestrian resuming from a reset when
the playhead jumps is invisible; the protagonist doing it is not.

### What this obliges

- **A field's purity follows its source.** A music influence field attached to a baked actor is a
  pure function of time and is therefore scrub-safe and offline-exact. Attached to a live entity it
  is not. This must be stated where an author chooses, not discovered when a render differs.
- **Reactions are pure either way.** A `ReactionDesc` is a signal read through a `ProcessorChain`
  onto a property. Signals come from the analysis, which is precomputed offline and deterministic.
  Only the *spatial gain* can be impure, and only by the rule above.
- **Falling back must resume, not reset.** The addendum's §14 is explicit: `Walking → Dance →
  Walking`, not `Walking → Dance → Idle`. A behaviour that is preempted keeps its state.
- **The live tier must say so.** `EntityWorld` reports which entities are live, and the editor shows
  it. A guarantee that is not visible is a guarantee nobody can rely on.

### What this does not close

Option 2 remains available **for the live tier only**, as a later addition: checkpointing an
ambient crowd does not disturb anything in the baked tier, because the baked tier has no state to
checkpoint. Choosing two-tier now does not spend that option; it just refuses to block Phase 3 on
it.

## Consequences

**Good.** Nothing that works today gets weaker — the sequencer's guarantees are untouched and the
entity layer's honest reset stays honest. No system has to be finished before the others can start.
The expensive tier (baked) is the small one: a music video has one protagonist and a hundred
extras, and only the protagonist needs frame-exactness.

**Bad.** There are now two answers to "where is this character at *t*", and an author has to know
which tier a character is in to know what a scrub will do. That is a real cost and it is paid in the
editor UI, which must show the tier rather than leaving it to be inferred.

**Watch for.** The temptation to promote an entity to the baked tier "just to be safe" — which would
gradually turn the live world back into a recording and cost the thing the brief actually wants.
Promotion should be a director's decision about a shot, not a workaround for a bug.
