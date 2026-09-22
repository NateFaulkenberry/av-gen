# ADR-582: Taking the camera back parks the director's cut; it never destroys it

- Status: Accepted (2026-09-21). The owner's ruling.
- Supersedes ADR-207's rule that a world effect gated on the cut stops firing once the cut stops
  driving the camera, and ADR-249's rule that handing the camera back deletes the directed camera
  shots.
- Related: ADR-158 (aim-follow), ADR-344 (a re-bake re-photographs the hero anchors), ADR-386 (the
  camera lock), ADR-441 (no compatibility shims), ADR-182 (a probe that cannot fail), ADR-618 on
  other branches (a key the reader reads and the writer drops).
- Tests: `tests/unit/test_directors_cut_parked.cpp` (10 cases), with the parking assertions added
  to `test_camera_director.cpp` and `test_song_director.cpp`.

## 1. What happened

The owner reported: *"the world effect 'hero pulse' does not display or animate in the rendered
scene as it does during playback."*

In `glowmere-valley-2-multicam` the Hero Pulse has `activation: heroFocus` and the Camera Travel
Beam has `activation: cameraTravel`. Both gate on the director's cut, which means the shot spans in
`Engine::shotSpans_`. `app::releaseDirectedCamera` is what runs when the owner deliberately takes
the camera back: the Disable Auto-director action, Free Camera, or unlocking in the Cameras panel.
It **destroyed** the director's camera tracks, the aim-follow table, the directed camera shots
and the shot spans. `saveProject` writes `cameraShotSpans` only when there are some, so the next
save deleted them from the file. An offline render loads the project, found no cut, and the pulse
never activated. The window had shown it until the moment of the hand-back, and the render never
had it.

It happened for real. The owner's 18:04 save on 21 Sep (`e61778df`) dropped 45 shot spans, 39
aim-follow entries and about 122 KB of camera tracks. ADR-386's lock did not help, because it
guards against *incidental* gestures and this was a deliberate one. Re-baking does not recover the
cut either: it re-photographs the hero anchors wherever the heroes stand at that moment
(ADR-344), so a different cut comes back.

The owner rejected a warning: *"a warning is going to be way too confusing, its entirely unexpected
behavior."*

## 2. The ruling

**Who is steering the camera and what the film's focus schedule is are two separate facts.**

1. Taking the camera back **parks** the director's cut and never destroys it. The steering half,
   meaning the director-owned camera tracks, the aim-follow table and the directed camera shots,
   comes off the camera and is kept in `Engine::parkedCut()`. It is saved with the project as
   `parkedDirector`, and that block's presence is the parked marker.
2. The shot spans are **not touched**. HeroFocus and CameraTravel effects follow the saved schedule
   whether the director is steering or parked, in playback and in an offline render alike. The
   owner flying a free camera still sees the pulse land on the spotlit hero at the right time.
   This overrules ADR-207's *"A world effect gated on 'the camera is travelling' must not keep
   firing against a cut that is no longer driving anything."* The owner ruled that it must keep
   firing.
3. **Resume Director** puts the parked cut back exactly as it was, with no re-bake. It sits beside
   Disable Auto-director in both the Camera menu and the Auto-director panel.
4. Only an **explicit re-bake** (Enable Auto-director, or moving a director control while it is
   steering) or an **explicit Discard Director's Cut...** (behind a confirmation) may replace or
   delete a cut. Nothing incidental or implicit may.

### Why the tracks leave the timeline instead of being disabled in it

`Track::enabled` exists, and disabling the tracks in place would have been one line. It would also
have been wrong. A disabled track still answers `Timeline::findTrack`, so the first camera key
somebody recorded while the director was parked would land on the director's own track and change
the cut that Resume promises to restore exactly. Parked tracks are held unbound (no pointers into a
parameter set a scene swap may destroy) and are bound again on resume.

### The edge cases, decided

- **Releasing while already parked is a no-op.** Parked means the director is not steering, so any
  camera track on the timeline was made by somebody after the hand-back. Parking it would overwrite
  the director's cut with their keys.
- **Resume refuses, and changes nothing, if the camera has keys of its own** on a director target.
  Resuming over them would replace them. The message names the targets.
- **A project that was never directed** (the Tree of Life: 0 tracks, 0 follow entries, 0 spans)
  gets nothing new. Releasing it is a no-op and its saved document is unchanged.
- **Old projects** have no `parkedDirector` key and open exactly as before. No alias and no
  migration, per ADR-441. This is a change to how directing behaves, not to the file format.
