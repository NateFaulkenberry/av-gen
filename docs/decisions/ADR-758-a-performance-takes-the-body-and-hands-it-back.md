# ADR-758: A scripted performance takes the character's body at a cut and hands it back where it ends

**Status:** Accepted
**Date:** 2026-09-24
**Related:** ADR-210 (the stage director; `DirectorMotion`, `driven`), ADR-091 (two-tier
determinism), ADR-700 (seek from checkpoints), ADR-620 (the gait's acceleration limit), ADR-089 (the
sequencer bake), ADR-755/756 (the Plan and its compiler); spec §24–§25
**Implemented by:** `Composition::Performer`/`setPerformers`/`applyPerformers` (applied after the
staging director on a play *and* in both seek-replay paths; mixed into `replayInputKey`);
`seq::performerFor`; `BakeOptions::performerNodes`; `Engine::installSequence`;
`DirectorMotion::performance` (re-asserted after the behaviours; speed read unramped)
**Tests:** `tests/unit/test_directing_handoff.cpp` (`[directing][handoff]`)

## Context: the probe (spec §25), measured

Spec §25 asks seven questions and says not to guess. The probe (now the regression file above)
measured the engine as it was:

1. **How an entity drives its node:** `EntityWorld::applyNodeOffsets` *adds* its travel and motion to
   the node's position parameter **final**, after the timeline has written it (and its yaw difference
   to the rotation final).
2. **How `seq::Actor` drives the same node:** the bake writes Replace tracks on `nodes/<n>/position`
   and `rotation`. Clip cues are applied by `seq::applyAnimation`, after the behaviours, every frame.
3. **Can both write at once? Yes, and they are summed.** The node sat at the actor's key *plus* the
   entity's travel, exactly (probe 1).
4. **Must one be disabled? Yes, and the obvious tool does not do it.** A Director-tier Wait holds
   *actions*; a behaviour that moves the body carried on (probe 4). And the actor's baked track
   pinned the node **for the whole film**, before its first key and after its last, because a track
   holds its end keys (probe 5).
5. **The handoff that exists:** `entity::DirectorMotion`, the staging director's mechanism (ADR-210).
   It writes where the body *is* as `travel`, and marks it `driven`, so body-moving behaviours yield
   while keeping their state. On the benchmark it held Rook exactly (probe 7). One exception: `orbit`
   with simulation authority ignored `driven` and overwrote it on the same step (probe 6).
6. **How control returns:** released, Rook's decider carried on **from where he was put**, not from
   where he had been (probe 7).
7. **Seeking:** a director motion is part of each entity's checkpointed state (ADR-700), so a
   director applied at the same point of the step on a play and a replay gives the same body.

The actor's clip also won Rook's rig (probe on the benchmark).

## The question: pause-and-resume, or start from where the simulation has him?

The evidence answers it. It is not a product call.

- **"Start from where the simulation has him" is ruled out for baked plans.** Where Rook is at 1:30
  is the result of 90 s of simulation. It is not a plan-time fact, and it changes whenever anything
  upstream changes. A seek to it does not even equal a play today (ADR-700's open residuals, being
  fixed separately). A performance keyed from it would bake live state into content that claims to
  be deterministic, which spec §1.3 forbids. Worse, the first upstream edit would silently break its
  continuity.
- **"Pause and snap back" is ruled out by the engine's own handoff.** A released body continues from
  where it was put (probe 7). Snapping back would need the performance to discard its own result.

