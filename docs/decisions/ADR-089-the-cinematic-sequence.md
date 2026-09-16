# ADR-089: The cinematic sequence — choreography through time

Status: Accepted

## Context

AV Gen could already render a world, analyze a song, animate a camera, pose a character, draw
typography over the frame and export a video. What it could not do was say **when**.

Every one of those systems had its own idea of time. `params::Timeline` keyed parameters.
`app::Sequence` (ADR-062) was a camera shot list. `comp::Layer` had a start and an end.
`scene::AnimationPlayer` had a state and the second it was entered. `entity::Entity` integrated a
delta. There was no object that could be handed a second and answer *what the piece looks like now*,
and so there was no way to author a piece at all: the camera director could cut a sequence to a
track, but nothing could put a character in it, put words over it, and cut to somewhere else.

The brief in `docs/sequencer-spec.md` asks for "the smallest coherent cinematic sequencing toolkit
that lets AV Gen author a real, timed, shot-based audiovisual piece", and then asks for the piece.

## Problem

Four things have to be true at once, and most of the obvious designs get at most three.

1. **Determinism** (spec 33). Frame *N* must be identical whether playback walked there or an
   offline renderer jumped to it. This project already verifies byte-identical captures.
2. **Scrubbing** (spec 30). 10 s → 45 s → 3 s → 30 s must each produce the right frame, with no
   accumulated state to rewind.
3. **Cost** (spec 35). The renderer is already performance-constrained. A sequencing layer that
   costs real time per frame is a sequencing layer that cannot be used on a real scene.
4. **One mechanism** (spec 3). A camera position, a character's transform, a text layer's opacity
   and a scene's fog density must animate through the same machinery, or the toolkit is five
   toolkits.

## Alternatives considered

**A. A per-frame evaluator.** `Sequence::evaluate(t)` walks the shots, the actors and the overlays
every frame and writes values. Simple, and it is what most sequencers do.

**B. Bake to timeline tracks.** `Sequence::bake()` turns shots, actors, scene slots, transitions and
overlay cues into ordinary `params::Track`s, once, when the sequence changes. From then on the
timeline the engine already has does all the work.

**C. A parallel animation system.** A `SequenceTrack` type with its own keys, its own interpolation
and its own binding, sitting beside the timeline.

## Decision

**B: a sequence bakes.**

`seq::Sequence` is a value — shots, scene slots, actors, overlay cues, markers and piece-level
tracks. `bake()` is a pure function from that value to the JSON `params::Timeline::fromJson` reads.
`seq::install()` puts the result on a real timeline, realises the overlay cues as real
`comp::LayerStack` layers, binds, and reports what did not resolve.

This is the same decision ADR-075 made for the camera director, for the same three reasons:

- **Determinism** is free, because evaluation is `Track::evaluate(t)` and nothing else.
- **Scrubbing** is free, because there is no state to rewind: four seeks are four evaluations.
- **Cost** is free, because nothing in `seq/` runs per frame. The per-frame cost of a sequence is
  the cost of the tracks it produced, which is the cost the timeline already had.

And **one mechanism** falls out: a baked shot writes `camera/position`, a baked actor writes
`nodes/walker/position`, a baked lyric writes `layers/7/opacity`, and a shot's own automation writes
whatever the author named. They are the same kind of object and they cannot drift apart in what they
support, because the bake goes through `Timeline::fromJson` rather than constructing tracks directly.

The price is that a bake is a *moment*: editing a shot means re-baking. That is stated rather than
hidden, and it is why the editor bakes on mouse-release rather than on every frame of a drag.

### The one thing that is not baked

A track carries numbers. An animation clip needs a **time origin** — which state, and since when —
and an index into a state list loses the second half of that. So clip cues are not baked.
`Sequence::animationAt(t)` is a pure function returning the active cue per actor, and
`seq::applyAnimation` pushes it into the composition every frame.

That required one change to the composition: `setNodeAnimation` gained a `rebase` flag.
`AnimationPlayer::play()` deliberately does **not** restart a state it is already in — right for a
behaviour calling every frame, wrong for a cue — so without rebasing, a character who walks at 0:12
and walks again at 1:04 would take the second cue with the first cue's phase, and scrubbing
backwards would be worse. The call is idempotent, so per-frame use costs a string compare.

### Scene switching is visibility

A shot names a **scene slot**, and a slot names a composition node. A `kind: "scene"` node already
loads a child scene file and already propagates its visibility through the whole subtree, and
`nodes/<name>/visible` is already a parameter — so a cut is a `Step` key on a boolean. It costs
nothing, needs no streaming, and is exactly as scrub-safe as everything else. Every slot stays
resident, which is the v1 trade spec 36 permits; `SceneSlot::file` records what a slot *is* so a
future preloader need not walk the composition.

