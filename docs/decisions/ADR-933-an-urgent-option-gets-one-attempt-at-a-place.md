# ADR-933: An urgent option gets one attempt at a place

**Status:** Accepted
**Date:** 2026-09-27
**Follows:** ADR-909 (the decider's habits: no walk straight back, no turning round mid-walk), ADR-910
(the character quality analyzer), Phase D §19 and §59 (a failed plan is left alone for `failSeconds`),
ADR-932 (routes that respect connected regions)
**Found by:** the GV3 revision's gv3-cast stream (`phase3/cast.md`, "Found" 2), and this stream's trace
of the same film with every decision and action result (`avgen_cast_trace --decisions`)
**Implemented by:** `entity::Attempt`, `DecisionContext::attempts` and `Option::retry`
(`src/entity/character_ai.hpp`); `Selector::select`'s ADR-933 pass and `optionDestination`
(`src/entity/decision.{hpp,cpp}`); `Decide`'s `attempts_`, `noteAttempt` and `stillHeard`
(`src/entity/behaviors.cpp`); `ActionQueue::standing` (`src/entity/action.{hpp,cpp}`); the pacing
measure `CharacterBehaviourMetrics::longestPacing`, `longestPacingSeconds` and `longestPacingFrom`, and
its "pacing" key in `avgen_character_quality`'s JSON (`src/entity/character_quality.{hpp,cpp}`); the
`--decisions` trace of `avgen_cast_trace` (`tools/cast_trace.cpp`)
**Tests:** `tests/unit/test_urgent_retries.cpp` (`[adr933]`: the selector's rule option by option; a
reaction that stalls in water it shares a region with; GV3's case, a beam across a river that runs edge
to edge; a flinch); `tests/unit/test_character_quality.cpp` (`[adr933]`: the pacing measure on
hand-built bodies)

## Context

ADR-909 made two habits the selector refuses by default:

- a walk that ends where the body set out from within `loopSeconds` (A->B->A);
- while the body walks an errand, an option more than 120 degrees behind it (turning back).

Neither touched an option with urgency: a reaction to an event, or a flinch out of someone's way.
ADR-909 said those "are not habits". That is right for a first attempt and wrong for every attempt
after it. When a reaction's walk could not get where it was going, nothing stopped the same reaction
sending the body back. Phase D's own guard against retries (a failed plan's option and subject left
alone for `failSeconds`, §59) is taken when the plan's action list *drains*. A reaction's list is a
walk, a face and a look, and the look is where GV3's aliens were overruled. So the list was replaced
before it drained, and nothing was remembered.

gv3-cast measured it on GV3: ember "paced the river bank through the drop: walk, stop, 180 degrees,
walk back, eight times in 40 s". This stream's decision trace of the film shows the mechanism (ADR-932
has the timeline): a reaction and roam errands sent ember to the water across the river; it stalled;
its post (`range`) pulled it home while it stood; the errand won again.

The brief asks for this: an urgent option keeps its exemption for its first attempt. Once its move has
failed or given up, the same event cannot send the body back to the same place in a loop. A flinch
out of someone's way must still work.

## Decision

**1. A decider notes an urgent errand that did not get there** (`Decide::noteAttempt`), once per errand,
as an `Attempt`: what it was about (its subject), where its walk was going and how near counted as there
(`optionDestination`, taken when it was chosen), and when. An errand did not get there when:

- **its walk failed,** noticed the step it fails, not when its list drains. `ActionQueue::standing` says
  whether the list a tier holds has had an action fail yet. `Drained` says so only once the whole list
  is over, and a list replaced first never drains;
- **it was given up stalled:** something else was chosen while its `move` had gone a second or more
  without getting nearer its goal (`kGivenUpSeconds`). A second is several strides, and a walk still
  getting somewhere resets that count every step it does. This is ember's case: overruled by its post
  while stalled at the water, it had failed nowhere the queue could say;
- **the stall breaker gave up on it** (ADR-351).

Only urgent errands, with a subject and a destination, are noted. ADR-909's rules already hold every
other option, and an attempt with no subject has nothing to key a retry by.

