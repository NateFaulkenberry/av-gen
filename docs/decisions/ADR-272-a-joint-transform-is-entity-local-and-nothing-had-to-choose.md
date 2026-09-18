# ADR-272: A joint transform is entity-local, and until something implemented it nothing had to choose

**Status:** Accepted
**Date:** 2026-09-18

`entity::ISkeletonQuery` declared one method, `jointWorldTransform`. Its only consumer,
`Entity::socketTransform`, did this with the answer:

```cpp
base = scene::detail::composeTransforms(base, joint);   // base is the entity's world frame
```

which is the arithmetic for a transform **local to the entity**, not a world one. The name promised
one thing and the single use demanded the other, and that had gone unnoticed for as long as the
interface had existed because **`ISkeletonQuery` had zero implementations and `Entity::setSkeleton`
had zero call sites**. Nothing ever had to choose.

The consequence was not a latent inconsistency. It was shipped behaviour: every socket in this
engine fell back to the entity's own frame, and `socketTransform` returned `true` on the fallback.
A consumer could not tell a hand from a body origin. ADR-262 is the record of what that costs when
the consumer is a tractor beam.

---

## 1. The contract: entity-local, and the name moves

Three facts decide it, and they are independent of one another.

**A rig is shared.** `scene::Scene::rigs` is a flat list and several `scene::Entity` records may
name the same `RigId`. `scene::updateRigs` poses each rig **once**, picking the nearest
authored-visible entity only to decide its rate. A posed rig therefore cannot have a world position:
it stands in as many places as there are bodies carrying it. A `jointWorldTransform` would have to
pick one of them and be wrong about the rest.

**glTF says so.** From `scene/skeleton.hpp`: the file's whole chain from its scene root is baked
into the joints, so "the model space these matrices land in *is* the file's scene space, and the
entity that carries the skin contributes only its placement in the world". The placement belongs to
the entity, and the entity is the only thing holding it.

**The GPU agrees.** The skinned vertex stage multiplies the entity's model matrix by the palette. A
world-space palette would apply the placement twice.

So the method is `jointTransform` and it answers in the rig's model space, which *is* the entity's
frame. The caller composes: `world = entityTransform * jointTransform(joint)`. The name moved to
meet the use rather than the use moving to meet the name, because the use was right and only one of
the two was load-bearing.

Implementing against the other convention would have been worse than leaving the fallback in place.
The fallback is at least honestly approximate; a socket composed twice by the entity transform is a
prop at a plausible-looking wrong place, which is the class of defect this repository has paid for
three times.

---

## 2. Model space, not the palette

`SkinnedRig::palette[k] = model[palette[k]] * inverseBind[k]` — what the GPU multiplies a bind-pose
vertex by. **Its translation is not where the joint is.** Measured on `hand.r` of
`alien-scout.glb`, posed: the model-space translation and the palette translation are **0.9112
model units** apart, on a character whose whole bind-pose height is 1.662. Reading a bone position
out of the palette is the plausible-looking mistake ADR-260 warns about, and the number is now in
`tests/unit/test_character_lab_sockets.cpp` rather than the warning being on trust.

The implementation re-derives model space from `SkinnedRig::pose` through `poseToModel`, the same
call `skinningPalette` makes before it multiplies the inverse binds in, cached on
`SkinnedRig::paletteVersion` so a second socket in the same frame is a lookup.

---

## 3. The fallback becomes sayable

`socketTransform` returns `entity::SocketResolution` rather than `bool`:

| | meaning |
|---|---|
| `None` | no socket of that name; `out` untouched |
| `EntityFrame` | resolved against the entity's own frame — no skeleton, no joint named, or a joint this rig does not carry |
| `Joint` | resolved against a posed joint |

The approximation is still offered, because a prop has to be somewhere and a scene must be
authorable before its skeleton exists. What changed is that a caller can now tell. `resolved()` is
the predicate the old `bool` meant; three call sites take it.

A socket naming a joint the rig does not carry is the case this matters most for: before, a typo in
a scene file was indistinguishable from a working socket, and cost a silent metre.

---

## 4. Which position (ADR-260)

`socketTransform` reads the **drawn** position — `state().position() + motion_.position`, what
`visualPosition()` returns — because a socket exists to put something where the body is on screen.
Glowmere's saucer carries 2.4 m of hover drift; a prop hung on `state().position()` would sit 2.4 m
off it.

It writes nothing. `MotionAuthority::PoseOnly`.

---

## 5. What implementing it found

**The node's scale was never part of a socket, and could not be seen to be missing while every
socket resolved to the body.** A joint offset is in the asset's own units. Glowmere draws its four
aliens at 3.344× to 3.610×. Measured at two scales of the same pose: the hand socket reaches
2.8785 m at 3.610× and 1.4392 m at 1.805×, exactly 2.000×. A scale-blind socket would have reported
1.000×, and a hand-mounted prop would have sat at 28% of the distance out from the body that the
hand is. The scale is read from the node parameter rather than remembered, because it is keyframable
and `applyOffsets` has already folded `motion_.scale` into it by the time attachments are placed.

**Attachments lag the pose by one frame.** `applyAttachments` runs inside `EntityWorld::update`,
which runs inside `Composition::updateBehaviour`; the rigs are posed later, in
`Composition::update`. So an attachment reads the previous frame's pose — 16.7 ms of a walk cycle.
Stated rather than hidden. The fix is an ordering change inside `EntityWorld::update`, which is P1's
file and not this one's.

---

## 6. What this unblocks

`Entity::socketTransform` is the authoritative world-space semantic-position call, and it now
answers. Every socket, every attachment, every carried prop and every aim that resolves through a
socket has a real joint underneath it for the first time. P6's animation layer stack, P9's root
motion and any interaction that names a socket on a prop were all written against a seam that was
returning the entity's origin and saying it was a joint.
