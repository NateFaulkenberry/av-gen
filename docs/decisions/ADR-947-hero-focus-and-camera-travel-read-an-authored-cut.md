# ADR-947: Hero focus and camera travel read an authored cut

**Status:** Accepted
**Date:** 2026-09-28
**Follows:** ADR-207 (world effects gated on the cut), ADR-245 (multiple cameras, the camera track),
ADR-582 (the parked director's cut), ADR-702 (Ground Pulse as an entity-owned effect)
**Implemented by:** `scene::cameraSubject`, `scene::shotSubject`, `scene::authoredShotSpans`
(`src/scene/authored_cut.{hpp,cpp}`); `CameraShot::subject` (`src/scene/camera_rig.{hpp,cpp}`);
`Engine::effectShots` (`src/app/engine.{hpp,cpp}`); the camera track's "focuses on" control
(`ControlPanel::drawCameras`, `src/ui/control_panel.cpp`)
**Tests:** `tests/unit/test_authored_cut_focus.cpp` (`[adr947]`, CPU);
`tests/rendering/test_authored_cut_gpu.cpp` (`[adr947]`, GPU -- written and built, not yet run: see
Consequences)

## Context

Two effect activations gate on the cut: `heroFocus` (the Ground Pulse, the "Hero Pulse" of ADR-207/702,
which fires while the cut holds its own hero) and `cameraTravel` (the Travel Beam, which fires while
the camera moves toward the hero the cut hands off to). Both read `world::ShotSpan`s, and until now
the only writer of those spans was the Auto-director's bake (`app::Sequence::shotSpans`, installed by
`installSequence`, saved as `cameraShotSpans`).

A film cut by hand -- the scene's `cameraDirection.shots`, cameras with aim and follow nodes, which is
how Glowmere Valley 3 is cut -- has no such spans, so both activations never fired. GV2 multicam's 16
Hero Pulses were deleted from GV3 for that reason, and GV3 has been faking them with cue markers
(`tools/gv3/look.py`, `apply_hero_pulses`). The owner has asked for them several times: "the hero
pulse that used to swell out from the mushroom and across the ground".

## Decision

**1. The authored camera track is a second source of spans.** `Engine::effectShots()` is what every
cut-gated effect now reads (the effect context, route liveness, the Effects panel's warning):

- the director's spans (`shotSpans()`) whenever there are any -- Song mode and every baked cut,
  **byte-for-byte unchanged**, same storage;
- otherwise `scene::authoredShotSpans(cameraDirection)`;
- nothing under a continuous take (ADR-892), which owns the frame.

The derived spans are cached against the camera track, the scene generation and the continuous-take
flag, and rebuilt on the first read after any of them changes. They are a pure function of the camera
track, so a seek lands on the same span -- and the same pulse phase -- as a play (ADR-089/091).

**2. Who a shot is about.** In order: the shot's own `subject` (new, "focuses on"), else its camera's
**aim node**, else its camera's **follow node**, else nobody. The main camera is never about anybody
(its placement is the legacy `camera/*` block). Aim-then-follow is the rule the Creative Critic's
adapter already used (`directing_evaluate.cpp`, `shotsDocument`, which now calls `shotSubject` so the
two cannot drift) and the rule GV3's framing tool uses (`tools/gv3/framing.py`:
`subject = rig.aim or rig.follow`).

**3. Holds.** The track is flattened the way `resolveActiveCamera` reads it without events: the last
shot containing an instant whose camera exists, else the default camera. Each stretch whose shot has
a subject is a hold (`spotlight`) over the whole stretch. A new shot is a new hold, so a pulse's
repeat restarts at a cut, as it does between Song-mode spans.

**4. A cut is a camera change.** Where the live camera changes and the incoming stretch has a
subject, a travel span opens **at the cut**, from the outgoing subject to the incoming one (the
`handoff`), for `kCutTravelSeconds` = 2.5 s -- long enough for the beam's default delay, fade-in and
fade-out (0.15 + 0.5 + 1.1 s) -- or the shot's blend when that is longer, and never past the incoming
shot's end. It overlaps the start of the incoming hold on purpose: the beam sweeps toward the hero
while that hero's ring starts. A cut back to the same camera is not a change. A cut to a shot about
nobody travels nowhere.

