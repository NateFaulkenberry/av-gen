# ADR-300: A head is a group of names, and the rig has no word for it

**Status:** Accepted
**Date:** 2026-09-18

P6 of `docs/character-ai-plan.md`. The unit asked for "two slots with a joint mask, so
`LocomotionState::reaction` and `lookTarget` stop being published into nothing".

---

## 0. What was actually wrong, in one quotation

`src/entity/behaviors.cpp`, in `LookAt::update`, on why it refuses to turn a walking character's
body towards what it is attending to:

> A character that is going somewhere faces where it is going. Turning the *body* towards something
> while walking elsewhere is not a compromise between the two, it is a character that strafes […] So
> while travelling this publishes the look target and leaves the yaw alone: **where the eyes and head
> go on top of a walk cycle is the animation layer's business, and LocomotionState carries it there.**

It carried it nowhere. `AnimationSink::setLocomotion` read five of `LocomotionState`'s fourteen
fields and `reaction`, `lookTarget` and `hasLookTarget` were not among them. The engine had a
behaviour that deliberately declined to do a job, named the layer that should do it instead, and
that layer did not exist — so every `lookAt` on every travelling character in this repository has
been a no-op since the day it was written. ADR-225 is the rule that makes that a defect: a setting
the application does not keep is not a setting.

The same paragraph exists in `Interest::update`, about the other field:

> A reaction is punctuation. It decays on its own, it does not stop a walk (**the animation layer
> blends a flinch over whatever gait is playing, which is what LocomotionState::reaction is for**).

Neither promise could have been kept. Until now the entire blending model of this engine was one
cross-fade between two clip slots, and three-way fades were explicitly refused
(`animation.cpp:244`). A cross-fade can say "walk, then run". It cannot say anything at all about a
*part* of a body.

---

## 1. What was built

`src/scene/pose_layers.{hpp,cpp}` and a `JointMask` in `src/scene/skeleton.{hpp,cpp}`. A
`PoseLayerStack` lives on `SkinnedRig` and is evaluated inside `SkinnedRig::evaluate`, in exactly
one place and in exactly one order:

```
player.evaluate(...)   ->   layers.apply(...)   ->   skinningPalette(...)
```

Two layer kinds, which is what the two published fields need and no more:

| kind | what it does | drives |
|---|---|---|
| `Aim` | turns a group of joints as a rigid group about one joint's pivot, so the body's forward axis points at a target, clamped in azimuth and elevation | `lookTarget` / `hasLookTarget` |
| `Additive` | adds a clip's displacement *from its own first frame* on top of whatever is posed, per joint, at a weight | `reaction` |

The binding from the seam to a layer is by **role** (`drive: "look"`, `drive: "reaction"`) and never
by layer name, so a scene may call a layer anything and a second look layer on the same body still
works. A layer with `drive: "manual"` is one a tool, a timeline or a test sets by hand.

**A stack rather than two slots.** `layers` is a vector, because a fixed pair is *more* code than a
vector and forecloses the third layer the moment anyone wants one. What the plan was asking for —
somewhere to put a thing that is not the gait — is a property of there being a stack at all, not of
its length. The evidence that two is not obviously the right number is in the lab fixture, which
already runs both at once on the same body in the same frame.

---

## 2. The finding: `descendants` is not how a head is expressed on this content

The obvious way to write a head mask is "the head joint and everything beneath it". Measured
against the asset it would actually run on:

**`alien-scout.glb` — 90 joints, and the rig is nearly flat.**

```
rig
  root.x   -> foot.l -> toes_01.l, thigh_twist.l -> …
  spine_05.x -> arm_stretch.l, shoulder.l, neck.x, …
  spine_04.x
  spine_03.x
  spine_02.x
  spine_01.x
  hand.l -> (fingers)
  hand.r -> (fingers)
  forearm_stretch.l -> forearm_twist.l, …
  Antenna -> Antenna.001 -> …
  Eye_L  -> Eye_ball_L
  Eye_R  -> Eye_ball_R
  head.x
  Mouth
```

`head.x` has **zero children**. The eyes, the mouth and the antenna are its *siblings* under the
armature. `spine_01` through `spine_05` are siblings of each other. `hand.r` is a sibling of
`forearm_stretch.r`, not a child of it. The mask "head.x and its descendants" resolves to **1 joint
of 90**, and a look-at built on it would turn a head mesh and leave the eyes and the antenna
pointing the old way, on a character six metres tall. That number is asserted in
`tests/unit/test_character_lab_layers.cpp`, not left in this document.

