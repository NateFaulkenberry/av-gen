# ADR-909: A decider does not pace, stand about for ever, or walk every errand at one speed

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-269 and ADR-333 (the decider), ADR-351 (the stall breaker), Phase D §19–§23 (plans, the
commitment boost, variety), ADR-907 and ADR-908
**Implemented by:** `Selector::select`'s loop, turn-back and restless rules (`src/entity/decision.cpp`). The
`DecisionContext` fields `tick`, `departures`, `loopSeconds`, `loopRadius`, `loopPenalty` and `restless`, and
`Option::directed` (`src/entity/character_ai.hpp`). `Decide`'s still clock and departure memory (`src/entity/behaviors.cpp`).
`HoldPostConsiderer`'s `duration` and its half-tolerance return. `SpeedRange` on `interest` and `react`, and
`react`'s `urgentSpeed` (`src/entity/decision.{hpp,cpp}`).
**Tests:** `tests/unit/test_decider_habits.cpp` (eight cases, each with a control arm)

## Context

The owner's brief (§10 and the character animation assessment) asks that aliens not stand around
doing almost nothing, not walk to a point and turn round and walk back, and not move like animated
objects. The GV3 audit traced each to the decider:

- **`sage` alternated between a glow patch and the ring of its post every 15 s for two minutes.**
  `holdPost` scores higher the further the body is from its post, and walked it back only as far as
  the ring (12 of 21 stops were exactly 9.0 m from a 9 m post). The next errand then pulled it straight
  out again.
- **`sage` then stood on the ring for 67.3 s, to the end of the film.** Nothing limits how long `idle`
  or an open-ended pose may win. `holdPost`'s pose has no duration, and the ADR-351 stall breaker
  watches only plans that are trying to move.
- **Every alien moved at exactly 3.07 m/s**, because no considerer ever set a pace.
- **A reaction hurried no more than a stroll.**

Building the regression gate turned up a worse form of the first pattern, which the audit's trace
had no way to show. Take an aware decider (every GV3 alien is one) with a post whose pull rises with
distance and an errand whose pull rises with nearness. The two cross halfway, and the body paces
between the crossings: out to 11.6 m, turn, back to 4.5 m, turn, every nine seconds for as long as the
run lasts. Phase D's 1.25× commitment boost delays each flip but cannot stop a pull that grows linearly.

## Decision

**1. The loop memory** (on by default, `loopSeconds` 20). `Decide` remembers where the body set out on
each errand that walks it somewhere (the last four departures, as members, so ADR-700's checkpoints
carry them). The selector, not the considerers, applies two rules, so every considerer (including one
an author writes) is held to them alike:

- **A->B->A:** an option whose walk ends within `loopRadius` (4 m, plus its own arrival tolerance) of a
  departure less than `loopSeconds` old scores `loopPenalty` of itself. That is 0 by default, meaning
  not on offer. A departure the body is still standing on does not count.
- **Turning back mid-walk:** while the body is walking an errand (speed over 0.3 m/s, a commitment in
  hand), an option whose destination lies more than 120 degrees behind its heading is not on offer.
  The errand in hand is exempt. A creature finishes (or fails) the walk it started before it walks
  back; a `move` that cannot progress gives up on its own after 4 s.

Neither rule touches an order (`Option::directed`, set by the `goal` considerer that ADR-824's runtime
goals fill) or an option with urgency (a reaction, a flinch out of someone's way).

A soft discount (0.2, then 0.1) was built first and did not hold. Phase D's variety term discounts the
errand in hand along with its kind, so an errand could hold at a tenth of what it was chosen at. A post
discounted to a tenth still cut its dwell short and took the body home: one A->B->A inside the window
in 240 s, and four quick loops from a turn-back discount. The brief asks for none. `loopPenalty`
remains for an author who wants the soft rule.

**2. A plan in progress keeps its slot when nothing is on offer.** Phase D §20 already held a running
plan unless it was beaten. This change made "nothing on offer" reachable: an errand's own option drops
out of the list inside the considerer's `minRange` while every other option is behind the body. In that
case the selector used to clear the commitment, which lifted the turn-back rule on the next tick and
turned the body round 4 m short of its goal. Without a held plan, nothing is committed to, as before.

**3. The still clock** (`maxStillSeconds`, off by default). A body that has not moved `stallDistance`
in that long has its committed option set aside for as long again. Until it chooses something that
walks, it is `restless`: an option that takes it nowhere is not on offer. An order (an `Action` or
`Director` tier action running) restarts the clock, because a body a shot holds still is doing what it
was told. `DecisionDebug` reports `stillBreaks` and `restless`.

**4. `holdPost` walks back to half its tolerance, and takes an optional `duration`.**
- Walking only to the edge of the ring left the body where one step out was "away from the post"
  again. Half way in, it is at its post.
- A `duration` ends the pose, so the errand is over and an aware decider chooses again. With no
  duration it stands until beaten, which is a sentry.

**5. Pace.** `speedRange: [lo, hi]` on `interest` and `react` gives each option its own pace, in
multiples of the body's gait walk speed. It is drawn as a hash of (seed, decision tick, option) (D2), so
the same decision draws the same pace, a scrub draws what the play drew, and one extra option does not
re-cast the others. `react`'s moves also hurry by their urgency: `urgentSpeed` (default 1.5) multiplies
the pace at urgency 1 and nothing at urgency 0. The approach's urgency is the event's intensity; the
flee's is half as much again.

**On defaults.**
- **The loop memory and its veto are on**, because walking straight back to where it just left is
  nobody's authored intention. The guard fixture's sentry still leaves its post for what it notices and
  comes back: every `[decision]` case, the guard's included, passes unchanged.
- **Reaction urgency is on.** A flee at a stroll was never the intent.
- **`maxStillSeconds`, `duration` and `speedRange` are opt-in.** Standing is a legitimate thing for a
  character to have been authored to do (a sentry, a beat in a shot), and only the scene knows which of
  its characters may. Pace variety is a matter of casting.
- **`holdPost`'s half-tolerance return is on**, because walking to the edge of the ring was the defect.

## Consequences

- **The pacing is gone.** On the ADR-909 fixture, 240 s:
  - With the loop memory off, the body made 52 stops and 50 A->B->A round trips inside 20 s (the
    halfway dither).
  - With the default, it made 14 stops and 0 round trips inside 20 s. Every errand reached its target,
    and home was reached by way of somewhere else.
- **Standing is bounded where a scene asks.** An idle-weighted decider stood the whole 180 s. With
  `maxStillSeconds` 6, its longest stand was 6.0 s, it broke 15 times and walked 180 m.
- **Paces vary per errand and per decision:** four candidates drew four different paces within
  [0.6, 1.4] × walk, the same tick drew the same paces again, and the next tick drew others. A
  reaction to a full-intensity event moves at 1.5× walk (both approach and flee); a faint one (0.4)
  approaches at 1.2× and flees at 1.3×.
- **A seek lands where the play did** with every new memory in use (loop memory, still clock, timed
  post, paced errand, walk-through turns, eased wander): under 1 mm at 45 s.
- **Every decided scene can decide differently**, because the loop memory and the veto are on:
  - `glowmere-valley-2*`, GV3, the Character Intelligence Lab's decided fixtures, the autonomy demo;
  - every `react` move is faster, and the valley's aliens hurry to the beam;
  - every `holdPost` return ends half a tolerance nearer its post.

  ADR-910 records the measured effect on GV2-multicam and GV3, and lists the tests re-baselined.
- **`DecisionContext` and `Option` gained fields**, defaulted so every existing considerer and every
  test that builds one by hand is unchanged.
