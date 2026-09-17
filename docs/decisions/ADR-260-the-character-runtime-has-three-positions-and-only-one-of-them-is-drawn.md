# ADR-260: The character runtime has three positions, and only one of them is drawn

**Status:** Accepted
**Date:** 2026-09-17

## The request

Build a Character Animation Lab: a controlled environment for diagnosing character problems that
currently have to be guessed at inside Glowmere. Two were named directly — animals and aliens
clipping through the terrain floor, and characters posing incorrectly on slopes.

## What the investigation found first

The premise of the brief was that character bugs would turn out to be *animation* bugs. They are
not. The skeletal animation system (ADR-086) is in good order: clips, sampling, cross-fading and
skinning are deterministic, pure functions of the timeline, and the 168 clips this project can load
all evaluate correctly. Every defect found here is in the seam between the **simulation position**
and the **drawn position**.

A character has three positions and they are not interchangeable:

| position | what it is | who writes it | who reads it |
|---|---|---|---|
| `state().position()` | `anchor + travel`, the simulation | navigation, actions, grounding, the director | the crowd field, path validity, triggers |
| `visualPosition()` | that **plus `motion.position`**, the frame's behaviour offsets | every behaviour, via `MotionOffset` | staging, points of interest |
| the node's `position` parameter | what `applyOffsets` writes, and what the renderer draws | `EntityWorld::update` | the renderer |

The second and third agree. The first does not agree with either, and **grounding only ever writes
the first**. That single sentence explains both reported bugs.

## Defect 1: a grounded body drawn underground

`liveliness` adds a stride bob as `motion.position.y += sin(phase * 2) * bounce * strength`. Its own
header calls it a rise — "a body rises on each foot" — but `sin` is symmetric, so it sinks exactly as
far as it lifts. Grounding cannot correct for it: grounding writes `state.travel.y` and the bob
writes `motion.position.y`, two different layers that never meet. In Glowmere the behaviour order is
not even consistent between species (farm animals run `liveliness` before `ground`; the aliens run
`explore` before `liveliness`), so there is no ordering that would have saved it.

Measured on Glowmere's own numbers for `ember` and `vane` (bounce 0.32, stride 5.35), walking on real
terrain for 30 simulated seconds:

| arm | drawn body sank | drawn body rose | **simulation** sank |
|---|---:|---:|---:|
| control, bounce 0.00 | 0.0030 m | 0.0054 m | — |
| bounce 0.32 | **0.1819 m** | 0.1838 m | 0.0030 m |

The right-hand column is the whole lesson. A diagnostic that read `state().position()` — the obvious
thing to read — reported a perfectly grounded character throughout, because the simulation *was*
perfectly grounded. The character was 18 cm underground anyway.

**Fixed** in `Liveliness::update` by rebasing the bob from an oscillation about ground contact to a
rise from it: `(1 - cos(phase * 2))` in place of `sin(phase * 2)`. Same amplitude, same period, same
meaning for the authored `bounce`; what moves is the phase reference, so the trough of the bob is
ground contact — where a walking body's lowest point actually is — instead of the midpoint of a swing
with nothing holding up its bottom half. After: sank 0.0024 m, rose 0.3641 m.

`hover` keeps its symmetric noise deliberately. A craft oscillates about a hover height and has no
ground contact to be the floor of.

## Defect 2: slope posing, which has no fix inside the current representation

`GroundFollower` resolves a slope lean into the body's own frame — `wantPitch` from the terrain
normal's component along the heading, `wantRoll` from its component along the body's right — and the
caller adds them to `motion.rotation.x` and `.z`, Euler degrees on the node. The yaw shares that same
triple.

`scene::quatFromEulerDegrees` is `glm::quat(vec3)`, which composes **Rz(roll) · Ry(yaw) · Rx(pitch)**
— verified against `glm::eulerAngleZYX`, exact to float. ADR-240 depends on that order and writes
`eulerDegrees` as its exact inverse, so it is a scene-wide representation contract, not a stray call.

Two consequences:

1. **Pitch is body-frame, roll is world-frame.** Pitch sits inside the yaw and rotates with the body;
   roll sits outside it and acts in world axes. Half the lean is in the wrong space.
2. **Yaw is the middle angle, so it gimbal-locks at ±90°.** Working the composition through on the
   body's up-axis gives `up.z = sin(pitch) · cos(yaw)`. At yaw = ±90° that is identically zero: no
   value of pitch can tilt the body along z at all, and roll — the only channel left — acts in world
   axes. A body walking due east or due west is not an edge case; it is a quarter of all headings.

Measured on one fixed patch of 10° ground, so the terrain normal is the same in every row:

| yaw | body up-vector | lean |
|---:|---|---:|
| 0° | (−0.0376, +0.9954, **−0.0884**) | 5.51° |
| 37° | (−0.0326, +0.9967, −0.0743) | 4.65° |
| **90°** | (+0.0507, +0.9987, **+0.0000**) | **2.91°** |
| 180° | (+0.0376, +0.9954, −0.0884) | 5.51° |
| 214° | (−0.0711, +0.9944, −0.0780) | 6.06° |
| **270°** | (−0.1260, +0.9920, **+0.0000**) | **7.24°** |