Three rig families, three vocabularies, no overlap. `joints` is `Skeleton::jointCount()` — the skin's
own joints plus every ancestor of one, which is one armature node more than the skin declares:

| asset | joints | the head | alone | + descendants |
|---|---:|---|---:|---:|
| `assets/aliens/alien-scout.glb` | 90 | `head.x`, with `Eye_L`, `Eye_R`, `Mouth`, `Antenna` **beside** it | 1 | **1** |
| `assets/farm/bull.glb` | 28 | `Head01` -> `Head02`, nested under `Neck01` -> `Neck02` | 1 | 2 |
| `assets/farm/chicken.glb` | 18 | `Head` -> `Beak` | 1 | 2 |

Each family's name for a head resolves to **nothing** on each of the other two, reported by name.
That is the arm that would have failed if any single name worked everywhere — in which case the
right design would have been a compiled-in head mask and a `lookAt` that simply worked.

So:

* **A mask is a group of names, resolved against the skeleton it will run on.** `descendants` exists
  and defaults to **off**, because on the primary content it adds nothing and on an aim layer it is
  actively wrong (see §3). The farm rigs are the ones it is for.
* **No joint name is hardcoded anywhere in the engine.** There is no default head mask and there
  will not be one: a name that is right for a third of the content and silently wrong for the rest
  is the same class of thing as a socket that returned `true` on its fallback.
* **A name a rig does not carry is reported.** `JointMask::missing` names it, `named` counts what was
  asked for and `joints` counts what resolved, and `PoseLayerStack::bind` turns the difference into
  prose the composition logs against the node's name. A layer that could not work answers
  `LayerResolution::NoJoints` and a layer nobody asked answers `Inactive` — two words, because
  ADR-274 is the record of what it costs to have only one.

**The aim rotates about a shared pivot** rather than about each joint's own origin, which is what
lets five siblings turn as one body part. On a nested rig the same arithmetic degenerates to the
obvious thing: rotate the parent, the children follow.

---

## 3. Two joints of one mask nested inside each other is a real footgun, and it is counted

An aim layer pre-rotates in model space; a child carries its parent's rotation *and* then applies
its own, so masking `neck.x` and `head.x` together with weight 1 each turns the head twice.
`JointMask::nested` counts masked joints that have a masked ancestor, and `bind` says so:

```
layer 'look': 2 of its 6 masked joints sit inside another masked joint, so an aim rotation is
applied to them twice
```

Counted rather than forbidden, because weighted distribution down a chain is a legitimate thing to
want — `{neck: 0.35, head: 0.65}` is a better-looking look-at than `{head: 1.0}` on a rig that has a
neck. Composition of two slerps about a common axis is exact, so the distribution adds up to the
whole turn and the only residual is that the pivot does not follow the joints it already moved.
Measured: asking for +30°, +60°, +90° and −90° against a 75° limit returns **+29.984°, +60.030°,
+75.000°, −74.998°**, a worst residual of **0.031°**.

---

## 4. The clamp is on the direction, not on the rotation

`aimRotation` decomposes into azimuth and elevation relative to the body's forward, clamps each,
rebuilds the wanted direction and takes the shortest arc to it. The alternative — build the rotation
and clamp its components — needs a right-hand axis and a composition order, and ADR-260 is this
repository's record of what that costs: the slope lean resolves pitch in the body frame and roll in
the world frame because the Euler triple it writes composes `Rz·Ry·Rx`, the lean varies by a factor
of 2.5 with heading on identical ground, and a quarter of all headings gimbal-lock. A direction has
no composition order to get wrong.

Forward defaults to **+Z**, and that is not a guess about glTF. It is this engine's own yaw
convention — `yaw = atan2(direction.x, direction.z)` in `behaviors.cpp` — so an asset that did not
face +Z would already be walking backwards everywhere. It is authorable anyway, because the
alternative the day such an asset arrives is a silent 180.

---

## 5. Which position, and which frame

