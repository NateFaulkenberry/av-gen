# ADR-216 — The Sequencer is where the song lives

**Status:** accepted · 2026-09-15
**Builds on:** ADR-102 (one transport), ADR-103 (the audio lane), ADR-215 (the editable structure)

## Context

Audio had two front doors and two transports. The Control panel carried an "Open Audio" button and a
compact `TransportBar`; the Sequence panel carried the timeline, the waveform, the audio clips and a
full `TransportBar` of its own. A person looking at a timeline had to go to a different panel to put
a song on it.

Meanwhile the sections ADR-215 made editable had nowhere to be edited: they existed as `Marker`s,
which are labels by design and carry no handles.

## Decision

### One front door, three routes to it

The import entry point moves into the Sequencer toolbar. The File menu item and the `O` shortcut
stay, wired to the **same callback** — `panel_->sequence.onOpenAudio = panel_->onOpenAudio` — because
a menu item and a keyboard shortcut are not duplicates of each other: one is discoverable and one is
fast. Three routes, one action, no chance of them coming to mean different things.

The Control section's compact transport is deleted. `TransportBar` was already a shared widget, so
this was removing a *call*, not rewriting a control.

**What must not come back with that button is the assumption that went with it.** The Control
transport was once disabled whenever no audio was loaded, and ADR-102 removed exactly that: a project
without audio has a transport like any other. The remaining Control controls are gated on `hasAudio`
only where that is real — the volume, which has nothing to be the volume of. The seek slider and the
Sequence panel's transport are not gated on anything, and
`tests/integration/test_transport_engine.cpp`'s "A project with no audio plays, pauses, seeks and
stops" is the test that would fail if they became so.

### Sections are a lane, not markers on the waveform

A lane directly under the ruler and **above** the audio, because the song's shape is what everything
below it is cut to and the two are only comparable when adjacent. Blocks coloured by function, a
boundary line down the whole strip, and a mark on any section a person has touched.

Drawing them *on* the waveform was never considered, because ADR-103 already paid for that: audio
clips drawn on the waveform stole the click that scrubs, and the strip was reported broken within the
hour. The lane geometry lives in `ui_logic.hpp` where the drawing and the hit test read one
description of it, and `test_ui_logic.cpp` checks it without a window.

Boundary dragging has its **own** snap — free, beat or bar — separate from the strip's snap mode,
because a person dragging a section boundary and a person dragging a shot are not necessarily asking
for the same grid. Snapping is optional and off-by-nothing: a snapped boundary takes the beat's *own*
value, bit for bit, and a free one takes the millisecond it landed on. Neither is rounded. Bars are
every fourth beat, the same assumption `BakeOptions::beatsPerBar` states.

### Analysis is a background job

`analysis::detectStructure` on a four-minute track is a self-similarity matrix and several novelty
passes. It runs on the `JobSystem`, never per frame and never on the UI thread, and:

* The engine's `AnalysisTrack` became a `shared_ptr` so the job can hold it while the audio is
  replaced underneath. The alternative was copying tens of megabytes of spectra into the job.
* The result carries the `audioRevision` it was started for and is **discarded** if the audio changed
  while it ran — otherwise opening two files quickly puts one song's sections on the other.
* The revision is claimed when the job *starts*, not when it finishes, or the auto-import trigger
  queues a second analysis of the same audio on the very next frame.
* Progress is the stage name and nothing else. `detectStructure` does not know how far through itself
  it is, and a bar invented in the UI is indistinguishable from a measured one — which is
  `job_system.hpp`'s own rule.

It runs **automatically once**, when audio arrives and the piece has no structure yet. A project
saved with a structure is *not* re-analyzed on open: that is what caching it is for. Re-running is a
button, and re-running merges under ADR-215's policy, so pressing it twice is safe.

### Two checkboxes and no third

The import popup offers `Analyze song structure` and `Generate initial Director sequence`. There is
deliberately no FFT size, no hop length, no confidence threshold and nothing about the Director's
internals: a person importing a song is deciding whether to look at its shape, not configuring a
spectrum analyzer.

### The Director seam, defined and empty

Generation is gated on the Director decision layer, which is being built separately, so
`seq/section_direction.hpp` contains **the shape of the connection and one hole**. A generated event
is:

```
when.kind   = TriggerKind::Section
when.name   = the section's display name (its label, else its function's name) -- the string
              the marker carries, which is why renaming a section re-points the event
when.repeat = 0, every occurrence: this is a table from section kind to behaviour, not a shot list
what.kind   = EventActionKind::EntityAction -- tier 2, SCHEDULED
what.target/value/argument = subject, verb, object, all defined by the Director layer
```

Tier 2 is correct by `seq/events.hpp`'s own rule: the *when* is knowable before the piece runs and
the *what* is imperative. It is not bakeable and must not pretend to be.

`defaultSectionDirectionTable()` declines everything, and that is not a stub. An empty generated
sequence is an honest report that the Director is not wired up; a full one would be a second director
competing with the real one. Every decline is reported by kind — "no direction for `bridge`" — so
filling the table in is a matter of reading the list rather than guessing at it. The checkbox is
present and disabled, with a tooltip that says why, because a checkbox that silently does nothing is
worse than one that explains itself.

## What the Director agent has to provide

One function, matching `seq::SectionDirectionTable`:

    std::optional<SectionDirection> (signals::MusicalSection)

returning, per section kind, the subject to act on, the verb, and the verb's object — all three being
strings the Director layer defines. Fifteen kinds; declining any of them is allowed and is reported.
Nothing else changes: the trigger, the tier, the event shape and the generation are already here.

## Consequences

The Control panel keeps the three audio things that are *not* duplicates of anything the Sequencer
has: which file is loaded, the seek slider, and the volume.

Not done: `AnalysisTrack::analyze` itself is still synchronous inside `Engine::loadAudio` (~130 ms for
ninety seconds). That predates this work and is a separate change to the engine's load path; the
structure detection, which is the expensive new thing, is the part made asynchronous here.
