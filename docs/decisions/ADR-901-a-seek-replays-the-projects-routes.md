# ADR-901: A seek replays the project's routes, so a delay, an attack or a decay lands where a play does

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-870 (a seek replays the signal bus), ADR-700 (seek checkpoints), ADR-091 (two-tier
determinism), ADR-900 (the delay stage)
**Implemented by:** `Engine::ReplaySignals` -- `begin`, `prepareRoutes`, `advanceRoutes`, and the
route states in its `capture`/`restore`/`inputKey` (`src/app/engine.cpp`); `Engine::seekSeconds`'s route-only
replay offline without a track; `Source::pureInTime`/`Source::sample` and `SourceRack::sample`
(`src/signals/source.*`); `Modulator::advanceChains`/`chainStates`/`restoreChainStates`/
`resetChainStates` (`src/params/modulation.*`); the delay and depth fields in the reaction routes'
checkpoint key (`withSignalKey`, `src/scene/composition.cpp`)
**Tests:** `tests/integration/test_route_seek.cpp` (`[adr901]`)

---

## Context

ADR-870 made a seek's replay rebuild the signal bus and apply the entities' reaction routes on it,
and wrote down what it left out: "Routes that aren't reactions, and timeline automation ... Their
envelopes are still reset by a seek." A reset route snaps its smoothing to the frame it lands on, so
a render started mid-film showed every attack, decay and envelope wrong for up to a few seconds.

ADR-900 makes that worse and makes it matter: a delay line is history by definition. A seek that
reset it would land a delayed route on its pre-history hold for its whole delay, and the brief says
flatly that delay and depth must be seek-exact. Depth is stateless and follows its signal. Delay
needs the history.

The offline renders this is for are the evaluator's: GV3's quality loop renders excerpts
(`--range a:b`), and each excerpt starts with a seek.

## Decision

**A seek's replay runs the project's routes too.** Offline with an analysed track, every replayed
step rebuilds the signals a replayable route reads and advances its chain on them, without writing its
target. The chain states ride in the replay's host checkpoint. The live pipeline continues from them,
as it continues from the replayed bus.

- **Which routes.** Enabled, bound routes that are not reactions (the composition already advances
  and applies those, ADR-870) and whose source the replay can rebuild: a signal on the replay's own
  bus (the deterministic prefix: `audio.*`, `time.*`, `beat.*`, `music.*`, and whatever is added to
  `declareFrameSignals`), or an output of a **pure-in-time source** -- LFO, noise and timeline
  (value or event mode), which publish a function of the context and their settings with no carried
  state (`Source::pureInTime`). Envelope and random sources integrate triggers, control and macro
  sources are live input, and `state.*`, `entity.*`, `character.*` and `field.*` are not functions of
  time: a route on one of them is not replayed and is reset by the seek, as before.
- **What a replayed step does for the routes.** It copies the replay's bus onto a scratch bus laid out
  like the engine's (so every id a route and a source already hold is valid on it), asks every
  pure-in-time source what it published at the step's instant (`SourceRack::sample`, with the beat
  clock the step just advanced, so a beat-synced LFO agrees), and runs every replayed route's chain
  on it (`Modulator::advanceChains`). Targets are not written: the entity replay reads finals as
  ADR-870 defines them (bases plus reactions), and that does not change.
- **Sources read their parameters' bases.** A source reads its parameters' finals, which a play
  rebuilds from the bases at every frame -- after the frame's sources have run. So a value a load or an
  edit has just set is in the base while the final still holds the old one, and the first test of this
  ADR caught the replay sampling an LFO at its default rate after `setBase`. When a replay begins the
  finals of every `sources/*` parameter are reset to their bases: what every played frame after the
  first reads. (The first frame after a load still reads the stale finals in a play; that is recorded
  as a defect.)
- **Checkpoints.** The replay's `Checkpoint` carries the replayed routes' chain states (delay lines
  included) and counts their bytes. The key mixes in every replayed route's source, target,
  component, polarity and chain, and every pure source's settings, so an edited route or score drops
  the checkpoints its states were recorded under. A project whose routes all read non-replayable
  sources keeps exactly the key it had.
- **The landing frame.** The replay's last step is the landed instant, and the frame at that instant
  after it runs with dt = 0: a one-pole does not move, and ADR-900's delay stage re-emits what it
  emitted there. So the landing frame shows what a play showed on that frame, and the next frame
  continues from it.
- **A landing is only good for the seek that made it.** The seek resets every route's chain in the
  modulator before it replays, so the replay cannot keep a landing across seeks the way ADR-870's
  bus-only replay could (its state was all its own). `ReplaySignals::begin` forgets where the last
  seek landed. With a composition nothing changes: `seekWithDirector` resets the replay first and
  the entity replay always replays at least one step. Without one, a second seek to the instant the
  last one landed on now replays again, where it used to reuse the landing. The first version reused
  it and left every replayed route reset. A render job seeks twice to its first frame (its pipeline
  warm-up, then for real), so its frames differed from a single seek's: the GPU suite's
  ring-versus-synchronous test had all 20 frame hashes wrong and its EXR test 1,740 mismatched
  channels.
