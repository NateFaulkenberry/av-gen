# Transport audit

What owns time in AV Gen today, what is wrong with it, and what the global transport has to become.
Written before any transport code was changed (Phase 1 of the global transport brief).

Related: ADR-012 (time model), ADR-018 (timeline), ADR-089 (the cinematic sequence), ADR-091
(simulation authority), ADR-098 (cinematic events), ADR-102 (the decision this audit produced).

---

## 1. What already exists, and is right

The engine is not short of a time model. It has a good one, and most of this brief is already
satisfied by it.

**`FrameClock` / `FrameTime` (`core/time.hpp`, ADR-012).** Every subsystem update takes a
`FrameTime { renderTime, deltaTime, frameIndex }`. `RealtimeClock` advances by measured wall delta,
clamped; `FixedStepClock` sets `renderTime = start + frameIndex / fps`. **No engine code outside
`RealtimeClock` reads a system clock.** This is exactly what §3.3 asks for and it is already true.

**Offline rendering is already deterministic.** `RenderJob::start` builds a `FixedStepClock`,
`restartAt(startSeconds)`, `engine_->seekSeconds(startSeconds)`, and then every frame is
`engine_->tick(*clock_)` → `engine_->update(time)` → render. There is no wall-clock dependence in
that path, and `Engine::tick` deliberately does not touch the clock in Offline mode. §16 is largely
already met; the work there is to keep it met, not to build it.

**Timeline evaluation is already pure and already ordered.** `Engine::update` runs a fixed sequence
— control → time signals → timeline clock → cues → `params_.resetFinals()` → `timeline_.apply()` →
fields → modulation routes → behaviour → sequence animation → events → controller → camera/post.
That *is* the §9.1 contract, written down in comments, with the reasoning for each ordering
constraint. Sequences **bake** to timeline tracks (ADR-089) precisely so that scrubbing is pure
evaluation, and events already distinguish stepping from jumping (`SequenceEvents::advanceTo`,
ADR-098) so a seek rebases standing intents instead of replaying everything in between.

**Seeking already does the hard part.** `Engine::seekSeconds` resets the modulator, the sources, the
music classifier, the beat clock, the cue state, **seeks the entity world** (ADR-093 — `seek`, not
`reset`, so characters land where that second says rather than at their t=0 pose), and rebases the
event scheduler. Every one of those lines is a bug somebody already found and fixed.

**Transport commands already have more than one caller.** `Engine::play/pause/togglePlay/stop/
seekSeconds` are called from the Control panel, the keyboard, the OSC/MIDI control map
(`ControlHub`, `control_map.hpp` kinds `Play/Pause/Stop/Toggle/Seek`), the AI tool surface, the UI
script driver and the stress harness. The *command surface* is already global; what is missing is a
state behind it.

**Markers already exist.** `seq::Marker { timeSeconds, name, kind }` with Section / Cue / Beat kinds,
filled from the analysis by the sequencer's "Sections" button. §14 needs navigation, not a data
model.

---

## 2. Who owns time today

| Question | Today's answer |
|---|---|
| What is "now"? | Two different values, see below |
| Am I playing? | `player_->isPlaying()` — the **audio device's** state |
| How long is the project? | `audioFile_->durationSeconds()` — the **audio file's** length |
| What advances the timeline? | The audio play-head if there is audio, the wall clock if there is not |
| Where is the loop state? | Nowhere. There is none |
| Where is the playback rate? | Nowhere. There is none |
| What is the project frame rate? | Nowhere. `RenderSettings::fps` is a property of a render job |

### 2.1 There are already two clocks, and nothing names them

```
FrameTime::renderTime      free-running render clock; drives deltaTime integration,
                           ambient motion, shader layers, the scene controller
timelineClock_.seconds     the timeline position; drives timeline_.apply(), cues,
                           baked sequence animation, sequence events, overlays
```

`updateTimelineClock` is the whole of the relationship:

```cpp
timelineClock_.seconds = audioFile_ ? positionSeconds() : time.renderTime;
```

They agree while audio plays, and diverge otherwise. That is not, in itself, wrong — a world's wind
and water should keep moving while the timeline is parked, and `renderTime` is what makes that
happen. The problem is that the divergence is unnamed, undocumented and derived from *whether a file
happens to be loaded*.

