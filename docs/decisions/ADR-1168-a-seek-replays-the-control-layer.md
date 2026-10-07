# ADR-1168: A seek replays the control layer

- Status: Accepted (proto/digital-mosh)
- Extends ADR-870 (a seek replays the signal pipeline) and ADR-901 (a seek replays the routes on pure-in-time sources).
- Found by: DIGITAL MOSH. Its flying camera's position along its path is a bounded integral (ADR-1161) of a macro that
  the scene states' presets set, plus the music's energy, which an interpret source derives. An offline render of a
  range, a scrub, or a still rendered mid-song framed a different shot from the same second of a full render.

## Problem

The pieces of a project's control layer that a seek did not reproduce were:

- **The scene state machine.** It was reset, not replayed.
- **The macros the states' presets set.**
- **Interpret and macro sources.** They are not "pure in time", so ADR-901 did not replay their routes.
- **Every route reading them.** Integrals, followers and envelopes were reset.

So any state the music had driven since 0 s was lost: which stage the song was in, a follower's level, an integral's
sum.

## Decision

Offline, with an analysed track, a project that has scene states replays its control layer on every seek
(`Engine::replayControl`). It runs from frame 0 to the target, on the play's own grid (the render rate, frame k at
k / fps, the first with dt 0). Each step does what a play does:

1. A fresh signal replay rebuilds the bus the play saw at that frame.
2. The sources update.
3. The state machine steps.
4. The finals reset.
5. Every route applies.

The landing update that follows a seek runs at dt 0, so it re-derives the outputs without stepping anything again.
Scenes, entities and the timeline are placed by the existing seek (ADR-700/870), not by this replay.

## Consequences

- An offline render of `--range a:b`, a still, and a scrub land where a play from 0 does, for the states and
  everything they drive.
- Test: `A seek lands the scene states, the macros and an integrating camera route where a play does`
  (`[adr1168]`). It runs at three instants across several state changes, and checks a bounded integral of a macro set
  by a state's preset, an integral and a follower of an interpret source, the state index and the fov, landing and
  one frame on.
- **Cost**, measured on DIGITAL MOSH (`[.bench][adr1168]`, M2 Max), as total seek time:

  | Seek to | Without the replay | With it, first version | With it, now |
  |---|---|---|---|
  | 30 s | 20 ms | 883 ms | 28 ms |
  | 100 s | 47 ms | 2,911 ms | 90 ms |
  | 200 s | 67 ms | 4,328 ms | 184 ms |

  The replay's own share at 200 s is 48 ms. The rest of the extra time is outside it, and was not traced.

  The first version's cost was the state machine's transition blend. Every frame of a transition it captured
  and re-applied all ~4,000 parameters, through a set of path strings, at 0.7 ms a frame. That is also what the play
  pays. Two changes fixed it:
  - **The blend.** A transition now blends only what its target preset names, from the start values captured when
    it began, with the parameters resolved once per transition. A parameter the target does not name keeps whatever
    its base is.
  - **The resets.** The replay resets only the finals it can move: route targets, macros and source settings.

  The cost is under the 100 ms budget, so no checkpoints were added. A project without scene states does not run the
  replay (tested).
- **Live input is not replayed.** It cannot be: the music it heard is gone. A live seek resets the control layer, as
  before.
- The signal replay inside it no longer advances the pure-in-time routes itself (they would integrate twice).