**ADR-260.** Every layer is `PoseOnly`. It writes `SkinnedRig::pose`, which becomes `palette`, which
the renderer draws. It writes no simulation position, no `MotionOffset` and no node parameter — and
that is structural rather than a convention: `src/scene/pose_layers.*` includes nothing from
`entity/` and nothing from the composition, so there is no expression it could write one with. The
stride-bob defect is what happens when that boundary is a habit.

The one thing that reads the *drawn* position is `AnimationSink::driveLayers`, which converts the
world-space `lookTarget` into the rig's own frame through the node's world transform — the
parameters' finals, which `applyOffsets` wrote earlier in the same `EntityWorld::update`, so there
is no frame of lag here (unlike attachments, ADR-274 §5). Through the node transform rather than
through `state.yaw` for three reasons, and the third is the expensive one: it carries the parent
chain, it carries the behaviours' bank and nod, and it carries the **scale**. Glowmere draws its
aliens at 3.344× to 3.610×, a joint offset is in the asset's own units, and ADR-274 §5 measured what
forgetting that does to a hand-mounted prop (28% of the distance out from the body that the hand is).

**ADR-274.** A layer's target is **entity-local**, for the three reasons that decided
`jointTransform`: a rig is shared, glTF bakes the file's chain into the joints, and the GPU
multiplies the entity transform in afterwards. Whoever sets the intent does the conversion, because
the entity is the only thing that knows where this rig is standing.

**ADR-091.** `apply()` is a pure function of (the layers' intent, the skeleton, the clips, the
sample second). Nothing is integrated, smoothed or carried across a frame. The smoothing a look-at
wants already exists one layer up, in the behaviour that decides *where* to look, and that layer is
re-simulated on a seek. The sample second passed to the layers is the rig's grid time `t`, not the
frame's `now`, so a rate-limited rig's layers move on the same fixed grid its clips do.

---

## 6. What is deliberately not here

**An additive layer's phase is the timeline second, wrapped — not the moment the reaction started.**
A start time is state carried across frames, and the only place it could be kept correctly across a
seek is inside the simulation a seek replays: `entity::LocomotionState`, published from
`entity.cpp`, which is P1's file for the duration of this plan. The consequence is visible and small
— the flinch's *strength* follows the envelope exactly and its *phase* does not restart — and the
fix is one `float reactionAt` on the seam, published beside `reaction`, read here. It is named here
rather than left for someone to rediscover.

**No IK, no foot lock, no morph targets, no look-at from the eyes.** An aim layer turns a group; it
does not solve a chain. The moment anything wants two-bone IK this is where it goes, and the mask is
already the way to say which bones.

**No root motion.** See §8.

---

## 7. The evidence

`tests/unit/test_character_lab_layers.cpp`, and lab case `character:10`. ADR-182's shape: the claim
is "the head turned and the legs did not", so *both halves* must fail when the mask is wrong.

| arm | head turn | Eye_L | foot.l | foot.r | root.x |
|---|---:|---:|---:|---:|---:|
| **A** the fixture as authored, target at (2, 0, 12), character **walking** | **+32.71°** | 0.0935 | **0.000000** | **0.000000** | **0.000000** |
| **B** the same layer, weight pinned to 0 | 0.000° | — | reference | reference | reference |
| **C** the same layer, the same intent, masked onto the **feet** | **+0.0000°** | — | — | **0.1447** | — |

Arm B is what makes A mean something: same world, same seed, same second, same 510 simulation steps,
and the only difference is whether the layer's intent is read. Arm C is what makes the *mask* mean
something: delete the mask and the layer would write the whole pose and C's head would turn; delete
the layer and A's head would not. The layer wrote **5 joints** of 90.

The additive half, driven by a `music.impact` pulse through `Interest`:

| | reaction | spine_05.x | neck.x | foot.l | toes_01.l |
|---|---:|---:|---:|---:|---:|
| flinch | **0.6733** | 0.0088 | 0.0334 | **0.000000** | **0.000000** |
| control, no impact | 0.0000 | reference | reference | reference | reference |

Two layers applied in the same frame on the same body, neither of them the gait, writing **20 of the
rig's 90 joints** between them: 5 for the head group, 15 for the upper body the flinch masks.