### 2.2 The defect this brief exists to fix

**Playback is the audio device.** With no audio file:

- `play()` fails with "no audio loaded"; `isPlaying()` is permanently false.
- `durationSeconds()` is 0, so the sequencer strip falls back to `max(sequence duration, 1.0)` and
  the progress signal is pinned at 0.
- `seekSeconds()` moves the modulator, the entities and the events — and then
  `updateTimelineClock` overwrites the timeline position with the free-running render clock on the
  very next frame. **A project without audio cannot be paused, cannot be seeked, and cannot be
  stopped.** It plays, from process start, forever.

With audio, playback is bounded by the audio file: a 30-second piece over a 45-second sequence stops
at 30, because "the end" is the end of the wav.

Everything else in §§4-15 of the brief — loop, rate, frame stepping, beat stepping, marker
navigation, timecode — is absent rather than broken.

### 2.3 ADR-012 named this class and it was never built

> **Timeline transport.** `Transport` owns play/pause/seek and the mapping between `renderTime` and
> audio position. — ADR-012, Decision

`Transport` does not exist. `Engine` absorbed the responsibility informally and delegated the state
to `AudioPlayer`. This work is the class ADR-012 specified, arriving late.

---

## 3. Audio

`AudioPlayer` (ADR-003, miniaudio) is sound and is **not** to be replaced. The real-time callback
copies samples, applies volume, advances an atomic play-head and feeds the analysis stream; position
is derived from the play-head frame index, never from wall time. Seeking is `seekFrames`. It is
sample-accurate and thread-safe already.

**One source, not many.** `Engine` holds one `audioFile_` and one `AudioPlayer`.
`seq::Sequence` has no audio at all — confirmed in `docs/cinematic-world-gap-analysis.md` §25:

> Multi-audio — **Does not exist** — `Engine` holds one `audioFile_`; `seq::Sequence` has no audio

So brief §8 (multiple audio files, clips with offsets, trims, gaps, per-clip analysis, a waveform
lane per clip) is **a feature that does not exist yet**, not a transport bug. Building an audio clip
model is a separate piece of work with its own analysis, serialisation and UI consequences.

**What this work does about it:** the transport is written so that it does not assume one file. It
owns the position; audio is a *follower* that it tells where to be, through one narrow interface. The
single-file player is today's only implementation of that interface. Adding clips later means adding
a second implementation, not re-opening the transport. That is the strongest correct version
available without inventing a clip model this brief did not ask to be designed, and it is recorded as
a limitation rather than claimed as done.

**Rate.** `AudioPlayer` has no rate control and miniaudio resampling at arbitrary rates is not wired
up. Per §15, this must not be faked.

---

## 4. UI

- **Control panel → "Control"** has the only transport: an Open Audio button, Play/Pause, Stop, a
  `position / duration` label and a scrub slider, all `BeginDisabled(!engine.hasAudio())`.
- **Sequence panel** has the timeline strip, the ruler, markers, the waveform lane, the actor lanes
  and a draggable playhead that calls `engine.seekSeconds` — and **no transport controls at all**.
  Zoom, snap and "Fit" are there; play is not.
- Keyboard: `Space` → `togglePlay`, `Left`/`Right` → ±5 s, handled in `Application::handleInputEvent`
  after the editor's shortcuts decline them (so arrows nudge a selection when there is one).
- There is an `app::EditSystem` dispatcher (ADR-101) for *document* edits. Transport is not a
  document edit and must not enter that history (§17); it needs its own command surface.

The right home for a real transport bar is the **Sequence panel header** — that is where the
timeline is, and §43 of the world-authoring spec warns against multiplying panels. The Control
panel's ad-hoc buttons should become the same widget so there is one implementation.

---

## 5. Problems found, in priority order

1. **Playback state is the audio device's state.** No audio, no transport. (§2.2)
2. **Duration is the audio file's length**, not the project's. A sequence longer than its audio is
   truncated; a sequence with no audio has no length.