Transitions are `Cut`, `FadeIn` and `FadeOut`. A crossfade between two 3D scenes would need both
drawn into separate targets and blended, which is a renderer change for one transition; a dip to
black is what a cutter actually reaches for and it is two keys on `scene/brightness`. Named honestly
rather than called a crossfade.

### A cut is a millisecond

Two shots meet at one second, and `Track::addKey` replaces a key within a microsecond of an existing
one. Written naively, the outgoing shot's final pose and the incoming shot's opening pose become
**one** key, and the outgoing shot spends its whole length gliding toward the next shot's opening
frame. So the last key of a shot that is cut away from lands a millisecond early: short enough that
no frame rate this engine renders at can see the ramp, long enough that both poses survive. A
following shot whose camera is `Inherit` is an author saying "keep going", and is not nudged.

### Derived data is not serialised

A project holds the sequence. It does **not** hold the tracks the bake produced, the layers the sink
made, or those layers' parameter values. Saving both means the next load reads them *and* re-bakes
them, and two tracks writing `camera/position` is not a blend — it is whichever one the timeline
applies second. The file holds what the author wrote; the bake is recreated from the shots it came
from.

## Consequences

- `src/seq/` is GPU-free and lives in `avgen_core`, so the whole model is testable without a device.
  `app/cinematic.cpp` moved into `avgen_core` with it: a shot holds an `app::Shot` by value, and
  `seq/` could not otherwise be tested without linking the editor.
- `app::Engine` holds a `seq::Sequence` by value, installs it on load and on a scene swap, and calls
  `applyAnimation` inside `update()`. A scene swap clears the parameter set, which would otherwise
  leave every baked track correct and bound to nothing — the failure ADR-075 exists to record.
- The editor gains a **Sequence** panel: one horizontal time axis with a lane for shots, a lane per
  actor and a lane for overlays, over a ruler carrying the song's own sections and beats. Dragging a
  block edits the value; releasing it bakes.
- Overlay cues are reconciled with the composition system's own vocabulary. An earlier draft of this
  work invented `offsetX`/`offsetY`, which no layer has; the two halves of the seam disagreed about
  spelling and the presets would have bound to nothing and said nothing. Properties are now spelled
  the composition system's way and split per axis, and a sink returns a path **and a component**.
- Lyrics import from LRC, SRT and WebVTT (`seq/lyrics.hpp`), decided by content rather than by file
  extension.
- An install reports its unresolved targets, and the editor shows them in red. A track naming a
  parameter that does not exist evaluates correctly and writes nowhere, forever, in silence.

## Rejected alternatives

**A, a per-frame evaluator**, was rejected because it makes determinism a property of the evaluator's
own discipline rather than of the architecture. Every frame it would have to decide what a shot's
camera is, what a character's transform is and what a layer's opacity is, and every one of those
decisions is a place where a wall clock, a cached value or an accumulated delta can get in. The
baked design cannot have that bug: after the bake there is no sequencer left to run.

**C, a parallel animation system**, was rejected on spec 3's own grounds. It would have needed its
own interpolation, its own binding, its own serialisation and its own editor, and every feature the
timeline gained afterwards — beat time, track modes, loop lengths, Bézier tangents — would have had
to be added twice or would have silently worked in one place and not the other.

**Baking clip cues into a state-index track** was rejected because the pose needs the second the
state was entered and a track cannot carry it. The alternative — noticing the change at whatever
frame the playhead happened to sample — is precisely the history dependence spec 33 forbids.

## Known limitations

- **Entity behaviours are not scrub-deterministic.** `entity::Entity` integrates `dt` (ADR-088), so
  a wandering character's position depends on how the playhead got there. `EntityWorld::reset()`
  exists and nothing calls it, and even if something did it would return entities to *t = 0*, not to
  the seeked time. A sequence's actors are the deterministic alternative and the proof-of-concept
  uses them; entities remain right for ambience in a realtime piece and wrong for an offline render
  that anyone will scrub.
- A crossfade between two 3D scenes is not supported; the dip is.
- `lookAtWeight` is per shot, not animated. A shot whose *subject changes* has to author its targets,
  which is what the proof-of-concept's reveal does.
- Scene slots are all resident. A piece with twenty environments will pay for twenty environments.

## Revisit triggers

- A piece that needs more environments than fit in memory at once — then the preloader the
  `SceneSlot::file` hook exists for.
- A second author asking for a crossfade rather than a dip, which would mean the renderer change is
  worth it.
- Any future behaviour system that wants to be part of a rendered piece — it would have to be
  evaluable from absolute time first.
