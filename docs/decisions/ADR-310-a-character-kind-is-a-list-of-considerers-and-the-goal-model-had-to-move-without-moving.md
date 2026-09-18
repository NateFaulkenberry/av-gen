# ADR-310: A character kind is a list of considerers, and the goal model had to move without moving a millimetre

**Status:** Accepted
**Date:** 2026-09-18

ADR-269 decided the shape of the decision layer and left it unbuilt: `Option`, `DecisionContext` and
`IConsiderer` were declared in `src/entity/character_ai.hpp` §3, they compiled, and nothing
implemented them. This is the implementation. ADR-269's central finding — that **nobody decides**,
and that a behaviour tree would be a second interpreter over the `ActionQueue` this repository
already owns — was not contradicted by building it. Four smaller things were.

---

## 1. What was built

`src/entity/decision.{hpp,cpp}` and one behaviour, `decide`, in `src/entity/behaviors.cpp`.

* **`Selector`** — runs its considerers in order into one `std::vector<Option>`, takes the highest
  score above zero, and holds it for a dwell and against a margin. It counts what it refused:
  `dwellRejections`, `marginRejections`, `empty`, `decisions`. A hysteresis nobody can see is a
  hysteresis nobody can tune.
* **Four stock considerers.** `idle` (a constant floor, and the legal empty-action case), `holdPost`
  (stand where you were put; score flat inside `tolerance` and rising at `pull` per metre beyond
  it), `investigate` (the first consumer of a percept in this engine), `interest` (the goal model
  extracted out of `Explore`).
* **`goalWeight`** — taste times the point's own weight, damped by distance, suppressed where the
  character has recently been. The arithmetic that was inlined in `Explore::pickGoal`.
* **`PerceptMemory`** — a bounded, time-limited fade over percepts, held by the *behaviour* and not
  by a considerer. Off by default.
* **`IBehavior::decisionDebug`** and `ScoredOption` in `entity/behavior.hpp` — the losing scores
  beside the winner, which is the overlay ADR-269 §4 said the whole design was for and which
  ADR-261 recorded the Character Intelligence Lab as owing.
* **`BehaviorContext::actions`** — the entity's own `ActionQueue`, handed to the behaviour rather
  than reached for through the world, which is `const` to a behaviour and must stay that way.

A character kind is now a list in a scene file:

```json
{ "kind": "decide", "hertz": 2, "considerers": [
    { "kind": "holdPost", "post": "gate", "pull": 0.30 },
    { "kind": "investigate", "kinds": ["character"], "weight": 6.0 },
    { "kind": "idle" } ] }
```

`examples/labs/character/guard-post.scene.json` carries a guard, a patrolling courier and an
explorer. The guard and the explorer are the same C++ class, reported by `IBehavior::kind()` as
`decide`, and there is no `Guard` anywhere in `src/`.

---

## 2. Hysteresis is counted in ticks, not in seconds

ADR-269 says a chosen option holds for a minimum dwell. It does not say what a dwell is measured in,
and the obvious answer — accumulate `dt` — is the one that breaks a scrub.

`decideTick(time, hertz, seed)` is a pure function of the instant, the same shape ADR-290 §2 gave
the sense cadence and for the same reason: an accumulator drifts with the frame rate, so a replayed
decision boundary lands on a different step from the played one. Measured over four seconds of a
2 Hz cadence: the eight tick boundaries land on the same instants at 60 Hz and at 37 Hz to within
**20 ms**, which is one frame of the coarser rate and is which frame *reports* the boundary rather
than where it is. An accumulated half-second over the same four seconds drifts **264 ms**.

So the only state the selector carries across frames is the tick index it last committed on, and a
replay reconstructs it by re-firing the same ticks. Play-vs-seek on the decided scene is
**0.036667 m at 30 s** over three bodies, against the lab's 0.025000 m on the five explorers — the
same order, on a scene where a body is being driven by the action tier rather than by a behaviour.

---

## 3. The extraction, and what it cost to prove

`Explore` is the only autonomous mind this engine has ever had and five Glowmere characters depend
on it. Its goal model — five affinities, a distance falloff, a visited-place suppression, three
range bounds — was inlined in `pickGoal`. It is now `entity::goalWeight`, called by
`InterestConsiderer`, which `Explore` holds one of.