**5. "Focuses on" is a control.** Most of GV3's shots are still cameras with no aim or follow node --
"The lantern from the hollow", "Spores falling from the bloom" -- so the aim/follow rule alone can
never reach the mushrooms' pulses (measured below). `CameraShot::subject` (JSON `"subject"`, written
only when set) names the hero a shot is about without changing the camera. It is edited on each row of
the **Cameras panel → Camera track**, as a combo reading "focuses on <hero>", "focuses on <node>
(camera)" when the camera's rule decides, or "focuses on nothing". The panel says, above the rows,
what focus drives ("its Hero Pulse fires for the shot, and a Travel Beam sweeps toward it at the
cut"), and says instead that the Auto-director's schedule decides when a director's cut is in force.

**Event cameras are not part of it.** Which scenario took the frame is recorded state
(`cameraEvents_`), and the schedule has to be known before the frame is. A shot an event camera
interrupts still counts as holding its subject.

**Subject positions.** A span's `subjectPosition` is only read for a World-owned `FocusHero` endpoint
whose subject is not a hero. Derived spans take it from the document -- the hero's authored point, else
a root node's authored position -- so they are the same whenever derived. An entity-owned pulse (the
canonical Hero Pulse) spreads from its owner's live position regardless.

## Consequences

- **Song mode is unchanged.** `effectShots()` returns the director's own vector when it is non-empty;
  a test loads GV2 multicam (45 baked spans plus an authored track of its own) and checks both the
  identity and that the pulses fire on the Song spans, with a control arm showing its authored track
  would read differently.
- **Projects with an authored cut and cut-gated effects change**: those effects now fire. No tracked
  example had such effects live on an authored cut except GV3's (which had removed them). The
  multicam's own authored track is shadowed by its Song spans, as before.
- The Effects panel's "No directed camera" warning becomes "No cut with a subject", and the route
  liveness reasons name both sources.
- The Critic's shot document now honours "focuses on" (unchanged for every existing file, none of
  which sets it) and no longer names the main camera's aim node as a subject.
- **GV3 measured** (CPU probe, a scratch copy of gv3-int's current project under `build/herofocus/`,
  its 15 pulses restored from GV2 multicam on native `heroFocus`): with the aim/follow rule alone the
  five aliens' pulses fire in their shots and the elder's in its orbit, and **none of the nine other
  mushroom pulses ever fires**, because no camera aims at or follows them. Setting "focuses on" on the
  shots about them is what reaches them; see the stream report for the shot table. Played at 10 Hz
  over the 226 s film (40 shots, 32 derived spans, 16 travels):

  | | elder | lantern | bloom | umbra | other 6 caps | rook | tide | sage | ember | vane |
  |---|---|---|---|---|---|---|---|---|---|---|
  | aim/follow only | 14.4 s | 0 | 0 | 0 | 0 | 7.1 s | 7.0 s | 14.4 s | 17.1 s | 29.0 s |
  | + "focuses on" from the shot labels (16 shots) | 84.6 s | 3.4 s | 3.3 s | 3.3 s | 0 | 7.1 s | 7.0 s | 14.4 s | 17.1 s | 29.0 s |

  GV3's current cut has no shot about the spire, veil, cairn, ridge, scree or ember cap, so those
  pulses fire only if a shot is set to focus on them. GV2 multicam, measured the same way, reads its 45
  Song spans (`director's schedule`), and all 16 of its pulses fire (6.6-38.0 s each).
- **Not yet run:** the GPU test `[adr947]` was built but not run -- the GPU was held by GV3's preview
  render and the owner stopped renders for the night. The coordinator runs it with the GPU suite at
  integration.
- Not a control: `kCutTravelSeconds`. The Travel Beam's own `lifetime` shortens its window; nothing
  lengthens it past 2.5 s on a hard cut. If an artist asks for longer, it should become a named control
  rather than a new constant.
