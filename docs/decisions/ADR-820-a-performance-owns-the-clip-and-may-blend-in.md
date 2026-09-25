# ADR-820: A performance owns the body's clip as well as its place, and may blend in

**Status:** Accepted
**Date:** 2026-09-24
**Related:** ADR-758 (the handoff this extends, adopted from `agent/director` as the one mechanism),
ADR-700 (seek from checkpoints), ADR-096 (an action's activity outranks the gait's), ADR-089 (clip
cues are applied, not baked)
**Implemented by:** `seq::Actor::entrySeconds`; `Composition::Performer::entrySeconds` and
`PerformerPose::clipOwned`; `entity::PerformanceEntry` (entity state);
`DirectorMotion::clipOwned` → `LocomotionState::clipOwned`; `EntityWorld::performing`;
`seq::applyAnimation`'s span gate; `seq::performerFor(…, scheduled)`
**Tests:** `tests/unit/test_motion_handoff.cpp` (`[motion][handoff]`), with ADR-758's
`test_directing_handoff.cpp` as the acceptance suite

## Context

ADR-758 made an actor on an entity's node a performance: it holds the body's **place** for its
span and hands it back where it ends. The lead ruled that the motion stack owns that mechanism
and asked for three additions agreed with the Director. Checking the rig, not only the place,
found a fourth.

1. **The decider's clip beat the performance.** On Glowmere, Rook was held on a 6 m/s path
   and his gait read Run, yet the rig was playing `Idle`. His decider had a Routine `observe`
   action pending, and "an action's activity first, the gait's second" (ADR-096) applies to any
   action, including one the performance has made meaningless. ADR-758's tests assert
   position, so nothing caught it.
2. **A performer's clip cues outlived it.** `applyAnimation` applies an actor's latest cue at
   every later second, so a jump cued inside a run kept Rook's rig in `Jumping` for the rest of
   the film after he was released.
3. **Two writers on one rig.** Inside a span the gait pushed its clip every frame, and the
   sequencer overwrote it later in the same frame.
4. **No entry blend.** ADR-758 rules out starting "from where the simulation has him" *for a
   baked plan*, because a live position is not a plan-time fact. The Director still wants the
   option, flagged as live-dependent by its validator.

## Decision

- **A performance owns the clip.**
  - While a body is performed, the action tier's activity does not reach the rig, on the play
    path and in the replay (`EntityWorld::performing`).
  - When the actor names a clip, by its own cue or by a clip the install scheduled for it, the
    sequencer owns the state machine and the gait's push yields (`clipOwned`).
  - When the actor names no clip, the gait picks from the path's unramped speed, as ADR-758 said.
- **A performer's cues last as long as the performance.** `applyAnimation` skips a performer's
  cue outside `[from, to)`. An actor with clips and no span is not a performer, and its cues
  apply as they always have.
- **`entrySeconds` is per actor, and 0 is the default.**
  - 0 takes the body at the authored mark on the span's first step, exactly ADR-758.
  - A value above 0 blends from where the simulation had the body on that step, by a smoothstep
    in position and the shortest arc in yaw.
  - The entry point is **entity state** (`PerformanceEntry`), captured on the first step inside
    the span and cleared at release. A checkpoint therefore carries it, and a seek into the blend
    lands where a play does, whether it replays from zero or restores a checkpoint inside the
    span.
  - It is written to the project only when non-zero, and a negative value is refused.
- **Height from the terrain while grounded:** unchanged from ADR-758, where every performance is
  grounded. The airborne exception belongs to the jump work (M4), where an arc must not be
  flattened.
- **`Actor::headingAt` returns degrees.** The bake writes it into `rotation`, which is in degrees.
  The header said radians and has been corrected to match the call sites.

## Consequences

- A blended entry depends on live state. That is by design: the Director's validator flags any
  non-zero `entrySeconds` as live-dependent, and a baked plan keeps 0.
- The smoothstep's peak speed is 1.5 × distance / `entrySeconds`. A short blend over a long gap
  is fast: 11 m in 0.5 s peaks at 33 m/s. The Director should keep the gap small or the blend
  long. The engine doesn't cap it, because a cap would hide the choice.
- **Not fixed here:** `Actor::positionAt` interpolates `Smooth` keys with a per-segment
  smoothstep, while the baked track uses a clamped Catmull-Rom.
  - For a performer, `positionAt` *is* the drawn place, so the body and a camera aimed at it
    agree.
  - For a non-performer actor with three or more `Smooth` keys, the camera aims at a place the
    node is not quite at.
  - A performance on such keys would come to a stop at each interior key. The Director compiles
    `Linear` keys, where the two agree exactly.
  - Recorded as a known defect. Changing it moves every existing camera that looks at a
    smoothly keyed actor.
