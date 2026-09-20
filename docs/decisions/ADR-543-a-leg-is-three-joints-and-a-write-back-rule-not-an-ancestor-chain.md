# ADR-543: A leg is three joints and a write-back rule, not an ancestor chain

**Status:** Accepted (2026-09-20). It was Proposed pending the implementation; the `PoseLayerStack`
change has landed and `tests/unit/test_foot_ik.cpp` now carries the positive regression arm --
the alien's detached chain binds, the foot moves to a reachable target, and an unreachable one
clamps and says so. The farm rigs take the same branch they always took and their arms are
unchanged.
**Date:** 2026-09-20
**Related:** ADR-274 (a joint transform is entity-local), ADR-300 (the layer stack), ADR-359 (a leg
is three names and the body has to sit down first), ADR-385 (a stated reason is not evidence),
ADR-540 (a synthetic fixture proves something about the synthetic fixture)
**Probe:** `tools/motion_probe.cpp`, run against `assets/aliens/alien-scout.glb`. Nothing in
`src/scene/` or `src/entity/` was modified to produce these numbers.

---

## Context

ADR-359 built analytic two-bone IK and scoped itself to the farm rigs, because the alien's leg is
not an ancestor chain and `PoseLayerStack::bind` refuses one that is not. The consequence, measured
in the Phase 0 research: **no Glowmere alien has ever had a foot planted**, and terrain adaptation,
step-over and contact correction are all blocked behind that one check.

The question was whether the refusal is protecting something real — whether a chain of
non-ancestors can be solved and written back at all — or whether it is a topology assumption that
happens to be false on this asset.

## The measurement

`bind`'s own predicate, run against the real rig, on the four namings an author would try:

| chain | mid below root? | tip below mid? | `bind` |
|---|---|---|---|
| `thigh_stretch.l` / `leg_stretch.l` / `foot.l` | NO | NO | `NoChain` |
| `thigh_twist.l` / `leg_stretch.l` / `foot.l` | NO | NO | `NoChain` |
| `thigh_twist_2.l` / `leg_twist_2.l` / `foot.l` | NO | NO | `NoChain` |
| `root.x` / `leg_stretch.l` / `foot.l` | NO | NO | `NoChain` |

The rest-pose geometry, model space, picked by **position rather than by name**:

```
hip  thigh_twist.l   y=+0.7785    thigh_twist.l <- root.x <- rig
knee leg_stretch.l   y=+0.4978    leg_stretch.l <- rig            (a SIBLING of root.x)
foot foot.l          y=+0.1377    foot.l <- root.x <- rig         (a CHILD of root.x)
upper=0.2819  lower=0.3736
```

Two write-back rules were run over the same solver, the same chain, the same eight targets, each
starting from the rest pose, with **forward kinematics re-derived from the written-back local pose**
rather than from the solver's own reported tip:

| rule | worst \|FK − solver\| |
|---|---|
| **Arbitrary** — transform a joint's parent only when that parent actually descends from the joint the rotation pivots about | **0.00000012** |
| **Ancestor** — what ships today: always transform the parent | **0.20615509** |

Bone lengths were preserved to `0.000000` on every arm under both rules, and quaternion drift never
exceeded 5.96e-08.

The ancestor rule's failure has a specific shape worth recording: on this rig its end-effector error
is **exactly the requested displacement** in every arm — 0.050000 for a 0.05 raise, 0.150000 for a
0.15 raise. It is not inaccurate. **It does nothing at all**, and a null-request control arm scores
it as perfect, which is why the probe does not use one as its only arm.

## Decision

**An IK chain is an ordered list of joints plus a rule for recovering each one's local transform,
and ancestry is a property to be reported rather than a precondition to be required.**

1. `PoseLayerStack::bind` stops refusing a non-ancestor chain. It resolves the three joints, records
   whether each link is an ancestor link, and binds either way.
2. The write-back applies an accumulated model-space pre-rotation to a joint's parent **only when
   that parent descends from the joint the rotation pivots about**, and otherwise recovers the local
   through the parent unchanged. That is one predicate in one lambda.