**2. The selector reads them** (`DecisionContext::attempts`, applied before ADR-909's rules):

- **Not back where it failed.** An urgent option whose subject has a noted attempt, and whose walk ends
  within `loopRadius` plus the larger of the two arrival tolerances of that attempt's place, is not on
  offer (factor "tried").
- **A second attempt anywhere else is a habit like any other.** Every other urgent option of that
  subject is marked `Option::retry` and held to ADR-909's rules: no turning round mid-walk, no walking
  straight back to where the body set out from, and the restless rule. A creature that could not get to
  something tries another way, or not at all. It does not turn round mid-walk for it.
- **A first attempt keeps its exemption.** An urgent option whose subject has no noted attempt is left
  alone, as before. A flinch out of someone's way still steps wherever it must, even behind a body
  walking an errand, even beside an attempt at something else.

**3. How long:** the mind's `failSeconds` (30 s by default), the time "a target that could not be
reached is left alone for", already used for a drained failure. An event's attempt is also kept for as
long as the body still remembers hearing the event (`stillHeard`), because until then the event can
still send it. A beam heard for ninety seconds must not send the body back to the same bank every
thirty. `failSeconds` 0 keeps no attempts. The attempts are members of `Decide`, so an ADR-700
checkpoint carries them, and `reset` clears them for a replay to rebuild.

**4. The measure, and why it is not the reversal count.** ADR-910's `reversals` counts stops walked out
of more than 150 degrees from the way in. ember's turnarounds at the water measured 121, 116 and 129
degrees: a body that walks through its turns (ADR-908) leaves a turnaround on a curve, and over its
first 1.5 m that is well short of 150. Its turnarounds at the south end measured 172, 173 and 176. So
the loop was six turn-backs running and never two reversals running. The recorder gains **`pacing`**:
the longest run of consecutive measured stops each walked out of more than 90 degrees from the way in
(back, not on), with the span from its first stop to its last and when it began. A stop too short to
measure neither extends a run nor breaks it. It is reported beside `reversals` in
`avgen_character_quality`'s JSON (`stops.pacing.longest`, `.seconds`, `.from`). Individual metrics
still, never a score.

**5. An instrument:** `avgen_cast_trace --decisions all|names` writes each decider's decision lines (the
`avgen_behavior_trace` format) and every action's result and reason, sampled every frame. A position
trace shows a loop; this shows why.

## Consequences

- **On the fixtures** (`test_urgent_retries.cpp`; a watcher with a reaction to a beam 46 m east across a
  river, a post pulling it home, walkers wading to 0.8 m; 90 s each):
  - **The river ends inside the world** (one region, so ADR-932 leaves the straight line). Now: one walk
    into the water, and it was not sent back. Control (`failSeconds` 0, no memory): home, water, home,
    water; two trips; 5 reversals; a pacing run of 4 turn-backs over 21.7 s.
  - The pacing left in "now" (2 turn-backs in 2.4 s) is the one stalled walk's own. It dithers at its
    wade limit for the four seconds the `move` verb's stuck clock allows before it gives up, and the
    control's first trip shows the same. ADR-933 does not change the stuck clock.
  - **The river runs edge to edge** (GV3's case, ADR-932 and this together). Now: one walk to the bank,
    dry, 31.0 m from the beam; it watched from there and later went home to its post. That is 1
    reversal and a pacing run of 1. Control (the old straight-line route and no memory): into the water
    twice, 5 reversals, a pacing run of 4.
- **The rule, option by option** (`[adr933]`):
  - A retry to the failed place is refused, walking or standing.
  - A retry behind a walking body is refused, while one to the side is offered.
  - A first attempt at another subject is offered even behind a walking body.
  - With nothing remembered, all of them keep their exemption.
- **A flinch still works.** A body walked at by another chose to step away and moved 4.2 m on its own
  feet. The control, a body with no reason to flinch, was moved 0.2 m by the crowd's push.
- **On the films** (with ADR-932 and ADR-934; whole film, before `ec515c8b`, after this branch; the
  pacing column is the trace's, computed the same way for both):
  - **GV3 with ember's reaction to E5 back at "go and see":**
    - ember: reversals 3 -> 0; stops walked out back the way they came 10 -> 3; its longest run of
      those, 6 over 28 s from 176 s (the loop gv3-cast saw) -> 2 over 5 s; A->B->A 3 -> 1;
    - the aliens: reversals 5 -> 1.
  - **GV3 as generated:** ember's longest run is 2 before and after (its 22-52 s dither on the bank
    becomes short walks to the bank; see ADR-932). tide's goes 2 -> 3 (33 s from 89 s), on its own
    bank, with no urgent option in it.
  - **GV2-multicam:** see ADR-932. The aliens' trajectories diverge from the first abduction. The one
    longer run (ember, 2 -> 4, 149-203 s) is its post pulling it home against errands on its own bank.
    The one urgent option inside it, a reaction to the saucer's beam at 158.4 s, was taken once, was
    overruled by an errand 2.5 s later while still walking, and was never taken again.
- **Every decided scene can decide differently** once one of its urgent errands fails or stalls:
  GV2-multicam, GV3, the Character Intelligence Lab's decided fixtures and the autonomy demo. A decider
  with no `mind` has no urgent options and is unchanged.
- **ADR-910's tables change meaning slightly:** `pacing` is new, and `reversals` is unchanged.
