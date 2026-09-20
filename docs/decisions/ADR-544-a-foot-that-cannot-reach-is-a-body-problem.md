# ADR-544: A foot that cannot reach is a body problem, not a solver problem

**Status:** Accepted
**Date:** 2026-09-20
**Related:** ADR-300 (the layer stack is `PoseOnly` by construction), ADR-359 (a leg is three names
and the body has to sit down first), ADR-541 (a provider that remembers lives in the entity tier),
ADR-543 (a leg is three joints and a write-back rule)
**Implemented by:** `scene::solveBodyCompensation` in `src/scene/ik.{hpp,cpp}`, applied by
`scene::PoseLayerStack::apply`, authored as `animation.bodyCompensation`.
**Tests:** `tests/unit/test_body_compensation.cpp` — 9 cases, 84 assertions, including three on the
real `alien-scout.glb` through the real importer and the real stack.

---

## Context

ADR-543 set out to answer whether the Glowmere alien's detached leg could be solved. It could. On
the way it measured something that mattered more:

```
maxReach (upper + lower) = 0.6555
rest hip-to-foot         = 0.6457      98.5% extended at rest
SLACK                    = 0.0098
```

**The leg can be lengthened by under one centimetre.** A foot may be raised freely; a 10 cm step
down is 0.0896 out of reach and wants 0.0902 of hip drop. So on the primary character, foot IK
without a body stage is upward-only, and terrain adaptation — the thing foot IK exists for — is
exactly the downward case.

This is not a property of this asset alone. ADR-359 already measured the nine farm rigs binding
their hind legs at **97.9% to 100.0% of their own span** and recorded that as the reason
`extension` defaults to 1. Every character in this repository stands nearly straight. A contact
system that assumes limb slack has no content to run on.

## Decision

**Contact solving gains a stage in front of the limb: decide whether the body has to move, and by
how much.**

```
demands (root, target, reach) per limb
        │
        ▼
solveBodyCompensation ──▶ translation ──▶ applied to the body joint
        │
        ▼
the limbs solve as they always did, against targets that have not moved
```

Four properties, each chosen against an alternative that was tried or considered:

1. **It is a property of the stack, not of a layer.** Every foot layer on a rig is asking the *same*
   body to be somewhere and the body has one answer; a per-layer version gives four feet four
   pelvis translations. So the stack gathers every foot layer's demand, solves once, translates
   once, and then runs the layers.

2. **It is pure.** `solveBodyCompensation` takes positions and lengths, returns a translation,
   allocates nothing, reads no clock and has no seed — the same contract as `solveTwoBone`
   (ADR-359) and for the same ADR-541 reason. The iteration count is **fixed at 8 rather than run
   to a tolerance**, because a loop that runs until it is happy is a loop whose length depends on
   its input, and two renders of the same frame must agree.

3. **Simultaneous projection, keeping the best sweep.** Each sweep moves the body by the *average*
   of the corrections the violated limbs are asking for, damped per axis by `compliance` and
   clamped to the limits. The reachable set for each limb is a ball and the intersection of balls
   is convex, so it cannot oscillate — but under a per-axis clamp the sequence is not monotone, so
   the solver returns the best sweep it visited rather than the last. A body that lurched for
   nothing is the failure that guards against.

4. **`Limited` and `Impossible` are different answers.** A body sitting on a limit would have been
   helped by more room; a body inside its limits and still short would not, because two limbs are
   pulling apart. Conflating them would send an author to tune a number that cannot help. This is
   the same rule `IkStatus`, `LayerResolution` and `PathStatus` already follow.

**Defaults.** `maxDown 0.25`, `maxUp 0.05`, `maxLateral 0.05`, `compliance {0.15, 1.0, 0.15}`. The
asymmetry is deliberate: a character drops its hips to reach down far more readily than it rises,
and a body that slides *sideways* to reach a step reads as a stumble. The whole thing is **off by
default**, because it moves a joint nothing else moves and a scene that did not ask must not get it.

**The body joint defaults to the skeleton root** — joint 0, whose parent is -1 — for the reason
`root_motion.hpp` already gives for compensating there: translating a parentless joint's local *is*
a rigid model-space translation of everything beneath it. On the alien, whose rig is nearly flat
and whose spine, hands and head are siblings of `root.x` rather than descendants, it is the only
joint the legs and the torso share. A rig with a real pelvis names one.

## Evidence

The primitive, on synthetic demands with known answers: a limb short by exactly 0.10 moves the body
exactly -0.10 and no further; a demand 2.20 beyond the limb goes to the 0.25 limit and reports
1.95 still short; two limbs pulling apart report `Impossible` while nowhere near a limit; a frozen
axis is not moved along and the shortfall is reported rather than hidden; four limbs at once get an
answer deep enough for the worst and no deeper.

**On the real rig**, `alien-scout.glb` loaded through the importer, one foot layer, a 10 cm step
down — the same target in all three arms:

| | body moved | leg | foot |
|---|---|---|---|
| compensation **off** | 0.0000 | `Clamped` | stops short, more than 5 cm from the target |
| compensation **on** | **-0.0902 in y** | `Solved` | **arrives**, within 2e-3 |
| target *raised* 10 cm, compensation on | 0.0000 exactly | `Solved` | arrives |

And the arm that separates "the character crouched" from "the leg stretched": with compensation on,
`head.x` ends up lower by exactly the compensation and more than 5 cm below where it started. With
it off, the head does not move at all.

## What this does not do

It is one translation. It is not full-body IK: no torso counter-rotation, no per-limb weighting, no
centre-of-mass or balance term, no spine bend. Those are the growth directions and the interface is
shaped to allow them — `ReachDemand` is a list and `BodyCompensationLimits` is per axis — but none
of them is built, because none of them has been measured as necessary and the brief for this phase
says to build the smallest useful mechanism.

## Rejected alternatives

* **`if (rig is the alien) lowerHip()`.** Named and rejected in the phase brief, and it would have
  been wrong anyway: ADR-359's measurement says every farm rig has the same problem to within two
  percentage points of extension.
* **Lowering the hip inside the foot layer.** A pose layer is `PoseOnly` and structurally cannot
  reach the simulation (ADR-300), which is right — but four layers each translating the same body
  joint is four times the correction, and the last one to run wins.
* **Iterate to a tolerance.** Rejected on determinism: see Decision 2.
* **Solve the translation in closed form.** For one demand it *is* closed form and the first sweep
  finds it exactly. For several it is the smallest point in an intersection of balls, which has no
  closed form worth the code.
* **Let the limb clamp and call it done.** That is what ships today, and it is why no Glowmere alien
  has ever stood on uneven ground.

## Consequences

* Foot planting on the primary character becomes two-directional for the first time.
* `PoseLayerStats` gains `bodyCompensations`; the full result — status, translation, shortfall
  before and after, how many limbs are still short — is on the stack for an overlay to read.
* Authoring is one scene block: `"bodyCompensation": { "enabled": true, "maxDown": 0.25 }`.
* A character standing on ground it cannot reach now reports `Limited` with a number, where before
  it reported a clamped limb and no explanation of why.

## Revisit triggers

* A rig arrives that needs the torso to counter-rotate, not just the body to translate.
* Several limbs with genuinely conflicting demands become common — the averaging is fair and is not
  weighted, and a hero foot that must plant exactly would want to outrank a trailing one.
* Contact *phase* lands (Phase A step 4/5): compensation should probably follow the planted feet
  only, and today it follows every foot layer with a target.