**What it costs, structurally** (ADR-170: joints, not milliseconds). An additive layer is two clip
samples and one pass over its masked joints. An **aim** layer is two extra forward passes over the
whole rig on top of the palette's own — one to derive the pivot from the pre-layer pose, one to
apply — so a posed alien with one aim layer composes 270 joint matrices where it composed 90. The
pivot pass cannot be folded into the applying pass, because a masked joint earlier in the topological
order needs the pivot before the pass reaches the joint that defines it. It can be narrowed to the
pivot's own ancestors and has not been, because nothing here is near a budget yet and a measured
reason to do it is worth more than a guessed one.

**Three things this file cost to learn, all of them the probe rather than the engine** (ADR-182
again, and the tally is now eleven for this lab):

1. The first harness restarted the timeline on every call to `play()`, so the arm that re-bound a
   layer halfway through ended half a second *behind* the arm that did not. It reported 0.026 m and
   0.043 m of "masked" foot movement that was really 0.5 s of walking. A control arm that is a
   different second is not a control arm.
2. Arm C measured `foot.l` — which was also the arm's pivot. A joint rotated about its own origin
   keeps its position exactly, so the control reported 0.000000061 m from a layer working perfectly.
   It now reads `toes_01.l` and `foot.r`.
3. The reaction arm fired the impact at 3.0 s and read at 4.0 s. `Interest` decays `reaction` at
   1.4 per second from a peak of 1, so it is back at exactly zero after 0.714 s: the arm was
   measuring a flinch that had already finished, and reported 0.0000 as though the seam were still
   dead.

---

## 8. What P9 (root motion) now has, and what it still does not

**Has.** A hook that runs after the player and before the palette, holding the final pose, with the
skeleton, the clips and the sample second in scope — which is where extraction has to happen,
because the root channel must be taken *and zeroed* on the same pose the palette is built from.
`JointMask` is how "the root joint, per clip" gets expressed, which is the exact thing ADR-260's
inventory had to find by hand ("the root has to be found per clip, as the lowest-indexed joint the
clip actually translates"). `findClip` is now a free function over a clip list. And `SkinnedRig::pose`
is the *final* pose, so a socket, an attachment and `ISkeletonQuery` all see what the layers did —
a root-motion layer cannot desynchronise them.

**Does not have.** A way to write the simulation. `MotionAuthority::Simulation` is precisely what
this module is built to be unable to do, and root motion is the one case that needs it. P9's seam is
a value read *back out* of the rig into the entity, in the opposite direction to everything here,
and it will need a decision of its own about who owns the ordering (the rigs are posed in
`Composition::update`, one stage after `EntityWorld::update` reads them). Nothing here forecloses
it. Nothing here provides it either, and a layer kind called `RootMotion` that quietly wrote
`travel` would be the fourth instance of the defect ADR-260 is named after.

---

## 9. A GPU crash found on the way, which is not this branch's

`avgen_render_tests` run whole, under `tools/gpu-lock.sh`, **dies with SIGBUS** late in the suite —
after `test_tree_gpu` and `test_texture_share`, in the same neighbourhood on every run. Three runs on
this branch, three crashes.

It is **not this work**. The control is the merge base, `9d5347c`, built in its own worktree and run
the same way under the same lock: **it crashes too**, with the same signal, in the same region.

And every GPU test that could reach a posed skeleton passes on this branch:
`[skinning],[animation],[character],[motion],[tree],[character5_3]` together are **11,743 assertions
in 28 test cases, 0 failures**, including the 239-joint skinned tree and its motion-vector arm. The
layer stack cannot be reached from any of those scenes in any case — no scene in this repository
authors a layer, and `apply()` returns on its first line when the stack is empty — which is why
every existing render is byte-identical.

Recorded here rather than fixed, because it belongs to whoever owns the render suite and because the
honest thing to say about a crash you did not cause is where its control was taken. The CPU suite is
the tier this unit is measured on and it is clean: **2,220 cases, 2,215 passed, 4 skipped, 1 failed
as expected** — `test_character_lab_slopes`, ADR-260's deliberate `[!shouldfail]` invariant.

---

## 10. What changed that nothing tested

Any scene authoring `animation.layers` on a node now poses differently; **no scene in this
repository authored one before this change**, so every existing render is byte-identical and the
only new behaviour in the tree is the `watcher` character added to the Character Intelligence Lab
fixture. `RigStats` gained `layers` and `layerJoints`. `SkinnedRig::findClip` now delegates to a free
`scene::findClip` with identical semantics.
