# The Tractor Beam Lab

A flat plane, one saucer, the production tractor beam and the production abduction scenario, with a
cast laid out so that each thing that could be wrong about the alignment has its own row. Built
because "the animal does not line up with the beam" had been answered three times from Glowmere
Valley 2 — a 700k-triangle world with a cutting director, a river, sixteen animals on a hillside and
a camera usually looking somewhere else — and every one of those is a reason a measurement can be
right while the picture stays wrong.

See ADR-262 for what it found.

## Running it

```sh
tools/make_tractor_beam_lab.py                      # examples/world/tractor-beam-lab.scene.json
tools/make_tractor_beam_lab.py --legacy \
    examples/world/tractor-beam-lab-legacy.scene.json   # the control: the pre-ADR-262 scenario
tools/make_tractor_beam_lab.py --case D-yaw90 \
    examples/world/_beamlab-yaw90.scene.json        # one case, camera framed on it, for a picture
```

The numbers, hop by hop, with no GPU:

```sh
build/release/tests/avgen_tests '[.probe][lab]'                       # the fixed lab
AVGEN_BEAM_LAB_SCENE=$PWD/examples/world/tractor-beam-lab-legacy.scene.json \
  build/release/tests/avgen_tests '[.probe][lab]'                     # the control
```

`AVGEN_BEAM_LAB_SCENE` points the probe at any scene carrying the abduction, including the shipped
Glowmere ones — which is how the production before/after below was taken with the same measurement
code as the lab's.

The regression arms (these run in the default suite, ~41 s):

```sh
build/release/tests/avgen_tests '[lab]~[.probe]'
```

A picture. **Every GPU invocation goes through `tools/gpu-lock.sh` (ADR-170).**

```sh
tools/gpu-lock.sh build/release/src/avgen \
  --composition examples/world/_beamlab-yaw90.scene.json \
  --render /tmp/after --range 0:8 --fps 8 --debug-draw beams,entityOrigins,entityBounds
```

`--debug-draw` is new with ADR-262 and is the other half of the lab. The overlays existed -- the
World panel's Debug tab has had entity origins and bounds for a long time -- but they lived behind
ImGui checkboxes, and `RenderJob` never built any debug geometry at all, so they were unreachable to
`--render`, to `--headless`, and therefore to anybody diagnosing from a rendered frame. `beams`
draws each emitter's disc, the axis its column fires along and **a second ring where the column
ends**; put next to `entityOrigins` and `entityBounds`, one frame answers what a still otherwise
cannot -- where the code thinks the animal is (the origin's axis cross), where the animal actually is
(the bounds box), where the column's axis runs, and whether the column reaches the ground at all.

## The cast, and what each row is for

| row | what it is | what it separates |
|---|---|---|
| A | one goat under the origin | the reference case |
| B | goats at world x = -10, -5, 0, +5, +10 | a fixed error from one that scales with world position |
| C | the same on z | the other horizontal axis |
| D | four identical cows at yaw 0/90/180/270 | **entity origin from drawn body.** A constant error is the same for all four; an asset's model-space offset is not, because it turns with the body |
| E | one of every farm species | an asset-specific model transform from a global bias |
| F | every row above has no locomotion behaviour | the base transform relationship |
| G | two animals with `wander`, `liveliness` and a walk clip | the transform from the pose |

The full production sequence is not a row: it is what the lab *does*. `acquire -> approach -> beam
-> abduct -> depart` runs on each of them in turn, from the same `STAGING` description Glowmere's own
scene is generated from (`tools/make_abduction_scenario.py`, imported rather than retyped), with the
saucer node and the beam node copied out of `glowmere-valley-2.scene.json` at generation time. The
lab overrides **durations only**.

## The coordinate-space audit

Every hop between "the director's idea of the craft" and "the box the renderer draws the animal in",
each attributable to exactly one piece of code. The probe prints one column per hop.

| quantity | read from | space | owner |
|---|---|---|---|
| craft, simulated | `Entity::state().position()` | world | navigation + the director tier |
| craft, drawn | `NodeRange::world` / `nodeWorldTransform` | world | parameters, after `applyOffsets` |
| craft, "visual" | `Entity::visualPosition()` | world | state + the behaviours' `MotionOffset` |
| beam emitter | `ParticleSystem::position` | world | node-local `(0, -2.05, 0)` through the craft node's world matrix |
| beam axis | a **vertical** line through it | world | `direction` is *not* transformed by `applyParameters`, so the column is world-down however the craft banks. Load-bearing; see ADR-262 §4 |
| beam reach | `speed x lifetime`, integrated with the system's gravity and drag | metres | the emitter |
| animal, drawn | `Composition::nodeCorners` / `visualPlacement` | world | the node's world matrix x the glTF node chain x the mesh box |

Two of those are not where anybody had been looking, and both are in ADR-262: the emitter is not the
craft's origin (up to 0.55 m, from the saucer's tilt), and the mesh box is not the node's origin
(up to 1.24 m on a Glowmere bull, turning with its facing at 230 deg/s).

## Before / after

Same build, same measurement code; the arms differ in the scenario configuration and in nothing
else. `before` is `tractor-beam-lab-legacy.scene.json`.

Body-centre offset from the column's axis, over the settled part of each lift:

| | before | after |
|---|---|---|
| worst, whole 23-case cast | **1.199 m** | **0.105 m** |
| worst at the top of the lift | 1.199 m | **0.065 m** |
| four identical cows, spread between them | 0.178 m | 0.040 m |
| lifts whose column stopped short of the animal | 23 of 23 | 0 of 23 |
| lifts putting the body above the emitter disc | 9 of 23 | 0 of 23 |
| worst **origin** offset on the same frames (what the old probe measured) | 0.411 m | — |

And the shipped film, `glowmere-valley-2.scene.json`, ten abductions over 190 s, measured the same
way — body centre against the column's axis at the top of each lift:

| | goat | chicken | goat | pig | sheep | cow | horse | sheep | bull | rooster |
|---|---|---|---|---|---|---|---|---|---|---|
| before | 0.471 | 0.219 | 0.166 | 0.300 | 0.774 | **0.996** | 0.766 | 0.553 | 0.774 | 0.310 |
| after | 0.010 | 0.012 | 0.021 | 0.011 | 0.036 | **0.058** | 0.037 | 0.035 | 0.049 | 0.010 |

(That row is `at the top` — the final frame the director drives the lift. The per-lift worst over the
settled part of the rise goes from 0.996 m to 0.086 m over the same ten.)

On those same frames, before the fix, the node **origin** — the quantity every previous diagnostic
reported — was 0.106 to 0.293 m from the axis. The instrument read 0.11 m while the picture was
1.00 m out. That is the whole of "the test passed and the render did not change".

## The residual is understood, not merely small

The director decides before the entity pass writes the finals the next flattening reads, so the
body-centre correction is one frame stale **in rotation**, and the lift spins the animal at
230 deg/s. One frame of that at 60 Hz is 3.8 degrees of a 0.89 m radius: **0.059 m**. Measured
worst-at-the-top across the four rotation cases: 0.058 to 0.065 m.

The probe measures only while `Entity::directorMotion().active` -- while the director is actually
driving the body. The `abduct` beat outlives its own `lift` step, and on the tail frames
`clearDirectorMotion` drops up to 330 degrees of accumulated spin, so the body snaps back to its
authored facing while its origin stays where the correction put it. Nothing is on screen to see it
(`retire` hides the body in the same instant), but a probe that included those frames read 0.53 m
instead of 0.065 -- which is a measurement window, reported here because leaving it unstated is how
a number becomes misleading.