3. **The timeline position is not writable without audio** — `updateTimelineClock` overwrites it.
4. **No loop, no rate, no frame rate, no frame stepping, no beat stepping, no marker navigation.**
5. **No timecode.** Position is `M:SS` from `formatTime`; there is no frame or bar display.
6. **The two clocks are unnamed.** Nothing in the codebase says which of `renderTime` and
   `timelineClock_.seconds` a new subsystem should read, so the next one will guess.
7. **`Engine::stop()` resets modulation state but does not move the timeline position** when there is
   no player, and `player_->stop()` seeks the *audio* to 0 while the render clock keeps running.
8. Multi-clip audio does not exist (§3).

---

## 6. Recommended integration

**One new class, `app::Transport`, owned by `Engine`.** Not a new service beside the engine: every
existing caller — UI, OSC, MIDI, AI tools, scripts, the render job — already goes through
`Engine::play/pause/stop/seekSeconds/isPlaying/positionSeconds/durationSeconds`. Keeping those
signatures and re-pointing them at the transport migrates every call site for free and leaves one
place that can answer "am I playing".

**The transport owns the timeline position.** `updateTimelineClock` becomes
`timelineClock_.seconds = transport_.positionSeconds()`, unconditionally. `FrameTime::renderTime`
keeps its job as the render clock and is documented as such: it equals the transport position while
the transport governs time (playing, or offline), and free-runs otherwise, which is exactly what it
does today for audio, generalised.

