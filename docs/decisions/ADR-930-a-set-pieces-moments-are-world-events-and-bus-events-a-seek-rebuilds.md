# ADR-930: A set piece's moments are world events and bus events, and a seek rebuilds both

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-245 (event cameras), ADR-767 (a watched play's events), ADR-870 (a seek replays the
signal bus), ADR-901 (a seek replays the project's routes), ADR-703 (entity-derived signals lag one
step), ADR-800 (the landing frame is not a second director pass), ADR-922 (a peak goes to its event's
subject), ADR-928 and ADR-929 (set pieces)
**Implemented by:** `Engine::declareStagingSignals`, `stagingEventsBefore`, `publishStagingSignals`,
`stagingStep_`, and the staging arm of `ReplaySignals::prepareRoutes`/`advanceRoutes`
(`src/app/engine.*`); `Composition::raiseDirectorBeats` (unchanged: it already raised every beat as
a world event, in a play and in a seek's replay); `Staging::lastBeats_` for a beat-named `startOn`
(`src/stage/staging.*`)
**Tests:** `tests/unit/test_setpiece_film.cpp` (`[setpiece][film][seek]`: a seek mid-lift of each set
piece lands on the play's frame; a seek rebuilds the play's world events; a route keyed on a set
piece's beam answers the frame after it in a play and after a seek, on the landing frame and half a
second into a decay, with the old seek as the control; a watched play's moments reach Song Mode's
events); `tests/unit/test_setpiece_reach.cpp` (the source picker names them);
`tests/unit/test_staging.cpp` (a scenario started on another's beat)

## Context

A set piece is only useful to the rest of the film if the rest of the film can hear it: an event
camera that cuts to the beam (ADR-245), a cue placed on the lift (ADR-767), a route that dims the
valley's light while the saucer drains it, the aliens looking up, and Song Mode giving the drop to the
saucer because the saucer is what happens there (ADR-922). Each of those keys on a named event, and
each must land on the same frame in a play and after a seek, because GV3's evaluation renders are
excerpts that start with one.

`Composition::raiseDirectorBeats` already raised every beat of every scenario as a world event named
`<scenario>/<beat>`, at the craft's position with the craft as its source, and a seek's replay raises
them identically (the entity world's event record is replayed and checkpointed). So characters,
watchers and event cameras could hear `setpiece/<key>/beam`. The bus could not: routes read bus
signals, and no staging beat was one.

## Decision

**Every beat of every staging scenario is a bus event `<scenario>/<beat>`**, declared in `rebind`
before the routes bind (so a route whose source is `setpiece/field/beam` binds on load), and labelled
for the Modulation panel's source picker: "UFO set piece 'field': beam" (an authored scenario's reads
"scenario 'abduction': beam").

- **It fires the frame after the beat was entered**, the one frame every entity-derived signal lags
  by (ADR-703): the director runs after the frame's routes, so a beat it enters can only reach the
  next frame's.
- **It is read from the entity world's record of world events, never from the director.**
  `stagingEventsBefore(t, step)` returns the staging beats stamped in [t - step, t): the step before
  this frame. That record is exactly what a seek's replay rebuilds, so the same question asked after
  a seek gets the answer a play got.
- **The frame a seek lands on carries its event.** A landing frame has no delta (a render opens with
  `dt = 0` at its first frame, ADR-901), and a paused frame drawn again has none either. Such a frame
  is the same instant as the step before it and carries what that step's frame carried: the window is
  the last step the engine took -- the replay's 1/60 s grid after a seek. The first version skipped
  frames with no delta, so a seek that landed on the frame after the beam lit showed no event where
  the play showed one. Carrying it again at `dt = 0` moves no chain that has consumed it (a one-pole
  and an attack ramp do not advance, a hold restarts at the same instant, and a delay line merges a
  second sample at one instant into the first).
- **A seek replays the routes that key on them** (ADR-901's replay, widened). The staging signals are
  replayable sources: at every replayed step, `advanceRoutes` sets the beats the step before entered,
  read from the same record, which the entity replay has written up to that step (or, replayed alone
  after it with no analysed track, holds the whole replay, and the window picks the same step out). So
  a route with a decay, a hold or a delay on a set piece's beam lands where a play has it -- before
  this it was reset by every seek, as every non-replayable route is.
- **A scenario can start on another's beat without the bus** (ADR-928): the director reads its own
  record of its last frame, which a checkpoint carries.
- **Song Mode reads them by the generic road.** ADR-922 takes a peak's events from a song plan's
  `events` or from any directing plan's watched observation (ADR-767). A watch of a film with set
  pieces records `setpiece/<key>/<moment>` with the craft's entity as the subject, so a peak near a set
  piece opens on the craft when the craft is a hero -- with nothing set-piece-specific in Song Mode.

**Not published into the replay bus itself** (ADR-870's `ReplaySignals::bus`). The entity reaction
routes bind by the live bus's index and that bus shares only the frame-signal prefix with the live
one, so the staging events would have to be declared there too and every reaction's ids re-checked;
the routes that need them are replayed on the engine-shaped scratch bus instead, where every id is
valid, and a scenario's own `startOn` reads its director's record.

**Where an artist finds it:** **Modulation panel -> Routes -> source**: `setpiece/<key>/<moment>  -
UFO set piece '<key>': <moment>`, for every moment of every set piece in the project (and every beat
of every authored scenario). **Director panel -> UFO set pieces** says when each moment is.

## Consequences

- **New signals in every project with staging.** GV2's and GV3's authored `abduction` scenario now
  declares `abduction/<beat>` events on the bus. Nothing reads them unless a route names them; no
  scene's look or behaviour changes, and the bus's frame-signal prefix -- the part a replay mirrors --
  is untouched (the staging signals are declared after it).
- **Routes on staging beats are replayed by a seek.** Before this ADR there were none (the signals did
  not exist), so no existing route changes.
- **Measured on the lab film** (`test_setpiece_film.cpp`, 60 fps): an instant route (amount 0.5) on
  `setpiece/field/beam` is at its base on the beam's own frame, at base + 0.5 on the frame after and
  back at base the frame after that; a decaying route (1 s) is past 0.99 on the frame after and
  between 0.4 and 0.9 half a second later. A seek that lands ON the frame after the beam gives both
  exactly the play's values; a seek half a second into the decay gives the play's decay to 1e-6. The
  control -- the same seek followed by `Modulator::resetState()`, the old seek -- lands the decay more
  than 0.3 away. A seek to 115 s rebuilds exactly the play's `setpiece/*` world events up to that
  instant (the comparison that found ADR-928's step-completion defect: the second abduction's
  `depart` was a frame apart until it was fixed); seeks mid-lift of each of the three set pieces land the craft, all eight animals and the
  beam's visibility, rate, size, brightness and colour on the play's values (bodies to 1e-4 m), and
  the thirty frames after each continue as the play does. A watch of the first 64 s reports
  `setpiece/field/beam` with subject `visitor` at the beam's frame, and `songEventsForEngine` returns it.
- **A generator that directs Song Mode without a watch** writes the set pieces' moments into the song
  plan's `events` itself -- `{"name": "setpiece/<key>/<beat>", "subject": <craft entity>, "seconds":
  <beat time>}` from `avgen_cast_trace`'s `setPieces[].beats` -- which ADR-922 reads the same way.
- **Limits, stated.** The entity world keeps a minute and 128 events (`kEventWindowSeconds`,
  `kEventCapacity`); a step's own beats are always the newest in it, so the landing frame and every
  played frame are exact, but a route whose memory is longer than that window, replayed alone with no
  analysed track, can miss beats older than the window. At other frame rates than 60 the replay's
  1/60 s grid applies, as it does for every replayed route (ADR-901).
