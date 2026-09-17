# Camera preset architecture — Phase 1 findings

**Status:** research complete; §4's Follow finding **withdrawn and replaced** by
[follow-chase-discrepancy.md](follow-chase-discrepancy.md) after it was measured
**Date:** 2026-09-16
**Mandate:** *Shot-Level Camera Presets & Behaviors*, Phase 1 — "audit existing camera architecture
and produce findings", "do not immediately implement anything"
**Reads:** ADR-245 (the camera rig and the shot), ADR-062/071 (the shot vocabulary), ADR-091
(two-tier determinism), ADR-216 (the sequencer owns the song), ADR-158 (aim-follow)

The mandate's §27 says: *"If a proposed implementation conflicts with existing architecture, stop and
document the conflict before choosing a workaround."* It does. This document is that stop.

---

## 1. The headline: a camera preset is not a thing that exists

`seq::CameraPreset` is an enum of seven values and `cameraFromPreset(preset, subject)` returns a
`ShotCamera` **by value**. The header says so without apology:

> *Useful initial configurations, not a system. Each one fills a `ShotCamera` with a move that reads
> as the named shot against a subject of the given radius; the author then edits it like any other.*

`seq::Shot` has no preset field. Nothing serialises one. The enum is a **stamp**: it writes seven
numbers into `ShotCamera::move` — a shot kind, two distances, two azimuths, two elevations, a focal
length — and is then forgotten. Choosing "Tracking" and choosing "Reveal" and then hand-editing the
distances produce shots that are indistinguishable afterwards, because they *are* the same shot.

Every one of the mandate's §19 requirements is therefore currently false, and not by oversight:

| §19 asks that a user can | today |
|---|---|
| choose a camera preset | yes — once, as a stamp |
| configure the preset | no — there is no preset to configure, only the shot it produced |
| duplicate / copy / paste the shot | yes, but it copies the *numbers*, not the intent |
| undo a camera change | yes as of `e11fa30`, at the whole-sequence level |
| have the preset serialize with the shot | **no** |

**This is the single largest piece of work in the mandate and it is not any of the ten presets.** It
is that presets have to become state before any of §4–§12 can mean anything.

## 2. There are two camera architectures, and they are complementary rather than redundant

The mandate's §1 says *"do not duplicate camera systems if an existing architecture already provides
this mechanism"*. There are two, and neither is a duplicate of the other.

### A. `seq::ShotCamera` — **baked to keys**

`Sequence::install` walks each shot and emits `camera/position` and `camera/target` **timeline keys**
— `samples` of them across the shot's span (2 for a static preset, 24 by default for a move). The
aim blends toward an actor via `lookAt->positionAt(time)`.

The consequence is the important part. The mandate's §14 lifecycle matrix — playback, scrubbing,
seeking, looping, cuts, offline rendering — **is satisfied by construction and needs no work at
all**, because a key-interpolated timeline is already a pure function of time. There is no state to
reset at a shot boundary because there is no state.

Its limit is equally sharp: the bake can only see what is a pure function of time *at bake*, and
that is exactly `Actor::positionAt(t)` — an actor's authored keys or its spline path.

### B. `scene::CameraRig` — **evaluated per frame**

`Composition::evaluateAuthoredCamera` resolves a rig every frame. `followNode` already replaces the
eye with a node's live world transform plus an offset, and `aimNode` already replaces the aim. Both
resolve through `nodeWorldTransform`, which reads `positionParam->value()` — the **modulated final**
value — so a rig already follows a node moved by *anything*.

Its limit is the mirror image: nothing about it is baked, so determinism is a property each behaviour
has to earn rather than inherit.

### The table that decides the design

| | `seq::ShotCamera` (baked) | `scene::CameraRig` (evaluated) |
|---|---|---|
| owned by | a sequencer shot | the camera direction (ADR-245) |
| evaluated | once, at install | every frame |
| scrub / seek / loop / render | **exact, free** | per-behaviour |
| subject motion it can see | `Actor::positionAt(t)` — authored keys and spline paths **only** | any node's live transform, after modulation, behaviours and staging |
| terrain query available | yes (positions are known at bake) | yes |
| preset retained | n/a — no preset exists | n/a |

## 3. The conflict, stated plainly

The mandate wants both of these at once, and today they are in different systems:

* **§19/§20** — the preset is a *shot property*, edited, duplicated, undone, serialised. That is
  architecture **A**.
* **§5** — chase follows *"an actor that is moving"*, and the request that opened this work said
  explicitly *"for any reason (modulation, etc)"*. An actor moved by modulation, by an entity
  behaviour's walk cycle, or by a staging scenario **is invisible to `Actor::positionAt`**. That is
  architecture **B**.

