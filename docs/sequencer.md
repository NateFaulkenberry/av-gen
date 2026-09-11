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

## The Sequence panel

One horizontal time axis. A ruler with the song's sections and beats on it, a lane of shots, a lane
per actor showing its clip cues, and a lane of overlays.

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
- Procedurally instanced geometry does not reliably reach the shadow cascades in a large scene. This
  is a renderer-side limitation, not a sequencer one; see `docs/renderer-2-backlog.md`.