**The arm is a position trace and not a walk test**, because a test that asserted the five bodies
still move would have passed against an extraction that changed every route in the world.
`tests/data/explore-position-trace.txt` is 3,600 samples of five bodies over 60 s at a fixed 60 Hz,
every fifth frame, each of x, y, z and yaw as its raw 32 bits, written by the build *before* the
move and committed in the commit before it. After the move, **0 lines differ** — bit equality, not a
tolerance, because the claim is that the arithmetic is the same arithmetic and not that it is close.

Two controls, because a trace that cannot differ proves nothing either:

* One metre on `entity/scout/explore/maxRange` moves **621 of the 3,600 samples**. The trace is
  measuring the goal model.
* Four of the five bodies must occupy more than one distinct sample. A golden of five stationary
  characters would match whatever the extraction did to them. (`penned` is walled in and genuinely
  does not move — lab case 4.)

The expressions in `goalWeight` are deliberately untidied. A reciprocal multiply instead of the
division, a `std::hypot` instead of `glm::length`, or hoisting `std::max(hi * 0.5f, 1.0f)` out of
the loop are all algebraically identical and none of them is bit-identical.

---

## 4. The weighted roll did not move, and that is the interesting half

ADR-269's selector takes the **highest** score subject to a dwell and a margin. `Explore` takes a
**weighted draw** from `Entity::rng_`, which is a stream. These are different designs and both are
now in the engine.

Replacing the draw with an argmax would change every route in Glowmere: a weighted pick over 141
candidates and a maximum over the same 141 are not the same character. And moving the draw anywhere
that consumed a different number of values from the stream would re-cast every later choice it
makes — which is the failure ADR-267's D2 exists to name.

So the extraction is of the **model** and not of the **choice**, and `Explore` keeps its stream. The
cost of that is recorded rather than hidden:

* `Explore` is the one thing in the engine that still violates D2's "draw from a seed and an index,
  never from a stream". It always did; this did not make it worse and did not fix it.
* Unifying them means replacing the roll with `hash(seed, selectionIndex)` and accepting a changed
  film, which is an owner's decision. The golden trace is the instrument that would price it: it
  says exactly which bodies move and from which second.

`Explore`'s `InterestConsiderer` is set to `Source::Omniscient` for the same reason. ADR-270's
finding is that reading `interestPoints()` is *why* two characters in one world walk the same route,
and the source is one word — but changing it for `Explore` is a changed film too. The lab's case 6
is the before-arm and case 14's explorer is the after.

---

## 5. Percepts had to accumulate after all, and the shipped fixture cannot measure it

ADR-290 §7 records that percepts do not accumulate — each sense tick rebuilds the working set, so a
thing that leaves the range is gone rather than remembered — and names that as the first thing to
revisit if a decider needs a character to keep looking for something it lost sight of.

It did. A guard walking towards a percept loses it the moment the thing crosses the range boundary;
`investigate` falls to zero, `holdPost` wins with nothing to beat, and the body turns round
mid-stride. The dwell delays that by its own length and does not fix it, because the incumbent's own
score is what collapsed.

`PerceptMemory` is the fade: `capacity` entries, `seconds` of life, off by default, cleared by
`reset`, reconstructed by a replay and never persisted (D4). A remembered percept keeps the `seenAt`
it was noticed at, so `investigate` scores it down rather than believing it, and a live percept of
the same thing replaces the remembered one rather than sitting beside it.

**It lives on the behaviour and not on a considerer.** ADR-269's rule that considerers hold no
per-character state is what makes D4 free — there is nothing in a considerer to checkpoint — and a
memory inside a shared scorer would break it for every character at once. The same goes for the
visited-place list the goal model reads, which is why `DecisionContext` gained one field,
`visited`. That is the only change to `character_ai.hpp` §3, and this is the ADR that header asks
for instead of an edit.