3. What the ancestor check was genuinely protecting — that joints *between* the named three ride
   along, which `ik.hpp` relies on — becomes a **reported** property, in the shape `JointMask::nested`
   already uses for exactly this hazard on aim layers. A non-ancestor link means the intermediate
   joints do not ride along, and the layer says so rather than being refused or silently wrong.
4. **The asset is not re-exported.** Re-parenting the deform hierarchy would change every joint's
   local transform, require re-baking all 26 clips, and invalidate every socket offset and `soleUp`
   resolution — and the source `.blend` is not in this repository.

## The finding that outranks it

The same probe measured the alien's leg reach, and this is the more consequential number:

```
maxReach (upper+lower) = 0.6555
rest hip-to-foot       = 0.6457      98.5% extended at rest
SLACK                  = 0.0098
```

**The leg can be lengthened by less than one centimetre before the knee locks.** A foot may be
*raised* freely — a 0.15 raise solves at a 97.7° knee — but a 10 cm step *down* is out of reach and
correctly reports `Clamped`, landing 0.0896 short. Reaching it needs **0.0902 of hip drop**.

So on this character, foot planting without a pelvis stage can only ever push feet upward. **Hip
lowering is not an enhancement to the IK architecture; it is a precondition for downhill terrain,
steps and any ground below the authored plane.** The Phase 0 report listed it sixth in "what is
missing"; it belongs second, behind the chain rule it depends on.

## A precision limit, recorded rather than fixed

`solveTwoBone`'s header defines `Solved` as "the tip is on the target, to floating-point". On a null
request against this rig it returns `Solved` with a residual of **3.152e-4**:

| case | knee | \|tip − target\| |
|---|---|---|
| null request, pole off | 160.0000° | 0.000315214 |
| null request, pole on | 160.0000° | 0.000315211 |
| the same chain, knee pushed 0.12 forward | 121.7891° | **0.000000000** |

It is not the pole — the two agree to 3e-9 — and `achieved` equals `requested` to six decimals, so
the tip is on a sphere of the correct radius and is off *laterally*. It is a property of a
**near-straight chain**, where the cross products that build the bend plane lose their direction.

That matters here more than it would elsewhere: this character is 98.5% extended at rest, so **the
solver's worst-precision regime is this alien's normal pose**. The magnitude is 0.3 mm on a 1.66 m
character, or 0.61 mm at Glowmere's 1.94× draw scale — sub-pixel at any camera distance in the
multicam. Recorded, not fixed.

## Rejected alternatives

* **Re-export the alien with a nested deform hierarchy.** See Decision 4. It is the eventual right
  answer for a rig authored from scratch and the wrong way to unblock this one.
* **Keep the refusal and write a separate solver for odd rigs.** Two solvers, two bend-plane
  conventions, two sets of degenerate cases. The solver was never the problem — it already takes
  three points in one space and says so.
* **Relax the check to a warning and leave the write-back alone.** This is the trap. The ancestor
  write-back on this rig produces a pose that is *exactly* the input pose, so the layer would report
  `Applied`, the overlay would draw a target, and the foot would not move. That is the
  "built but unreachable" failure this repository keeps paying for.
* **Move the pole/bend logic into the layer.** Not needed. The solver's answer was faithful to
  1.2e-7 through the corrected write-back.

## Consequences

* Foot planting, terrain adaptation and contact correction become reachable on the primary character
  for the first time — subject to the hip stage, without which they are upward-only.
* `LayerResolution` gains a reported non-ancestor state; `NoChain` narrows to "these names are not
  three distinct joints this rig carries".
* The farm rigs are unaffected: every link in them is an ancestor link, so the new predicate takes
  the same branch it takes today. The regression arm is that their solved poses stay bit-identical.

## Revisit triggers

* A rig arrives where an intermediate joint between two named chain joints must itself be written.
* The 3.152e-4 near-straight residual becomes visible — a close-up, a much larger draw scale, or a
  contact test tighter than a millimetre.
* Hip lowering lands: re-run this probe, because the slack figure is what sets its range.
  **Landed as ADR-544**, and the probe's 0.0902 is now asserted by the engine rather than by the
  probe.
