# ADR-800: The instant a seek lands on is simulated once

**Status:** Accepted
**Date:** 2026-09-24
**Related:** ADR-700 (seek checkpoints), ADR-671 (a scrub replays the director), ADR-521 (the first
frame of a render has no elapsed time), ADR-186 and ADR-191 (the live and offline detail limits),
ADR-703 (the Wave 1 film scrub case that found it)
**Implemented by:** `Composition::updateBehaviour` and `Composition::seekWithDirector`
(`seekLandedSeconds_`)
**Tests:** `tests/integration/test_engine_seek_parity.cpp`

---

## Context

`test_glowmere_scrub.cpp` holds a scrub of the Glowmere film to exactly zero against a play. That
harness drives a `Composition` directly. Wave 1's UFO stack case went through an `Engine` instead,
and there a scrub to 150 s put the saucer 11.6 m from where a play put it.

Each candidate was measured with the others removed. The gap has three causes:

| Cause | What it moved | Removed by |
|---|---|---|
| 1. The live entity distance cull (a play culls far bodies, the replay never does) | the saucer, 12 m at 150 s | `entityDistanceCull = false` |
| 2. Audio-reactive world events and behaviours (the replay has no signal bus) | Vane, 92 m by 90 s | removing the audio |
| 3. **The landing instant evaluated twice** | the saucer, 0.03 m at 1 s, then 0.35 m at 150 s, and Ember 70 m | this ADR |

Modulation routes, the camera timeline and the song plan's section actions were each ruled out:
the numbers were identical to the centimetre with them removed.

Cause 1 is the preview's documented trade. Offline renders already lift it (ADR-191). Cause 2 is
open; the next section says why it is not this ADR.

### Cause 3

`Engine::loadProject` ends in `seekSeconds(0)`, and ADR-671's replay runs the director at t = 0.
The application's first frame is then *also* at t = 0 (ADR-521), and it ran the director again. A
render does the same thing at its start second: `RenderJob` seeks to the start, and its clock's
first tick is at the start.

The director log shows it plainly:

- the load's replay step entered the saucer's `acquire` beat at t = 0;
- the first frame, at the same t = 0, entered `approach`;
- a scrub's replay entered `approach` at t = 1/60, one step after `acquire`, as a play from a
  cold start does.

Every beat after that ran a frame early in the play. The saucer was a frame ahead of its scrubbed
self for the rest of the film, and the aliens that watch it inherited the difference.

## Decision

**A frame at the instant a seek has just landed on is that instant again, not the next one, and the
director does not run in it.**

- `seekWithDirector` records the instant it landed on.
- `updateBehaviour` skips `Staging::update` and `raiseDirectorBeats` for a frame at exactly that
  instant.
- The first frame at any other instant clears the record, so returning to the same second later is
  an ordinary frame.

Nothing is lost by skipping. The director writes parameter **bases** (`writeParameter` ends in
`setBaseComponent`), which the frame's `resetFinals` does not touch, so the landing step's writes
are still in force. The entity pass is left alone: at dt = 0 it is already idempotent, as measured
(every body exactly equal once the director stopped double-stepping), and skipping it would
also skip the pose chain a render's first frame draws.

## Consequences

- Through an `Engine` with the cull lifted and no audio, a scrub equals a play for every body in
  the film, exactly, at 1, 2, 3, 5, 10, 30, 60, 90, 120 and 150 s. Before this, the gap was 0.03 m
  at 1 s and 70 m (Ember) at 150 s.
- A render that starts mid-film now takes the director where a render from zero has it. ADR-671's
  "render from mid-film" harness never saw this, because it ticks at start + 1/60 after its seek.
  The real `RenderJob` ticks at the start itself.
- A paused editor that redraws the landed second repeatedly no longer advances the director with
  each redraw.

### Not fixed here: cause 2

The replay passes a null signal bus, so audio-driven world events (`audio.beat`, `audio.onset`,
`music.impact` …) and behaviours that read signals fire in a play and never in a scrub. Offline,
the analysis track is a pure function of time, so the replay *could* rebuild the bus at each step.
That is a larger change, to the replay's inputs and the checkpoint key, and it is a separate
decision. The hidden case `[.known-defect][adr800]` in the test file records it.
