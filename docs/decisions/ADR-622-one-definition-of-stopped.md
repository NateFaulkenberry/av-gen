# ADR-622: One definition of "stopped" — two detectors, a rate floor and a threshold that had never been introduced

**Status:** Accepted — **with one decision open for the owner** (the `rateMin` look, below)
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

## The open decision — the `rateMin` value, which is a look and belongs to the owner

**The value is left at 0.005.** It is below `kVisibleClipRate`, which is now visible in the code
rather than hidden across two files, and the question of whether to move it is **a look decision**:

**0.005 is not visually identical to 0.** Over a ten-second hold at 60 fps it advances the clip by
0.05 s — three frames of clip time. That is a slow crawl, not a still.

| option | what a slowly-moving farm animal looks like | cost |
|---|---|---|
| **A — keep 0.005** (current) | legs creep at 0.5% of walk speed: reads as nearly held, a statue drifting | at the lowest travel speeds the body slides further than its legs move — mild foot skate *under* the cycle |
| **B — raise to `kVisibleClipRate` (0.02)** | legs visibly shuffle at the slowest crawl: reads as a body moving on its own feet | at the lowest travel speeds the cycle may *outpace* the travel — mild skate *over* the cycle, and 4× more leg motion than now |

Both are defensible and they fail in opposite directions. Neither removes the underlying gap — that
takes an idle clip. **Not decided here.**

## Consequences

- **Any code that asks "is this clip advancing enough to see" reads `kVisibleClipRate`.** A second
  literal for the same idea is the defect this ADR removes.
- **Any detector that asks "is this body stuck" should read `withinAuthoredRamp`** once it can see
  a body with an authored gait, or it will count acceleration as failure.
- **What was not done:** `rateMin` is authored per scene, so it cannot literally *read* the
  constant — it can only be compared with it. The value is in the owner's hands; the comparison is
  now in the code where the next reader will see it.
