# ADR-870: A seek replays the signal bus, offline

**Status:** Accepted
**Date:** 2026-09-25
**Related:** ADR-800 (cause 2 of three), ADR-700 (seek checkpoints), ADR-671 (a scrub replays the
director), ADR-521 (frame 0 has no elapsed time), ADR-088 (entity reactions are routes), ADR-073
(music.* signals), ADR-012 (the beat clock), ADR-093 (why a seek had a null bus)
**Implemented by:** `Engine::ReplaySignals`, `Engine::consumeAnalysis`, `Engine::advanceClock` and
`SignalClock` (`src/app/engine.*`); `entity::ReplaySignalSource` (`src/entity/entity.hpp`);
`Composition::seekWithDirector`'s `signals` argument; `Modulator::applyRoutesWhere`
**Tests:** `tests/integration/test_engine_seek_parity.cpp`, the cases tagged `[adr870]`

---

## Context

ADR-800 measured three causes of an `Engine` scrub of the Glowmere film missing a play. It fixed
the third and left the second open: **the replay had no signal bus.** With the distance cull lifted
and no audio, scrub equalled play exactly. With the audio on, at 90 s the aliens were off by
92.0 m (Vane), 42.2 m (Rook), 7.5 m (Sage) and 6.9 m (Ember).

Replaying the bus alone did not close it. With a rebuilt bus the replay heard the film's
`audio.beat` spin and `music.impact` behaviours, and Vane was still 92 m off. A diagnostic that
disabled only the routes compiled from entity `reactions` (ADR-088) brought the gap to exactly
zero. The saucer's `reactions` put its height on `audio.bass` and `audio.onset`. A play applies
those routes to the parameter finals before the director runs, and the director and the aliens
read where the saucer is *drawn*. The replay reset the finals and never applied them. ADR-800 had
ruled out "modulation routes", but that test removed the project's routes, not the entities'.

So what a replayed step needs from the signal pipeline has two parts: the bus, and the reaction
routes applied on it.

## Decision

**Offline, with an analysed track, a seek's replay rebuilds at every step the bus a play from zero
built at that instant. It applies the entities' reaction routes on that bus. It carries both in the
seek checkpoints. The live pipeline then continues from the replay's state, not from a reset.**

- **One pipeline, two owners.** The state a frame carries into the next moved into one copyable
  struct, `SignalClock`: the analysis cursor, the last frame, `MusicRuntime`, the beat clock and the
  phrase index. `Engine::update` runs it through `consumeAnalysis` and `advanceClock` on
  `clock_`/`bus_`. `Engine::ReplaySignals` runs the same two functions on its own `SignalClock` and
  bus. No signal code is duplicated. The declaration order is shared too (`declareFrameSignals`),
  so the replay's bus is an id-for-id prefix of the engine's.
- **Layering.** `scene` does not learn about `app`. The composition receives an
  `entity::ReplaySignalSource*`, which offers `reset`, `build(now, dt)`, `capture`, `restore` and
  `inputKey`. Each replayed step does this, in order:
  1. build the bus;
  2. reset the finals;
  3. apply the `fromEntity` routes;
  4. run the director with `ctx.bus` set;
  5. run the performers.

  `EntityWorld::replayStep` then raises the bus's world events (`raiseSignalEvents`) exactly
  where `update` does.
- **The grid.** Steps are k/60, with dt = 0 at step zero (ADR-521). A target between two grid
  instants gets the same short final step as the entities. ADR-800's rule is kept: a frame at the
  landed instant skips the director. The pipeline itself needs no special case, because that frame
  consumes no new analysis frame and advances the beat clock by dt = 0.
- **Checkpoints.** Each host checkpoint now also carries:
  - the replay's `SignalClock`;
  - its bus values (continuous signals persist between analysis frames);
  - the reaction routes' envelope states.

  The key mixes in the audio revision, the track length, the tempo override, the embedded tempo,
  the duration, phrase bars, section phrases and the playing flag. It also mixes in every reaction
  route's definition, so a changed track or an edited reaction drops the set.
- **After the seek.** The engine adopts the replay's `SignalClock` and its non-event bus values.
  The reaction routes' envelopes are already where the replay left them, because the replay ran on
  the modulator's own routes after the seek's `resetState`. If the replay was not exact (no
  composition, or `SeekMode::Window` starting mid-film), the pipeline alone is rerun from zero to
  the target (`ReplaySignals::runTo`).
- **Everywhere else, nothing changes.** This covers live mode (a real-time analysis thread, MIDI,
  OSC, `controlHub_`) and projects with no audio. `seekReplaysSignals()` is false there, so the
  replay passes a null bus, applies no routes and keeps its old key, and the seek resets the
  pipeline as before. For an audio-less scene the checkpoint key is unchanged by construction
  (`withSignalKey` returns its input).

## What is still not replayed

These are not claimed, and the tests compare only what is:

- **Signals that aren't a function of time:** control sources and OSC/MIDI (`sources_`,
  `controlHub_`), the scene states (`state.*`) and the entity-derived signals. They are not on the
  replay's bus, and a reaction routed from one of them is skipped.
- **Routes that aren't reactions,** and timeline automation. The project's own routes target
  materials, particles, effects and the wind, and none of them moves a body in this film. Their
  envelopes are still reset by a seek.
- **A field's spatial gain** (ADR-097). The replay uses the value the field pass last wrote, so a
  scene with music influence fields is not exact yet. The film has none.
- **Frame rates other than 60 Hz.** Replays and checkpoints step at 60 Hz, so a 30 fps render's
  beat clock and onset batching differ, just as its entity steps already did.
- **HIST under reactions.** A director-less scene with a history subscriber records its history
  without the reaction offsets.

## Consequences

Through an `Engine` on the Glowmere film, with the cull lifted and **with audio**, the worst body at
30, 90 and 150 s went from:

| | 30 s | 90 s | 150 s |
|---|---|---|---|
| before (main) | not measured | 92.0 m (Vane) | not measured |
| bus replayed, reactions not | 0.10 m | 92.0 m | 80.0 m (Ember 70 m) |
| after | 0 | 0 | 0 |

"After" means every body exactly equal. The same holds:

- through a checkpoint: a restore at 45 s, then ten steps;
- for 300 played frames after a scrub to 30 s, every frame, with the carried clock and every
  replayed signal equal.

**Cost.** The first seek on this machine, loaded by other agents, took these times:

| Target | With audio | No audio (the path this ADR doesn't touch) |
|---|---|---|
| 30 s | 468 ms | 463 ms |
| 90 s | 1387 ms | 1365 ms |
| 150 s | 2542 ms | 2337 ms |

A warm seek (checkpoints present) took 6-10 ms in both cases. The signal replay adds about 2-9%. A
before/after comparison on this machine is dominated by load: the baseline binary, run earlier,
measured 1194, 3568 and 6967 ms with audio and 2369, 5386 and 7099 ms without.

The replay also changes what a **render that starts mid-film** sees on its first frame. Before, that
frame consumed the whole track from zero in one batch: one merged onset for everything before it,
and a classifier fed the entire track at once. Now the render starts where a play from zero stands.
A render from zero is unchanged, because the first analysis frame lies after t = 0.

Each new test was shown to fail:

- Handing the replay no signal source fails the acceptance case at all three instants (0.10 m at
  30 s, with Ember 6.9 m at 90 s as on main).
- Skipping the checkpoint restore of the signal and reaction state fails the checkpoint case.
- Resetting the live pipeline after a seek, instead of adopting the replay's state, fails the
  play-on case from frame 1805.
