# ADR-622: One definition of "stopped" — two detectors, a rate floor and a threshold that had never been introduced

**Status:** Accepted. The `rateMin` look decision is **decided by the owner, 2026-09-21: option B**
**Date:** 2026-09-21
**Related:** ADR-620 (the authored ramp), ADR-621 (the emptier world), ADR-619 (`turnRate`)
**Implemented by:** `tests/support/ramp.hpp` (`withinAuthoredRamp`), `entity::kVisibleClipRate`
**Tests:** `test_abduction_poc` (stall), `test_farm_locomotion` (frozen while moving)

---

## The defect was a vocabulary, not a number

After ADR-620 made authored acceleration real, two detectors went red:

- `test_abduction_poc`'s **`stalledPercent`** — "commanded to move and did not move" — read 4.0%;
- `test_farm_locomotion`'s **`frozenWhileMoving`** — "the clip is frozen while the body covers
  ground" — read 1–6 frames per animal.

Both were written for a world where `state.speed` was assigned outright, so a body was either at
full speed or stopped and those two things could never disagree. With a ramp a body spends real
frames **between** them, and both detectors counted those frames as faults.

And behind them sat a third pair that had never been introduced: the farm pack authors
**`rateMin: 0.005`** — the floor a rate-matched clip is clamped to — while the frozen detector's
own literal for "frozen" was **0.02**. A body could be officially playing its clip and officially
frozen at once, and neither number knew the other existed.

**Three parties with private definitions of one word.** The fix is to give them one.

## Decision 1 — a body inside its authored ramp is accelerating, and accelerating is not stalled

`testsupport::withinAuthoredRamp(gait, previousSpeed, speed, dt)`, read by **both** detectors from
one place.

**It is a definition, not a tolerance.** `Gait::approach` moves the speed by *exactly* `accel·dt`
while climbing and `decel·dt` while falling, and by less once it has arrived. So "is this body on
its own ramp" is a question with an exact answer rather than a threshold someone picked. A body
that is genuinely stuck is not changing speed at its authored rate, and still counts. Nothing was
loosened; the words were made to mean what the engine now does.

**Result: both detectors to zero. No residue.**

| detector | before | after |
|---|---|---|
| `stalledPercent` | 4.0% (2,628 frames), longest 0.43 s | **0** |
| `frozenWhileMoving` (farm, per animal) | 1, 1, 1, 2, 2, 4, 6 | **0** |

Returns `false` when nothing was authored, so a body with inherited defaults keeps the old, strict
reading — correct, because that body still has no ramp.

## Decision 2 — `kVisibleClipRate`, one number for "advancing enough to see"

`entity::kVisibleClipRate = 0.02`, in `gait.hpp`, beside `rateMin`. The frozen detector now reads it
instead of its own literal, so the two can no longer drift apart.

**Derived from what a viewer can see**: at 60 fps a rate of 0.02 advances a clip by 1.2 frames of
clip time per second — the slowest advance that still reads as motion rather than as a held pose.

**The compensation is now labelled where it lives.** `rateMin: 0.005` and `idleRate: 0` exist
because the nine farm assets ship **one clip, called `Walk`, and no idle**. The floor is not a
playback preference; it is standing in for a missing animation. The real gap is the idle clip,
which is content work. It is written down at `GaitSettings::idleRate` so the number is not later
"corrected" by someone who does not know what it is covering for.

## Decided — the owner chose option B, 2026-09-21

> **"farm animal: Raise it to 0.02."**

**The farm animals' floor is now `kVisibleClipRate` (0.02)**, so the legs visibly shuffle at the
slowest crawl instead of creeping at 0.5% of walk speed.

**Scope.** It was applied by rule, not by value. Every **farm-animal** floor below 0.02 in a
**live** scene went to 0.02. A body counts as farm when its node loads an asset under
`assets/farm/`, and that was checked per entity.

| | floors | action |
|---|---|---|
| `glowmere-valley-2`, `-song`, `-multicam`, `glowmere-atmospherics`, `tractor-beam-lab` | **80** | raised to 0.02 |
| `tractor-beam-lab-legacy` | 23 | **left**: it is `test_beam_lab`'s "before" arm (ADR-262) |
| `_pre-defects` | 16 | **left**: it reproduces a historical render (`_pre-defects.mov`), and nothing tests it |
| aliens (0.08), `alien.gltf` (0.17) | 26 | left: not farm, and already above the threshold |

