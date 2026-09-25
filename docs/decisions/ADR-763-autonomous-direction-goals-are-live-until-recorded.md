# ADR-763: Autonomous direction — a goal is live until it is recorded

**Status:** Accepted. The goal compiler and recording are built on the Motion lead's ADR-824/828;
directed mode and event-driven proposals are not.
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

## Implemented (2026-09-25)

The Motion lead built the engine side as ADR-824 and ADR-828.

- **Goal compile (ce3a740e):**
  - `go_to` and `inspect` beats each become one `CharacterGoal` event. Each beat's `emits` becomes a
    named live listener: `ActionComplete` on `goal.arrived` or `goal.done`, for that character,
    after the goal's second.
  - The validator:
    - refuses a goal in a baked plan;
    - needs a goal slot on the card (`goalSlot`);
    - warns when the entity world's own path provider finds no walking route from the character's
      mark to the place (what made a goal to Umbra do nothing);
    - refuses a cue on a live event, or slow motion on a goal, until the performance is recorded.
  - A goal and another performance of the same character are a `TIMING_CONFLICT`.
  - **Played:** Rook, told at 5 s to go to the Lantern, reports arrival at 14.4 s, 3.9 m from it.
    On his own he was 19.4 m away at that moment.
- **Recording (`app::recordLivePerformances`, `src/app/directing_record.*`):**
  - It plays a scratch copy from zero, with no live control, no audio and the cull lifted. It keeps
    the body every frame (Linear keys every 0.1 s, yaw unwrapped), its clip changes (clip cues from
    `Composition::clipReadout`), any airborne span, and the times its events were heard.
  - Each live performance gains a `recording`, and the plan becomes baked once every live
    performance is recorded. A recorded performance compiles verbatim as a performer. Its events
    become markers at their recorded times, and cues on them bake there.
  - It then checks itself on fresh scratch copies:
    - the recorded plan played back must put the body on every key;
    - a scrub into the recording must land where the play did, for every entity.
  - **Measured (benchmark, one 10.4 s goal):**

    | Step | Time | Result |
    |---|---|---|
    | Recording | 5.0 s | 106 keys, 2 clip cues, the arrival at its time |
    | Checks | 9.5 s | played back 0.0000 m from the recording; scrubbed 0.0000 m from the play |

    Removing the scrub copy's install turns the scrub check red, and not hearing the events turns
    the event check red.
- **Not done:**
  - directed mode (Director-tier action lists);
  - event-driven proposals and runtime candidate shots;
  - a way to start recording from the panel or an AI tool. The AI layer is in the core library
    and the recorder is an application source, so a tool needs a host hook. Recording is reachable
    from code and tests only.
