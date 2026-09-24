# ADR-756: Validate per item, compile the feasible part into a staging copy, never overwrite a hand edit

**Status:** Accepted
**Date:** 2026-09-24
**Related:** ADR-755 (the Plan), ADR-754 (capabilities), ADR-752 (one undo), ADR-245 (two shot
types, two camera architectures), ADR-098 (event tiers), ADR-091; spec §17–§21, §34, §56
**Implemented by:** `directing::validatePlan`, `cameraSupport`, `activityFor` (`src/directing/validator.*`);
`directing::compilePlan`, `contentOf`, `fingerprint` (`src/directing/compiler.*`); `directing::SceneFacts`;
`app::sceneFactsFor`, `app::applyCompilation` (`src/app/directing_context.hpp`)
**Tests:** `tests/unit/test_directing_compile.cpp` (`[directing][compile]`, `[directing][validate]`)

## Decision

**Validation is per item, and it decides what is built.** Every issue names the plan item it
concerns. An error blocks that item and everything that depends on it:
- an unresolved subject blocks every item that names it;
- an unplaceable time blocks its item;
- a cue on "rook.backflip_peak" is `BLOCKED` when the backflip is.

Everything else still compiles. This is spec §34's "construct the feasible portion". Nothing is
substituted: a missing backflip is `CAPABILITY_UNAVAILABLE`, naming the airborne actions that do
exist and suggesting a jump. The model may propose that substitution as a revision, which the
person then sees.