The diff is exactly 80 lines out and 80 in, and every one is a `rateMin` line. There were 8
distinct authored values, from 0.005 to 0.0168, plus float-noise versions of each.
`glowmere-stylized` was named in the brief but carries no farm floor, since it has no entities.

**The decision is now guarded in code.** "Every farm animal's rate floor is visible, and a save keeps
it" requires every farm floor in a live scene to be at least `kVisibleClipRate`, to be rate-matched,
and to survive `entitiesToJson`. That last check matters because `rateMin` is written **only when
`matchRate` is on**, so a farm body without rate matching would have its new floor dropped on save,
which is ADR-618's shape. The test covers exactly 80 bodies. Putting one scene's old floors back
fails exactly 16 assertions, one per farm body in that scene.

### What B costs, measured

On frames where the playback rate sits at the floor, over-travel is how much further the legs'
stride carried than the body actually moved on screen, in millimetres per frame. It is reported as
an absolute value because at a crawl the ratio's denominator goes to zero (ADR-609):

| | floor-pinned frames | legs outrun the body | p50 | p90 | p99 | worst |
|---|---|---|---|---|---|---|
| **before** (0.005–0.0168) | 119 | 107 (89.9%) | +0.268 | +0.271 | +0.271 | +0.271 (cow-3) |
| **after** (0.02) | 235 | 190 (80.9%) | **+0.367** | **+1.053** | **+1.079** | **+1.079** (bull-1) |

Read plainly:

- **The legs already outran the body before the change**, on 90% of floor-pinned frames. Option B
  did not introduce skate over the cycle. It made it larger.
- **The predicted ~4× shows up in the tail and not in the median.** The worst case goes from 0.27
  to 1.08 mm per frame (4.0×) and p90 goes up 3.9×, while p50 only goes up 37%.
- **The worst case is about 1 mm per frame, roughly 6 cm/s of leg travel the body doesn't make.**
  That is the size of the shuffle the owner chose.
- **Twice as many frames now sit at the floor (119 → 235)**, simply because a higher floor catches
  more of the slow speeds.
- `frozenWhileMoving` and `stalledPercent` stay at **zero**.

**Measurement caveat.** Actual displacement is the drawn node delta, so it includes the
crowd-separation push. That is what a viewer sees, and it slightly understates over-travel on frames
where the herd is jostling.

## The decision as it was put — kept for the record

The rest of this section is the options as they were put to the owner, before the ruling above.


**The value is left at 0.005.** It is below `kVisibleClipRate`, which is now visible in the code
rather than hidden across two files, and the question of whether to move it is **a look decision**:

**0.005 is not visually identical to 0.** Over a ten-second hold at 60 fps it advances the clip by
0.05 s — three frames of clip time. That is a slow crawl, not a still.

| option | what a slowly-moving farm animal looks like | cost |
|---|---|---|
| **A — keep 0.005** (current) | legs creep at 0.5% of walk speed: reads as nearly held, a statue drifting | at the lowest travel speeds the body slides further than its legs move — mild foot skate *under* the cycle |
| **B — raise to `kVisibleClipRate` (0.02)** | legs visibly shuffle at the slowest crawl: reads as a body moving on its own feet | at the lowest travel speeds the cycle may *outpace* the travel — mild skate *over* the cycle, and 4× more leg motion than now |

Both are defensible and they fail in opposite directions. Neither removes the underlying gap — that
takes an idle clip. **Decided: B** (above).

## Consequences

- **Any code that asks "is this clip advancing enough to see" reads `kVisibleClipRate`.** A second
  literal for the same idea is the defect this ADR removes.
- **Any detector that asks "is this body stuck" should read `withinAuthoredRamp`** once it can see
  a body with an authored gait, or it will count acceleration as failure.
- **What was not done:** `rateMin` is authored per scene, so it cannot literally *read* the
  constant — it can only be compared with it. The value is in the owner's hands; the comparison is
  now in the code where the next reader will see it.