- `directedCameraBakeSize` (the lock's "is there anything to protect") now counts only what a
  release takes *off the camera*. The spans stay after a release, so counting them would keep the
  lock armed on a project that has already been handed back.

## 3. Every other path that could destroy or replace a cut

The audit covered every caller of `setShotSpans`, `setAimFollow`, every erase of
`directedCameraTargets()` tracks, every re-cut, scene reloads and the save guard.

| Path | Before | Now |
|---|---|---|
| `releaseDirectedCamera` (menu, Free Camera, Cameras-panel unlock, unlocked viewport drag) | destroyed all four parts | parks three, leaves spans |
| `refreshDirection`, last hero unstarred | destroyed via release | parks via release |
| `refreshDirection`, a hero **moved** while the transport was parked | **re-cut the whole film** | reports `Redirect::Stale` once; the cut is unchanged |
| `refreshDirection` after a project load or scene swap | compared one composition's revision counters with another's, and **re-cut the freshly opened project** when they differed | `Engine::sceneGeneration()` changed, so the claim is taken up afresh with no re-cut |
| `Engine::installComposition` (assistant rollback `setCompositionJson`, `loadComposition`) | the new composition started with an empty aim-follow table, so a steering cut **lost its follow** and the next save wrote the loss | the table is carried across, like the tracks and spans already were; a project load still replaces it from the document |
| `installSequence` (Enable, Song Mode, a director control moved while steering) | replaced the cut | replaces it and clears any parked cut: this is the explicit re-bake |
| Auto-director settings and Song Mode autonomy controls | re-cut while `camera/position` was automated | the same, and also required to be *not parked*, so a hand-keyed camera during parking cannot trigger a re-bake over the parked cut |
| `saveProject`'s `!shotSpans_.empty()` guard | turned a release into a deletion | correct as written, because a release no longer empties the spans; empty now means only "no cut ever" or "discarded" |
| `loadProject` | spans and follow cleared when absent | unchanged; `parkedDirector` cleared when absent |
| `discardDirectorsCut` | (new) | the explicit delete |

The hero-**moved** re-cut is the one where this ruling takes away something that was a feature.
It re-framed the film when somebody dragged a hero while paused. It also fired whenever a paused
scrub re-simulated the world: walkers move, their heroes follow and settle, and the whole cut was
silently replaced by a re-bake photographed wherever the scrub left them. The two cases cannot be
told apart at this layer, so the re-cut is gone and the status line says what to press.

**Kept as it was:** changing the *cast* (starring or unstarring, and undoing either) still re-cuts
a *steering* director at once. That is the ADR-075 behaviour the owner asked for: one click shows
the effect. It never touches a parked cut, because a parked director has no claim. It is still an
implicit replacement of a steering cut, and it is listed here so it can be ruled on separately if
wanted.

**Known and not changed:**

- Explicit per-track deletion in the timeline editor or by the assistant's remove-track tool.
- Opening a different scene. The camera tracks, spans and (now) follow table are carried across.
  The old scene's directed camera shots go with its composition. That is an explicit "open".
- `seq::install` erases by *target*. If the sequencer ever bakes `camera/position` while the
  Auto-director also owns it, reinstalling the sequence erases both, and `saveProject` filters
  sequencer targets out of the saved timeline. This is the two-camera-writer conflict itself and
  predates this work.
- A *steering* Song Mode cut's directed camera shots are saved only inside an inlined scene
  document. A by-reference project does not persist them. Parked ones are saved, in
  `parkedDirector`.

## 4. Evidence

- The owner's scenario, with the Hero Pulse and the Camera Travel Beam: direct, release, save, and
  reload in a fresh engine the way a render does. **On `main`'s code it fails with expansion**:
  spans `0 == 10`, pulse `0 == 1`, beam `0 == 1`, and `cameraShotSpans` absent from the saved file.
  The composition-swap case fails `0 == 1` and the project-open case fails `Recut == Nothing`. All
  of them pass with the fix.
- Resume after release gives back byte-identical camera tracks, the whole timeline JSON, the follow
  table, the spans, the camera shot list and the **whole project document**, both in the same
  session and in the next session from the saved project.
- Playback equals render: at every half-second of the film, the live engine with a parked director
  and a fresh engine that loaded its save pack the same effect count, and the packed GPU structs
  match with `memcmp`.
- Round-trips: load, save and reload, with the scene inline and by reference. The top-level keys
  are identical and `parkedDirector` and `cameraShotSpans` are equal JSON.