**The checks** (spec §17's categories, in the form this build can answer):

- **Reference:** subjects, rigs, parameters, plan events.
- **Capability:** each performance action needs an activity on the character's card. `run_to`
  needs `run`; an action with no mapping needs an activity of its own name. When the rig did not
  load, the cause says so.
- **Spatial:** an over-the-target action must clear the obstacle. The requirement is the obstacle's
  authored height plus clearance (default 0.25 m), checked against the character's jump apex.
- **Timing:**
  - a shot must lie inside the audio;
  - it must not overlap another shot, whether in the plan or already in the sequence (this plan's
    own earlier revision excepted);
  - shot names must be unique;
  - a cue must not end before it starts;
  - a baked cue must not target a parameter the author keyed, because `seq::install` owns every
    track on a parameter it bakes to and would erase their keys.
- **Camera:**
  - moves must be compilable (see below);
  - an unlocked shot in a scene with an event camera is a `CAMERA_CONFLICT` warning, and locking
    it is the fix;
  - a named rig must exist.
- **Determinism:** a directed or goal performance in a baked plan is an error. A cue on an event a
  live performance emits is an error in a baked plan and a warning otherwise.
- **Not compiled yet (reported, never silently dropped):**
  - performances (Slice 2);
  - retimes and time-varying camera moves such as `rise_over`, `pass`, or a second move within
    one shot (Slice 3);
  - effect cues (after ADR-702 merges).

**Camera compilation is chosen by what the subject is** (`cameraSupport`):
- A **place** (a hero, a node) is framed by a `seq::ShotCamera` move built from the engine's own
  presets (wide, close, top_down, reveal, push_in, pull_out, hold, orbit). It is baked at install
  and editable in the shot inspector as that move.
- A **character** is followed by a `scene::CameraRig` with `followNode`/`aimNode` on its node. It
  is evaluated every frame, because a character's position belongs to its simulation, and it is
  editable in the Cameras panel.
- `low_angle` modifies whichever of the two it accompanies.
- **Every plan shot writes both shot types**: the `seq::Shot` (the framing) and a
  `scene::CameraShot` (which camera is live, locked when the plan says so). They cannot disagree,
  and a Director shot is never silently overridden by the camera track.

**Parameter cues** compile to a baked `SequenceEvent` (Time trigger, SetParameter) in **Add** mode,
with the amount (value − the parameter's base). The first version compiled a Replace. The install
test caught the engine's own warning: a Replace has nothing to ramp from or return to, so the
flash would have held from t = 0 for the whole film. An Add's identity is known, so the ramp and
the return are exact. Cues therefore target single-component parameters.

**A staging copy.** `SceneFacts` carries copies of the sequence and the camera collection.
`compilePlan` writes into those copies and returns them with the diff, so a dry run changes nothing
(tested). `applyCompilation` installs both, plus the plan (inserted, or replacing its revision), as
**one** command through `EditCapture`. Undo takes back the content and its provenance together. A
refused install is undone before returning.

**Revisions and hand edits.** A plan whose id the project already holds is a revision: its
`revision` increments, and its previous `produced` content is taken out of the staging copy before
compiling. **An item is kept or replaced whole.** If any piece of an item's previous content no
longer matches its fingerprint, the person edited it by hand:
- every piece of that item stays exactly as they left it;
- the item is `HAND_EDITED` and blocked;
- it stays in `produced`, so its provenance is not lost.

Replaced items are `~` in the diff, and items the revision drops are `-`.

**The diff** reads as intent, one line per change:
- `+ Shot "rook-umbra" 01:30.000-01:35.000; cut to camera 'rook-umbra'`
- `+ Camera "rook-umbra": low-angle chase on rook (follows node 'rook', 0.4 m up, 3.0 m behind)`
- `! CAPABILITY_UNAVAILABLE: rook does not have a backflip capability. Available airborne actions: fall, jump, land.`

Findings come last, after the changes, so the person reads what will happen and then what will not.

## Consequences

The benchmark against spec §34, on Glowmere Valley 2 multicam:

| Step | Result |
|---|---|
| 1–3: resolve Rook, Umbra and 1:30 | Done |
| 4–7: discover capabilities; report the missing backflip; evaluate spatial feasibility | Done, including 5.75 m required against a 1.1 m apex |
| 8: construct the feasible portion | Done: the shot, a low-angle chase rig on Rook's node, a locked cut |
| 9: camera plan | Partial: `rise_over` and `pass` wait for Slice 3 |
| 10: effect cues | Blocked, correctly: they depend on the impossible performance, and effects wait for ADR-702 |
| 11: semantic markers | Not built. They come from performance events (Slice 2) |
| 12: diff | Done |
| 14–17: apply, undo, save/load, determinism | Done |
| 13: wait for approval | Slice 1.5 |

Proven red: whole-item keeping, Add-mode cues, and blocked dependencies.

## Addendum (2026-09-24): effect cues, after ADR-702 merged

**Resolution.** A cue's `EffectRef {id?, owner, type}` resolves against the staged effect list
(`resolveEffect`):
- the owner is the world, or a subject's node (ADR-702's Entity owner);
- the type must exist and be allowed on that owner, otherwise `CAPABILITY_UNAVAILABLE`, naming the
  owners it allows;
- several instances are `AMBIGUOUS_REFERENCE`;
- none means "make one from the type's factory".

Per the owner's ruling, "the Umbra hero effect" resolves to `umbra-cap-hero-pulse`.

**Activation** (a cue with no `field`) compiles to a **second instance**: a copy of the owner's
instance (or the type's defaults when there is none), activated in a `Window` on the transport
clock at the cue's time for its hold. The owner's own instance keeps its activation, so Umbra's
pulse still fires on hero focus. Reasons for this choice:
- ADR-702 allows several instances of one type on one owner;
- the transport clock makes the window deterministic;
- the copy is editable in the Effects section like any other instance.

The alternative, re-timing the existing instance, would have destroyed an authored behaviour.

**A field cue** compiles to the same baked Add event as a parameter cue, on `fx/<id>/<leaf>`. The
leaf must be one of the type's fields and a single number.

**The effect list is part of the staging copy.** `Staging {sequence, cameras, effects}` is shared
by `SceneFacts`, `Compilation` and `contentOf`, and `applyCompilation` installs the effects only
when they changed.

**Fingerprints are of content as installed.** Installing is not the identity: an effect's window
start becomes a float parameter (118.645 s becomes 118.64499… s). A fingerprint of the compiled
value made the reloaded project's next revision read the plan's own effect as a hand edit. This
was caught by the effect round-trip test, and proven red by reverting it.