A baked chase cannot follow a modulated actor. An evaluated chase is not a shot property. Neither
system alone satisfies the mandate, and building a third would be exactly what §27 forbids.

### The resolution this document recommends

**Put the preset on `seq::Shot` as state, and let the bake choose which architecture executes it.**

```
seq::Shot::camera.preset = { type, target, offset, lag, aim, clearance, ... }
                              |
            Sequence::install asks: is the target bake-visible?
                              |
        +---------------------+---------------------+
        |                                           |
  a seq::Actor with keys or a path          a live composition node
        |                                           |
  BAKE it to camera/position keys           emit a CameraRig binding for
  (free determinism, free scrub,            the shot's span, evaluated per
  terrain clamp at bake, no new             frame, with the determinism cost
  per-frame cost at all)                    stated rather than hidden
```

Three properties make this the right shape rather than a compromise:

1. **The common case pays nothing.** A hero the user animated in the sequencer bakes, and the whole
   of §14 comes free.
2. **The hard case is possible at all**, which it is not in either system alone.
3. **The choice is visible.** The panel can say which route a shot took and why, which is the
   difference between a camera that is deterministic and a camera nobody can answer for.

The cost that must be stated up front: **the two routes are not bit-identical.** A shot that bakes
and the same shot evaluated live will differ at the sub-pixel level, because one samples the subject
`samples` times and interpolates and the other samples it every frame. That is a real seam and it
should be named in the UI, not discovered.

## 4. What the existing presets actually are

All seven are `app::Shot` configurations — the ADR-062/071 vocabulary, which already knows fourteen
move kinds, five aim modes, four path shapes and easing. Against the mandate's §4–§12:

| preset | today | against the mandate |
|---|---|---|
| **Isometric** | `Establish`, fixed 45° azimuth, fixed elevation, 55 mm, `samples = 2` | §12 correct as-is. It is a composition and it holds still on purpose. |
| **Follow** | `ShotKind::Track`, `LookMode::Subject`, azimuth swings 0.9 → 0.55 | **§4 conflict.** The mandate defines Follow as *position fixed, orientation dynamic*. Today's Follow **moves the camera** — it is a tracking arc. Renaming today's behaviour or redefining the preset is a decision, not a refactor. |
| **Wide / Close** | `Establish` at 14→12 radii / `Approach` at 3.4→2.4, 24 mm / 70 mm | §10 correct as-is: compositional, with real lens values. They already move slightly, which §10 permits ("unless existing functionality already does so"). |
| **Top Down** | `Establish`, elevation 7 radii, `samples = 2` | §11 partially. It holds a **static** subject; there is no target-follow option. |
| **Tracking** | `ShotKind::Drift`, `MovementCurve::Straight`, azimuth 1.25 → 0.25 | §8 **already correct and already distinct from Chase** — lateral travel with the aim held, which is parallax rather than a trailing offset. The mandate's recommended distinction is the one the code already implements. |
| **Reveal** | `ShotKind::Reveal`, distance 3 → 16, azimuth 0.5 → 0.95 | §9 partially. It pulls back *and* arcs, which is a real reveal; it has no notion of an *obstruction*, so "starts occluded, becomes visible" is not expressible. |

**Two findings worth acting on before any new preset is written:**

* ~~**Follow is the conflict, not Chase.**~~ **Withdrawn — see
  [follow-chase-discrepancy.md](follow-chase-discrepancy.md).** The reading was factually right and
  the conclusion was wrong. Follow's eye does move, by **3.48 m**, and the engine reproduces that to
  five significant figures — but in the same shot the *aim* travels **40 m**, an 11.5:1 ratio at
  constant distance and constant height, which is why a viewer correctly reports a camera that holds
  still and turns. It is a 20° authored flourish that does not react to the actor at all, so on the
  letter of the contract ("Follow does not translate the camera *because the actor moves*") today's
  Follow already complies. What remains is a one-line preference, not an architectural conflict, and
  this entry overstated it.
* **Tracking needs no work.** §8 asks for a refactor "if the current implementation does not maintain
  the distinction". It does. This should be recorded and closed rather than reopened.

## 5. Evaluation order (§15), as it actually is

Measured from `Composition::update`, not from the mandate's diagram:

```
rebuild (if dirty)
  -> rootAngle_ += root/rotationSpeed * dt        <- path-dependent; see §6
  -> cameraAngle_ += camera/orbitSpeed * dt       <- path-dependent; see §6
  -> applyParameters()                            <- timeline + modulation land here
  -> syncHeroesToNodes()                          <- heroes adopt their node's world position
  -> recordFollowTrails()                         <- added this session; see §7
  -> applyDirectedAim()                           <- Auto-director nudges the MAIN camera's aim only
  -> ... floating layers, culling ...
  -> evaluateActiveCamera() -> evaluateAuthoredCamera() / evaluateMainCamera()
```

The mandate's §15 diagram is accurate in order and wrong in one emphasis: entity behaviours do not
run in a separate later stage, they write `setFinalComponent` onto the node's transform parameters
during the parameter pass. So "after `applyParameters`" already means "after behaviours". **A camera
evaluated in `evaluateAuthoredCamera` cannot read a stale actor transform**; the ordering the mandate
asks to establish is already established and already commented.

## 6. Two path-dependent integrations already in the camera path

`rootAngle_` and `cameraAngle_` both integrate `value() * dt`. Neither is scrub-safe, both predate
this work, and `camera/orbitSpeed` is refused by name in `benchmark-scenes.md` §4.2 for exactly this
reason. The mandate's §27 ban on casual stateful springs is therefore a rule this repository already
holds and has already been bitten by — worth citing rather than re-deriving when a future preset
wants a spring.

`root/rotationSpeed` additionally carries a **default modulation route** (`audio.mid`, amount 1.5)
installed by `addDefaultRoutes` on every composition. Any camera preset measured against world
positions must account for the world itself turning under it when audio is playing.

## 7. What has already been built, and where it sits

Ahead of this document — and before the mandate arrived — chase was implemented at the
**`CameraRig`** level, which by §2's table is architecture **B**:

* `CameraRig::followLocal` — interpret `followOffset` in the subject's frame, so "behind" stays
  behind through a turn. The mandate's §5 local convention.
* `CameraRig::followLagSeconds` — the camera stands where the subject *was*. A **time lag, not a
  spring**, which is the mandate's §5 preference stated as `targetPosition(t - lag)`.
* `CameraRig::followClearance` — the eye is kept above `TerrainQuery::surfaceAt`. The mandate's §17
  "simplest robust spatial query already supported by the engine".
* `Composition::FollowTrail` — a per-node time-keyed ring, sampled in the same place
  `syncHeroesToNodes` reads, cleared on a seek so a chase never interpolates across a cut in time.

Against the mandate this is **Phase 3 (shared behaviour infrastructure) and the live half of Phase
4**, and it is genuinely reusable: Orbit's centre, POV's eye and Follow's aim all need "the
subject's evaluated transform, optionally lagged", which is what `followTrailAt` is.

What it is **not** is a shot-level preset. It is configured on a camera rig, not on a
`seq::Shot`, so §19 and §20 remain entirely open.

**The honest limit of the trail, recorded because an un-lagged chase looks exactly like a working
one:** it is *seen*, not re-derived. At the head of a render and for `lag` seconds after a seek the
camera runs un-lagged and closes to its lag over that interval. It logs when this happens. This means
a render from 10 s and the same frame inside a render from 0 s are **not** the same frame — which is
a real violation of the mandate's §14 seek requirement, and the reason §3's recommendation prefers
the bake wherever the bake can see the subject.

## 8. Recommended order, revised from the mandate's §26

The mandate's order puts Chase first "because it establishes the most important moving-camera
architecture". The findings above suggest a different first step:

1. **Make a preset be state.** `seq::ShotCamera` gains a preset block; it serialises, duplicates and
   undoes. Nothing behaves differently yet. Without this, every later phase builds on a stamp.
2. **Resolve Follow.** Decide whether the mandate's Follow replaces today's Follow or joins it under
   a different name. This is a naming decision with a user-visible consequence and it should not be
   made silently inside an implementation.
3. **Bake-vs-live routing**, with the panel saying which a shot got.
4. Then Chase, Orbit and POV on the shared infrastructure — at which point all three are mostly
   parameter plumbing, because §7's groundwork already answers "where is the subject".
5. Audit the rest; record Tracking as already correct.

## 9. Open questions that need a person

1. **Follow's meaning** (§4 above). Redefine, or add "Aim" as a separate preset and leave Follow as
   the tracking arc it has always been?
2. **Is the bake/live seam acceptable**, given the two routes are not bit-identical?
3. **Does Chase need to work on a modulated actor at all**, or is a sequencer Actor enough? If the
   latter, everything bakes and §14 is free — the request that started this said "for any reason",
   but that may have been about the *engine* rather than about chase specifically.
4. **Reveal's obstruction** (§9) — does it need real occlusion queries, or is "starts close and low,
   ends wide and high" the reveal that was wanted?
