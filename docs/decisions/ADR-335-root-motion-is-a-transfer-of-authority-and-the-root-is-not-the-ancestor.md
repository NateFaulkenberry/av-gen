# ADR-335: Root motion is a transfer of authority, and on this content the root is not the ancestor

**Status:** Accepted
**Date:** 2026-09-18

P9 of `docs/character-ai-plan.md`, and A4 of `docs/character-ai-research.md`: *"extraction behind a
per-clip opt-in, writing `MotionAuthority::Simulation`."*

Depends on P5 (ADR-274) and P6 (ADR-300). ADR-300 §8 wrote down exactly what P9 would inherit and
exactly what it would not: a hook in the right place, a way to name a root joint, and **no way to
write the simulation**, because `scene::pose_layers.*` is built to be structurally incapable of it.
This is the unit that adds the one authority that module refuses to have.

---

## 1. Where ADR-161 was right, where ADR-260 corrected it, and what was still open

ADR-161 read three clips of `assets/imported/alien.gltf`, measured net XZ displacements of 0.0002,
0.0000 and 0.0000, and concluded there was no root motion in the content to extract. It was right
about those three clips and it said what would reopen it: *"If an asset with real root translation
is ever imported, this ADR is the place to start."*

Two packs arrived and ADR-260 re-ran the check across all 168 clips. Five clips per alien translate
their root; `Landing` carries **−0.567 m** of it and Glowmere plays `Landing` every time a jump
resolves. ADR-260 recorded that as an inventory and explicitly declined to build the feature,
because *"which clips should own their own displacement is an art decision before it is an
engineering one."*

That sentence is the design brief, and it is why the answer is an **opt-in** rather than a
behaviour. 163 of the 168 clips must be exactly what they were, and §6 proves it rather than
promising it.

---

## 2. Root motion is a transfer of authority. It is not a change to the picture.

This is the part that decides everything else, and it is not the intuitive reading of *"`Landing`'s
−0.567 m is discarded every time Glowmere plays it."*