**And the guard fixture cannot measure the fade.** With it and without it the sentry goes 13.442 m
from its post, bit for bit, because its 24 m range and the courier's 18 m closest approach mean it
keeps the courier in view for the whole of every approach. A memory it never consults is not a
memory an arm can measure, and asserting on that fixture would have been an arm that could not fail
(ADR-182). The arm is `ScriptedPerception`, which is the only thing that can take a percept *away*
at a chosen instant: two seconds of a percept and then none sends the guard **8.514 m** from its
post remembering and **1.497 m** forgetting, from the identical 2.715 m at the moment it vanished.

---

## 6. Where this corrects ADR-269 and its own plan

### 6.1 A decider must stand off from what it walks to

`ActionQueue`'s `move` routes through `NavigatorPath`, which returns `Unreachable` for a goal that
is not navigable. A landmark's interest point is *at* the landmark and a landmark is usually solid —
the lab's cairns are heroes with a radius and `obstaclesFromHeroes` turns every one into a cylinder.
So an option that named the landmark failed the instant it won. Measured before the fix: the
explorer scored six options, committed to one every decision tick, and travelled **0.00 m in 75
seconds** while its queue drained with `unreachable`.

`Explore` never met this, because it goes through `Navigator::requestPath`, which carries a
`goalTolerance` and snaps. The action tier has no such thing, so the stand-off is the considerer's
job and `approach` is the knob.

### 6.2 A body remembers where it has been, not where it is going

The obvious version of the novelty memory records the destination on commitment. It is wrong: the
goal model suppresses a visited place to 0.12 of its weight, so a body that recorded its errand on
departure devalued the errand it had just set out on. The winner in the overlay was never the
highest score and the body pivoted on the spot for 75 seconds.

`Explore` never had this either, because `remember(goal_)` is called in `stepArrive` — on arrival,
not on departure. Recording the body's position at the moment it changes its mind is the same fact
seen from the other end and needs no arrival event: a body that walked to a cairn is standing at the
cairn when it decides what to do next, and one that gave up halfway records the halfway point, which
is honest.

### 6.3 Four stock considerers is the right number for a guard and an explorer, and one short for a river

ADR-269 asked for three or four. Four is what a guard and an explorer need and the fourth —
`idle` — earns its place by being the floor every other option is measured against; without it a
character whose considerers all score zero has no winner, which is correct and is indistinguishable
on screen from a decider that is not running.

None of the four prices a **route**. `holdPost` and `investigate` score a place; `interest` scores
taste times nearness, and nearness is a straight line, so a character with a river between it and a
glow patch scores it exactly as it scores one on the same bank. Lab case 9 is still blocked and its
`blockedBy` is rewritten rather than deleted, because a case whose blocker is removed on the grounds
that the unit it named finished is a case that passes by asserting nothing. `docs/character-ai-plan.md`
§P11 is the unit it names now.

### 6.4 The lab's `decides` entry expired exactly as it said it would

`src/labs/lab.cpp` named `behaviors.cpp:Explore` with a comment saying it "will be renamed when P3
extracts the goal model out of it — at which point this test fails, which is the point". It did, and
it is now `decision.cpp:Selector`.

---

## 7. What this does not do

**`Explore` still reads the omniscient list and still draws from a stream.** §4. Both are one word
and an owner's decision, and the golden trace is what would price either.

**Nothing draws the option scores yet.** `IBehavior::decisionDebug` is published and read by the
tests and by nothing in `src/ui/`. That is the same shape ADR-266 complains about for
`LocomotionState`, with the difference that the consumer is a lab overlay somebody can write in an
afternoon against a seam that exists.

**A second fixture, because the first one is frozen.** `guard-post.scene.json` exists rather than a
sixth body in `character-intelligence-lab.scene.json`, because a body added to that scene changes
what the other five perceive and score — and case 15's golden position trace is taken from it. The
cost is that the guard's world has no ecology and therefore no `Glow` interest point, so lab case 7's
mushroom is a scripted percept and its live arm is a `Character` percept on the same code path.

**`Schedule` and `decide` both write `Authority::Routine`.** A character with both would have its
schedule overridden every time it changed its mind. Nothing in the repository does this and nothing
stops it; it is recorded here rather than guarded against, because the guard would have to decide
which of the two an author meant.