**Decision:**
- **Start.** At the performance's first instant, the character **yields his body** to the
  performance, at the stage mark the plan chose. The compiler places a performance's start **at a
  cut** (a plan shot's start), where cinema conventionally cheats continuity. The validator warns
  when it cannot, because the jump from wherever the simulation had him would be visible (Slice 2
  compiler).
- **During.** His behaviours are paused *by yielding*: `driven`, keeping their state.
- **End.** He is **handed back at the performance's end pose** and carries on from there.

This is pure in time, the same on a play and on every seek, and it needs nothing from the simulation
at plan time.

## Decision: the mechanism

- **An actor on a node an entity drives *is* a scripted performance.** No new persisted type: the
  Director compiles performances to ordinary `seq::Actor`s (spec §24), editable in the Actors lane.
  - `Engine::installSequence` finds such actors, and `BakeOptions::performerNodes` stops the bake
    writing their position, rotation or scale tracks. Clip cues and visibility still apply.
  - `seq::performerFor` turns the actor into a `Composition::Performer`: its span (first to last key
    or path time), a pure pose function (position; speed from the path's own velocity; heading from
    explicit rotation keys, else the direction of travel, else the last direction it travelled in),
    and a signature.
- **The composition applies performers after the staging director**, as a `DirectorMotion`, at the
  same point of the step:
  - on a play;
  - in the staging replay;
  - in the director-less replay, which now gets a `before` hook when there are performers (so every
    body is replayed deep, as with any director).

  The body is released on the step its span ends, a pure function of `(now, dt)`. The signatures
  are mixed into `replayInputKey`, so a changed performance drops stale checkpoints (tested).
- **The authored performance outranks the live stage director** during its span: it is applied
  after staging. A scenario that also claims the same character during that span is a conflict for
  the Director's validator to report (open item).
- **`DirectorMotion::performance`** marks a motion as a performance's, which owns the body outright.
  Staging's motions do not claim that, and are left exactly as they were:
  - **Re-asserted after the behaviours** (`directorAfter`), so no behaviour that moves the body
    itself (simulation-authority `orbit`, `explore`'s airborne hop) can overwrite it on the step.
  - **Its speed is read by the gait unramped.** Through ADR-620's acceleration limit, a run-in at
    6 m/s read as a **walk** for its first second, because Rook's 4.8 m/s² ramp had not reached his
    5.13 m/s run threshold, and his legs lagged his body.
  - **Its height comes from the terrain** (`performerFor`'s ground query, the same `surfaceAt` the
    bake uses for camera clearance). A directed body is airborne to its `ground` behaviour.
- **A first attempt was reverted.** It made `orbit` yield to `driven`, and it broke the autonomy
  demo's saucer (`test_phase_d_autonomy`): the action tier sets `driven` for *any* action,
  including the saucer's `wait` timers, so `driven` means "the action tier is active", not
  "someone owns the body". My scan of shipped scenes had missed the saucer, because it writes
  `authority` at the behaviour's top level. The ownership is now scoped to performances, and staging
  and actions are untouched.

## Consequences

- On the benchmark:
  - Rook runs a 12 m, 2 s performance and is drawn exactly on its path (xz; the `ground` behaviour
    keeps deciding his height);
  - he is in his run gait, facing the way he runs;
  - ten frames after its end he is within ten frames of running from its end mark.
- A seek into the span lands the body exactly where the actor says. That is checked against the
  actor, a pure function, never against a play: seek≠play is a known open engine defect, and this
  does not depend on it or work around it.
- **Behaviour change:** an actor on an entity's node no longer bakes position tracks and no longer
  sums with the simulation. No shipped project has such an actor (checked:
  `examples/camera/behaviors.json` and `examples/city/night-shift.json` put actors on plain nodes).
  Actors on plain nodes bake exactly as before (tested).
- A performance now overrides `explore`'s airborne hop (it is re-asserted after it).
- **Found in passing:** `seq::Actor::headingAt` returns **degrees**, while its header says radians.
  `performerFor` computes its own radians and does not use it; the comment is left for the
  sequencer's owner.

## Addendum (2026-09-24): ownership and the entry blend (coordinator's rulings)

- **The mechanism moves to the Motion lead as its M1.** The entity, scene and seq parts of this ADR
  (`DirectorMotion::performance`, the composition's performers, the bake skip, `seq::performerFor`)
  are cherry-picked into `agent/motion`, and `tests/unit/test_directing_handoff.cpp` becomes M1's
  acceptance suite.
  - **Merge order:** M1 lands on main first. `agent/director` is then rebased onto it and drops its
    copy. There is one mechanism, never two.
  - **Done 2026-09-25:** `agent/motion` (M1-M5) was merged into `agent/director`, and this branch's
    copy was dropped in favour of the Motion lead's version (ADR-820-823 build on it).
- **Entry blend.** M1 adds a per-actor `entrySeconds`. The default, 0, means the body is taken at
  the authored mark instantly; the Director uses 0 at cuts. A non-zero blend starts from wherever
  the simulation had the body, which is not a plan-time fact. The Director's validator must
  therefore flag it as live-dependent in a baked plan. This check is added when the field exists on
  main.
- **Also accepted into M1:** the gait takes the clip from the path speed, unramped, when no clip cue
  is active; and the height comes from the terrain while grounded.
- `seq::Actor::headingAt`'s degrees-versus-radians mismatch is passed to the Motion lead.