The lean varies by a factor of 2.5 on identical ground, purely with heading, and the z component is
annihilated exactly at ±90°.

**Not fixed, deliberately.** The obvious repair — have grounding solve for the (pitch, roll) that
produces the wanted lean under the engine's actual ZYX composition — provably does not exist: the
equation `sin(pitch) · cos(yaw) = wanted_z` has no solution at yaw = ±90°. Anything else would be a
compensating offset of the kind §33 of the brief forbids, and flipping `quatFromEulerDegrees` to a
yaw-outermost order would change every authored rotation with two non-zero components in every scene
and every save, on a representation ADR-240 spent a day stabilising.

The real fix is architectural: a body's rotation should reach the node as a composition — authored
base, then yaw, then a body-frame tilt — rather than as three Euler components a downstream layer
composes in an order chosen for a different purpose. That is a change to how `MotionOffset` and the
node rotation parameter relate, and it wants its own decision rather than being smuggled in here.

The invariant is kept as a test tagged `[!shouldfail]`, so it is measured on every run and Catch2
shouts the day it starts passing.

## Root motion: ADR-161 is right about the locomotion clips and wrong in general

ADR-161 concluded "there is no root motion in this content to extract" from three clips of one Mixamo
file, and invited a re-check if a new asset ever arrived. Two did — the modular alien pack the
following day (26 clips, Auto-Rig Pro, 89 deform bones) and the farm pack after it — and the check was
never re-run on them.

Run now across all 16 animated assets and all 168 clips, five clips per alien genuinely translate
their root:

| clip | net XZ | net ΔY |
|---|---:|---:|
| `Dying_forward` | 0.985 m | −0.674 m |
| `Dying_1_backpack` | 0.875 m | −0.464 m |
| `Dying_1_no_backpack` | 0.868 m | −0.629 m |
| `Crazy` | 0.071 m | +0.069 m |
| **`Landing`** | 0.022 m | **−0.567 m** |

The locomotion clips — `Walking`, `Running`, `Idle`, `Idle_turn`, `Jumping`, `Fall_loop` — are
in-place, so the premise the code-driven locomotion path rests on holds where it matters and root
motion stays unimplemented. But `Landing` is played by Glowmere, and it carries over half a metre of
vertical root translation that the engine ignores, at exactly the moment a jump resolves.

This is recorded as an inventory rather than a fix. Extracting root motion is a feature, and which
clips should own their own displacement is an art decision before it is an engineering one.

## The grounding surface is not the surface anybody sees

A character is grounded against `WorldMap::height()`, an analytic function with no resolution. It is
drawn standing on `buildChunkMesh`, a piecewise-linear approximation whose vertex spacing depends on
how far the camera is. Nothing had ever compared them. Mean absolute deviation over one chunk:

| LOD | vertex spacing | mean \|deviation\| | worst drawn-above-feet | worst drawn-below-feet |
|---|---|---:|---:|---:|
| 0 | 1.25 m | 0.0119 m | 0.051 m | 0.144 m |
| 1 | 2.5 m | 0.0458 m | | |
| 2 | 5 m | 0.1670 m | | |
| 3 | 10 m | 0.5898 m | 1.637 m | 2.022 m |

The sign is the symptom: a chord across a hollow lies above the ground (feet under the drawn floor —
clipping), a chord across a ridge lies below it (floating). At the distances a posed character is
actually visible the terrain is LOD 0–1, so this is a centimetres-scale contributor rather than the
main event — worth knowing, and worth not "fixing" by grounding against the drawn mesh, which would
make a character's height depend on where the camera is and break ADR-091 outright.

## The lab

`examples/lab/character-animation-lab.json`. Deliberately boring: neutral ground, studio light, no
ecology, no atmospherics. Zones for animation playback, root motion (an in-place clip beside
`Dying_forward` and `Landing`, each on an origin pip, so the displacement the engine does not extract
is a thing you can look at), four yaws, three scales, and the farm pack for telling an asset bug from
a systemic one.

The diagnostic weight is in the tests, not the picture: `tests/unit/test_character_lab_{grounding,
bob,slopes,inventory}.cpp`.

## What this cost to learn

Three control arms failed before any of the measurements above were worth anything, and each failure
was the probe rather than the engine:

* The bob harness registered its node at the origin while binding the anchor elsewhere. `applyOffsets`
  *adds* to whatever final the parameter carries, so the test was reading `travel` — a displacement —
  and sampling terrain under it. The control arm reported 4.19 m of penetration with the bob switched
  off. The authored node position **is** the base.
* The root-motion inventory took `joints[0]` as the root. It is an armature wrapper no clip animates,
  so all 168 clips reported a root that never moved, for the most boring possible reason. The root has
  to be found per clip, as the lowest-indexed joint the clip actually translates.
* The first slope probe read a single `GroundFollower::update`. Its tilt is smoothed over 240 ms, so
  that reports the start of a ramp rather than the steady state.

ADR-182 is the reason any of those were caught. A probe that cannot fail proves nothing, and all three
of these would have "passed" into the report as findings.
