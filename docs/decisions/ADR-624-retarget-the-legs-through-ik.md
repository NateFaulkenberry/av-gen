# ADR-624: Retarget the legs through IK, on top of the rotation retarget

**Status:** Accepted
**Date:** 2026-09-21
**Resolves:** ADR-553's open repair ("retargeting through IK ... deserves its own decision")
**Related:** ADR-543 (the alien's leg is three siblings, not a chain), ADR-548 (the rotation
retarget), ADR-337 (the travel joint), Phase C §21 (the foot-target-plus-IK step this reuses),
§64, §92
**Implemented by:** `scene/retarget_positional.{hpp,cpp}`, `avgen-motion pack --positional-legs`
**Tests:** `tests/unit/test_retarget_positional.cpp`, `tests/unit/test_motion_slice.cpp`

---

## Context

ADR-553 measured 100STYLE retargeted onto the Glowmere alien by rotation. Every leg sat at its rest
reach on every frame, because the alien's foot is not downstream of the joints the rotations land
on. The coordinator asked for the repair to be built on §21's machinery, and for ADR-553's
finding to be kept as the test: the reach must actually vary.

## Options

1. **Re-export the alien as a hierarchy.** This is ruled out by a standing constraint (ADR-553).
2. **Teach the rotation retarget to write foot translation.** The foot's translation would be
   copied from the source's scaled offset, with no knee solve. The knee then points wherever its
   rotation happened to land, and it fights the foot.
3. **Keep the rotation retarget for the body, then solve the legs through IK.** Chosen.

## Decision

After the rotation retarget, for each leg and frame:
- take the source ankle's offset from the source **body joint** (the hip's parent, which carries the
  travel);
- turn it from the source's rest frame into the target's, once;
- scale it by the ratio of the legs' lengths;
- add a constant rest correction for the rigs' different proportions. The correction turns with
  the body's heading, averaged over a second, and only while the foot is in the air;
- solve the target leg to put its foot there with `solveLegInPose`, the step §21 uses.

The target's travel joint has its deviation from rest rescaled from the rotation retarget's root
scale to the legs' ratio, so the body and the feet share one scale. Channels that never leave rest
are dropped, so the travel joint is still the lowest-indexed translated joint (ADR-337).

## What each piece bought, measured on `Neutral_FW` onto the scout

| | reach mean | reach range | planted-foot slide |
|---|---|---|---|
| source (100STYLE) | 0.977 | 0.843–1.000 | 0.025 m/s |
| rotation retarget (ADR-553) | 0.985 | **0.985–0.985** | — |
| positional, first version (hip-relative) | 0.977 | 0.843–1.000 | 0.046 m/s |
| + root on the legs' scale | 0.977 | 0.843–1.000 | 0.027 m/s |
| + body-relative with a rest correction held through each stance | 0.969 | 0.856–1.022 | **0.019 m/s**, the source's own residual × 0.754 |

Three of those rows were wrong turns, and each was caught by the slide measurement, not by the reach:
- The root and the legs had different scales (0.826 against 0.754), so a planted foot slid at 7% of
  the walking speed.
- Offsets taken from the hip were rotated by the pelvis's sway, because the two rigs' hip widths
  are not in the legs' ratio.
- A correction turned during a stance swung the foot around by the correction's own length.

**One more defect**, found by the slice's database and not by any retarget number: writing a channel
for every joint made the unmoving armature root the "travel joint". The database then read every
retargeted walk as standing still (763 of 852 s "idle"). With rest channels pruned, the same pack
reads 317 s of walking, 181 of strafing, 228 of reverse and 272 of turning. §21's augmentation had
the same latent defect, and it is fixed there too.

## Consequences

- **The legs flex.** On the Strutting pack the mean reach is 0.94–0.99 per clip, against ADR-553's
  pinned 0.970. The foot now moves relative to the pelvis.
- **Human legs straighten more than the alien's.** 14% of leg-frames are at or past a straight leg,
  where the alien's own clips top out at 0.956, and the worst foot misses by 0.8% of the leg. Phase
  0's predicted clamp is now observable, as ADR-553 said it would become. Whether it *looks* right
  is an owner question.
- §64 and §92 are unblocked: a scout matches on retargeted 100STYLE in a scene, with its foot layers
  on top, and scrubs to the played frame.

---

## Amendment, 2026-09-22: the reach cap (owner ruling), and a knee left behind

**Ruling (owner, 22 Sep):** "cap it at the alien's own maximum (0.956 of leg length)". Borrowed
human motion must never look hyperextended on the alien. The cap goes in this retarget, not in the
pose layers as a clamp after the fact. Planted-foot slide stays the gate.

**Built:** `PositionalReachCap` (`kAlienMaxLegReach` = 0.956), on by default in
`avgen-motion pack --positional-legs`; `--max-reach 0` turns it off. Tests:
`test_retarget_positional.cpp`, "the reach cap".

**Two ways to cap were measured, and the gate chose between them.** Lowering the body brings the
hips down to the feet. Pulling the feet in brings them toward the hips.

| clip | mode | reach max | planted slide (m/s) | cost |
|---|---|---|---|---|
| Neutral_FW | uncapped | 1.000 | 0.0193 | — |
| | **lower the body** | **0.956** | **0.0175** | body lowered on 174 of 181 frames, mean 0.023, worst 0.050 |
| | pull the feet | 0.956 | 0.0267 | 240 of 362 leg-frames pulled, mean 0.020, worst 0.043 |
| Strutting_FR | uncapped / lower / pull | 1.000 / 0.956 / 0.956 | 0.0227 / 0.0201 / 0.0318 | lowered mean 0.025, worst 0.049 |
| Strutting_SW | uncapped / lower / pull | 1.000 / 0.956 / 0.956 | 0.0158 / 0.0142 / 0.0198 | lowered mean 0.024, worst 0.054 |
| March_SR | uncapped / lower / pull | 1.000 / 0.956 / 0.956 | 0.0142 / 0.0125 / 0.0208 | lowered mean 0.025, worst 0.050 |

(Units are the scout's model units; the scout is drawn at 1.94×.)

**Lowering the body is the default.** A planted foot cannot slide if it does not move. Slide with
the cap on is *lower* than without it on all four clips. Pulling the feet in makes each planted foot
follow its moving hip, which is +40–60% slide. That is exactly the failure the gate exists for.

**The cost the ruling anticipated, feet landing short, does not occur this way.** The feet land
where the source put them. The cost moves to the body instead: it dips by about 0.025 on average
(about 4% of the leg) and at most 0.054 on the longest strides. A human pelvis does the same. Whether
that dip reads well on the alien is a look question. The pull-feet mode is kept, and its numbers
are above, if the owner prefers short feet to a lower body.

**A latent defect, found by the cap.** The alien's `leg_stretch` (the knee) is a child of the
armature root, while `thigh_twist` and `foot` are children of the travel joint `root.x`. So as the
retargeted body travels, the knee is left behind: 1.1 from the hip at the first frame of a walk and
2.6 after six seconds, against a rest 0.28. The two-bone solve takes its bone lengths from the pose
it is handed, so it solved with a thigh four to nine times too long, and put the knee there.

The foot still landed within 0.8% of its target, which is why none of this ADR's reach or slide
numbers showed it. The cap did: with that thigh the leg's *shortest* reach was past 0.956, and
`Strutting_FR` stayed at 0.979 whatever it was asked. The knee and foot are now put back at their
rest distances before every solve. Uncapped reach tops out at exactly 1.000, not 1.022; a reach above
1 had been the knee's error showing through. **Packs built before this commit carry the displaced
knee and must be rebuilt.**
