# The sequencer (user guide)

A **sequence** is the whole timed audiovisual performance: which shot is on, where the camera is,
what the character is doing, which words are up, and what the scene's parameters are, at every
second of a song. It is the one thing in this engine that says *when*.

```
SONG ─ analysis ─ sections, beats
                        │
                    SEQUENCE ──── bake ────▶ params::Timeline ──▶ every parameter
                        │                          (camera, nodes, layers, scene)
        ┌───────────────┼───────────────┐
      SHOTS          ACTORS          OVERLAYS
     scene, camera   transform,       text, shapes,
     transitions     clip cues        timing, presets
```

Everything except the clip cues becomes ordinary timeline tracks. That is the whole design, and
[ADR-089](decisions/ADR-089-the-cinematic-sequence.md) is why.

---

## The model

| Concept | What it is | Where it lives |
|---|---|---|
| `seq::Sequence` | the piece: duration, scene slots, shots, actors, overlays, markers, tracks | `src/seq/sequence.hpp` |
| `seq::SceneSlot` | a place the piece can cut to; names a composition node | |
| `seq::Shot` | a span with one visual setup: scene, camera, transitions, its own automation | |
| `seq::Actor` | anything the sequence moves: a character, a vehicle, a prop, a light | |
| `seq::OverlayCue` | a timed, ordered, styled intention: a lyric, a title, a border | `src/seq/layers.hpp` |
| `seq::Marker` | a label: a section, a beat, or a cue an author wants to find again | |

A sequence is a value. Editing it costs nothing; **installing** it is what turns it into tracks and
layers.

```cpp
seq::Sequence piece = /* ... */;
auto report = engine.setSequence(std::move(piece));   // bakes, realises, binds
// later, after an edit:
engine.sequence().shots[1].startSeconds = 42.0;
auto again = engine.installSequence();                // replaces; never stacks
```

`InstallReport` carries the track and key counts, the parameter paths the sequence now owns, the
warnings, and — the one that matters — `unresolved`: targets this scene has no parameter for. A
track naming a parameter that does not exist evaluates perfectly and writes nowhere, forever, in
silence, so the editor draws that list in red.

---

## Shots

```json
{ "name": "The Walk", "start": 20.0, "duration": 20.0, "scene": "plaza",
  "in":  { "kind": "cut" },
  "out": { "kind": "fadeOut", "seconds": 1.4 },
  "camera": { "kind": "keys", "lookAtActor": "hero", "lookAtHeight": 1.15,
              "keys": [ { "time": 0.0, "position": [-77, 15, 1.1], "target": [-50, 1.2, -3.4] },
                        { "time": 20.0, "position": [-35, 15, 1.1], "target": [-8, 1.2, -3.4] } ] } }
```

- **`scene`** names a slot. Empty inherits the previous shot's, so a run of shots in one place says
  so once.
- **`camera.kind`** is `inherit`, `move` or `keys`.
  - `inherit` emits nothing: the previous shot's camera holds, which is how you continue a move
    across a cut.
  - `move` is a shot in the ADR-062 vocabulary — fourteen kinds, five ways to aim, four path shapes,
    easing at both ends, all derived from a subject's position and radius. `cameraFromPreset` fills
    one in from `Isometric`, `Follow`, `Wide`, `Close`, `TopDown`, `Tracking` or `Reveal`.
  - `keys` is hand-authored positions and targets, keyed relative to the shot's own start — so
    moving a shot moves its camera with it.
- **`lookAtActor`** aims at a subject that *moves*. The bake samples the actor at each camera sample
  and blends it into the aim by `lookAtWeight`. This is the difference between a camera that tracks
  a walking character and one whose aim has to be hand-authored frame by frame.
- **Transitions** are `cut`, `fadeIn` and `fadeOut`, and a fade is two keys on `scene/brightness`.
  A dip is a fade out followed by a fade in, and is what a cutter reaches for at a section change.

Shots may not overlap. Two cameras at once is not something a single-camera engine can honour, and
quietly picking one is worse than refusing.

### A cut is a millisecond

The last key of a shot that is cut away from lands 1 ms before the boundary. Without that, the
outgoing shot's final pose and the incoming shot's opening pose land on the same key time, one
replaces the other, and the first shot spends its whole length gliding toward the second shot's
opening frame. You will not see the ramp at any frame rate this engine renders at.

