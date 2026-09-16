# ADR-245 — A camera is not *the* camera

**Status:** Accepted
**Date:** 2026-09-16

## Context

Until this change AV Gen had exactly one camera: the `camera/*` parameter block that
`Composition::applyParameters` reads into `Scene::camera` once a frame. Everything that wanted a
different viewpoint had to *become* that camera — the Auto-director by baking keys onto it
(ADR-075), the viewport by dragging it (ADR-068), a spline by riding it (ADR-026), ADR-217's hold by
nudging it. "Which camera" and "where the camera is" were the same question, because there was only
ever one answer.

That is a conceptual ceiling, not a missing feature. A piece that wants a wide establishing shot and
a hero follow wants them to be two *objects*, not one object with a schedule. And it is why the
Auto-director had grown controls that are really camera properties: a global wide/hero lens pair
exists because one camera cannot be two lenses.

There was one latent piece of multi-camera support and it is not the one it looks like:
`Scene::cameras` is the glTF importer's list, the first of which becomes `Scene::camera` on import.
It is import plumbing, it is never evaluated, and nothing selects from it. Nothing else in the
repository anticipated more than one camera.

### What the survey said

The three established models agree on the separation and disagree on the spelling.

* **Unreal Sequencer** ([camera cut
  track](https://dev.epicgames.com/documentation/unreal-engine/cinematic-camera-cut-track-in-unreal-engine),
  [creating camera
  cuts](https://dev.epicgames.com/documentation/unreal-engine/creating-camera-cuts-using-sequencer-in-unreal-engine),
  [sequences, shots and
  takes](https://dev.epicgames.com/documentation/unreal-engine/sequences-shots-and-takes-in-unreal-engine)).
  A Camera Cut Track is a *pure time → camera selector*. A section on it holds a time range and a
  camera binding id — **no transform and no lens**; those live on the camera actor's own tracks.
  Blending is enabled per track (`Can Blend`), specified by overlapping two sections, and
  interpolates the transform *and* the camera component properties (depth of field, focal length).
  A gap hands the view back to gameplay.
* **Blender** ([cameras](https://docs.blender.org/manual/en/latest/render/cameras.html),
  [markers](https://docs.blender.org/manual/en/latest/animation/markers.html)). A camera is an
  ordinary object with an ordinary transform and ordinary keyframes; "active camera" is one
  scene-level pointer; and "which camera over time" is a sorted list of `(frame, camera)` markers —
  piecewise constant, hard cuts only.
* **Final Cut Pro / Resolve multicam** ([cut and switch camera
  angles](https://support.apple.com/guide/final-cut-pro/cut-and-switch-camera-angles-ver23c76c9c/mac)).
  Named angles, one highlighted live angle, and two distinct verbs: *cut and switch* (insert an
  edit) versus *switch only* (retarget the segment in place).

What we took: Unreal's separation and its blend semantics (interpolate the picture *and* the lens),
Blender's authoring form (an ordered list of times and cameras is diffable, scrubbable and
serialisable), and the NLE insistence that the live angle is visible and named.

What we refused: Unreal's shot/take/sub-sequence hierarchy with hierarchical bias (a studio
asset-management answer to a problem this engine does not have); Unreal's implicit gap-to-gameplay
fallback (here a gap is a *named* state — `ActiveCameraReason::Default` — never a silent hand-off);
Blender's integer-frame marker timing (seconds, so a frame-rate change is not an edit); and
Unreal's blend-by-overlapping-sections, because an explicit `blendSeconds` on the incoming shot says
the same thing without making two overlapping spans mean something different from two overlapping
spans anywhere else in this engine.

## Decision

Three concepts, separately owned, in `src/scene/camera_rig.hpp`.

```
CameraRig    a persistent viewpoint: name, stable id, placement, lens, and an opinion about
             when it should be used. Does not know whether it is on screen.
CameraShot   a span of time that names a camera, and how the picture arrives. Holds no
             transform and no lens: those belong to the camera.
resolveActiveCamera(cameras, shots, events, t) -> ActiveCameraState
             a pure function. Does not move anything.
```

**The main camera is a camera.** `kMainCamera` (id 1) names the legacy `camera/*` block.
`CameraRig::channelPrefix()` returns `"camera/"` for it and `"cameras/<slug>/"` for everything else
— one line, and it is the whole backward-compatibility story: the Auto-director still bakes onto
camera 1, the viewport still drags camera 1, and a project that has never heard of this ADR resolves
to camera 1 at every instant and renders the identical image.

**A camera's channels are ordinary parameters.** `cameras/<slug>/position`, `.../target`,
`.../fov`, `.../focalLength`, `.../splineT`, `.../lookAhead`, `.../splineOffset` — seven, registered
by `Composition::registerCameraChannels`. That is the entire animation story: a camera is keyframed
by putting timeline keys on these, modulated by routing audio at them, and *static* by leaving them
alone. There is no camera animation system, no camera keyframe type and no camera curve editor,
because none of them had to exist.

**"Static" and "animated" are not modes.** `Composition::cameraIsAnimated(id, timeline)` asks the
timeline whether it drives any of that camera's channels. A stored flag would be a second source of
truth about a question the timeline already answers, and it would be wrong the first time somebody
deleted a track.

**Resolution order**, highest first:

0. a **locked** authored shot — the author's veto;
1. an **event**: a camera whose named staging scenario is running, widened by its lead and tail;
   highest `priority`, then lowest id;
2. an authored **shot** containing `t` — the *last* match in the list, because that is the only rule
   under which dragging a shot on top of another does what it looks like;
3. the **default camera**, unbounded.

The outgoing camera during a blend is found by resolving again at the instant before the current
claim began — one step back, never two. That is what makes a blend a function of the clock rather
than of a remembered previous frame, and it is why a cut survives a scrub.

**Only the live camera is evaluated.** One camera, or two while a blend is running. An inactive
camera costs its seven parameters in `resetFinals` and nothing else: no spline sample, no node
lookup, no arithmetic.

**Authored cameras may be Free or Spline and may not orbit.** Orbit integrates `orbitSpeed * dt`
into an accumulator, which makes it the one camera placement in this engine that depends on how the
playhead arrived rather than on where it is. The legacy camera keeps it for the scenes authored
against it; no new camera gets it.

**A camera may watch something that moves.** `aimNode` + `aimOffset` and `followNode` +
`followOffset` name composition nodes. This is what makes an *event* camera possible without the
event's world leaking into the camera system: `UFO Watch` rides the node called `visitor` and looks
seven metres below it, and the engine does not know what a saucer is.

**An authored camera owns its optics.** A non-zero `focalLength` drives the frame's physical lens
(ADR-037), so depth of field and the circle of confusion agree with the picture and not only with
the framing. An authored camera also focuses on what it is aimed at, because otherwise it inherits
whatever `camera/lens/focusDistance` the Auto-director baked for a shot on a different camera — which
is exactly what happened in the first demo render, and the subject came back out of focus.

**Where it lives.** `CameraDirection` is owned by `scene::Composition` and serialised in the *scene*
document under `"cameraDirection"`, written only when there is more than one camera — so an
untouched `.scene.json` is byte-identical to what it was. In the scene rather than the project
because a shot names a camera by an id that only means something inside the world it was composed
for; splitting the two across two documents makes a project that loads a different scene a project
full of dangling shots.

### The published surface

Exactly one thing in the engine says which camera is on screen:

```cpp
scene::ActiveCameraState Engine::activeCamera() const;   // app/engine.hpp
const scene::ActiveCameraState& Composition::activeCamera() const;
```

```cpp
struct ActiveCameraState {
    CameraId camera;              // what is on screen (kMainCamera == 1 when nothing is directing)
    CameraId previous;            // what it is coming from while blend < 1; else == camera
    float blend;                  // 0 = entirely `previous`, 1 = entirely `camera`
    ActiveCameraReason reason;    // Default | Shot | Event
    std::string name;             // the active camera's display name
    double sinceSeconds, untilSeconds;  // until <= since means "no end"
    std::string eventName;        // the scenario that claimed it, when reason == Event
    float focalLength, focusDistance;   // 0 = the camera states no opinion
    bool blending() const;
};
```

The camera's **pose** is, as it always has been, `Scene::camera`. This says *which* camera that pose
belongs to and *why* — which is what an overlay, a sequencer lane or an output preview needs and
what the pose alone cannot answer. Nothing consumes it by mutating it: it is a reading of the frame.

### The one impurity, stated

`resolveActiveCamera` is pure in its inputs. Its *event* input is not, and cannot be: a staging
scenario (ADR-210) is started by `autoStart` or by a signal edge, its `Run` state is accumulated
frame by frame, and nothing anywhere records when it will end. So `Composition::observeCameraEvents`
records what it has *seen*: a span opens when the scenario reaches one of the camera's named beats
and closes when it leaves them; an open span has `endSeconds <= startSeconds`. A seek clears the
table, next to `clearAimHoldState`, for the same reason that one is cleared.

This is the identical compromise ADR-217's hold already makes, and it is documented here rather than
hidden. Everything else about camera direction — shots, blends, the default, the priority order — is
a pure function of the playhead, which `tests/unit/test_camera_rig.cpp` checks by resolving four
hundred instants in shuffled order and requiring every answer to match the forward pass.

## What was measured

**Regression.** Unit **1880 cases / 1,935,575 assertions** (3 skipped), all passing — against a main
baseline of 1854 / 1,934,850 (3 skipped). The 26 new cases and 725 new assertions are this change's.
Render **298 cases / 410,033 assertions** (1 skipped) — *exactly* the baseline, every pixel test
unchanged, run through `tools/gpu-lock.sh` (ADR-170).

**Inactive cameras.** Structural, not a timing, because a count cannot be confused by a contended
machine. An authored camera costs **exactly seven parameters** and the update cost does not move:

| cameras | parameters added | engine update |
| ------- | ---------------- | ------------- |
| 1       | +7               | 3.72 µs/frame |
| 5       | +35              | 2.89 µs/frame |
| 10      | +70              | 3.15 µs/frame |
| 25      | +175             | 3.06 µs/frame |
| 50      | +350             | 3.45 µs/frame |

Fifty cameras is not measurably slower than one — the spread is noise, and the 1-camera case is the
*slowest* row, which is how you can tell. Run it with
`tools/gpu-lock.sh ./build/release/tests/avgen_tests "[cambench]"`.

**Control arms (ADR-182).** Every probe was shown capable of failing. Each row is the whole
`[multicam]` suite (27 cases, 737 assertions) built against a deliberately wrong implementation:

| the implementation was made to... | result |
| --------------------------------- | ------ |
| take the **first** matching shot instead of the last | 1 case, 1 assertion FAILED (`2 == 1`) |
| let an authored shot outrank a running event | **4 cases, 11 assertions FAILED** |
| never look back, so a blend's `previous` is always itself | 2 cases, 5 assertions FAILED |
| leave a deleted camera's shots in the list | 1 case, 2 assertions FAILED |
| drop `eventTail` from `toJson` | 1 case, 2 assertions FAILED (`0.0 == 1.25`) |
| treat a shot span as closed at both ends | 1 case, 1 assertion FAILED |
| ignore `CameraShot::locked` | 1 case, 2 assertions FAILED |
| never put the active camera's focal length on the lens | 1 case, 1 assertion FAILED |
| leave a deleted camera's parameters in the set | 1 case, 2 assertions FAILED |
| not write `cameraDirection` into the scene document | 1 case, 1 assertion FAILED (and 12 assertions vanished) |
| ignore the resolved camera and always evaluate the main one | **3 cases, 6 assertions FAILED** |

Restored, all 27 cases and 737 assertions pass. **Had the change been wrong**, these are the numbers
the suite would have reported instead of a clean run — which is the point of recording them.

**The demo.** Glowmere Valley 2 with three cameras, 1800 frames at 1920×1080, 0 GPU errors. The
director's own log is the acceptance test, and it reads:

```
camera: Valley Wide    (shot)             at  0.00 s
camera: Hero Free Roam (shot)             at  7.00 s
camera: UFO Watch      (event abduction)  at 13.50 s
camera: Hero Free Roam (default)          at 19.53 s
camera: Valley Wide    (shot)             at 26.00 s
```

Three different mechanisms — an authored shot, a running event, and falling through to the default —
in one thirty-second piece, with no Glowmere-specific code anywhere in `src/`.

## Consequences

* The Auto-director is now *one* camera in a list rather than the only camera there is, and the
  concept migration below is what that makes possible. **Nothing has been deleted from
  `AutoDirectorSettings`**: which capabilities the owner is willing to lose is the owner's decision,
  not this change's (autodirector-cleanup §20).
* `camera/*` is now a *prefix that happens to be historic*. It is not renamed and will not be:
  renaming it would orphan every baked track in every saved project.
* A scene document gains an optional `"cameraDirection"` key. Absence means one camera, which is
  what every existing scene has. No version bump, following the convention `autoDirector`,
  `timeline` and `sequence` already set: optional, unversioned, written only when non-default.
* Deleting a camera removes its parameters *and* the timeline tracks that drove them
  (`Engine::setCameraDirection`). Leaving either behind is ADR-242's "a target nobody reads".
* ADR-158's aim-follow and ADR-217's hold are gated on the main camera being live. They nudge the
  shot the Auto-director baked; while an authored or event camera has the frame, that shot is not
  what is on screen and dragging its aim would move a camera nobody pointed at the heroes.
* A `Cameras` panel exists (`ControlPanel::drawCameras`). **It is visually unverified** — this agent
  cannot see ImGui — and it is deliberately small: it makes a camera where the viewport is looking,
  shows which is live, sets eligibility and lens, and adds a shot at the playhead. Deeper editing is
  the Parameters and Sequence panels' job, because a camera's channels are ordinary parameters and a
  second set of controls for them would be a second source of truth.

---

## Appendix — Auto-director concept migration (for the owner; nothing has been removed)

autodirector-cleanup §20 asks for this table before anything is deleted, and §20 says plainly that
the deletions are a human decision. This is the complete inventory of `AutoDirectorSettings`
(`app/camera_director.hpp`), each traced through the panel (`ui/control_panel.cpp` §
`drawAutoDirector`), the `autoDirector` project block, `--director k=v`, and the runtime that reads
it.

| Old concept | What it controls today | Where it is implemented | Proposed new home | Keep? | Reason |
| --- | --- | --- | --- | --- | --- |
| **Shot mode — Continuous shot** | one uninterrupted take; the bake emits no cuts | `DirectionBrief::mode`, `seq::Sequence` bake | Auto-director | **Keep** | Still a genuine director policy: "this camera does not cut". It is not the shot track saying so, it is the Auto-director's own cutting behaviour on *its* camera. |
| **Shot mode — Edited sequence** | the bake cuts between shots | same | Auto-director | **Keep** | Same argument. Note the redundancy the cleanup suspected does *not* exist: the shot track selects *between cameras*, this selects *between framings of one camera*. |
| **Shortest shot** (`minShotSeconds`) | floor on an Auto-director shot | `Sequence` bake | Auto-director | **Keep** | Genuine policy on the automatic cut. Possibility A in §5. |
| **Longest shot** (`maxShotSeconds`) | ceiling; a long passage becomes several shots | `Sequence` bake | Auto-director | **Keep** | Same. |
| **Shortest build** (`minBuildShotSeconds`) | a build is exempt from the floor down to this | `Sequence` bake | Auto-director | **Owner's call** | §5 singles this out. It is not a single-camera workaround — it is about musical structure, and a build genuinely wants a shorter minimum than a verse. But it is the one control whose meaning cannot be guessed from its name, and it is a candidate for folding into `minShotSeconds` as a fixed ratio. |
| **Wide lens** (`wideFocalLength`) | mm baked into `camera/lens/focalLength` for establishing/drifting shots | `Sequence` bake | **`CameraRig::focalLength`** | **Migrate (not yet done)** | §6 is right: a lens is a camera property. The new home exists and works (`Valley Wide` is a 24 mm camera in the demo). What is *not* done is retiring the global pair, because doing so re-cuts every existing project. |
| **Hero lens** (`heroFocalLength`) | mm for hero shots; defaults from the subject's proportions | `Sequence` bake, `applyTo` | **`CameraRig::focalLength`** | **Migrate (not yet done)** | Same. Note the subtlety: `applyTo` only overrides when the value is non-default, so the subject-derived choice survives — that behaviour has to be preserved or re-homed, not dropped. |
| **Hold subject** (`dwellShots`, ADR-203) | consecutive shots one subject keeps | `Sequence` bake | Auto-director | **Keep** | Subject *selection* policy, not camera movement. Nothing about multi-camera makes it redundant. |
| **Max speed** (`maxCameraSpeed`, ADR-200) | m/s ceiling; shrinks a shot's ground, never its timing | `Sequence` bake | Auto-director, or a per-camera constraint | **Keep for now** | §7 suggests a camera-controller home. It constrains the *bake*, and the bake is the Auto-director's; a per-camera version only becomes meaningful if a second camera ever gets a bake. |
| **Max swing** (`maxViewRate`, ADR-200) | deg/s ceiling on the aim | `Sequence` bake | same | **Keep for now** | Same, and ADR-200 measured that it is a genuinely separate problem from speed. |
| **Stay with a scenario** (`holdScenario`, `holdRole`, `holdReleaseSeconds`, ADR-217) | while the scenario holds its role, the camera rides the actor and then rejoins the cut | `Composition::applyAimHold` | **superseded by an event camera** | **Owner's call — the strongest deletion candidate** | §8 predicted this exactly. `UFO Watch` does the same job better: a camera composed for the event, cut to on the event's own beats, returning by falling through the resolver. The hold makes *one* camera behave *approximately* like a second camera. Retiring it means migrating `holdScenario` into a generated event camera at load — doable, and it removes a whole state machine. **But it changes the picture of every project that uses it, Glowmere Valley 2 included.** |
| **Seed** (`seed`) | same seed, same edit | `Sequence` bake | Auto-director | **Keep** | Determinism handle; nothing to do with cameras. |
| `CompositionProfile::framing`, `headroom` | *nothing* — stored, serialised, read by no geometry function | — | delete | **Delete** | Already documented as dead in `camera_director.hpp`. Not a multi-camera question; it was dead before. |
| `HeroPoint::preferredCameraElevationDegrees` | *nothing* — authored per hero, never read | — | delete or wire up | **Owner's call** | Also already documented as dead. A per-camera `aimOffset` now covers the intent for authored cameras. |
| `Shot::speed` | real, but only via `Sequence::retime()`, which the director never calls | `seq::Sequence` | — | **Leave** | Not exposed, so not a knob wired to nothing. |

**Recommended order, if the owner says yes:** (1) move the two lens settings onto cameras and
generate a wide/hero camera pair at migration; (2) retire `holdScenario` in favour of a generated
event camera; (3) decide `minBuildShotSeconds`. Steps 1 and 2 both change existing films and both
need a render-diff before and after.
