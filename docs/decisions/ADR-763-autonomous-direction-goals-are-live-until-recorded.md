# ADR-763: Autonomous direction — a goal is live until it is recorded

**Status:** Proposed (Slice 4 research; the interface is requested from the Motion lead through the
coordinator, and nothing has been built yet)
**Date:** 2026-09-25
**Related:**
- ADR-091 and ADR-098 (the determinism tiers);
- ADR-245 (camera precedence);
- ADR-671, ADR-700 and ADR-800 (replay, checkpoints, seek parity);
- ADR-758 and ADR-820 (performers);
- ADR-761 (scripted airborne);
- spec §1.3, §54.

## Context: what exists (surveyed 2026-09-25)

- **Plan side.** A plan already carries `mode: scripted | directed | goal` and `tier: baked |
  directed | goal`. The validator refuses a non-scripted performance in a baked plan
  (`NON_DETERMINISTIC`), and marks every non-scripted mode `UNSUPPORTED (Slice 4)`.
- **The goal considerer.** It exists (`GoalConsiderer`, entity/decision.*). Its fields are subject,
  affordance, intent, from/until, approach, dwell, activity and weight.
  - It is **authored JSON only**: there is no runtime or timeline way to issue a goal.
  - It runs on the `Routine` tier.
  - Its actions set no `onComplete`, so **nothing tells anyone that the character arrived**.
- **Director-tier actions.** These exist (`EntityWorld::direct`, `Authority::Director`), but the
  only way in is `SequenceEvent{EntityAction}`: one verb per event, and `FiredEvent::restored` is
  ignored.
- **Live triggers have no producer.** `ActionComplete`, `InteractionComplete`, `VolumeEnter` and
  `VolumeExit` are live trigger kinds, and nothing in the engine calls `EventDispatcher::post`.
  `WorldEvent`s are raised (action completions, audio profiles, director beats), but they are not
  bridged to the sequence's triggers or to plan event names.
- **Nothing records a live run into content.** ADR-091 and ADR-098 rejected recording the whole
  world. A scrub replays the staging director and the entities from checkpoints at 1/60 s. It does
  not replay modulation or audio: audio-reactive divergence is ADR-800's open "cause 2", and the
  seek-audio work is ADR-870's range.

## Decision (proposed)

1. **Three compile targets, one per mode.**
   - `scripted`: a `seq::Actor`. This is the baked tier, as today.
   - `goal`: a **`CharacterGoal` timeline event**. This is the Motion lead's F7: the plan's
     from/until, subject and activity, compiled onto the existing `goal` considerer. It is not
     written into the entity's authored behaviour, so it is sequence content, owned by the plan,
     and undone with it.
   - `directed`: Director-tier action lists through scheduled events. This comes after `goal`,
     because it needs multi-verb events and `restored` handling first.
2. **§1.3: live output is never labelled baked.** A goal or directed performance compiles only in
   a plan whose tier is `goal` or `directed`. Its effects are reported as live in the diff.
3. **Recording is the only route from live to baked.**
   - "Record" plays a scratch session (ADR-753) from zero, audio off, with the cull lifted. While
     it plays, it samples the performer's body and clip into `seq::Actor` keys and clip cues, and
     the character's semantic events into plan events.
   - The result is a **scripted** performance, the same thing ADR-758/761 already bake, scrub and
     render. The plan records the source mode and the recording's inputs.
   - **Replay validation is part of recording.** The recorded actor is played and scrubbed at its
     event times, the ADR-800 way (drawn positions equal, 0 m). A plan that reads an audio-reactive
     signal is refused for recording until ADR-870 lands.
4. **Live events become plan events.** A bridge posts a character's semantic events (`arrived`,
   `interacted`, `jump.peak`, ...) as named sequence events, `<entity>.<event>`, at simulation
   time. It posts them in the replay as well as in play, so a recording captures them.
   - A cue `on` such an event in a live plan is a warning, as today.
   - After recording, it is an ordinary baked cue on a computed marker.
5. **Precedence stays as it is, and is stated.**
   - For a body: performer, then Director tier, then `goal` (Routine), then behaviours.
   - For the frame: a locked authored shot, then an event camera, then an authored shot.
   - The validator adds a conflict check: a goal and a performance on the same character with
     overlapping spans is `TIMING_CONFLICT`, because the performer wins silently.
   - Runtime candidate shots (event cameras proposing cuts) stay the camera director's business.

## Interface requested (through the coordinator; the Director builds none of it)

1. `CharacterGoal` event (F7):
   - fields: entity, subject (place or entity), affordance?, intent, activity, approach, dwell,
     from/until;
   - **scheduled**: a seek restores the standing goal at the landing instant, and the replay
     applies it.
2. Semantic character events: goal `arrived` / `done` (and action completions), with the
   simulation time. They must be raised identically in play and in the `seekWithDirector` replay.
3. A read-only per-entity **clip readout** (state name, clip seconds, speed, and whether it is a
   one-shot), so a recording can write faithful clip cues without touching motion internals.
4. Confirmation that the goal considerer's state is part of the checkpoint (ADR-700). If it is, a
   pure-of-time goal replays.
5. Separately: `Engine::applySectionActions` ignores `FiredEvent::restored`, which ADR-098 says it
   must not.

## Consequences

- A Director can say "Rook wanders to Umbra and reacts when he gets there" as a live goal, preview
  it, and bake it by recording. It is never labelled reproducible before it is.
- Recording costs a play from zero. On the benchmark that is about 1–1.6 s of simulation per
  minute of film (§37's seek numbers), so recording runs as a job, not on a click.
