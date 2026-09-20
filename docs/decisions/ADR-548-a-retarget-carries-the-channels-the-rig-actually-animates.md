# ADR-548: A retarget carries the channels the rig actually animates

**Status:** Accepted
**Date:** 2026-09-20
**Related:** ADR-086 (rigs are copied per node), ADR-192 (the alien pack), ADR-204 (clip key spans),
ADR-540 (§1.5: there is no way to get animation onto a rig), ADR-542 (the motionpack),
ADR-543 (the alien's leg is three branches), ADR-546 (the in-place finding)
**Implemented by:** `src/scene/retarget.{hpp,cpp}`
**Tests:** `tests/unit/test_retarget.cpp` — 8 cases, 95 assertions, including the real 90-joint rig.

---

## Context

Phase 0 named this the blocking dependency for everything else. `Importer::importClips` fills only
the rigs built in the same `loadGltf` call, so a character's motion is whatever shipped inside its
own file — which is why each of six alien variants carries its own copy of all 26 clips, 3.9 MB of
each 4.5 MB file, and why a four-million-frame CC BY corpus has nowhere to go.

## The decision

A `RetargetProfile` is **data**: joint-name pairs, a root, a scale, a sample rate. There is no list
of joint names in the engine and no `if (alien)` anywhere, because the three rig families here share
no joint name at all — `head.x`, `Head01`, `Head`. `bindRetarget` resolves a profile against two
real skeletons; `retargetClip` resamples a clip through it.

The core is the textbook bind-pose reconciliation:

```
targetModelRot(t) = sourceModelRot(t) · delta,    delta = conj(sourceRestRot) · targetRestRot
targetLocalRot(t) = conj(targetParentModelRot(t)) · targetModelRot(t)
```

written parent-first, which is why the links are sorted by target index once at bind time.

## Three measurements that changed the design

**1. Rotation-only is wrong for this rig.** The textbook retarget carries rotation everywhere and
translation only at the root, because copying translation stretches the target onto the source's
proportions. Measured on `alien-scout.glb` retargeted onto **itself**, name for name: orientations
reproduced to **0.0396°** and `foot.l` landed **0.38 model units** away. The alien is an Auto-Rig
Pro deform export with a nearly flat hierarchy — 18 joints hang directly off the armature — and
**all 89 joints carry an animated translation channel in every clip**. Most of that clip's motion is
not in its rotations.

So translation is carried as a **deviation from the joint's own rest**, scaled by the ratio of the
two rigs' rest bone lengths at that joint (by the whole-rig `rootScale` at the root, because travel
scales with the body rather than with one bone). On identical skeletons that is exactly the
identity; on a target twice the size the deviation doubles; and a joint the source never translates
contributes nothing, so a rotation-driven rig still produces rotation-only output. A channel whose
values never leave their first is dropped.

`rootTranslationOnly` restores the textbook behaviour for a rig that wants it.

**2. It is not scale.** The obvious next suspect, once translation was carried and 0.08 remained,
was the `*_stretch` bones — Auto-Rig Pro's stretch joints are scale-driven and the names say so.
Measured directly out of the GLB: **`Walking` has 89 scale channels and 0 of them vary.** The
suspicion was wrong and the measurement cost two minutes; assuming it would have cost a wrong
implementation.

**3. It was the time base.** The remaining 0.08 was one frame of walk. A retargeted clip is
normalised to start at zero; this pack's clips start at **1/30 s**, because Blender's exporter
writes the frame range it was given (ADR-204). Source second `clip.start + t` is output second `t`.
The test was comparing instants one frame apart.

## Evidence

| arm | result |
|---|---|
| identity, stub rig | worst orientation error **< 0.1°**, bone lengths unchanged, every joint reproduced to 5e-3, and the feet move 0.2+ |
| bind orientation differs by 55° | orientations reproduced; the two rigs are genuinely in different places at rest and the arm asserts that too |
| target bones 2× | orientations reproduced, `rootScale` derived as **2.0** from the rest poses, target's own 0.90 bone lengths preserved |
| a mapping the target lacks | both bad mappings reported by name, the six good ones still work |
| a mapping that resolves to nothing | **refuses** — no channels, zero length — rather than emitting a still clip |
| travelling source | source travels 0.80, target travels **1.60** |
| role guessing | finds `LeftUpLeg`, `UpperLegB.L`, `HoofB.R`, `Head01`, `head.x`, `mixamorig:LeftHand`; declines `Antenna`, `Bone.003`, `rig` |
| **the real alien, 90 joints, onto itself** | worst **0.0396°**, mean **0.00043°**, `foot.l`/`foot.r`/`head.x` reproduced to **< 0.01** at four instants, and the walk still walks |

Every arm that asserts bone preservation also asserts that the target **moved**, because a retarget
that returns the rest pose preserves bone lengths perfectly and animates nothing.

## Rejected alternatives

* **Copy translation channels outright.** Stretches the target onto the source's proportions. The
  deviation rule is the same thing with the target's own geometry as the datum.
* **A built-in humanoid name table.** A silent no-op on two of this repository's three rig families.
  `roleForJointName` exists as a *guess* that produces an editable profile and lists what it could
  not fill; it is never consulted at retarget time.
* **Reusing the source's key times.** A key that meant something on one rig means nothing on the
  other. A retarget is a resampling.
* **Deriving the root scale from overall height.** Leg length is what decides how far a stride
  carries a body, so the ratio is taken between the mapped root and the lowest mapped joint.

## Consequences

* A clip can be moved between skeletons for the first time. 100STYLE and ACCAD become reachable
  (ADR-542 §8.3), and the six-copies-of-26-clips problem has a fix.
* `RetargetStats` gives the offline pipeline its first quality metric: orientation error, bone-length
  error, frames and channels.
* The output is an ordinary `AnimationClip`, so everything downstream — contacts, phase, the layer
  stack, root motion — works on retargeted motion with no special case.

## Revisit triggers

* **A travelling source corpus.** Every measurement here is on in-place content. The deviation rule
  and the root's `rootScale` are exactly where a travelling clip will behave differently, and
  ADR-546's threshold is the other one.
* A rig whose motion is in scale after all. `Walking` has none; 25 other clips were not checked.
* A source and target that disagree about handedness or up axis. Nothing here transforms between
  coordinate conventions, and a mirrored rig would need it.