---

## Scene switching

A slot names a composition node, usually a `kind: "scene"` node holding a whole environment:

```json
"scenes": [ { "id": "plaza",    "node": "plaza",    "file": "plaza.scene.json" },
            { "id": "downtown", "node": "downtown", "file": "downtown.scene.json" } ]
```

The bake writes one `Step`-keyed `nodes/<node>/visible` track per slot. A cut costs nothing, needs
no streaming, and is exactly as scrub-safe as everything else. Every slot stays resident: a piece
with twenty environments pays for twenty environments. `file` is not read by the bake — it records
what a slot *is*, so a future preloader need not walk the composition.

**Put your districts in the same world space.** Two environments side by side triple the scene's
radius, and the shadow cascades are fitted to that radius. They are never both visible, so there is
nothing to gain by separating them.

---

## Actors

An actor drives a composition node. Nothing about it is character-specific: a character is an actor
that happens to name a rig.

```json
{ "id": "hero", "node": "walker",
  "keys":  [ { "time": 0.0,  "position": [-62, 0, -3.4] },
             { "time": 40.0, "position": [-8, 0, -3.4] } ],
  "clips": [ { "time": 0.0, "clip": "Idle" },
             { "time": 12.0, "clip": "Walk" },
             { "time": 44.0, "clip": "Run", "speed": 1.05 } ] }
```

- **`keys`** bake to `nodes/<node>/position`, `/rotation` and `/scale`. With no explicit rotation the
  heading is derived from the direction of travel, so a character faces where it is going.
- **`path`** is a `spatial::Spline` walked between two times, with the heading following the tangent.
  Use it instead of a hundred hand-placed keys.
- **`clips`** name animation states on the node's own rig (ADR-086). They are the one thing a
  sequence does **not** bake, because a clip's phase needs a *time origin* and a track carries
  numbers. `seq::applyAnimation` pushes the active cue — its clip and the cue's own absolute second
  — into the composition every frame, so the pose is a pure function of the playhead.
- A key with `"interp": "step"` is how you move a character across a cut without smearing it across
  one frame.

### Actors, not entities

`entity::Entity` (ADR-088) integrates `dt`: a wandering character's position depends on how the
playhead got there. That is right for ambience in a realtime piece and wrong for anything rendered
offline and scrubbed. An actor is the deterministic alternative.

---

## Overlays: lyrics, titles and the frame

```json
{ "id": "lyric007", "kind": "text", "content": "EVERY WINDOW", "style": "lyric",
  "start": 49.0, "end": 52.15, "order": 10, "anchor": [0.5, 0.135],
  "preset": "fadeInOut", "presetSeconds": 0.32 }
```

`kind` is `text` or `shape` (`rectangle`, `ellipse`, `line`). `order` is explicit and ascending:
lower draws first, under everything above it — a border at `-10`, lyrics at `10`, a title at `20`.

`style` is a handful of typographic defaults: `lyric` (the default), `title`, `caption`, `credit`.
Anything a style does not cover goes in `extra`, which is handed to the layer untouched:

```json
"extra": { "font": "Futura", "weight": 0.4, "tracking": 0.08, "align": "left",
           "size": [1.70, 0.93], "strokeWidth": 0.0035, "strokeColor": [0.9, 0.9, 0.8, 0.4] }
```

`preset` is `none`, `fadeIn`, `fadeOut`, `fadeInOut`, `scalePop`, `slideUp` or `slideDown`. A preset
is not an animation engine: it creates keyframes on the layer's own parameters, and you may edit
them afterwards like any others. Every preset keys opacity to zero outside the cue's range, because
"invisible outside its timing" is only true if something says so.

A cue becomes a layer named `seq:<id>`. Those layers belong to the sequence: a re-install replaces
them, and they are not written into the project's `composition` block (the cues are). Layers you
make by hand are never touched.

### Importing lyrics

```cpp
auto lines = seq::loadLyrics("night-shift.lrc");   // LRC, SRT or WebVTT
auto cues  = seq::lyricCues(*lines, seq::LyricImport{ .anchor = {0.5f, 0.14f} });
```

The format is decided by what is in the file, not by its extension. SRT and WebVTT carry end times
and keep them; LRC does not, so a line runs until the next one begins less `gap`, and the last holds
for `defaultHold`. In the editor: **Import Lyrics…**, which replaces a previous import rather than
doubling it.