- **Offline without a track** there is no signal replay (ADR-870's scope, unchanged). If any route is
  replayable the seek runs the routes on the pipeline alone from zero -- silence, the clock, the pure
  sources -- and leaves the live pipeline reset as it always was. There is no checkpoint on this path;
  it costs one pass over the piece at 60 Hz.
- **Live mode is not replayed.** Its audio is a real-time analysis of the device, which is not a
  function of time; a scrub resets the routes as before.

Rejected: applying the replayed routes to the finals during the replay. It would make the entity
replay read what a play's bodies read, which is closer to a play, and it would move every body in
every film whose project routes reach an entity parameter -- a different decision with a different
blast radius (ADR-870 ruled the project's routes out of the entity replay after measuring them).

## Consequences

- **A render that starts mid-film opens with its routes where a render from zero has them.** Before,
  every route with an attack, a decay or an envelope snapped on the first frame; now it carries the
  history. Renders from zero are unchanged (the replay's one step at 0 is the play's first frame).
  This affects every project with a smoothed route on an audio, beat, music, LFO, noise or timeline
  source -- which is nearly every project in the repository -- but only in renders that do not start
  at 0 and only in the first seconds after the seek.
- **Measured exact** (`test_route_seek.cpp`), every target compared with `==` against a play from
  zero at 60 fps, on the landing frame and the frames after it:
  - through the signal pipeline alone (no composition), a delayed onset route, a delayed event-mode
    timeline route, a 1.5 s decay, a depth-scaled route and a delayed LFO;
  - through the entity replay and its checkpoints: a first seek to 4.5 s (replayed from zero,
    recording a checkpoint a second), then to 7.25 s (resumed from the 4 s checkpoint) and back to
    2 s (resumed from the 1 s checkpoint, whose delay lines carry the history across it);
  - offline without audio: the delayed LFO and timeline routes;
  - a second and a third seek to the same instant, with a landing frame between them and then with
    two played frames, on each of the three paths (the render job's order). Before the fix, the
    pipeline-alone arm failed 8 of its 82 assertions: the four stateful routes, on the second and
    third landings.
  - **Controls:** the same seek followed by `Modulator::resetState()` -- the old seek -- lands the
    delayed and decayed routes elsewhere (and the stateless depth route in the same place); an edited
    route after a seek lands where a play of the edited route does, which a kept checkpoint would not.
- **The render job's two paths agree again** (GPU suite, `tests/rendering/test_render_job.cpp`): the
  ring-versus-synchronous test and the EXR determinism test pass with 120 assertions, as on main.
- **Cost: not measurable on the multicam film** (40 project routes; `"[.bench][adr901]"`, the two arms
  interleaved in one process, minima of three, two runs at a load average of 60-130). A cold seek to
  150 s took 2,633 and 2,745 ms with the routes replayed and 3,194 and 3,165 ms with them removed; a
  warm seek to 149.5 s took 7.20 and 7.18 ms against 7.23 and 7.25 ms. The arm without routes was the
  slower one both times, which is an ordering effect, not a saving: a replayed step adds a few
  microseconds against the entity step's milliseconds. The checkpoints grow by the replayed chain
  states -- a `ProcessorChain::State` per route, plus a delay line's samples (16 bytes each, at most
  4 s at the replay's 60 Hz) -- counted in `Checkpoint::bytes`.
- **Cost with no composition** (no checkpoints; every seek replays the pipeline from zero, as
  ADR-870's did): **5.0 ms** to 170 s on a three-minute track with the orb scene's eight routes, and
  **4.8 ms** for a second seek to the same instant, which used to cost nothing. Measured by the
  hidden case `the cost of a seek's route replay with no composition` (minima of three), which also
  checks that the replay consumed the track's analysis to the target (14,641 frames).
- **Not replayed, as before:** routes on envelope, random, control, macro, state, entity, character
  and field signals; timeline automation (it is a pure function of the clock and needs no replay); a
  source's parameters modulated by a route or a track (the replay samples a source with its bases).
- **Other frame rates.** Routes are replayed on the 60 Hz grid, as the entities and the beat clock
  already are (ADR-870). At 60 fps the replay and the play see the same instants and the result is bit
  for bit. At 30 fps a delay line holds 60 Hz samples after the seek where a play holds 30 Hz ones, so
  a delayed value is interpolated between different samples (the same value for a signal that is
  linear between them) and a delayed event can land a frame earlier; smoothing converges to the play
  within its time constant (the one-pole is frame-rate independent). GV3's finals are rendered at
  60 fps.
