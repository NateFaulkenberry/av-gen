# ADR-892: A continuous take owns the frame

**Status:** Accepted
**Date:** 2026-09-25
**Decided by:** the owner, answering ADR-891's open question
**Amends:** ADR-245 (camera precedence), in Continuous mode only
**Implemented by:** `Composition::continuousTake` and `scene::continuousTakeCamera`
(`src/scene/composition.*`, `src/scene/camera_rig.*`); `app::installSequence` and the park,
resume and discard paths (`src/app/camera_director.cpp`); the project's `cameraContinuousTake`
and `parkedDirector.continuousTake` keys (`src/app/engine.cpp`)
**Tests:** `tests/unit/test_continuous_owns_frame.cpp` (`[adr892]`)

## Context

ADR-891 fixed the two ways the Auto-director's own Continuous take cut itself. It left ADR-245's
precedence alone, which applies in every mode:

0. a locked authored shot;
1. an event camera whose scenario is running;
2. an authored shot;
3. the default camera (the Auto-director's).

On `examples/world/glowmere-valley-2-multicam.json` that still gave 18 camera changes in
Continuous mode. The authored Valley Wide shot holds 0–7 s and 26–31 s, and the UFO Watch event
camera takes the frame at each of the ten abductions. ADR-891 asked the owner which should win.

**The owner's answer (2026-09-25):** in Continuous mode the continuous take owns the frame. No
authored shot and no event camera makes a camera change.

## Decision

**While the director's cut in force is a continuous take, the active camera is the main camera at
every instant.** `Composition::applyParameters` asks `continuousTakeCamera` instead of
`resolveActiveCamera`. The answer is the main camera, reason `Default`, unbounded. No shot, locked
or not, and no event claims the frame.

**The author's shots are ignored, not deleted.** The camera track and the event cameras stay in the
scene document untouched. The event table is still observed. Directing again in Edited sequence or
Song Mode resolves exactly as ADR-245 says, because the resolver was never changed.

**It is a fact about the bake, stored with the bake.** `installSequence` sets
`Composition::continuousTake` to `mode == ContinuousShot`. So an Edited or Song bake clears it.
Releasing the camera parks it with the rest of the cut (`ParkedDirectorsCut::continuousTake`),
resume restores it, and discard clears it. A scene swap keeps it, as it keeps the aim-follow table.
It is saved as `cameraContinuousTake: true` beside `cameraAimFollow`, and inside `parkedDirector`,
and only when true.

It is not read off `AutoDirectorSettings::mode`. That mode defaults to Continuous shot, so every
project that never directed, and every project cut before this, would lose its camera track. With a
stored flag those projects load `false` and render byte-for-byte as before. A project directed in
Continuous mode before this ADR keeps ADR-245's order until it is directed again.

### Precedence, per mode

| Claim on the frame | Continuous shot (a continuous take is the cut) | Edited sequence | Song | Never directed, or cut before ADR-892 |
|---|---|---|---|---|
| Locked shot (authored, or Song's for a Locked section) | ignored | 0 (wins) | 0 (wins) | 0 (wins) |
| Event camera | ignored | 1 | 1 | 1 |
| Unlocked shot (authored, or Song's own) | ignored | 2 | 2 | 2 |
| The director's camera (main) | **always** | 3 (default) | 3 (default) | 3 (default) |

Parked (the camera handed back to the viewport) behaves as the right-hand column: the take is not
steering, so it does not own anything.

### How an event reads in a continuous take

**It does not cut, and in this change it does not reframe either.** The brief asked for the event to
be seen inside the take, for example by easing the aim toward the saucer over 1–2 s and back. It
also allowed a fallback if that was large or risky: the event camera simply does not take the frame.
This ADR takes the fallback. The reason is determinism.

A reframe needs a weight that rises when the event starts and falls when it ends. Scrub and play
must agree on it (ADR-800, ADR-870, ADR-891). Nothing available gives that weight from the current
state alone:

* **The event table** (`observeCameraEvents`) is history. It records when it *saw* a scenario
  engage, and a seek clears it. After a scrub into an abduction, the span starts at the landing
  instant, not at the abduction's start. An ease read from it would differ from a play.
* **Staging's replayed state** does survive a seek (ADR-671, ADR-870). But it has only the current
  beat and that beat's start. UFO Watch engages across four beats (`aim`, `beam`, `abduct`,
  `depart`). Neither the engagement's start inside `beam` nor its end after `depart` can be
  recovered. An ease keyed to the current beat jumps whenever `aim` is shorter than the ease.

Making either one exact means running the camera-event observation inside the seek replay and
carrying it in the seek checkpoints. That also changes when ADR-245's event camera cuts after a
scrub in Edited and Song modes. That is out of scope for a change that must leave those modes
byte-for-byte alone.

**Follow-up:** an event reframe in the continuous take. It should observe camera events during the
seek replay (the ADR-870 path) so a span's start is the same after a scrub as after a play. Then it
should ease the main camera's aim toward the event camera's `aimNode` over about 1.5 s from the
span's start, and back over the tail. It must be tested for scrub/play agreement at instants inside
an abduction.

## Measured

`tests/unit/test_continuous_owns_frame.cpp` plays the multicam film through the Engine at 30 fps,
after the owner's route (saved in Song, directed, then re-directed in the new mode).

* **Continuous shot:** 6,790 frames, **0 camera changes**. Every frame is the main camera by
  default. No frame pair turns 20° or more or moves 5 m or more. The abductions still ran: the
  director could see an event on 3,150 frames. It just no longer cut to it. All three authored shots
  are still on the track.
* **Edited sequence (the control):** the same film cuts on 22 frames. Valley Wide holds 360 frames by
  its shot and UFO Watch holds 3,251 frames by event, so ADR-245's precedence is intact outside a
  continuous take.
* **Fails before:** on the same tests without the change, Continuous shows the Edited numbers:
  22 camera changes, four different claims, and 22 snaps of 17–164° and up to 249 m. Those are the
  cuts to and from Valley Wide, the hero shot and UFO Watch. (ADR-891 counted 18; this probe counts
  every change of live camera, including the ones into and out of the hero shot on the main camera.)
* The flag's lifecycle is tested too. It is set by a Continuous bake, and a seek into an abduction
  still lands on the take. It survives save and reload. Release parks it, and it is saved inside
  `parkedDirector`. Resume restores it. An Edited bake clears it and writes no key.

## Consequences

* ADR-245's "event cameras are how a Continuous cut sees the world's events"
  (`docs/auto-director.md`) is no longer true. The doc now says a continuous take does not see
  them, and names the follow-up.
* The Cameras panel says so when a continuous take is the cut, so a camera track that is not being
  played does not look broken.
* ADR-891's `[adr891]` measure compared only frames in the director's take. Here the take is the
  whole film, so that measure now covers every frame.
