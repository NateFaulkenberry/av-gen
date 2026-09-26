# ADR-911: A follow camera reads a filtered reference of its subject's history

**Status:** Accepted
**Date:** 2026-09-26
**Found by:** the GV3 revision brief, section 2 ("the cameras are still occasionally too wobbly,
particularly shots following the aliens"), and the camera audit that measured why.
**Follows:** ADR-245 (authored cameras), ADR-700 (seek checkpoints), ADR-703 (HIST), ADR-870 (the
replayed signal bus), ADR-267 (the scrub already matches the play)
**Implemented by:** `scene::followReference`, `SubjectTrail`, `softFloor` and `headingOf`
(`src/scene/follow_reference.{hpp,cpp}`); `Composition::evaluateAuthoredCamera`,
`appendCameraHistoryNeeds`, `subjectHead` (`src/scene/composition.cpp`); `CameraRig`'s new fields and
their JSON, and the Cameras panel's rows for them, `scene::followControls` (`src/scene/camera_rig.{hpp,cpp}`);
`ControlPanel::drawFollowControls` (`src/ui/control_panel.cpp`); `Engine::refreshHistorySubscriptions`
(`src/app/engine.cpp`); the Director compiler's `followRig` (`src/directing/compiler.cpp`)
**Tests:** `tests/unit/test_follow_camera.cpp` (`[adr911]`; the panel's rows are also `[ui]`), below
**Measured with:** `avgen_cast_trace --camera` and `tools/camera_stability.py` (ADR-913)

## Context

A follow rig read its node directly. The eye stood at the node plus an offset, the aim looked at the
node plus an aim offset, and `followLagSeconds` delayed the eye by reading a per-frame trail. The
camera audit rebuilt every GV3 follow rig from the cast trace with the engine's own arithmetic and
found five causes of the wobble:

1. **The node carries the stride bob.** The camera read the drawn position, bob included. On a
   straight walk (s03) Ember's height above the ground rises 0 to 37 cm at 1.03 Hz; an audio reaction
   on the bounce (`audio.bass -> liveliness/bounce`) doubles what the stride alone predicts. Eight of
   the eleven follow shots were welded to it.
2. **The lag turned the bob into a nod.** Only the eye lagged, so it bobbed 0.3 s after the aim and
   the view pitched with the stride: 1.14 degrees HF RMS on s19, 1.22 on s38, 0.39 on s18.
3. **Lag-free rigs translated rigidly,** bobbing 24-35 cm and taking every start, stop and turn one
   to one, with the subject pinned dead centre and the world bouncing behind it.
4. **The clearance was a hard `max`,** a kink in the eye's height: on s20 the vertical velocity
   stepped from 0.22 to 1.44 m/s in one 50 ms sample where the floor let go.
5. **`followLocal` turned the offset by the drawn rotation,** sway, nod and slope tilt included, and
   the Director compiler's chase rig on top of that whipped round at the aliens' turn rate: 144
   degrees a second at the 95th percentile, the eye at 10 m/s.

And the lag was not seek-exact: its trail was seen, not re-derived, so for `lag` seconds after a seek
-- the head of every render of a range -- the camera ran un-lagged.

There was no position, rotational or target smoothing anywhere. The owner wants controlled cinematic
movement, not procedural wobble, and not a frozen camera.

## Decision

**A follow rig reads a REFERENCE of its subject: the subject's own past through a causal, finite,
critically damped kernel** (`scene/follow_reference.hpp`),

    h(tau) = w^2 tau e^(-w tau),   w = 2 / T,   truncated at 8 / w = 4 T,

whose mean delay is exactly T. The kernel runs over the subject's HIST ring (ADR-703): taps every
1/60 s, binned from the kernel's closed-form distribution function so the weights need no table and
T -> 0 approaches the raw node rather than a one-step delay. The trail reads HIST's samples strictly
before `now`, then the subject as it stands this frame (the head), and holds its ends.

There is no integrator anywhere, so the reference is a pure function of the history. The ADR-700
seek replay rebuilds HIST and every checkpoint carries it, so **a scrub to t and a render from t read
the same history a play to t recorded** -- and the camera lands on the same pose. That is the
property a spring could not have, and the reason this is a kernel.

**The knobs** (all on `CameraRig`, all 0 by default, and all 0 is the raw node):

- `followSmoothSeconds` -- T for X and Z.
- `followVerticalSmoothSeconds` -- T for the height, separately: the bob wants a longer constant.
- `followLead` (0..1) -- adds `lead * T * d/dt` of the smoothed horizontal path. 1 cancels a steady
  walk's delay exactly, using the discrete kernel's own mean delay. Horizontal only: a lead on the
  height re-injects the bob.
- `followGround` -- the subject walks on the terrain: its height is smoothed RELATIVE TO THE GROUND.
  `y = F_v[P.y] + G(F_h[P.xz]) - G(F_v[P.xz])`: the vertically filtered height, moved from the ground
  under the vertically-lagged path to the ground under the horizontally smoothed one, where G is the
  terrain height averaged over a 2.5 m footprint (the centre twice and four points on the circle: a
  body's own grounding weighting, without its lookahead). The
  stride is in P.y and is filtered; the descent's lag shrinks from the vertical constant to the
  horizontal one. Not for things that fly.
- `followHeadingSmoothSeconds` -- with `followLocal`, T for the heading the offset turns by. The
  heading is the yaw alone (the facing's +Z projected on the ground), unwrapped tap to tap.
- `followLagSeconds` now reads the same history: the eye reads the reference at `t - lag`.

**The eye and the aim share one reference:** eye = ref(t - lag) + offset, aim = ref(t) + aimOffset.
The audit showed why this is a rule and not a convenience: smoothing only the eye's height, or
delaying only the eye, is itself a 0.7-1.0 degree nod. The knobs filter every node the rig reads, so
an aim-only rig (a fixed eye watching something move) gets look-at damping from the same numbers.

**The clearance floor is a softplus on the filtered eye:** `floor + w ln(1 + e^((y - floor)/w))` with
w = 0.25 m. Never below the floor, within 5 mm of the free eye a metre above it, and no step in the
eye's vertical velocity.

**`followLocal` turns by the filtered yaw,** not the drawn rotation: "behind" swings round after a
turn instead of whipping, and never rocks with the sway or the slope.

**HIST subscribes the rigs' subjects.** `Composition::appendCameraHistoryNeeds` lists every node a
lagged or smoothed rig reads, as deep as it reads (lag + 4 T + two taps), and the engine folds them
into its subscription set. A rig that reads more than HIST's 16 s is refused at load. A rig asking
for history that nothing records warns once and follows the raw node, rather than looking like a
working rig.

**Deleted, per ADR-441:** `FollowTrail`, `recordFollowTrails` and `followTrailAt`; and ADR-245's
aim-follow smoother (`setAimFollowSmoothingMs`, `clearAimFollowState` and its state), which
integrated across frames and was unreachable -- its constant defaulted to 0 and no file, tool or
panel ever set it.

**The Director compiler's chase and follow rigs use the knobs:** `followSmoothSeconds` 0.3,
`followVerticalSmoothSeconds` 0.8, `followLead` 1, `followGround` on, `followHeadingSmoothSeconds`
1.2, and no lag (was 0.25 s on the eye alone).

**Every knob is adjustable in the Cameras panel, under the name of what it does to the picture**
(the owner's rule that anything visible must be findable and adjustable). The knobs are rig settings,
not parameters, so the Parameters panel cannot show them, and before this nothing in the app did --
nor the lag, clearance or `followLocal` that predate it. `scene::followControls()`, beside the fields
in `camera_rig.hpp`, is a UI-free table of them (label, units, range, which cameras a row acts on,
getter and setter); `ControlPanel::drawFollowControls` draws it in the selected camera's section of
the **Cameras** panel, under its lens slider, headed "following its subject" with the node it follows
named beneath:

| Cameras panel row | key | range | acts on |
|---|---|---|---|
| follow smoothing (s) | `followSmoothSeconds` | 0-3 s | a camera with a subject (`followNode` or `aimNode`) |
| height smoothing (s) | `followVerticalSmoothSeconds` | 0-3 s | a camera with a subject |
| keep up with the subject (0-1) | `followLead` | 0-1 | follow smoothing above 0 |
| follow the ground | `followGround` | on / off | either smoothing above 0; terrain in the scene |
| follow lag (s) | `followLagSeconds` | 0-3 s | a camera whose eye follows (`followNode`) |
| stay behind as it turns | `followLocal` | on / off | a camera whose eye follows |
| turn smoothing (s) | `followHeadingSmoothSeconds` | 0-3 s | "stay behind as it turns" on |
| ground clearance (m) | `followClearance` | 0-10 m | a camera whose eye follows; terrain in the scene |

A row that would do nothing on the selected camera is drawn greyed, with the reason in its tooltip,
rather than hidden or left live; the main camera, placed by the legacy `camera/*` block, has none.
An edit is one undo step (a drag, press to release) installed through `Engine::setCameraDirection`,
which re-subscribes HIST, so smoothing turned on for a camera that read no history records its
subject from that frame on. The ranges keep every reachable setting inside HIST -- a 3 s lag and four
3 s constants read 15 s of its 16 -- so the panel cannot make a rig `validate` refuses. `followLocal`
is a row because turn smoothing does nothing without it. Which node a camera follows is not a row
(it is the scene's), and the two offsets were already parameters (`cameras/<slug>/followOffset`,
`.../aimOffset`, in the Parameters panel).

## Consequences

**Measured on GV3** with the engine's own camera (`avgen_cast_trace --camera` at 60 fps,
`tools/camera_stability.py`; ADR-913). "Before" is the unmodified engine on GV3 as authored, over the
whole film; "after" is this engine with the knobs recommended in ADR-913 -- walkers
`followSmoothSeconds` 0.3, `followVerticalSmoothSeconds` 1.0, `followLead` 1, `followGround` on, no
lag; s25 0.3 / 1.2 / 1 with its eye raised clear of the floor -- traced over each shot's own window
from a seek (which reproduces the whole-film trace exactly: ADR-913).

| shot | pitch HF (deg) | yaw HF (deg) | eye vertical HF (cm) | subject off-centre max (%) | travel |
|---|---|---|---|---|---|
| s03 | 0.000 -> 0.000 | 0.000 -> 0.000 | 11.17 -> 1.28 | 0.0 -> 7.6 | 1.00 -> 1.00 |
| s09 | 0.000 -> 0.001 | 0.000 -> 0.000 | 7.09 -> 0.66 | 0.0 -> 7.2 | 1.00 -> 1.01 |
| s10 | 0.001 -> 0.001 | 0.004 -> 0.004 | 0.00 -> 0.00 | 0.0 -> 0.0 | (subject idle) |
| s13 | 0.223 -> 0.037 | 0.000 -> 0.000 | 0.65 -> 0.62 | 0.0 -> 3.7 | 1.00 -> 1.00 |
| s18 | 0.386 -> 0.017 | 0.373 -> 0.000 | 3.36 -> 1.88 | 0.0 -> 11.4 | 1.00 -> 1.02 |
| s19 | 1.139 -> 0.000 | 0.412 -> 0.000 | 7.94 -> 1.99 | 0.0 -> 13.6 | 1.00 -> 1.01 |
| s20 | 0.107 -> 0.040 | 0.000 -> 0.000 | 8.29 -> 0.82 | 0.0 -> 4.7 | 1.00 -> 1.02 |
| s25 | 0.022 -> 0.014 | 0.087 -> 0.091 | 2.36 -> 1.57 | (fixed target) | 1.00 -> 1.03 |
| s28 | 0.000 -> 0.001 | 0.000 -> 0.000 | 9.06 -> 0.78 | 0.0 -> 9.8 | 1.00 -> 1.00 |
| s36 | 0.000 -> 0.006 | 0.000 -> 0.000 | 6.02 -> 1.58 | 0.0 -> 7.7 | 1.00 -> 1.08 |
| s38 | 1.218 -> 0.000 | 0.278 -> 0.000 | 7.19 -> 0.92 | 0.0 -> 6.7 | 0.93 -> 1.02 |

- **Every follow shot passes the audit's bar** (pitch and yaw HF <= 0.1 degrees, eye vertical HF
  <= 2 cm, subject off-centre <= 20%, travel about 1); before, ten of the eleven failed it. Pitch HF
  is at most 0.040 degrees against up to 1.22; yaw HF is at most 0.004 except s25's 0.091, a
  fixed-target shot whose view turns as its eye travels, unchanged. The eye no longer bobs 24-35 cm:
  vertical HF 0.6-2.0 cm against 2.4-11.2. The camera still travels as far as its subject (1.00-1.08
  of the subject's path), and the subject drifts gently in frame (3.7-13.6% from centre at most)
  instead of being pinned dead centre while the world bounces behind it. s19 is the closest call, at
  1.99 cm: what is left on the eye's height there is the terrain itself, which `followGround`
  follows -- the ground under Vane carries 2.3 cm of relief at walking frequencies. The footprint the
  ground is read over was widened from 1.5 m to 2.5 m for it; at 1.5 m s19 kept 2.06 cm and s18 2.00.
- **Seek-exact.** On GV3 after a seek to 123 s (s19, mid-descent), the old engine's s19 eye was
  0.92-0.98 m from the played camera for the shot's rest -- the trail ran un-lagged after a seek, the
  head of every range render. This engine: 10 um with GV3's own rigs (the trace's resolution), and
  40 um with the recommended smoothing. The residual is the simulation's, not the camera's: on both
  engines a cow (cow-3) is 9 mm off its played position 3 s after that seek. The unit test's scrub
  equals its play bit for bit.
- **A 30 fps play draws the camera a 60 fps play does,** to 0.07 mm on the test's walk (one 60 fps
  frame of delay is 5 cm there): the kernel reads HIST by time, not by frame.
- **Changes to existing scenes with no new keys** (this engine on GV3 as authored against the old):
  - Lagged rigs are identical in a play from zero (s13 0.223, s19 1.139, s38 1.218 degrees, unchanged)
    and now also after a seek or at the head of a range render, where they used to run un-lagged.
    At the very head of a film a lagged eye now holds the subject's first position for `lag` seconds
    instead of running un-lagged.
  - The soft floor moves the eye where it rides within half a metre of the floor: up to 0.17 m higher
    where the free eye is under it (s13 and s25 ride it), a few millimetres a metre above it. Without
    the vertical smoothing, the soft floor also compresses the eye's bob while the aim keeps all of
    it, a small nod: s20 0.107 -> 0.166 degrees, s36 0 -> 0.018, s28 0 -> 0.012, s09 0 -> 0.006 (and
    s18 0.386 -> 0.375). The velocity step it replaces is gone. With the recommended smoothing all of
    these are at most 0.041.
  - `followLocal` rigs turn by the yaw alone: the Director's compiled chase rigs no longer rock with a
    character's sway or tilt with a slope. GV3 has none.
  - A root that rotates or scales moves the followed subject where it is drawn: the subject is now
    read through the root fold, as HIST records it. Every follow rig in the repository has an
    identity root, for which this is bit-identical.
  - Every lagged or smoothed rig's subject is now a HIST ring, so it also publishes
    `entity.<node>.*` signals and rides in every ADR-700 checkpoint: about 12 KB per ring per
    checkpoint at GV3's depths (4-5 s), some 15 MB over the film for its five subjects, against
    ADR-700's 256 MB cap.
- **The Director's chase,** modelled on GV3's walkers in the audit's four windows with the new
  defaults: the view turns at 47-96 degrees a second at the 95th percentile (was 143-145), the eye
  moves at 3.1-5.3 m/s (was 10), and it nods at most 0.23 degrees (was up to 1.3). Measured in the
  engine with the compiled rig on those four shots (ADR-913's windows), it agrees: 48-97 degrees a
  second, pitch HF at most 0.24, the subject within 20% of centre. The chase's yaw HF stays 1-1.7
  degrees, outside the follow shots' bar by design: a camera behind a body that pivots on the spot
  turns through the whole angle, and the heading constant chooses only how fast.
- **Not done:** ADR-158's aim-follow (the Auto-director nudging the main camera's baked aim by a
  hero's walk) still adds the hero's raw position, stride bob included. Its unreachable smoother was
  removed rather than rewired, because the aim-follow's zero point is where the hero stood when the
  keys were baked, not a history the kernel can read. A director shot on a walking hero is the case
  that would want it.