---

## Markers and snapping

```cpp
piece.setSectionMarkers(structure);                  // from the musical fold (ADR-073)
piece.setBeatMarkers(engine.track()->beats().beatTimes);
```

In the editor, **Sections** does both from the loaded track. Section markers are derived and are
replaced wholesale; a `cue` marker you placed yourself is never discarded.

Snapping is `Off`, `Frames`, `Beats` or `Markers`, and it applies to every time field in the panel
as well as to dragging in the strip — so "cut on the downbeat" is a drag, not arithmetic.

---

## Events: when X happens, do Y

An event is a **when** and a **what** (ADR-098). It is not a second sequencer: almost all of them
stop being events at bake and become the same timeline keys everything else in a sequence becomes.

```cpp
seq::SequenceEvent e;
e.id      = "drop-shake";
e.when    = {.kind = seq::TriggerKind::Section, .name = "FinalDrop"};
e.what.kind   = seq::EventActionKind::CameraShake;
e.what.amount = {0.22f, 12.0f, 0.4f, 0.0f};   // metres, hertz, degrees of aim swing
e.what.seconds = 0.9;                          // decay
piece.events.push_back(e);
```

### The three tiers, and what a scrub does to each

This is the one thing to know before authoring events, and the bake tells you which tier each of
yours is in.

| Tier | When | What | A scrub |
|---|---|---|---|
| **Baked** | time, beat, bar, section, shot start/end, cue, a clip the sequence bounds | a parameter, camera shake, a clip, an overlay, a scene transition | **Exact.** The event became keys; there is nothing left to fire, skip or double-fire. |
| **Scheduled** | the same | an entity action, a notification | Forward play delivers each once. A jump delivers the *latest standing one per target*, marked `restored`, and drops the rest. |
| **Live** | a volume crossing, an action or interaction completing | anything | Posted by the world when it happens. A seek discards anything pending. |

`seq::triggerIsScheduled(kind)` and `seq::actionIsBaked(kind)` answer this without running anything.

### Triggers

```cpp
{.kind = TriggerKind::Time,   .timeSeconds = 42.5}
{.kind = TriggerKind::Beat,   .every = 4, .index = 0}         // every downbeat
{.kind = TriggerKind::Bar,    .every = 8, .fromSeconds = 60}  // every eighth bar, after a minute
{.kind = TriggerKind::Section, .name = "Drop"}                // "" = every section
{.kind = TriggerKind::ShotStart, .name = "reveal"}            // "" = every shot
{.kind = TriggerKind::Cue,    .name = "hit"}                  // an author's own marker
{.kind = TriggerKind::ClipEnd, .subject = "elder", .name = "Wave"}
{.kind = TriggerKind::VolumeEnter, .name = "porch", .subject = "elder"}  // "" = anyone
```

Every trigger also takes `fromSeconds`/`toSeconds` (a window), `delaySeconds` and `repeat`.

Beat and bar triggers read the sequence's **beat markers**, so run `setBeatMarkers()` first — a beat
event in a sequence with no analysis fires nothing and the bake says so.

### Actions

```cpp
EventActionKind::SetParameter    // target = any parameter path. A light, a material, a particle
                                 // rate and the fog are all parameter paths; this is the one verb.
EventActionKind::CameraShake     // amount = (metres, hertz, degrees), seconds = decay
EventActionKind::PlayClip        // target = actor id, value = clip, amount.x = speed
EventActionKind::Overlay         // target = cue id, value = "position.x" | "opacity" | ...
EventActionKind::SceneTransition // target = slot id, value = "cut" | "fadeIn" | "fadeOut"
EventActionKind::EntityAction    // target = entity, value = verb, argument = the verb's object
EventActionKind::Notify          // target = a name the host knows about
```

### Use `add` or `multiply` when the event means *a change*

A track holds its first key's value backwards forever. So a `replace` event that sets the fog at
0:12 has also set it at 0:00, and the bake warns about exactly that. An event authored in `add` or
`multiply` mode has a known identity (0 or 1), so it contributes nothing until it fires and its
delta afterwards — whatever the author set the property to — and `holdSeconds` then returns to that
value exactly:

```cpp
e.what = {.kind = EventActionKind::SetParameter,
          .target = "procedural/lamp/parts/1/emissiveGain",
          .amount = {4.0f, 0, 0, 0},
          .seconds = 0.05,        // ramp up
          .holdSeconds = 0.20,    // ...hold, then return to x1
          .mode = params::TrackMode::Multiply};
```

### Camera shake

Four ordinary parameters, so a beat can drive it through a modulation route like anything else:

```
camera/shake/amplitude   metres of camera-space displacement
camera/shake/frequency   hertz
camera/shake/decay       seconds to fall to exactly zero; 0 = sustained
camera/shake/rotation    degrees of aim swing
camera/shake/start       the second the impulse began
```

`start` is why a decaying shake still scrubs correctly: the engine evaluates `now - start` rather
than running a timer, which is the same trick a clip cue uses for its phase origin. A `CameraShake`
event keys all five. A shake applies in every camera mode because it is an offset on whatever placed
the camera, not a fourth way to place one.

### A scripted cursor (breaking the fourth wall)

There is no cursor feature. A pointer is an overlay cue plus events:

```cpp
seq::OverlayCue pointer{.id = "pointer", .kind = seq::OverlayKind::Shape};
pointer.content = "ellipse";
pointer.startSeconds = 4.0; pointer.endSeconds = 12.0;
pointer.anchor = {0.10f, 0.90f};          // where it starts
piece.overlays.push_back(pointer);

// ...then two Overlay events moving position.x and position.y over two seconds, one more shrinking
// scale.x for 50 ms as the click, and a Notify carrying the selection.
```

The moves are baked keys on `layers/<id>/position`, so the pointer scrubs exactly. Only the
selection leaves the baked tier, because "the host now considers the elder selected" is not a value
over time.

### Shot-driven quality

A shot whose camera move carries an active `Spotlight` raises its subject's level-of-detail floor
for the length of the shot, and hands it back afterwards. The subject's `name` must be the node's
name for this to bind; when it does not, `install()` lists the path under **unresolved** rather than
doing nothing quietly. `BakeOptions::spotlightQuality = false` turns it off.

### Match cuts

`TransitionKind::MatchCut` on a shot's `in` makes it a hard cut whose incoming subject lands at the
same apparent size and frame position as the outgoing one. Both shots must use a subject-relative
camera move; the incoming shot's opening distance is rewritten at bake, so it costs nothing at
render time. A subject with a `preferredDistance` can refuse the match, and the bake says so.

---

## The transport

Across the top of the Sequence panel, and in a shorter form in Control. One playhead, and everything
follows it. The full account is [ADR-102](decisions/ADR-102-the-transport.md); what matters when
using it:

- **A project does not need audio to play.** Its length is the longest of the audio, the sequence and
  the timeline, so a sequence over a shorter piece of audio plays to *its* end rather than the wav's.
- **Pause freezes the piece, not the world.** Wind, water and anything else animated continuously
  keep going, because they are not on the timeline.
- **Stop returns to the start of the play range** -- the loop start when a loop is on, else zero.
- **Frames are the project's frame rate**, which is the Render panel's: the frames you step through
  are the frames the project exports. 29.97 and 59.94 are the exact rationals, not the decimals.
- **The loop** wraps when playback *crosses* its end, so a playhead parked past it plays on rather
  than being pulled backwards. It is saved with the project and is ignored by offline renders.
- **Playback speed** is picture-only away from 1x: `AudioPlayer` has no rate control, so the sound is
  silenced rather than allowed to drift, and the bar says which you are getting.
- `Space` plays, the arrows step a frame, shift and the arrows step a beat, `Home`/`End` go to the
  ends, `L` toggles the loop.

**Adding something that follows the playhead.** Read `Engine::timelineClock().seconds` (or
`Engine::transport().positionSeconds()`), never `FrameTime::renderTime` -- the second is the render
clock and keeps running while the piece is paused. Everything timed by the piece must be a pure
function of that position, because that is what makes scrubbing and offline rendering agree; if it
cannot be, it needs a `seek` like `EntityWorld` has (ADR-091) and a line in `Engine::seekSeconds`.

---

## Audio: one piece, several files

**Sequence panel → Audio...**. A project's sound can be more than one file: a song and a spoken
outro, two cues with a gap, a stem set. Each clip says where it sits on the timeline, how far into
its file it starts, how long it plays and how loud, with optional fades at its edges.