**Audio stays the master clock while it is playing** (ADR-012's rule, unchanged): the transport reads
the play-head rather than integrating a delta whenever the device is running at rate 1. With no audio
— or at any other rate — it integrates `deltaTime * rate`. One branch, one documented rule.

**Duration is the project's**, `max(audio, sequence, timeline)`, so the transport has an end to stop
at and the sequencer strip has a length that does not depend on a wav.

**Frame rate belongs to the project**, as a rational (`30000/1001` for 29.97), and is what frame
stepping and timecode are computed from. `RenderSettings::fps` stays what it is — a property of a
render job — and defaults from the project's.

**Loop and frame rate persist in the project file**, additively, under a `"transport"` object;
playback rate and playing state are transient (§17).

---

## 7. Risks

| Risk | Mitigation |
|---|---|
| `durationSeconds()` changing meaning breaks `--render` defaults | `RenderSettings::resolvedEnd` already takes audio and timeline separately and prefers an explicit `--range`; add a test pinning the resolved end for each combination |
| Timeline position no longer free-running changes existing scenes | It only changes what happens with **no audio**, where today there is no transport at all. Every audio path keeps its current behaviour, and that is what the existing tests cover |
| Arrow keys changing from ±5 s to ±1 frame | Deliberate (§13.5), documented, and the nudge path is unaffected because it only fires with a selection |
| A second undo history for transport state | Transport commands are explicitly not `EditAction`s and do not touch `EditSystem` |
| Audio drift at a non-unit rate | Rate ≠ 1 pauses the device rather than pretending; stated in the UI |

---

## 8. Tests that already exist

- `tests/unit/test_timeline.cpp` — track evaluation, cue location, loop length, serialisation.
- `tests/integration/test_timeline_engine.cpp` — the timeline against a running engine.
- `tests/unit/test_audio_player.cpp` — play/pause/seek/position on the device player.
- `tests/rendering/test_render_job.cpp` — offline frame counts and determinism.
- `tests/integration/test_sequence_project.cpp` — a sequence baked and evaluated through a project.
- `tests/unit/test_engine_stress.cpp` — the random-action harness, which already calls
  `togglePlay`/`seekSeconds` thousands of times and is the existing guard against transport crashes.

## 9. Tests that must be added

- Transport state machine: every command in every state, including the no-ops.
- Frame/time conversion at 24, 25, 29.97, 30, 50, 59.94, 60 — frame 0 is time 0, round-tripping, and
  no off-by-one at a frame boundary.
- Long-timeline precision.
- **Play with no audio at all** — the headline defect, and its negative control.
- Seek clamping at both ends; seek while stopped, paused and playing.
- Loop: wrap, repeated wrap without drift, toggling mid-play, degenerate ranges, seeking outside.
- Rate: position advance scales; audio goes silent off unit rate.
- Frame stepping, beat stepping, marker navigation.
- Determinism: the same position evaluates identically; the offline path is unaffected by wall time.
- Project round-trip of the persisted transport settings, including a file that has none.

---

## 10. What was built (2026-09-12)

Everything in §6 landed as described; see [ADR-102](../decisions/ADR-102-the-transport.md) for the
reasoning and `docs/help/sequencer-transport.md` for the user-facing account.

Not built, and why:

| §  | Requirement | Status |
|----|---|---|
| 8  | Multiple audio files, clips, offsets, trims, gaps | **Not built.** The model does not exist (§3). The transport owns the position and audio follows through one call, so a clip model is a second follower rather than a change here |
| 15 | Rate-corrected audio playback | **Refused.** `AudioPlayer` has no rate control; away from 1x the device is silenced and the bar says so, rather than drifting a second per second |
| 6  | Drop-frame timecode | **Not built.** Non-drop, stated in the UI and the docs. Nothing here has broadcast timecode to match, and drop-frame done wrong is worse than absent |
| 12 | Audio preview while scrubbing | **Not built.** Scrubbing moves the play-head; it does not play the samples under it |

## 11. Manual QA checklist

The checks a person has to make, because this repository cannot screenshot an ImGui frame. Each one
is a thing the automated tests cannot see.

**Opening and playing**

- [ ] Open a project with audio. Press Play. Picture and sound start together and stay together for a
      minute — watch a transient (a kick, a cut) rather than the readout.
- [ ] Press Space. It pauses. Press it again: it resumes from the same second, and the sound resumes
      without a click or a jump.
- [ ] Press Stop. The playhead returns to the start. Play from there.
- [ ] Open a project with **no** audio (any world scene). Play, pause, seek, stop. All four work, and
      the piece has a length in the readout.
- [ ] With nothing playing, confirm the world still moves — wind in the grass, water. Pause is not a
      freeze of the world.

**Seeking and scrubbing**

- [ ] Drag the playhead across the Sequence strip. The scene follows continuously; the camera, the
      characters and the automation all move. No stall longer than a frame or two.
- [ ] Scrub backwards over a one-shot event (a character's walk cue) and forward again. The character
      is where the piece says it should be, and the event does not fire twice.
- [ ] Scrub while playing. Playback continues from where you dropped it.
- [ ] Seek past the end and before the start. Nothing escapes the piece.

**Frames and beats**

- [ ] Click the time to cycle seconds → timecode → frames → bars. All four agree with each other.
- [ ] Tap `Right` ten times, then `Left` ten times. The frame number returns exactly.
- [ ] Hold `Right`. It walks frame by frame and does not run away.
- [ ] Set the Render panel's fps to 24 and confirm the frame readout and stepping change with it.
- [ ] `Shift+Left/Right` lands on beats you can hear, not a metronome's idea of them.
- [ ] `Up`/`Down` jump between section markers after pressing **Sections**.

**Looping**

- [ ] Set a loop over a chorus and turn it on. It wraps cleanly and keeps wrapping; the audio wraps
      with it; nothing accumulates over twenty laps.
- [ ] Move the loop's boundaries while it plays.
- [ ] Turn the loop off mid-play: the piece carries on past where the loop ended.
- [ ] Press Stop with a loop on: the playhead parks at the loop start.
- [ ] Save, reopen: the loop is where you left it.

**Rate**

- [ ] 2x and 0.5x: the picture changes speed, the sound goes silent, and the bar says so.
- [ ] Back to 1x: the sound returns, in sync.

**Editing while running**

- [ ] Edit a shot in the sequencer while it plays. The bake lands and playback continues.
- [ ] Move an object in the world editor while paused, then play. Undo still only undoes the edit —
      no transport command appears in the history.
- [ ] Open a different project while playing. The new one is stopped at its own start.

**Rendering**

- [ ] Set a loop and a 2x rate, then render a range. The output ignores both.
- [ ] Render the same range twice and compare the files. Identical.
- [ ] Render frames 0..N and confirm the first and last frames are the ones you asked for.

**Endurance**

- [ ] Play a long piece to its end. It stops at the end rather than running past it.
- [ ] Leave it looping for ten minutes and confirm picture and sound are still together.
