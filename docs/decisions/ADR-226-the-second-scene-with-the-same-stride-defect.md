# ADR-226: The second scene with the same stride defect, and the bands nobody scaled

**Status:** Accepted
**Date:** 2026-09-15

ADR-204 §3 fixed four characters in Glowmere Valley 2 and named the mechanism: **an authored stride
speed is a claim about a clip, and `Gait::footSlip` cannot check it** — with rate matching on and
unsaturated it returns `speed / (authored * (speed / authored))` = 1.0 whatever the clip contains,
because the authored number is both the expected value and the thing under test.

The Glowmere Stylized wanderer had the same defect, worse, and for a different reason: its gait
block never set `walkSpeed` or `runSpeed` **at all**.

## What it was running on

`examples/world/glowmere-stylized.scene.json` places `assets/imported/alien.gltf` — a Mixamo-rigged
alien 121 model units tall — at node scale 0.08, so a body about nine metres tall. Its entity gait
block carried `matchRate`, `rateMin` 0.55 and `rateMax` 1.9 and nothing else, so it ran against
`GaitSettings`' defaults: a **1.6 m/s** walk clip, a **4.0 m/s** run clip, `runEnter` 3.0 and
`moveExit` 0.05. The explore behaviour cruises at 5.0 m/s and dashes at 11.0.

## The measurement

The same estimator ADR-204 used, now shared as `tests/support/stride_speed.hpp` because a second
rig names its feet differently (`mixamorig:LeftToeBase`, not `toes_01.l`): the median backward speed
of a toe along the clip's forward axis while that toe is in the bottom fifth of its own height
range.

The expected values were produced by a pure-stdlib Python script that walks the glTF node hierarchy
by hand and shares no line with this engine — the loader, the skeleton and the sampler the C++ reads
them through are all independent of it. Stable across contact thresholds from 10% to 30%:

| clip | first key | last key | stride speed | × 0.08 |
|---|---:|---:|---:|---:|
| Walk | 0.0000 | 1.1000 | **53.12** units/s | **4.25 m/s** |
| Run | 0.0000 | 0.9000 | **114.27** units/s | **9.14 m/s** |
| Idle | 0.0000 | 3.6333 | 0.41 (no stride) | — |

Note the first key: this pack begins at zero, so ADR-204 §1's held pose does not apply here. That is
a property of the exporter, not of the engine, and it is worth having measured rather than assumed.

Against what the scene claimed: `walkSpeed` **1.6 against 4.25** (−62%) and `runSpeed` **4.0 against
9.14** (−56%).

## The half ADR-204 named and this scene needed more

ADR-198 already recorded that `runEnter` 3.0 and `runExit` 2.2 are **person-scale**, and that "a
six-metre creature is running at a speed a person would never reach — left alone, all four would
have sprinted from the moment they moved." The four Valley 2 aliens got bands derived from their own
cruise and dash. The stylized wanderer never did.

Measured over two simulated minutes of the shipped scene, with the camera pinned to the body so
nothing is culled or coarsely updated: **108 walk frames against 5,633 run frames**. The walk clip
the scene names was effectively never played. Its cruise speed of 5.0 m/s is above a `runEnter` of
3.0, so it entered Run before it had finished accelerating and stayed there.

That is not a taste, and the check says so without a tolerance: **the speed the behaviour cruises at
has to be a walk**, or the walk clip is decoration.

## The decision

Both stride speeds from the clip, and the bands from the behaviour, using the ratios the four Valley
2 aliens already carry (recovered from their shipped values: `moveEnter` 0.30 × cruise, `moveExit`
0.15 × cruise, `runEnter` the midpoint of cruise and dash, `runExit` 1.15 × cruise, `accel` 1.6 ×
cruise, `decel` 2.2 × cruise). The clamp is then derived from the bands rather than chosen:

```
rateMin <= min(moveExit / walkSpeed, runExit / runSpeed) = min(0.75/4.25, 5.75/9.14) = 0.176
rateMax >= max(runEnter / walkSpeed, dash    / runSpeed) = max(8.00/4.25, 11.0/9.14) = 1.882
```

| | was | is |
|---|---:|---:|
| `walkSpeed` | 1.60 (default) | **4.25** |
| `runSpeed` | 4.00 (default) | **9.14** |
| `rateMin` | 0.55 | **0.17** |
| `rateMax` | 1.90 | **1.89** |
| `moveEnter` | 0.15 (default) | **1.50** |
| `moveExit` | 0.05 (default) | **0.75** |
| `runEnter` | 3.00 (default) | **8.00** |
| `runExit` | 2.20 (default) | **5.75** |
| `accel` | 6.00 (default) | **8.00** |
| `decel` | 8.00 (default) | **11.00** |

## Evidence

Two simulated minutes of the shipped scene, same seed, same 60 Hz step, camera pinned to the body:

| | before | after |
|---|---:|---:|
| ground covered | 479.6 m | 479.6 m |
| walk frames | **108** | **5,533** |
| run frames | 5,633 | 165 |
| foot slip outside 1.1× | **176 / 5,741** (3.07%) | **10 / 5,698** (0.18%) |
| worst slip | **6.27×** | **1.33×** |

And on the shipped project rather than the scene, `--headless --project
examples/world/glowmere-stylized.json --frames 3600`: the runtime warning

> entity 'wanderer': travelling at 0.57 m/s against a walk clip authored for 1.60 m/s (0.6x foot
> slip); rate matching is on and saturated

fired at 120 simulated seconds before and does not fire after.

`tests/unit/test_stylized_wanderer.cpp`, five cases. Before the fix three of them failed:

* *the stylized wanderer's authored stride speeds describe the clips it names* — the scene states
  neither number
* *rate matching covers the wanderer's whole travelling range* — 0.057 at the bottom of the walk
  band, 1.447 at the top of the run
* *the wanderer's feet keep step with the ground it crosses* — 108 walk frames of 5,741

The instrument itself is checked first and passed before and after: the C++ estimator recovers
53.12 and 114.27 from the same asset the Python did, to within 2%, and returns no stride at all for
the idle. Without that every number above would be this engine agreeing with itself.

## What this does not fix

The 10 remaining out-of-band frames are ADR-204's own "what this does not fix": `minDwell` holds a
gait for a quarter of a second after the body's speed has left that gait's band, so a body
accelerating at 8 m/s² through `runEnter` spends up to 0.25 s labelled Walk at up to 10 m/s against
a 4.25 m/s clip. Bounded, at 0.18% of travelling frames and a worst of 1.33× — under the 1.5× the
engine itself warns at, which is what the test asserts. Removing it means either a gait that
flickers or a minimum dwell that knows about clips.

`Gait::footSlip` still cannot see a `walkSpeed` that does not describe its clip; that is ADR-204's
standing conclusion and the reason the check lives in a test, where the foot bones can be named.
What is new here is that the test is now reusable: the estimator takes its toe joints as an
argument, so the third scene to acquire an animated character costs two literals and a `TEST_CASE`
rather than a rediscovery.
