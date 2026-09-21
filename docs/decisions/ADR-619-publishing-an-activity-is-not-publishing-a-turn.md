# ADR-619: Writing the activity is not publishing the turn — a standing character rotating at 120°/s was classified `Idle` on 599 of 600 frames

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-615 (one-ended contracts), ADR-618 (a key the parser read and the writer never
wrote), ADR-091 (the gait is remembered across a pass-through), `docs/testing.md` #32
**Implemented by:** `LookAt::update` in `src/entity/behaviors.cpp`
**Tests:** `tests/unit/test_alien_locomotion.cpp` — "a standing character turning to look is
classified as turning, not idle"

---

## The two halves, each correct

**`LookAt` turns the body and announces it through `activity`.** It computes a rate into a local,
rotates the yaw, and sets `state.activity = Activity::Turn` when the angle it still has to cover is
worth mentioning. Reasonable in isolation.

**`Gait::select` does not trust a locomotor proposal.** `Activity::Turn` is a locomotor, so the
proposal is discarded and the gait is re-derived from `speed` and `turnRate`. Also correct, and
deliberately so: `gait.hpp` states that hysteresis lives in exactly one place, and a proposal that
bypassed it would be the flicker the class exists to prevent.

**What neither states:** `LookAt` publishes its turn through `activity`; `Gait::select` reads a turn
through `turnRate`. And `LookAt` was **the only turner in the engine that never wrote that field**.
Every other one does — the `turn` helper, `Spin`, `Wander`, `Explore` twice, and the action tier
twice.

So a body standing still and rotating to look at something arrives at the gait as speed 0, turn
rate 0, and comes out `Idle`.

## The measurement

A sentry with `lookAt` at guard-post's own 120°/s, tracking an orbiting beacon, over 600 frames:

| | frames turning | classified `Turn` | classified `Idle` |
|---|---|---|---|
| before | 599 of 600 | **0** | **599** |
| after | 599 of 600 | **599** | **0** |

**Not a margin that moved — a classification inverted on every frame it applied to.** The ground
truth is the yaw the engine itself published, differenced frame to frame, so it is independent of
anything the gait decided.

## Why it matters, and the scene that names it

`examples/labs/character/guard-post.scene.json` authors `lookAt` on its `sentry`, a character whose
entire premise is standing at a post and turning to look at things. **That scene's stated purpose is
what this defect defeats.** `lookAt` is authored in eleven scenes, including the shipping Glowmere
set.

**Who hits it:** an author who gives a character a turn-in-place clip and a `lookAt`, watches it
swing round to face something, and sees the idle clip play through the whole turn. They will check
the clip mapping, the `turnEnter` threshold and the gait's dwell — all three of which are fine —
because nothing connects the symptom to a field one behaviour declined to write.

There is a quieter second case. **`turnRate` is not zeroed per frame**, so the value the gait sees
for a `lookAt`-only character is either 0 or a *stale* rate left by a `wander` or `explore` that
stopped turning several frames ago. The first is the bug above; the second is the same bug
producing an intermittently correct answer, which is worse to diagnose.

## The fix

One line, guarded exactly as the activity write beside it is:

```cpp
state.turnRate = applied / std::max(static_cast<float>(ctx.dt), 1e-4f);
```

**The guard is the design decision, not the assignment.** `LookAt` defers to whatever else is
driving the body — it claims the activity only when nothing else has — and a character that is
walking already has a turn rate from the behaviour walking it. Writing unconditionally would make
this a second opinion on a field another behaviour owns that frame. `state.activity` is reset to
`Idle` at the top of every step on **both** the update and the seek paths, so the guard is live on
every frame where `LookAt` is the one doing the turning.

## The second defect on the same field, and the one that validated the first fix

`turnRate` had **two** faults, and they are inverses:

1. **`LookAt` never published it** — turning, reported `Idle`. Above.
2. **Nothing ever cleared it** — `EntityState` persists, every turner writes it *while* it turns
   and none writes zero when it stops, so the last rate a body turned at survived indefinitely.
   A body that has finished turning and is standing still was classified `Turn` and played its
   turn-in-place clip. Measured on the same harness: **509 of 600 stationary frames, all 509
   classified `Turn`, at a stale 2.094 rad/s against a 0.35 threshold.** On `glowmere-valley-2`
   that is `Idle_turn` on five characters through the idle window of every loop.

Cleared per frame alongside `activity`, on **both** the update and the seek paths, because they
reset per-frame state separately and a field cleared in one and not the other is cleared for a
played frame and latched for a scrubbed one. Every existing writer already computes an
instantaneous `turned / dt`, so none relied on the latch.

### And the first fix was wrong, and passed anyway

**The original `LookAt` fix guarded the rate on `d`, the remaining error, rather than on `applied`,
what the body actually did.** Those differ exactly when a character is tracking a moving target: it
holds a small error while turning steadily, so a rate guarded on the error goes unpublished on the
frames it is turning hardest.

It measured 599 of 599 anyway — **because the latch was carrying a stale rate from an earlier
frame**. Removing the latch dropped the same test from 599 to **24**, which is the first honest
reading of that fix. Corrected to publish the rate whenever the body rotated, keeping the activity
claim on the error: **599 of 599, with the latch gone.**

> **A fix validated against a system that still contains a second, compensating defect is not
> validated.** The measurement said 599 of 599 and the mechanism was wrong. What caught it was
> fixing the other defect and re-running the first test — not any amount of rereading the first fix.
> The transferable form: **when two defects touch the same field, fix one and re-measure the other,
> because either may have been holding the other up.**

## Consequences

- **`docs/testing.md` #32 is the general form** — two locally correct decisions composing into a
  defect neither side's tests can see. This is that shape in behaviour rather than in
  serialisation, and it is the second instance found in one day.
- **A published signal is only published if its consumer reads *that* signal.** `LookAt` wrote a
  field that is read by diagnostics and drove nothing; the field the decision actually depends on
  went unwritten. Asking "who reads this?" rather than "did I set this?" is what separates the two.
- **`Explore` maintains `turnRate` on two of its seven exits**, which is now correct rather than
  incomplete: with the field cleared per frame, "write it when I turn" is the whole contract, and
  the five exits that do not write it are publishing zero, which is true.
- **`docs/testing.md` #31 applied again**: the clear had to go on both `seek` and `update`, which
  reset per-frame state in two separate blocks.
