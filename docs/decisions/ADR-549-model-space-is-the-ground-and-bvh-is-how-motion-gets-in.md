# ADR-549: Model space is the ground, and BVH is how motion gets in

**Status:** Accepted
**Date:** 2026-09-20
**Related:** ADR-204 (clip key spans), ADR-260 (three positions), ADR-337 (the root is the lowest
translated joint), ADR-540 (every locomotion clip is in place), ADR-542 (§8.3, the licensing
survey), ADR-546 (a planted foot is low and still), ADR-548 (retargeting)
**Implemented by:** `src/assets/bvh_loader.{hpp,cpp}`, and two corrections in
`src/scene/motion_analysis.cpp`
**Tests:** `tests/unit/test_bvh_loader.cpp` — 7 cases, 60 assertions

---

## Context

Phase 0's licensing survey found exactly one corpus that is large enough to matter and can be
shipped in a commercial product: **100STYLE, CC BY 4.0, four million frames of stylized
locomotion** with the starts, stops and turns this repository's own content does not have. ACCAD
(CC BY 3.0) and the usable CMU conversions are in the same position.

All three ship **BVH**. AV Gen read glTF and nothing else. The licensing gate Phase 0 found open led
to a door with no handle.

## The reader

A BVH is a text hierarchy of `OFFSET` and `CHANNELS` declarations followed by one float per channel
per frame. Two details are worth naming because getting either wrong produces a rig that looks
almost right:

* **Channel order is declared per joint and the rotations are applied in that order, intrinsically.**
  `Zrotation Xrotation Yrotation` is common and `Xrotation Yrotation Zrotation` also occurs. A
  reader that assumes one is correct wherever a single axis is non-zero and wrong everywhere else.
  The test asserts that the same six numbers under two orders give poses 5+ units apart, and that
  both are still rotations of a 50-unit bone rather than garbage.
* **`End Site` joints are kept**, named after their parent. A BVH does not name them, and a foot's
  end site is where the toe is — which is the only thing a contact detector has to watch on a BVH
  rig, and the only thing a retarget profile can name.

Units are whatever the capture used; 100STYLE and CMU are in centimetres, and `scale` is how a
caller says so. Parsing is `std::from_chars`, not `atof`, so a machine with a comma decimal
separator does not silently truncate every value in the file.

## Two corrections the first travelling content found

Both were in code that had never run meaningfully, because **every clip in this repository is in
place** (ADR-540). This is the failure mode worth naming: a branch that is a no-op on all existing
content and wrong on all future content cannot be caught until the data arrives.

### 1. There is no ground frame. Model space is the ground.

ADR-546 removed the root's horizontal travel before measuring a joint's motion, reasoning that a
"ground frame" is what a planted foot is stationary in. **That reasoning is wrong in both
directions:**

* in a **travelling** clip a planted foot is stationary in the *world*, so subtracting the root's
  advance makes it move backwards at the travel speed — and the horizontal contact test then
  rejects every genuine plant. Measured on a hand-built walk with three plants: **0 spans found**;
* in an **in-place** clip there is no root travel to subtract, so the subtraction was a no-op and
  the code path had never actually executed.

Model space is the right frame for both. A travelling clip's world *is* the ground; an in-place
clip's planted foot moves backwards in any frame, which is exactly why ADR-546's vertical test
exists. The subtraction is gone, and the alien pack's numbers are unchanged to four decimal places
— which is the confirmation that it had always been a no-op there.

### 2. The travelling joint is not the skeleton root.

`ClipAnalysis::rootTravel` measured the first parentless joint. On the alien that is `rig`, an
armature wrapper no clip animates, while `root.x` is what moves — so a retargeted clip that crosses
the room reported a stationary body and `groundSpeed` of 0.

The rule ADR-337 already established and paid for is reused rather than re-derived: **the
lowest-indexed joint this clip gives a translation channel to.** Two answers to "where did the body
go" is how ADR-260 started.

## Evidence

The fixture is a walk **built rather than typed**: the hips advance along +Z at 1.2 m/s while the
left foot alternates between planted — held at a fixed world position by cancelling the hips'
advance in its own local channel — and swinging forward. That is a property no clip in this
repository has.

| arm | result |
|---|---|
| minimal file | 3 joints, 1 end site, 2 frames, 9 channels, offsets and frame time read, and the root **moved** between frames |
| channel order | ZXY and XYZ over identical numbers give poses 5+ units apart, both still 50-unit bones |
| units | 100 cm arrives as 1 m |
| malformed | six ways to be wrong, each refused; a truncated file names **the frame it ran out on** |
| travelling | the body covers 1.2 m/s × duration, and the foot's world Z is constant to 0.02 through stance and moves 0.5+ in swing |
| **contacts** | `groundSpeed` > 1.0 so the horizontal arm fires; **3 plants found for 3 cycles**, duty 0.3-0.7, phase cyclic at the fixture's own 1.0 s |
| **retarget onto the alien** | `rootScale` derived at **0.757** (neither 1 nor absurd — a 1 would mean the derivation did nothing), the alien travels 2.60 m against the source's 3.44, in proportion, and the result is analysable by the same pipeline |

## What this does not do

**It is not the 100STYLE experiment.** That is Phase A step 10 and it is gated on the data, which is
not in this repository. What is built is the reader and the validation of every code path that
corpus will take, so that when it arrives a failure looks like a bug rather than like a data
problem. The subset experiment's measurements — retarget quality, foot sliding, contact
preservation, processing time, output size — remain to be taken.

## The corpus, and one thing deliberately not fetched

**100STYLE, Zenodo record 8127870.** Licence verified at the authoritative source rather than from
a summary: the record's API returns `"license": {"id": "cc-by-4.0"}`. Creators Mason, Starke and
Komura; DOI 10.1145/3522618. Retrieved 2026-09-20.

Two files are offered:

| file | size | what it is | taken |
|---|---:|---|---|
| `100STYLE.zip` | 1,468,515,354 B | the original BVH capture — all four million frames | **yes** |
| `100Style-Labelled-Data.zip` | 14,751,560,934 B | Mason et al.'s own preprocessed features and local-phase labels | no |

The BVH half is the corpus in source form. The labelled half is a derived artifact of a pipeline
AV Gen is not using: this engine has its own contact detection, phase extraction and feature
pipeline (ADR-546), and ADR-542 makes the MotionPack the format — ingesting someone else's feature
layout would mean adopting their conventions or converting them, with the source data in hand to
derive our own. Extracting it would also have put the pair past half the machine's free space.

**One use for it remains on the list.** Their local-phase labels are a published reference against
which to *compare* what this engine's phase extractor produces from the same frames. That is a real
validation of a thing built from scratch, and it belongs with Phase C's phase-aware matching rather
than here. Recorded so it is not lost.

## Revisit triggers

* The real corpus arrives: re-run every measurement here. A hand-built fixture proves the branch
  executes and says nothing about whether real mocap is clean.
* A BVH with a non-Y-up or left-handed convention. Nothing here converts between coordinate
  systems, and 100STYLE's own convention has not been checked.
* Rotation-order handling meets a file using `Yrotation Xrotation Zrotation` or a 6-channel joint
  that is not the root; both are legal and neither is in the fixtures.
