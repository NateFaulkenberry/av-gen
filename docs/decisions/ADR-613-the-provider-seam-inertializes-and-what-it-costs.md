# ADR-613: The provider seam inertializes — what that fixed, what it did not, and what it costs

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-612 (the defect and its threshold), ADR-547 (`AnimationPlayer`'s inertialization),
ADR-556 (advance and pose are two calls), ADR-360 (a scrubbed frame reproduces a played one),
Phase C §32
**Implemented by:** `MotionMemory::Blend`, `MatchSettings::derivedHalflife`,
`MatchMotionProvider::pose`
**Tests:** `tests/unit/test_matching_loop.cpp` — "§32: the provider seam inertializes…",
"§32: what the blend costs…"; `tests/unit/test_cross_clip_matching.cpp` — "§32 against ADR-612's
own population…"

---

## The finding that reframes ADR-612

**The provider the product installs is not the matcher.**

`MatchMotionProvider` is constructed nowhere in `src/` outside its own two files.
`Composition::AnimationSink::buildChain` adds exactly one provider — `clipProvider_` — so a body
with `proceduralMotion` on is posed by `ClipMotionProvider` and the matcher is unreachable from the
application. Fixing the matcher's blend, which is what §32 was written to do, would therefore have
changed nothing for the named beneficiary.

And the clip provider was **worse than the defect ADR-612 recorded**. Measured on the Glowmere
alien, gait changing every 0.5 s, same bar, same 1/30 s frame:

> **0.4484 m mean foot jump at a gait change, worst 0.6410 m — 8.8× the 0.0510 m bar.**

Its header explained why it did not blend: *"those are the player's job and duplicating them here
would be a second answer to a question already settled (ADR-547)"*. **That argument holds only
while the player is still consulted, and it is not.** When a provider poses a body,
`SkinnedRig::evaluate` takes the external pose and `AnimationPlayer::evaluate` is never called.
There is no first answer for the provider's to be a second one to. The rationale was not wrong when
written; it was invalidated by the seam it was written for, and nothing re-read it.

Both providers now inertialize, through one implementation that lives at the seam
(`derivedInertializeHalflife`, `inertializationDecay`, `applyInertializedOffset` in
`motion_provider.hpp`) rather than two that can drift apart.

## Read this before the numbers

**The headline number below is measured on a population that ADR-612's number was not.** ADR-612's
0.3566 m is a mean over transitions *forced* to a different clip every twenty steps. The shipping
loop, driven by a request instead of by a target clip's feature vector, chooses its own transitions
and most of them are mild. Its before-figure in the same instrument is **0.0892 m, not 0.3566 m** —
four times smaller. Neither figure is wrong and neither supersedes the other; they are two
populations. So both are measured here, each with its own before and after in its own instrument,
and **no ratio in this document crosses between them.**

**And the mean at a transition is not readable on its own.** An inertialized pose at the instant of
a switch is *exactly* the outgoing pose — that is the property that removes the teleport — and the
outgoing pose at that instant is the sample the previous frame already showed. The foot is
therefore held for one frame and the switch-frame metric reads near zero, which it would also do
for a blend that merely deferred the same displacement by a frame. **Every table below reports the
worst single frame of the whole run beside it.** That figure counts the deferred motion wherever
the blend eventually puts it, and on ADR-612's own population it is the figure that still fails.

## What was built

ADR-612 recorded that `MatchMotionProvider::advance` writes `transitionStart` and that nothing
blends. The fix is ADR-547's inertialization — the same arithmetic as `AnimationPlayer`, chosen
because the bar for switching `proceduralMotion` on is **parity with the player it replaces**, and
a provider that blended *differently* would be a change of look as well as of architecture.

Three things made it not a copy:

1. **The offset may not be stored.** `MotionMemory` holds no containers and `advance` may not touch
   a skeleton (ADR-556), so the pose difference can be neither remembered nor computed on the
   simulation half. What is recorded is **the pair of database samples it is the difference
   between** — two integers and a clock — and `pose`, which has a skeleton and runs once per drawn
   frame, recomputes the rest. This is also what keeps ADR-360: a scrub landing mid-transition
   reconstructs the offset instead of inheriting one.
2. **`transitionStart` could not be the blend's clock, despite its name.** The matcher writes it on
   **every search**, including the searches whose winner was the continuation and which therefore
   changed nothing; under its convention it means "when the last search ran", which is what the
   search interval and the continuation lock need. `ClipMotionProvider` writes it on a genuine
   change. One name, two conventions — the fourth time this phase has been bitten by that — so the
   blend carries its own `elapsed` and the field is documented rather than trusted.
3. **More than one transition is kept.** See "the depth is a measurement" below.

## The halflife is derived, not chosen

A switch introduces a pose offset of size `J`, and a critically damped decay can fail with it in
two opposite ways:

* **too fast** — the decay itself moves the foot, at a peak of `J·y/e` where `y = 2 ln2 / halflife`,
  so the worst single frame costs `J·y·Δ/e`;
* **too slow** — the blend is still running when the next switch arrives, and the slot it occupies
  is reused, so whatever is left of it (`J·(1+yT)e^{-yT}`) vanishes in one frame. That is a
  teleport again, smaller and the same defect.

The worst thing a transition can do is the larger of the two, and the halflife that minimises it is
the one where they are equal. **`J` cancels**, which is what makes this a constant rather than a
per-transition estimate: the trade does not depend on how big the jump was, only on how soon the
next one may arrive (`minimumContinuation`, because the lock makes searches at least that far
apart) and on how long a frame is.

`Δ` is **1/30 s — the interval ADR-612's 0.0510 m was measured over**, not the product's frame
rate. A distance budget carries the frame length it was measured at, and pairing it with a
different one is the units mismatch §28 has already paid for once.

Solved by bisection at startup: **halflife = 0.0902 s** at the shipping `minimumContinuation` of
0.2 s. Nothing about it was picked, and it moves if the lock moves.

## The depth is a measurement, not a preference

`AnimationPlayer` keeps one outgoing state. At this seam a slot is two integers and a clock rather
than a pose, so keeping more is affordable — and the one-slot arm is exactly where the remaining
defect was. Shipping settings, 1/30 s steps, 299 switches over 60 s of motion, threshold 0.0510 m
re-derived from the content (it reproduces ADR-612's figure to four decimals):

| arm | mean at a switch | worst at a switch | worst frame anywhere |
|---|---|---|---|
| blend off (before) | 0.0892 m | 0.8095 m | 0.8095 m |
| 1 slot | 0.0494 m | 0.2377 m | 0.2377 m |
| 2 slots | 0.0229 m | 0.1461 m | 0.2295 m |
| **3 slots (shipping)** | **0.0205 m** | **0.1459 m** | **0.2295 m** |

**One slot passes the bar by 3%.** That is not a comfortable margin and it is the reason the depth
was investigated rather than the depth being investigated for its own sake. The second slot is the
one that does the work — it halves the mean — because it is the one that stops the 18.8% of the
outgoing offset still alive at 0.2 s from being thrown away in a single frame. **The third slot
buys 10% of the mean and 0.1% of the worst**, which a reader deciding to cut it back to two should
see stated plainly rather than have to re-derive.

**Shipping verdict on this population: 0.0205 m against a 0.0510 m bar — 0.40×. It passes.**

## On ADR-612's own population, it passes the bar and fails the companion

Same driver as §30's, same forced cross-clip targets, same search; only the posing changed, to go
through `MatchMotionProvider::pose`. **The before-figure reproduces ADR-612 to within the noise of
one switch** — 31 switches, mean 0.3737 m, worst 1.6488 m, against the recorded 32 / 0.3566 /
1.6437 — which is what makes the instrument trustworthy for the after.

| | mean at a switch | worst at a switch | worst frame anywhere |
|---|---|---|---|
| before | 0.3737 m | 1.6488 m | 1.6488 m |
| after | 0.0002 m | 0.0025 m | **0.4112 m** |

**The mean at a switch is 0.00× the bar and that number means almost nothing** — it is the
one-frame hold described at the top. **The figure to quote is the last column: 0.4112 m, still
8.06× the bar**, down from 1.6488 m (−75%).

So: on the motion the shipping loop actually produces, the defect is fixed and the threshold is
met. **On a forced cross-clip transition the worst frame is still eight times an ordinary step.**
The blend converts a teleport into a fast slide; it does not make a violent cross-clip switch into
a graceful one, and nothing in a pose-space blend could.

## Where it does nothing at all

With the search interval, the continuation lock and the switch margin all removed — a
configuration nothing ships, run to explain the gap between the two populations — the loop switches
on 1777 of 1799 frames, the blend is re-anchored every frame, and the decay never runs. Measured:

* worst frame **1.1697 m → 0.5509 m** (−52.9%), because the chain's three slots still carry
  something;
* mean at a switch **0.0107 m → 0.0113 m (+5.9%)** — **worse**. Three live offsets decaying at once
  add a little motion to what, in that regime, is a one-frame re-selection rather than a
  transition. In absolute terms it is 0.0006 m, 1.2% of the bar.

**The fix depends on the continuation lock.** That is not incidental: the halflife is *derived*
from the lock. A future change to `minimumContinuation` changes the halflife by construction, and a
future change that removes the lock removes the blend's ability to work, silently.

## What it costs

`pose` now samples both ends of every live transition instead of sampling one clip once. Measured
per character per drawn frame, Glowmere alien, 49 joints:

* **3.43 µs → 20.69 µs averaged over the shipping loop (6.0×)** — 805 characters per 60 Hz frame,
  posing only. The five Glowmere aliens cost **0.10 ms a frame** against a 13.4 ms budget.
* Synthetic worst case, all three slots live: 0.61 µs → 20.81 µs.

The two are nearly the same number, and that is the finding: **at the shipping halflife the slots
are live nearly all the time**, because the decay's tail outlasts the 0.2 s between switches. The
first draft of this ADR said the worst case was reached "only while nesting"; the run average
disagreed and the sentence was wrong.

That is also why the floor at which a slot is let go is **derived rather than an epsilon**.
Dropping a slot is itself a discontinuity of `decay` times its offset, so the floor decides how big
that last teleport is: at 1% the worst offset this corpus produces (0.81 m) leaves 0.008 m, 16% of
the bar, and it ends the tail at 0.43 s instead of 0.81 s. Moving the floor from 1e-4 to 0.01 cost
0.0002 m of mean and saved **6.8 µs per character per frame**.

## The clip provider, before and after

Same instrument, same bar, 119 gait changes over 60 s. Halflife **0.1056 s**, derived from
`GaitSettings::minDwell` (0.25 s) — the soonest the tier above will change its mind, which is the
soonest one clip change can follow another.

| | mean at a change | worst at a change | worst frame anywhere |
|---|---|---|---|
| before | 0.4484 m | 0.6410 m | 0.6410 m |
| after | 0.0029 m | 0.0037 m | **0.1332 m** |

**The last column is the one to quote: 0.6410 m → 0.1332 m, −79%, and 2.61× the bar.** The means
are the one-frame hold and mean little on their own.

This is the provider Glowmere would run, so **this is the number that unblocks the flag**, not the
matcher's.

## Consequences

- **§32 is met on both providers and `proceduralMotion` is no longer blocked by the missing
  blend.** Two qualifiers belong in any note that turns the flag on: the forced cross-clip worst
  frame above, and the fact that the body posed will be the clip provider's, not the matcher's.
- **A handover between providers is still a jump, deliberately.** `MotionChain::advance` clears the
  blend slots on the frame the answering provider changes, because the slots name content in the
  settling provider's own index space and the new one would read a database sample as a clip index.
  Two providers have no common pose to interpolate in, so the body jumps once when the answer moves
  — for instance when a body leaves the ground and the matcher declines by design. Honest rather
  than hidden, and unmeasured: it needs its own before-figure.
- **The matcher is still not in the chain, and that is a decision rather than an omission.**
  Wiring it into `buildChain` carries a per-character-per-frame search cost, and §34 established
  that the only consumers of provider output in `src/` are diagnostics — so it blocks no spec
  section now that §32 is done. **Recorded as available and unexercised**: the provider works, has
  steady-state parity, and now blends; nothing runs it. A future reader finding it unused should
  read this line rather than conclude it was abandoned or that it does not work.
- **The residual on a forced cross-clip transition is accepted, and the mechanism is why.**
  0.4112 m at 8.06x the bar is not a failure of the blend; it is the honest limit of the approach.
  **The blend converts a teleport into a fast slide, and nothing in a pose-space blend could make a
  violent cross-clip switch graceful** — the outgoing and incoming poses simply are that far apart,
  and interpolating between two distant poses more smoothly does not bring them closer. **The fix
  for it is better selection, not better blending**, which is where §16's severity work already
  points: a matcher that does not choose a pose 2.8x further away than an adjacent frame does not
  need a blend to rescue it. Recorded as a residual rather than opened as a section.
- The default halflife is on. No shipping body is affected, because `proceduralMotion` is off on
  every one of them; the cost above is what it will cost when they are switched on.
- **`transitionStart` was misnamed for one of its two writers and is now `decisionTime`.** The
  matcher writes it on every search, the clip provider on a genuine change; "when did the current
  transition begin" is the first question an inertializer asks, and the old name would have
  answered it with the time of the last search. Renamed in its own commit, deliberately outside
  the measurement change, so that a rename could not be mistaken for a result.
