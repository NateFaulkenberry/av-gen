# ADR-174: The tree is skinned to a branch skeleton, because a per-tier wind uniform cannot hold its joints together

**Status:** Accepted
**Date:** 2026-09-14

*Numbered 174 because two other agents are working concurrently; 170 is this project's, 172 and 173
are Glowmere Valley 2's. Expect to renumber on merge.*

## Problem

The Tree of Life needs hierarchical motion: the trunk barely moving, primary limbs swaying, twigs
and foliage lively, with a primary limb carrying its children when it moves (brief §40), inertia
rather than direct audio-to-transform mapping (§41), and baseline motion when nothing is playing
(§42).

`wind::VegetationMotion` (ADR-055/056) is the engine's existing answer to "make this plant move". It
is analytic, evaluated in the vertex stage, costs a per-frame uniform and nothing else, is already a
field on `ProceduralGeometry`, and is calibrated so its analytic and integrated tiers agree at DC.
It was the obvious choice and the architecture proposal named it.

## Decision

**It cannot be used for this tree, and the tree is skinned to a 239-joint branch skeleton instead.**

## Rationale

The reason is a property of this tree rather than a shortcoming of that system, which is why it is
worth recording: someone will otherwise re-derive it by trying.

The tree is **six meshes, one per semantic tier** — a granularity forced by picking, which resolves
an object index and never an instance index, so whatever granularity the meshes come out at is the
granularity an artist can select (research doc §3.4). `bendDisplacement` bends a mesh about **its
own base**, with a height curve over its own extent. Two tiers bent about different origins
**separate at every joint between them**: a tertiary branch's base pulls away from the secondary
carrying it. No setting of the amounts fixes it, because the discontinuity is in the decomposition
and not in the parameters.

The obvious repair makes it worse. Giving every tier the same `baseY` and `extentY` makes the
displacement a continuous function of world height, so the joints hold — and then every tier moves
identically, which is the hierarchy the whole exercise is for.

A skeleton has neither problem:

- A joint shared across a fork carries **both sides of it**, so a limb and its children cannot
  separate. The question does not arise.
- `poseToModel` composes a child's local transform onto its parent's model matrix. That **is**
  transform inheritance — §40's requirement directly, rather than an amplitude ladder that resembles
  it from a distance.
- Inertia is a spring per joint with the audio moving its *target* (ADR-176), so §41 is the
  integrator rather than an easing curve.

## What the budget buys, and what it does not

`kMaxPaletteJoints` is 256 and the tree has ~1,500 axes. Joints go on the trunk, the primary limbs
and as many secondaries as fit, **thickest first**, and the twigs ride whichever joint carries them.

That is the right place to spend it. What a viewer reads as "the tree is alive" is the slow travel of
big limbs; a twig that moves exactly with its parent is not a defect anyone can see at this scale.
The specific thing given up is per-twig flutter, and it is named here rather than discovered later.

A secondary whose primary did not fit is **skipped rather than reparented to the trunk**. A limb
rigidly attached to the trunk while its neighbours flex is more visible than a limb that does not
flex at all.

## Determinism, and where this project's usual rule is deliberately not met

A pose is **not** a pure function of `(parameters, time)`. An integrator is state, and the pose
depends on the frames before it. That is what inertia is, and pretending otherwise would mean
choosing an analytic response — which is what was rejected above.

Both properties are kept anyway:

- `TreeAnimator::settle` runs a fixed simulated interval before frame zero, so an offline render
  starts from the wind's steady state rather than from a tree standing still and then lurching.
- The **substep count comes from the stiffest joint, not from the frame rate**. A spring integrated
  at a step near its own period does not merely lose accuracy, it gains energy and diverges. A test
  asserts four seconds of wind integrate to the same mean bend at 24 and at 120 fps.

## Consequences

- Two bugs this shape invites, both found by rendering and both now fixed in a way the code
  explains: a leaf card's four corners each binding to their own nearest joints **sheared the cards
  into torn quads**, so every vertex of a cluster binds at the cluster centre while a branch vertex
  still binds at itself; and a per-joint bend limit of 0.30 rad **compounded over a dozen joints
  into a crown displaced metres**, so the limit is 0.055 and belongs per joint.
- A vertex binds only within its own axis's chain and its ancestors'. A purely positional bind
  attaches a twig to whatever limb it happens to hang beside.
- `SkinnedRig::enabled` is false. `updateRigs` would otherwise evaluate an animation player this rig
  does not have and overwrite the pose; the header's contract for a disabled rig is that its palette
  "is left exactly as it is".

## Alternatives considered

**One `ProceduralGeometry` for the whole tree with `VegetationMotion`.** Rejected: it makes the tree
a single unselectable object in the editor, and §3.4's granularity argument runs the other way.

**A custom WGSL deformer reading a per-vertex compliance value.** Rejected: `scene::Vertex` has no
spare channel, `DeformerUniform` has no free lanes, and adding a deformer kind means changes in
lockstep across the enum, the JSON, the CPU reference and the shader.

**CPU-animating the graph and rebuilding the meshes each frame.** Rejected on measurement: mesh
generation is ~200 ms.

## Verified vs assumed

**Verified:** that rotating one joint moves every joint below it and no joint above or beside it;
that every skinned vertex binds to real joints with unit weight; that ten seconds of steady wind
does not diverge and the trunk's mean bend stays under 0.02 rad while outer joints move more; that
the same inputs give bitwise-identical bend; that 24 and 120 fps agree; that three rendered frames
differ from each other while mean luminance does not swing.

**Assumed:** that 239 joints is enough for the motion to read at the showcase camera. It has been
looked at in three still frames, not in motion — nothing here has been watched as video.
