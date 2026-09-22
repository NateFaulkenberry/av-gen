# ADR-602: A layer arrives over a stated duration, and a parameter that snaps at a threshold should have been a weight

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-554 (a seam published from two places must be written by both), ADR-557 (a foot
lock anchor is derived, not remembered), ADR-086 (rate-limited rigs sample on a fixed grid),
ADR-600 (a gate with nothing configured refuses nothing), ADR-360 (determinism under scrub)
**Implemented by:** `src/scene/pose_layers.hpp` (`PoseLayer::effectiveWeight`),
`src/scene/pose_layers.cpp`, `src/entity/locomotion.hpp`, `src/entity/entity.cpp`
(`Entity::publishLookSchedule`), `src/scene/composition.cpp` (`driveLayers`)
**Tests:** `tests/unit/test_layer_blend.cpp`, `tests/unit/test_vertical_slice.cpp`

---

## Context

Phase B §46 asks for one continuous run — idle, accelerate, turn, curve, slope, look, slow, stop,
reach, return — and says it "should look like a continuous character motion system, not a
collection of disconnected demos". Every stage before it proved a mechanism alone, which is the
right way to prove a mechanism and a poor way to find out whether they compose.

Continuity is measurable: across 870 frames, no joint may move further in one frame than a body at
this speed accounts for. The first run of the slice breached it by an order of magnitude, and
**every one of the four causes was a discontinuity nobody had to author**. In the order they were
found, each number measured after the previous fix:

| worst per-frame joint step | joint | cause |
|---|---|---|
| 0.803 m | `hand.l` chain | the reach layer switching on at full weight in one frame |
| 0.301 m | `leg_twist.l` | `strideRatio` stepping 0.3 → 1.0 when speed crossed a threshold |
| 0.231 m | `toes_01.r` | the *script's* terrain stepping 0.23 m between beats |
| 0.150 m | `leg_stretch.r` | the stride layer's weight finishing its sweep in four frames |
| 0.098 m | `c_ring3.l` | a 0.8 m reach blended over 0.25 s, i.e. a hand at 3.2 m/s |
| **0.072 m** | `leg_stretch.r` | the walk clip's own loop seam — the floor |

The first was identical with the motion controller bypassed, which is how it was clear the
controller was not the thing at fault. A control arm that reproduces the defect is worth more than
a diagnosis.

## Decision

**Two rules, and the second is the transferable one.**

### 1. A layer arrives over a stated duration, and the duration is derived from an elapsed time

`PoseLayer` gains `blendSeconds`, `weightBefore` and `blendElapsed`; `effectiveWeight()`
smoothsteps between them, and every use of the weight inside `apply` — including the body
compensation pre-pass, which would otherwise disagree with the apply loop about whether a foot is
on — reads that one value.

Three things about its shape are load-bearing:

- **Derived, not accumulated.** ADR-557's reason exactly: the pose tier poses the rig once on a
  scrub, at frame N, with no frame N-1 to have ramped from. A remembered ramp would make a seek
  differ from a play, which ADR-360 forbids. Seek into the middle of a blend and you get the middle
  of the blend.
- **Elapsed seconds, not an absolute time.** The stack is applied on the rig's sample clock
  (ADR-086's fixed grid) while a driver knows the entity's. An absolute second from one clock
  compared against `now` from the other is a bug that shows only on a rate-limited rig. An elapsed
  duration is computed where both its terms live, so there is no clock to get wrong.

  The first implementation had the absolute form. **Nothing failed, and that is the part worth
  recording**: no rig in this repository currently rate-limits *and* carries a look layer, so the
  two clocks happen to be the same clock everywhere the code runs today. A correctness argument
  that holds only because the case does not exist yet is not a correctness argument — it is a
  coincidence with a deadline, and the deadline is the first scene that sets `updateHz` on a
  character. Passing tests said nothing about it either way.
- **Smoothstep, not lerp.** A lerp arrives at full rate and stops dead. No position jumps and the
  derivative does, which reads as a tick.

`blendSeconds` at 0 is the old behaviour to the bit, so the field is safe to add to a struct every
layer already uses — but a default nobody sets is ADR-600, so `driveLayers` sets it on the shipping
`Look` drive, and the seam carries the schedule. `LocomotionState` gains `lookTargetSince` and
`lookTargetBefore`, published by `Entity::publishLookSchedule`, which **both** `EntityWorld::update`
and `EntityWorld::seek` call — one function rather than two copies of three lines, because this is
the struct ADR-554 was discovered on and a schedule written by one of two publishers is that defect
with a longer name.

The division of labour is the clean statement: **the entity tier may remember, because a seek
replays every step and anything accumulated there is reconstructable at frame N by construction.
The pose tier may not, and so it is handed a time instead of a ramp.**

### 2. A parameter that snaps at a threshold should have been a weight

**Stated generally, because it is not about this stack:** *a parameter that snaps at a threshold
should have been a weight, because a neutral value is not neutral when the thing reading it scales
by it.* Anywhere a function returns an identity value to mean "I do not apply here" — 1.0 for a
multiplier, 0.0 for an offset, a rest pose for a pose — and a consumer applies that value rather
than skipping, the boundary is a discontinuity in the output that no one wrote and no one can see
in the source. The two questions being conflated are always the same pair: *what is the value* and
*does this apply at all*. The second one is a weight.

The second-largest discontinuity was `strideRatio`, and its shape is `Gait::footSlip`'s:

```cpp
if (activity != Walk && activity != Run) {
    return 1.0f;    // "standing, turning, observing: no stride to be out of step with"
}
return speed / (authored * rate);
```

The comment is correct about the meaning and the code is wrong about the transition. At the
walk-to-stand boundary the ratio steps from 0.3 to 1.0 and the stride layer triples a foot's
excursion between two frames.

The bug is a conflation. *How far out of step is this clip* and *should this layer do anything at
all* are two questions, and the threshold answers the second by returning a neutral value for the
first. Neutral values are not neutral when the thing that reads them scales by them.

So: the ratio stays continuous all the way to zero speed, and the **weight** falls off instead. A
weight of zero scales nothing, which is what returning 1.0 was trying to say by jumping. Better
still, the weight *is* the clamped ratio — one coefficient rather than two. The first attempt used
a separate 0.4 m/s window and left 0.150 m, because at 6 m/s² the weight finished its sweep while
the ratio was at 0.28: the layer arrived at full authority before it had anything to say.

## Consequences

- The slice asserts 0.08 m per frame across 870 frames and separately on the ten beat changes, with
  a control arm that runs the identical script with the controller bypassed and must breach it
  (measured 0.108 m). The bound is above the 0.072 m floor set by the walk clip's own loop seam and
  below what any of the six causes above produced.
- Three of the six causes were **in the driver, not the stack** — and one was in the test's own
  script, a terrain that teleported 0.23 m under a standing foot. That one is worth keeping
  separate: a continuity bound is only meaningful over inputs a world could actually present, and
  the discipline is to fix the script rather than widen the bound.
- `Gait::footSlip` is **not** changed here. Its callers include the farm pack, and the product masks
  the snap by changing clip at the same moment, so the fix is a change to what drivers ask for
  rather than to what the function returns. The slice demonstrates the correct driver shape; making
  every driver use it is §48's and §50's work, with the farm pack in front of it.
- The look blend is 0.25 s and the reach 0.45 s, and neither is tuned to the bound. A glance is
  fast; a hand with 0.803 m to travel is not, and at 0.25 s it moves at 3.2 m/s.
