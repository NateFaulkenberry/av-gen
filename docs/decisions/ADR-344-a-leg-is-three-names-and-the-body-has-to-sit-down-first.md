# ADR-344: A leg is three names, and the body has to sit down before the feet can reach

## Status

Accepted. 2026-09-18.

## Context

This engine had no inverse kinematics of any kind, and it had never had any. ADR-266 and
`src/entity/character_ai.hpp:358` record the survey that found it: the animation layer was "one
cross-fade between exactly two clip slots", with no additive, no masks, no layer stack, no IK, no
morph targets and no root motion extraction. Three of those have since landed -- P5's
`ISkeletonQuery` (ADR-274), P6's layer stack (ADR-300), P9's root motion (ADR-337). IK was surveyed,
written down as absent, and then left out of `docs/character-ai-plan.md` entirely. It was never
rejected; it was never scheduled.

What the absence costs is visible in one place. Glowmere Valley 2 carries sixteen farm animals on
terrain that is a noise function, and `entity::GroundSettings::slopeAlign` tilts the whole body
towards the surface normal at 0.55 of the way. That is a body-level approximation and it is the
right one; it says nothing at all about where any individual hoof is. On a bank the downhill hooves
hang and the uphill hooves go through the ground, and the soles point wherever the walk cycle left
them.

## Problem

Put a foot on the ground it is actually over, per limb, without moving the body, without carrying
any state across a frame, and without any existing scene moving a millimetre.

## The finding that decided the shape of it

**The alien rig has no leg chain.** `assets/aliens/alien-scout.glb` has 92 nodes and the parent
chains say:

```
toes_01.l        -> foot.l -> root.x -> rig
foot.l           -> root.x -> rig                       <- a direct child of root
thigh_twist_2.l  -> thigh_stretch.l -> thigh_twist.l -> root.x -> rig
leg_stretch.l    -> rig                                 <- a sibling of root.x
```

The foot, the thigh and the "leg" are three independent branches under two different parents. It is
a stretch/twist deformation rig, not an FK chain, and a textbook two-bone solver has nothing to
solve on it. ADR-337 already found the same rig "nearly flat" for root motion; this is the same fact
a second time.

**The farm pack is the opposite, and it is where the content is.** All nine rigs carry a textbook
chain -- and they spell it three different ways:

| rig | hip | knee | foot | note |
|---|---|---|---|---|
| bull, cow, horse | `UpperLegB.L` | `LowerLegB.L` | `HoofB.L` | horse adds `HoofB.L.001` below |
| sheep, goat, pig | `UpperLegB.L` | `LowerLegB.L` | `FootB.L` | `AnkleB.L` sits between the knee and the foot |
| chicken, rooster, chick | `UpperLeg.L` | `LowerLeg.L` | `Foot.L` | chick has no ankle |

So: start with the farm animals, name the chain outright rather than deriving it, and check that the
three names are a chain rather than three joints that happen to exist.

## Decision

### 1. A two-bone analytic solver as its own unit

`src/scene/ik.{hpp,cpp}`. Three points, a target, an optional pole, in one space. No rig, no clock,
no allocation, no knowledge of what a joint is.

**Analytic and not iterative.** CCD or FABRIK would buy an iteration count and a convergence
threshold and nothing else. More to the point, an iterative solver seeded from the previous frame is
state carried across frames, which is exactly what ADR-091 forbids the baked tier, and one seeded
from rest converges to a slightly different place depending on how far it had to go. A law-of-cosines
solve has no seed.

Three steps, and the order is what makes it exact: the bend at the knee fixes the *distance* to the
target, the aim at the root fixes the *direction*, and the pole spin about the root-to-target axis
fixes the *side*. A rotation about the root cannot change `|tip - root|`, so the second step cannot
disturb the first. Doing the aim first is the common bug where the tip walks off the target as the
knee closes.

It reports five outcomes by name (`IkStatus`): `Solved`, `Clamped` (out of reach -- extended towards
the target and short of it, which is an ordinary answer and not an error), `DegenerateBone`,
`DegenerateTarget`, and `DegenerateBend` (a straight chain with no pole: the plane the knee should
fold in is undetermined, and a guessed plane is a hock that bends sideways on a whole herd).

