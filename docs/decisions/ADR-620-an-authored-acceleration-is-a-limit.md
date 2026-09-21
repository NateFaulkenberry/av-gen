# ADR-620: An authored acceleration is a limit, not decoration — applied only where it was authored

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-360 (a scrubbed frame reproduces a played one), ADR-618 (a key read and never
written), ADR-619 (`turnRate`), ADR-621, ADR-622, `docs/testing.md` #31 and #32
**Implemented by:** `limitSpeedToGait` and the gait history requirement in `src/entity/entity.cpp`,
`GaitSettings::accelAuthored`, the `accel`/`decel` parse and write in `src/entity/action.cpp`;
`fa85caa3`. Follow-on: `Gait::playbackRate` in `src/entity/gait.cpp`, `a4dff7b1`
**Tests:** `tests/unit/test_alien_locomotion.cpp` — "an authored acceleration limits how fast a body
may reach its speed"

---

## The defect

`GaitSettings::accel` and `decel` were authored in scene files, parsed, kept on `EntityDesc` and
written back out — **and read by nobody for a body driven by behaviours.** `gait.hpp` states their
purpose: *"a body that reaches full speed in one frame and stops in one frame reads as a sprite, not
a character."* The only caller of `Gait::approach` was the action tier's `Move` verb. Every
behaviour assigns `state.speed` outright.

Measured with `rook`'s authored numbers from `glowmere-valley-2` (accel 4.8151, decel 6.6207), over
900 frames:

| | worst rise per frame | worst fall per frame |
|---|---|---|
| what the authored numbers allow | 0.0803 m/s | 0.1103 m/s |
| before | 0.1379 (1.7×) | **3.0685 (28×)** |
| after | 0.0803 | 0.1103 |

**The fall is the finding.** The body lost its entire walking speed in one frame. After the fix the
rise and fall land exactly on budget and the peak speed is unchanged: the body takes 0.27 s to
reach a walk and 0.5 s to stop.

## Decision

**The limit is applied centrally, after every behaviour, the action tier and the director have
written `state.speed`, and before the gait, the velocity measurement and the arc read it.** It is
not added to each behaviour. There are about twenty `state.speed` writes in `behaviors.cpp`, and a
limit that only some of them honour is a limit no author can reason about. It changes nothing for
an action-driven body, because the action tier has already limited that speed with the same
numbers.

**It applies only where the scene authored the numbers.** `GaitSettings::accelAuthored` is set by
the parser when either key is present. A body that inherits the defaults behaves exactly as it did
before. The defaults themselves are unchanged.

## Three things that nearly went wrong

**1. Zero does not mean "unlimited".** The obvious way to make the limit opt-in was to default
`accel`/`decel` to 0. `approach` computes `current + std::max(0.0f, accel) * step`, so a rate of
zero returns the speed **unchanged**. That default would have frozen every action-driven body at
whatever speed it held. The mistake was reasoning from what the field's name suggests instead of
reading the function. **A name is a claim about behaviour, and names in this codebase go stale.**

**2. Running on both paths is not enough; the replay depth also has to cover the ramp.** The limit
was applied on both `seek` and `update`, and it still broke ADR-360: the seek test read 0.10 m/s
where play read 0.27. `drift` is a shallow body. The seek rebuilds it in **2 steps, not 3,600**.
A rate-limited speed builds up over many frames, so two steps cannot reproduce a ramp. The fix is
an entity-level history requirement beside `IBehavior::historySteps()`, sized by how long the ramp
takes to finish: about 41 steps rather than the whole window, and only for bodies with an authored
gait. Without that condition every shallow body in every scene would have gone from 1 or 2 replay
steps to 41, because the defaults are not zero.

**3. The serialiser would have promoted every body to authored.** It wrote `accel` and `decel`
unconditionally. The first save of any scene would therefore have written the inherited defaults
back as explicit keys, the next load would have read them as authored, and every body would have
started ramping without anyone asking. This is ADR-618's problem in reverse. The values are now
written only when authored; otherwise they equal the defaults, so the file round-trips identically.

## What the ramp exposed

Before this change a body was either at full speed or stopped, never in between. Code that had
relied on that became visible:

- **`Gait::playbackRate` chose its reference speed from the gait's activity**, so a decelerating
  body the gait already called `Idle` got `idleRate`, which is 0 on the farm pack, and froze its
  walk cycle while still covering ground. It now follows the body's speed. On the farm pack the
  frozen frames fell from 58 to 17, and ADR-622 accounts for the rest (`a4dff7b1`).
- **Two test detectors counted acceleration as failure.** `stalledPercent` and `frozenWhileMoving`
  could not tell accelerating from stuck. ADR-622 gives them one shared definition, and both are
  now at zero.
- **A body rotating before it reaches walking speed is classified `Turn`, not `Walk`**, because the
  gait works from a speed that is still rising. That is correct. It is recorded here because the
  next person to see it will probably report it as a bug.

## Consequences

- **An authored `accel`/`decel` now takes effect wherever it is authored.** 73 of the 75 farm
  animals and all five Glowmere aliens author them.
- **Every scrub of a body with an authored gait replays about 41 steps instead of 1 or 2.** That is
  bounded and small next to the 3,600-step window, and bodies with no authored gait are unchanged.
- **Not fixed here:** the latent ordering hazard where `Explore` overwrites `state.radius` and
  `Ground` only writes it under a guard. No scene orders those two behaviours in the way that would
  trigger it.