The displacement is **not** discarded from the picture. It is baked into the clip, it is sampled
into the pose, it becomes the palette, and the renderer draws a body descending 2.81 m (at
Glowmere's 3.61× scale) over 1.1 s. Anybody looking at the screen can see it happen.

What is discarded is **the fact that it happened.** `state().position()` — ADR-260's first column,
the one navigation, the crowd field, path validity, grounding and every trigger read — does not
move a millimetre. For 1.1 s the simulation and the drawing disagree about where the body is by up
to two metres, and nothing in the engine can tell.

So extraction does two things in the same frame, and they cancel:

| | what it writes | authority |
|---|---|---|
| 1 | the displacement, into `EntityState::travel` | `MotionAuthority::Simulation` |
| 2 | minus the same displacement, into `SkinnedRig::pose` | `PoseOnly` |

**Measured, at Glowmere's own 3.61× on `alien-scout.glb` playing `Landing`:**

| ADR-260's three positions | before | after |
|---|---:|---:|
| `state().position()` | 0.0000 m | **(+0.0508, −2.0457, −0.0566)** |
| `visualPosition()` | 0.0000 m | the same, to 1e-6 (no behaviour writes an offset here) |
| the node's `position` parameter, y | 0.0000 m | **−2.0457** |
| **the drawn toe** (node parameter ∘ posed joint ∘ scale) | falls 2.8120 m | **falls 2.8120 m** |

The last row is the whole decision. The two arms agree to under a millimetre. An implementation
that wrote only row 1 would move the body twice; one that wrote only row 2 would stop it moving at
all; and a test that checked either alone would pass on the wrong one. So the assertion is a
conjunction and both halves have their own control (§7).

---

## 3. The root is not the ancestor, and that is a fact about the asset

The textbook implementation of half (2) is "zero the root joint's translation channel". **On this
project's primary character content that is wrong**, and the probe run before a line of this was
written is why it is known to be wrong rather than shipped.

`assets/aliens/alien-scout.glb`, 90 joints, nearly flat:

```
[0] rig                       <- the armature wrapper; no clip animates it
  [1] root.x                  <- children: foot.l, toes… no: foot.l, thigh_twist.l,
                                 foot.r, thigh_twist.r.  Four joints, all leg.
  [12] Backpack   [13] spine_05.x   [24..27] spine_04.x .. spine_01.x
  [28] hand.l     [48] hand.r       [68/71] forearm_stretch.l/.r
  [74] Antenna    [78] Eye_L        [80] Eye_R   [82] head.x   [83] Mouth
  [84/87] leg_stretch.l/.r
```

The spine, both hands, the head, the antenna, the eyes and the mouth are **siblings** of `root.x`
under the armature, not descendants of it. Nearly every joint carries its own baked translation
channel — this is a flat Auto-Rig Pro export, not a nested hierarchy.

| compensation applied at | `root.x` residual over `Landing` | `spine_01.x` residual |
|---|---:|---:|
| the skeleton root (joint 0) | 0.000000 m | **0.000732 m** |
| `root.x` itself — the textbook target | 0.000000 m | **0.567037 m** |

Both hold the root perfectly still. A probe that had only looked at the root would have reported
both implementations correct. The second one is a character whose legs stay where they are while
its torso drops half a metre through them.

So the displacement is **read** at one joint and **compensated** at another:

* **read** at the lowest-indexed joint the clip gives a translation channel to. This is ADR-260's
  rule, already paid for: `joints[0]` is an armature wrapper no clip animates, and taking it cost
  ADR-260's first attempt 168 clips of silent zero. An opt-in may name a joint instead.
* **compensated** at that joint's topmost ancestor, whose parent is −1. Subtracting a model-space
  vector from a parentless joint's local translation is exactly a rigid model-space translation of
  everything beneath it, because the translation component of `T · R · S` is `T` and neither `R`
  nor `S` touches it. On a properly nested rig the two coincide in effect, so this is a general
  rule rather than a special case for one asset.

The clip really does hold one coherent whole-body displacement, which is what makes reading it at
one joint legitimate: over `Landing`, `root.x` moves (+0.0151, −0.5666, −0.0158) and `spine_01.x`
moves (+0.0145, −0.5666, −0.0162) — the same number, carried twice.

---

## 4. The ordering problem ADR-300 §8 left, and why it dissolved

ADR-300 §8: *"P9's seam is a value read back out of the rig into the entity, in the opposite
direction to everything here, and it will need a decision of its own about who owns the ordering
(the rigs are posed in `Composition::update`, one stage after `EntityWorld::update` reads them)."*

That is a real problem for anything that has to read a **posed rig** — ADR-274 §5 records
attachments paying 16.7 ms of it. Root motion does not have to. The displacement is a pure function
of *(the clip, the joint, the axis mask, the clip second)*, and the clip second is a pure function
of *(the player, now)*. The pose is another function of the same two things. So both sides of the
frame can ask at the same second and agree exactly, and there is no lag to own.

`SkinnedRig::rootMotionAt(now)` is therefore deliberately **not** a read of `SkinnedRig::pose`, and
`SkinnedRig::evaluate` calls the same function rather than keeping its own copy of the arithmetic.

**Nothing accumulates.** Every sample is measured from the clip's own first key; the entity
subtracts consecutive samples to get a step. A running total on the rig is the obvious
implementation and it is the one thing that could not have survived a seek (ADR-091, ADR-267 D4).
The two fields the entity does keep — the previous sample and its generation — are cleared by
`EntityWorld::reset`, so a replay rebuilds them from the first step it runs.

**Measured.** The entity reaches its final position by summing ~60 differences, and the sum of a
telescoping series is its endpoints. Against the clip's closed form at the second each arm actually
stops on:

| rate | frames | last second | summed y | closed form y | error |
|---|---:|---:|---:|---:|---:|
| 60 Hz | 60 | 0.98333 | −2.04594 | −2.04594 | **0.0000005 m** |
| 40 Hz | 40 | 0.97500 | −2.04607 | −2.04607 | **0.0000000 m** |
| 24 Hz | 24 | 0.95833 | −2.04646 | −2.04645 | **0.0000007 m** |

The control on that arm is that the three rates are *not* three copies of the same arithmetic: they
stop on three different seconds and 0.00274 m apart, so each agreeing with its own closed form to
under a micron is a statement and not a tautology.

`RootMotionSample::generation` is the run a sample belongs to, hashed from the state index and the
second it was entered at. Two samples may only be differenced when it agrees. Without it, a
cross-fade out of `Landing` and back in hands the entity **+0.5306 model units in one step** — the
whole clip backwards, 1.92 m at Glowmere's scale, as a teleport. That number is measured in arm E
rather than asserted here.

---

## 5. Where it sits in the update, and the finding that follows from it

Root motion is applied **before the behaviours run**, not after.

It is a displacement, and every behaviour with an opinion about where a body may be — grounding,
crowd separation, the obstacle push — gets to have that opinion about this displacement exactly as
it does about navigation's. Running it afterwards would make an animation clip outrank the terrain,
which is the shape of four defects in this repository rather than a feature.

**The consequence, measured rather than discovered later:** a grounded body keeps no vertical root
motion at all. `applyGrounding` and `GroundFollower` both *assign* — `state.travel.y = ground.height
- anchor.y` — so the y component is overwritten inside the same update that produced it.

| arm | simulation position after `Landing` |
|---|---|
| no grounding behaviour | (+0.0492, **−2.0459**, −0.0560) |
| `ground` | (+0.0492, **+0.0000**, −0.0560) |

The horizontal survives intact and the vertical is gone. That is correct — a body standing on
terrain has its height decided by the terrain, not by an animator — and it is why `RootMotionAxes`
exists.

**So the production recommendation for `Landing` in a world with ground in it is `"axes": "xz"`,
not the default `xyz`.** 96% of `Landing`'s displacement is vertical, and in Glowmere all of that
vertical is erased the frame it is produced; opting the whole thing in buys 0.022 m of horizontal
shuffle and a y that grounding will overwrite. The clip that this mechanism is actually *for* is
`Dying_forward`, which travels 0.985 m across the ground and whose body genuinely ends up a metre
from where it was standing.

**And `Landing`'s displacement is not travel in the first place.** The clip's feet start at model y
0.80 and are planted at 0.022 by t = 0.30 — it is authored with the ground at y = 0 and its **end**
at the origin, a body arriving from a raised start pose rather than one setting off from the
origin. Root motion assumes the opposite convention. This is recorded as a fact about the take
rather than a defect: the mechanism transfers whatever the clip holds, faithfully, and what
`Landing` holds is a descent onto the origin.

---

## 6. The other 163 clips, proved

A claim of "bit-identical" deserves `memcmp`, not an epsilon.

Two copies of every rig in every animated asset the project loads: one with no opt-in — the shipped
configuration of every rig in this repository — and one with `Landing` opted in and nothing else.
Every clip played on both, the full joint palette compared byte for byte at nine sample seconds
across the clip's own key span.

**165 clips compared, 159 bit-identical, 6 changed — and all six are `Landing`**, one per alien
asset that carries it. The arm is symmetric: if the opt-in leaked, the changed count climbs; if the
comparison were blind, it is zero and the second half of the assertion catches it.

A rig with an opt-in that is playing something else reports `rootMotionApplied == false`, so "no
opt-in" and "opted in and not playing it" are different sentences — the distinction
`SocketResolution` exists for.

---

## 7. The controls, and what each one would have let through

ADR-182. Five arms, and each one's control can fail:

| arm | the control | what it rules out |
|---|---|---|
| A extraction | `Walking` through the same sampler: \|d\| = 3.6e−7, and its root's **span** over the cycle is 0.0556 m | a dead sampler reading the rest pose twice would have reported 0 for `Landing` too; the span says the measurement is alive |
| B the carrier | compensate at `root.x` instead: `spine_01.x` moves 0.567037 m against 0.000732 m | "the obvious implementation works here". It does not, and an arm that measured only the root would have said it did |
| C the three positions | the same scene with the opt-in removed: every number collapses to <1e−5, and the drawn toe still falls 2.8120 m | a measurement blind to two metres. It is looking straight at 2.81 m of movement and finding the arms agree on it to 0.8 mm |
| D the 163 | the changed count must be **non-zero** and must be `Landing` | a comparison that cannot see a difference passing the "identical" half vacuously |
| E the generation | the step a generation-blind consumer would take: **+0.5306 model units** | the guard being untested decoration |

Every band is a band. `Landing`'s dY is −0.5666 and the assertion brackets it at (−0.575, −0.560);
an assertion of `< −0.10` would have stayed green through an implementation that doubled it, which
is the bug ADR-182 is named after.

---

## 8. What this cost to learn, all of it the harness

1. **The baseline frame was a different clip.** The arm read its "start" pose one frame in, and the
   entity's `GaitSettings::blend` default of 0.2 s overrides the node's, so the first twelve frames
   are a cross-fade out of whatever state an imported rig happens to make current — `Button_push`
   here. The drawn toe "started" at the clip's *end* height and the fall measured 0.0008 m. The
   fixture now authors `"gait": {"blend": 0.0}`.
2. **`addDefaultStates` makes every clip loop.** `Landing`'s playable length is 1.0667 s; an arm
   that sampled at 1.10 s read a body that had landed and then teleported back into the air, and
   reported −0.06 for a −0.57 displacement.
3. **The closed form was 33 ms of clip early.** A state's local clock starts at zero and the clip
   it plays starts wherever its keys do -- 1/30 s on every take in the alien pack, because Blender
   writes the frame range it was given (`animation.hpp` says what that cost the pack once
   already). Arm F's closed form omitted `clip.start` and reported a **3.9 mm** error at all three
   rates. The tell was that it was the same at all three: accumulation noise grows with the number
   of accumulations and a constant offset does not.
4. **A floor that was a snapshot.** `test_character_intelligence_lab.cpp` asserted
   `blocked >= 2` over the lab's case list; P9 answering case 13 dropped it to 1 and the line
   failed, correctly. The check is a liveness control on the "a blocked case names a unit" arm
   above it -- which asserts nothing when nothing is blocked -- so it is now `>= 1` with that said
   out loud, plus `runnable + blocked == cases.size()`, and a note that the day the last case is
   answered the check and `LabCase::blockedBy` go together rather than the number being lowered
   again.
5. The scene fixture is a temp file, not an addition to
   `examples/labs/character/character-intelligence-lab.scene.json`. Six bodies there perceive and
   score one another and case 15's golden position trace is taken from it; a seventh would change
   what the other five see (the plan says so about P11 and it is just as true here).

---

## 9. A node with an opt-in and no entity deletes motion

There is nothing to hand the displacement to, so the compensation lands on the pose and the clip's
travel simply disappears: a body that used to descend stands still. `Composition` warns by name at
load. This is why `examples/lab/character-animation-lab.scene.json`'s `C-travelling-Landing` — a
bare glTF node with no entity, whose whole purpose is to make the discarded displacement visible —
is **not** opted in.

---

## 10. What changed that nothing tested

**Nothing in any shipped scene.** No scene in this repository authors `animation.rootMotion`, the
path is entered only for a clip named there, and §6 is the byte-level proof. Every existing render
is unchanged.

What is new in the tree: `NodeAnimation::rootMotion` in the scene format (read, written, and
round-tripped short-as-short), `RigStats::rootMotion`, `SkinnedRig::rootMotionApplied`,
`AnimationPlayer::currentStateIndex()` and `currentStart()`, `Entity::rootMotionStep()`, and a
third interface on `Composition::AnimationSink`.

`Landing` in Glowmere is **left alone**. Opting it in there would hand a grounded body a vertical
that grounding erases and a 0.022 m horizontal shuffle, and §5 explains why neither is worth a
change to a shipped world. The engine can now do it; which clips should is still the art decision
ADR-260 said it was.
