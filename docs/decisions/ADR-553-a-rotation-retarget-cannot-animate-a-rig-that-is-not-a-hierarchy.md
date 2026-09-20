# ADR-553: A rotation retarget cannot animate a rig that is not a hierarchy

**Status:** Proposed
**Date:** 2026-09-20
**Related:** ADR-337 (which joint carries travel), ADR-385 (a stated reason is not evidence),
ADR-540 (§10.b corollary: a synthetic fixture proves something about the synthetic fixture),
ADR-543 (a leg is three joints and a write-back rule, not an ancestor chain), ADR-548 (a retarget
carries the channels the rig actually animates), ADR-549, ADR-550 (the MotionPack)
**Implemented by:** measurement only — `tools/motion_build.cpp` `reach`
**Tests:** none yet; this records a result, not a mechanism

---

## Context

Phase A step 10b asks a question with a yes-or-no answer: **can ordinary human motion be converted
into convincing Glowmere alien motion?** The pipeline to answer it exists — BVH import (ADR-549),
retargeting (ADR-548), MotionPack (ADR-550) — and 24 clips of 100STYLE are retargeted onto the
alien and packed, validating clean, with orientation error worst **0.056°** and bone-length error
worst **0.000000**.

Those numbers are excellent and they are about the wrong thing.

## The measurement

`avgen-motion reach` reports, per frame, the distance from the pelvis to the ankle as a fraction of
the leg's own length, and the ankle's excursion relative to the pelvis over the clip.

| source | clip | reach mean | reach p99 | reach worst | ankle excursion |
|---|---|---|---|---|---|
| alien, authored | `Walking` (left) | 0.879 | 0.956 | 0.956 | 0.746 m |
| alien, authored | `Running` (left) | 0.687 | 0.927 | 0.927 | 0.873 m |
| alien, authored | `Idle` (left) | 0.926 | 0.927 | 0.927 | 0.010 m |
| retargeted | `Neutral_FW` (left) | **0.970** | **0.970** | **0.970** | 0.587 m |
| retargeted | `Neutral_FR` (left) | **0.970** | **0.970** | **0.970** | 0.908 m |
| retargeted | `Neutral_BW` (left) | **0.970** | **0.970** | **0.970** | 0.806 m |

0.970 is the **rest** value. It is identical on every frame of every retargeted clip, to three
decimals, on both legs. The pelvis-to-ankle distance never changes: **the foot is welded to the
pelvis.** The remaining excursion is the whole lower body sweeping rigidly as the pelvis rotates.

The alien's own clips, by contrast, vary from 0.69 to 0.96 — the leg flexes, because it is supposed
to.

## Why

ADR-543 established that the alien's leg is **not an ancestor chain**. The hierarchy:

```
rig
├── root.x
│   ├── foot.l ── toes_01.l
│   ├── thigh_twist.l ── thigh_stretch.l ── thigh_twist_2.l
│   ├── foot.r ── toes_01.r
│   └── thigh_twist.r ── ...
├── leg_stretch.l ── leg_twist.l, leg_twist_2.l
└── leg_stretch.r ── ...
```

`foot.l` is a **sibling** of `thigh_twist.l`, and `leg_stretch.l` — the joint a retarget maps the
knee to — hangs off `rig` entirely. The foot's only animated ancestor is `root.x`.

A rotation retarget moves a joint by rotating its ancestors. On this rig the mapped knee and thigh
rotations are written faithfully, land on the joints they were asked to land on, and **cannot move
the foot**, because the foot is not downstream of any of them. The alien's own clips move the foot
by animating `foot.l`'s **translation** directly.

The retarget does carry translation — as a scaled deviation from rest, which is what made the
identity retarget exact — but only where the *source* has a translation channel. BVH carries
translation on `Hips` alone. So a BVH source produces, on this rig, a body that translates and
rotates above a pair of legs that do not articulate.

ADR-548's title is precise and was not enough: the retarget carries the channels *the source*
animates, which is not the same as the channels *the rig* animates.

**Every quality gate in the pipeline passed.** Orientation error is tiny because the rotations are
right. Bone-length error is zero because rotation preserves bone length. Validation passes because
the pack is well-formed. Nothing in the pipeline asks whether the motion arrived, and the one
number that would have shown it — pelvis-to-ankle distance having zero variance — was not among
the ones being reported.

## Decision

Record the result. Do **not** repair it inside the rotation retarget.

* **A.10b's answer, as built, is no.** Rotation retargeting of a human corpus onto the Glowmere
  alien produces a stiff-legged character. This is a property of the target rig, not of 100STYLE,
  and not of the import.
* **The repair is positional, and it is Phase B/C work.** Making the alien's feet follow retargeted
  motion means running FK on the *source* to find where the foot should be, then solving the
  target's leg to put it there and writing the result as the translation the rig expects. That is
  retargeting through IK, which is a different mechanism from ADR-548's and deserves its own
  decision rather than being bolted onto this one.
* **Do not re-export the alien to make it a hierarchy.** The standing constraint holds. The rig is
  what it is, and ADR-543 already established that the engine must cope with it.
* **The pipeline gains a reachability report, not a repair.** `reach` is the probe that would have
  caught this on day one, and its shape generalises: *measure whether the joint you were animating
  actually moved*, not whether the numbers you wrote were the numbers you meant.

## Consequences

* The `pack-100style` artefact is a valid MotionPack of motion this character cannot perform. It is
  useful for benchmarking (it is real data at real scale) and must not be used to answer "does this
  look right".
* The 1.79 m-onto-1.66 m clamp that Phase 0 predicted **was not observed, and could not have been**:
  the leg never leaves its rest extension, so nothing clamps. The prediction is untested, not
  refuted. It becomes testable once the feet move.
* **Revisit when** positional retargeting exists, or when a target rig with a real leg hierarchy is
  available to separate "this corpus does not suit this character" from "this rig cannot receive
  rotation".