### 2. A third `PoseLayerKind`, inside the ADR-300 contract

`PoseLayerKind::Foot`. It writes `SkinnedRig::pose` and nothing else -- structurally, because
`scene/pose_layers.*` includes nothing from `entity/`. `apply()` remains a pure function of (layer
intent, skeleton, sample second), and for this kind the sample second is not an input at all.

**It reads a chain and not a mask, and says so when handed one.** A two-bone solve is not maskable
per joint, because half a knee does not reach half a target. An authored mask on a foot layer could
only be a silent no-op, and an unreported no-op is the failure `pose_layers` exists to stop
repeating, so `bind` reports it and the scene parser refuses it outright.

`bind` checks the topology: the named knee must be below the named hip and the named foot below the
knee. Joints *between* the named three keep their own locals and ride along, which is why a goat's
`AnkleB.L` is a non-problem.

### 3. Where the pure-function line is drawn against grounding's smoothing

Grounding is entity-side, stateful and smoothed. Pose layers are pure and unsmoothed. The line runs
through **`entity::LocomotionState::groundPoint` / `groundNormal`**:

* Above the line, `entity::GroundFollower` samples the terrain over the body's own footprint,
  filters it, runs the normal through the same `slopeSmoothingMs` one-pole that `pitch` and `roll`
  already use, and publishes a world-space plane. It is allowed to remember, because it is
  re-simulated on a seek and `reset()` exists for the case where it is not.
* `Composition::AnimationSink::driveLayers` converts that plane to entity-local, the same conversion
  and for the same reason as ADR-300's look-at.
* Below the line, the layer and the solver are a pure function of (the sampled pose, that plane).

The feet and the body therefore lean off the *same* filtered normal. Duplicating the filter one
level down would have been a second answer to the same question with its own lag, and the two would
have fought.

The normal is converted world-to-local by the **transpose of the basis**, not the inverse. They
agree only for a pure rotation, and Glowmere draws these bodies at 3.3x to 3.6x.

### 4. `footDrop`: the other half of the move, and why it is not optional

**Every farm rig binds with its leg 97.9% to 100.0% extended.** A bull's hind leg is at 98.5% -- four
millimetres of straightening in 0.90 m -- and its *front* leg is at 100.0%, knee angle 177.7 degrees.
Across the pack the walk cycle folds it a further 1% to 20% and no more.

Two consequences, and they are the shape of the whole feature:

* **A foot layer on this pack can raise a foot and essentially cannot lower one.** Which is how foot
  IK is supposed to work anywhere: the body drops to the lowest contact and the solver lifts every
  other foot to the surface. `GroundSettings::footDrop` is the first half -- it slides the body from
  the footprint mean down towards the lowest ground the footprint covers -- and it is **0 by
  default**, so no existing scene sits a millimetre lower than it does today. The split across two
  files is not an accident: a pose layer structurally cannot move a body (ADR-260), and
  `GroundFollower` structurally cannot pose one.
* **`extension` cannot default below 1.** The textbook value is something short of 1, so the knee
  keeps a hair of bend and the bend plane never vanishes. At 0.99 this solver reports a bull's front
  hoof as out of reach while it is standing exactly where the artist put it. What makes 1 safe here
  is that the solver is a pure function of the pose it is handed: the argument for keeping a bend
  assumes the next frame starts from this frame's answer, and it does not.

### 5. The defaults that had to be read out of the rig rather than named

**`soleUp`.** The obvious convention is a cardinal axis of the tip joint -- +Y. On this pack it is
wrong on all nine: these joints run along the *bone*, not along the ground. Not one of them has an
axis within 26 degrees of vertical and a bull's hind hoof is 42.8 degrees off, so a +Y convention
lays the sole at 43 degrees to the slope and calls it aligned. It now defaults to whatever direction
was up in the **rest pose**, resolved once by `bind`.

**`groundOffset`.** A hoof joint is not the sole of the hoof; it sits 0.120 model units above it on
the bull's rear leg and 0.097 on the front. The plant reads that height out of the rest pose and
preserves it, so the rule is "stand on this slope the way you stand on the flat" and there is nothing
to author. The field survives as an extra nudge and defaults to zero.