The clips are **mixed down** to one buffer, and everything else in the engine — the player, the
analysis, the waveform, the transport, the offline renderer — sees that buffer and nothing else.
Two consequences worth knowing:

- Overlapping clips sum. Gaps are silence. The arrangement's length is where the last clip ends.
- A project with a single untouched file is *bit-identical* to that file and is saved exactly as it
  was before arrangements existed, so nothing about an existing project changes.

On the strip, the waveform lane shows the whole mix and is **always a scrub** — clicking it moves the
playhead, and nothing there can be grabbed by accident. The clips live in a thin lane under it, each
outlined and named: drag one to move it, its right edge to trim it. The re-mix happens when you let
go, not during the drag — it is a pass over every sample, about 50 ms for six minutes.

Clips at different sample rates are resampled linearly and **warned about**: it is audible on music,
and converting the file is the real fix. See
[ADR-103](decisions/ADR-103-audio-arrangement.md).

---

## The Sequence panel

One horizontal time axis. A ruler with the song's sections and beats on it, a lane showing the
song's waveform, a lane of shots, a lane per actor showing its clip cues, and a lane of overlays.

The audio lane is drawn from an `audio::WaveformSummary` -- the minimum and maximum sample in each
five-millisecond slice, summarised once when the file loads. It is kept in *time* rather than in
pixels so that zooming reads more or fewer buckets per column instead of needing a rebuild, and its
amplitude is clamped rather than normalised to whatever is on screen, so a passage keeps the same
shape as you scroll past it.

- **Click** in the strip to scrub. **Click** a block to select it.
- **Drag** a block to move it, its right edge to resize it. Both snap.
- **Right-drag** pans, the **wheel** zooms about the pointer.
- **Add Shot** starts where the piece currently ends, with a Wide preset aimed at the first actor.
- **Key from viewport** adds a camera key at the playhead from wherever you have flown the camera.
- **Cue here** adds a clip cue at the playhead, from the states the node's rig actually has.

The bake runs when you let go of the mouse, not during the drag.

---

## Rendering

Nothing special. The sequence is timeline tracks, and the offline renderer already renders timeline
tracks:

```sh
avgen --render out/night-shift.mov --project examples/city/night-shift.json
avgen --headless --project examples/city/night-shift.json --frames 1500 --capture f1500.png
```

A frame is a pure evaluation, so the frame at *t* is the same whether playback walked there or the
renderer jumped to it. `tests/integration/test_sequence_project.cpp` checks exactly that, in two
engines, one played forward and one seeked in an order no playback would produce.

---

## One performance trap worth knowing

**Do not animate a procedural sky's own parameters smoothly.** `SkyRuntime::hash()` covers the
zenith and horizon colours, the sun's colour, intensity, size and direction, and the sky's own
intensity; a changed hash rebuilds a 256-pixel cube with nine mips, a 32-pixel irradiance probe and
a six-mip prefiltered radiance chain, which is 93-146 ms of CPU. A smooth `env/sky/*` track rebuilds
it *every frame*, and a modulation route pointed at one does the same however the track is keyed.

Key those parameters with `Step` at the section boundaries instead. The dusk then arrives at a cut,
which is where a cutter would have put it anyway, and everything that should move continuously --
`scene/keyLight`, `scene/fogColor`, `scene/fogDensity`, `scene/styledSkyAmbient`, the practicals'
`emissiveBoost` -- still does, because none of those are in the sky's hash. On the Night Shift project this is the
difference between 8.7 and 63.3 frames per second over the whole piece at 1280x720 (364.0 s to
49.7 s for 3,150 frames), and 8 sky rebuilds instead of 3,150.

## Known limitations

- No crossfade between two 3D scenes; a dip to black is what exists.
- `lookAtWeight` is per shot, not animated. A shot whose subject *changes* — a reveal that starts on
  a character and ends on a city — has to author its targets.
- Every scene slot is resident.
- Entity behaviours are not scrub-deterministic; see above.
- An event that acts on a live system (an entity action, a notification) is not scrub-exact. A seek
  restores the latest standing intent per target rather than replaying the history (ADR-098).
- Procedurally instanced geometry does not reliably reach the shadow cascades in a large scene. This
  is a renderer-side limitation, not a sequencer one; see `docs/renderer-2-backlog.md`.