**The pole defaults to nothing.** The animation has already decided which way the knee goes and the
solver has no better opinion; a pole overrides that plane, and a wrong one is a leg folded through
the body. It is required only for a chain the clip leaves dead straight, which is the one case with
no plane to keep.

## What the render found that the numbers did not

The owner's standing instruction is that numerical agreement is not proof of visual alignment. Three
times over, here:

1. **The plane was inert.** `GroundResult::surfaceHeight` published the *dropped* body height rather
   than the surface, so the plane passed exactly through the body's own origin on every slope and
   every foot's correction came out as zero. The mechanism ran, reported `solved` four times a
   frame, and did nothing.
2. **The authored pole inverted both hind legs.** Four layers, four `solved`, four hooves on their
   targets to four decimal places, and a bull with its hind legs folded up through its own body.
3. **A bull cannot stand head-down a 19-degree bank.** It is 2.65 m long, the bank drops 0.9 m over
   that, and its hind legs are 1.1 m. Three of four hooves clamped, correctly, and it looked like a
   bug. Turned along the contour only the animal's 0.76 m width is on the slope, all four solve, and
   that is also what a real animal does on a hill.

`renders/footik/bull-slope-before.png` and `-after.png`, from
`examples/labs/footik/foot-ik-lab-off.scene.json` and `foot-ik-lab.scene.json`: the same frame of the
same bull on the same shipped Glowmere hillside, with the layers off and on. Before, the near hooves
are tipped onto their toes and driven into the surface and the far pair hangs; after, all four soles
lie on the slope and the uphill pair has stepped up onto the higher ground. The control scene sets
`footDrop` too, so the pair compares the layers and not the seating.

## Consequences

* Nine rigs gain a solvable leg. The alien pack does not, and is reported rather than approximated.
* `footDrop` and every foot layer are opt-in. The farm locomotion and Glowmere suites are unchanged.
* One new scene key family on an animation layer: `kind: "foot"`, `chain`, `poleDirection`,
  `footAlign`, `groundOffset`, `extension`, `soleUp`, and `drive: "ground"`.
* `LayerResolution` gains `NoChain`, `Clamped` and `Degenerate`; `PoseLayerStack::ikStatuses()` says
  *which* degeneracy.

## Rejected alternatives

* **ozz-animation's two-bone IK job** (`docs/research/assets.md` [A15]). It is the right library and
  this engine does not use it: ADR-086 hand-rolled the sampler, so adopting ozz for one job means
  importing its skeleton format and joint ordering for one job. The solve is forty lines.
* **Smoothing inside the layer.** It is the obvious home for it and it is the one place it may not
  live: a layer that remembered would break scrub == play, which is the thing ADR-300 spent a unit
  making structurally impossible.
* **Deriving the chain from a mask plus `descendants`.** It works on the farm pack and produces a
  confident wrong answer on the aliens, whose three leg-ish names are three branches.
* **Replacing `slopeAlign`.** Body lean and per-limb correction are complementary. The body leans
  part of the way into a hill; a hoof lies flat on it.
* **Making the alien rig solvable** by treating `thigh_stretch.l -> leg_stretch.l -> foot.l` as a
  chain. They are not one, the solver now says so by name, and a test asserts that it says so.

## Revisit triggers

* A rig arrives with a proper leg chain and non-uniform joint scale: the writeback conjugates a
  rotation by the parent and `Transform::fromMatrix` discards the shear that produces.
* The aliens need feet. That is a rig question -- an export that keeps the control chain, or a
  retarget -- and not a solver question.
* Anything needs more than two bones (a spine, a tail, a trunk). This solver is closed-form for
  exactly two and does not generalise; that is a new unit, not a parameter.
* **The plant drops along model-space -Y, and the body it belongs to is tilted.** `slopeAlign` leans
  the entity up to 0.55 x 34 degrees, so entity-local down is not world down and the foot slides
  along the plane by `tan(tilt) x drop` -- around three centimetres on a 0.2 m correction at a
  10-degree lean. The fix is the caller handing down world-down in entity-local as a second
  direction; the number did not justify the field. Revisit if a scene leans a body harder, or if a
  hoof is seen to drift across the terrain as the slope changes under a walk.
