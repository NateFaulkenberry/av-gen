# ADR-335: A considerer that prices the way and not the place, and the river the lab already had

**Status:** Accepted
**Date:** 2026-09-18

ADR-333 built the decision layer and four stock considerers, and said in its own closing that the
fifth one nobody wrote was `docs/character-ai-plan.md` §P11. This is that considerer.

The gap is exact and it is not a gap in the shape. `holdPost` and `investigate` score *where a thing
is*. `interest` scores taste times nearness, and `goalWeight`'s nearness is `glm::length` — a
straight line. So a character with a river between it and a glow patch scores it **exactly** as it
scores one on the same bank, and the one choice the Character Intelligence Lab's case 9 has been
waiting three units to see — ford it, or walk round — is not a choice any of the four can express.

---

## 1. What was built

`RouteConsiderer` in `src/entity/decision.{hpp,cpp}`, registered in `makeConsiderer` and
`considererKinds` the way the other four are, plus one overload in `src/entity/navigation.{hpp,cpp}`
that the arithmetic needs.

```json
{ "kind": "route", "name": "cross", "wadePenalty": 12.0,
  "destinations": [ { "name": "north-bank", "point": [-66, 0, 46] } ] }
```

For each destination it asks `Navigator::requestPath` **twice**, with the same `PathRequest` and two
different `NavPathCost::wadePenalty`:

| probe | penalty | what comes back |
|---|---|---|
| `fordPenalty` | 0 | water is free, so this is the shortest way there — through the river if the river is in the way |
| `detourPenalty` | 40 | water is ruinous, so this is the driest way there — round the end of the river if there is a way round |

**Neither number is the character's opinion.** They are the two probes that *find* the two ways. The
opinion is `wadePenalty`, and both ways are then scored with it:

```
cost  = length + wadePenalty × (metres of the route in water, weighted by depth over the wade band)
score = weight × the place's own weight ÷ (1 + cost / falloff)
```

which is why `wadePenalty` is a registered parameter (ADR-225) and the two probes are not: the taste
is what an author tunes and an overlay drives, and the probes are how the question is asked.

Two options come out, not one — and only when there **are** two. When the two requests come back as
the same way, which is every destination on this bank and every destination at all in a world with
no water, one option is appended. A second option identical to the first would put the selector's
margin between a route and itself and would print the same line twice in the overlay.

---

## 2. `Navigator::requestPath` grew the argument `NavGrid::path` always had

`NavGrid::path(request, cost)` has taken a `NavPathCost` since ADR-093. The seam above it,
`Navigator::requestPath(request)`, did not, so the one call an action layer is told to use was the
one call that could not ask the question. The one-argument form is now written as a call to the
two-argument one with `NavPathCost{}` — one search, not two implementations. That every existing
caller's answer is unchanged is not an assertion of taste: `tests/unit/test_decision_extraction.cpp`
compares 3,600 samples of five `Explore` bodies against a golden taken before any of this, on the
raw bits, and 0 lines differ.

The gridless branch does not apply the price and cannot: there is no graph to spend it on, so two
requests at two prices return the same straight line. `price()` therefore refuses to score at all
without a grid, because two identical answers from a world with no graph are not evidence that there
is no water between here and there.

---

## 3. The option has to *be* the route, or the overlay is lying

`NavigatorPath::route` (`src/entity/action.cpp`) answers a move with **the straight line to the
goal** and leaves the rest to local steering, which — `navigation.hpp` says so in as many words —
"gets a walker round a trunk and a boulder, and it cannot get one round a lake". So an option whose
single action named the far bank would be a body that waded whatever the considerer decided, with
the overlay reporting the detour and the film showing the ford.

That is stated from reading the path provider, and it is also **measured**, because a design
decision defended only by reading the code is a decision nobody can check. `river-crossing.scene.json`
carries a third body, `plodder`, with one authored `move` action straight at the far bank and no
decider at all — precisely what the naive option would have emitted. Its deepest water is
PLODDER_DEPTH m against the drylander's 0.27 m, in the same run.

So the winning option's actions are a **`Move` per waypoint of the route that was priced**, and the
middle legs carry a `legTolerance` wide enough not to oscillate on a corner and narrow enough not to
cut one. Cutting the corner of a detour is walking into the river the detour was chosen to avoid.

---

## 4. The wet metres are measured off the grid, not off the world

`NavSample::waterDepth` is carried "for exactly this" and `Navigator::sample` is an analytic
evaluation of the world at **10.325 µs** (ADR-268). A 110 m route sampled every two metres is 55 of
them — 0.57 ms — and this runs twice per destination per decision tick. `NavCell::wade` is the same
0..1 quantity, already computed at build, at **0.024 µs** a lookup.

The four-metre grid is honest here in a way ADR-295 says it is not for `pathClear`: this is not
deciding whether a body may stand somewhere, it is weighing how wet a route already accepted as
walkable is, and an error of one cell is an error of four metres in a hundred. The analytic
fall-back remains for a world with no graph, so a test holding a bare `Navigator` gets an answer
rather than a zero that looks like dry land.

---

## 5. The fixture constraint was a claim, and half of it is false

§P11 says case 9 needs a fixture of its own because "adding a body to
`character-intelligence-lab.scene.json` changes what the other five perceive and score, and case
15's golden position trace is taken from it". Measured, on the same 3,600-sample trace
`tests/data/explore-position-trace.txt` holds, comparing raw bits:

| one hero stone added | samples differing | worst |
|---|---|---|
| at (−40, 92), 90 m away across the river | **0 of 3,600** | 0.000 m |
| at (6, 10), 6 m from `scout` | **1,779 of 3,600** | 13.685 m |

So the lab fixture is **not** fragile to any edit. `goalWeight` rejects a point outside the taste's
`maxRange` — 26 to 30 m for these bodies — before it is ever weighed, and the five explorers keep to
a `homeRadius` of 28 to 34, so a body they cannot reach is invisible to the trace to the bit.

The conclusion is unchanged and the reason for it is narrower: **case 9's body would have been a
reachable one.** A river needs banks somebody walks on and a character standing on one of them, in
the ground the explorers cross. Both halves are asserted in
`tests/unit/test_route_pricing.cpp`, because a second fixture written on an untested premise is a
second fixture to keep in step for nothing.

`examples/labs/character/river-crossing.scene.json` is the new fixture: the same flat 240 m world,
a 14 m river (7 m half-width, as case 9 says) that is 1.40 m deep at the channel and **ends at
x = −50**, so there is a ford and there is a way round. `navWadeDepth` is 1.8, which is what makes
the channel walkable and the ford a real option rather than a wall.

---

## 6. The two sides of the crossover

ADR-182: a probe that cannot fail proves nothing, and a test that only checked the high-penalty side
would be exactly that. The same two routes, from (−70, −8) to (−66, 46), weighed by two tastes:

```
ford    54.15 m, 11.26 weighted wet metres
detour  84.56 m,  1.61 weighted wet metres

wadePenalty  0.4:  ford cost  58.65  score 0.4055  |  detour cost  85.20  score 0.3195   -> ford
wadePenalty 12.0:  ford cost 189.29  score 0.1745  |  detour cost 103.83  score 0.2781   -> detour
```

and on screen, in one 150-second run of one world with two bodies at those two tastes: the wader
crossed water **1.40 m** deep and never went east of x = −66; the drylander's deepest water was
**0.27 m** and it went east to x = −39. Both reached the far bank. At two seconds the overlay reads

```
wader:     north-bank = 0.4125   north-bank round = 0.3185   idle = 0.0500
drylander: north-bank = 0.1760   north-bank round = 0.3262   idle = 0.0500
```

which is case 9's "both scores in the overlay", through `IBehavior::decisionDebug` rather than round
the back of it.

**The detour is not bone dry and is not asserted to be.** It clips the shallow tip of the river
where the channel runs out, because 1.6 weighted wet metres at `detourPenalty` 40 is cheaper than
the sixty-odd dry metres of walking further round. That is the planner being right rather than the
fixture being wrong, and the arm claims the *ratio* — the ford is seven times as wet — instead of a
zero it would have had to engineer.

The third control is a world with no water at all: the same considerer over `guard-post.scene.json`
publishes **one** way, 54.15 m, 0.00 m wet.

---

## 7. What it costs, and the cap that keeps it bounded

Each destination is two A\* searches — `requestPath` is 24.969 µs on `glowmere-valley-2` (ADR-268) —
plus two grid walks of a few dozen lookups. With no authored `destinations` the goal model chooses
the places, and `maxDestinations` (4 by default) is a **hard cap and not a hint**: an explorer that
priced all twenty of its percepts would spend a millisecond of every decision tick on routes it was
never going to take. The candidates are ranked by `goalWeight` and the top few are priced.

No timing was taken for this record. The two figures above are citations, and the one number that
would have been new — what a `route` considerer costs per decision tick on Glowmere — is not
measured here because nothing in Glowmere carries one yet.

---

## 8. The distance is damped twice when the goal model chose the place

`score` multiplies the destination's own weight by the route's falloff. For an authored
destination that weight is 1 and the route price is the only term. For one the goal model chose it
is `goalWeight`'s answer, which already carries a **straight-line** distance damping — so such a
candidate is damped once by how far away it is and again by what the route costs.

This is a known simplification and it is recorded rather than fixed. Undoing it means re-deriving
`goalWeight` without its falloff, which is a second copy of the goal model and precisely the thing
ADR-333 §3 went to some trouble to have exactly one of. The two terms are monotone in the same
direction, so where they agree the ordering is unchanged; where they disagree — a near place behind
a river against a far one on this bank — the route term is the larger and wins, which is the case
this class exists for.

---

## 9. Consequences

* **Nothing that exists changes.** No scene in the repository declares a `route` considerer, and no
  behaviour gained a stage. `Navigator::requestPath`'s one-argument answer is bit-identical.
* **The Glowmere Valley 3 showcase is unblocked.** Its second demonstration — a jetpack alien
  crossing the river while a walking-only one routes around it — is two `route` considerers with two
  `wadePenalty` values, which is what `river-crossing.scene.json` already demonstrates with two
  aliens in one run.
* **Case 9 is runnable** and `examples/labs/character/cases.json` carries no `blockedBy` for it. The
  Character Intelligence Lab is down to one blocked case, P9's root motion, and
  `tests/unit/test_character_intelligence_lab.cpp`'s shape assertion moved from `blocked >= 2` to
  `blocked >= 1` to say so.
* **A destination the world does not have contributes no option**, rather than a silent fall back.
  It shows in the overlay as a missing line, which is the same rule `holdPost` follows for a post.

## Rejected alternatives

* **Score the straight-line distance and subtract a "water in the way" term.** It is cheaper and it
  is a second navigator: the term would have to know about banks, ends, bridges and islands, and it
  would disagree with the planner that actually walks the body. The arithmetic belongs to
  navigation, which is why §P11 left this rather than hurrying it into P3.
* **One option per destination, with the considerer picking the way itself.** It halves the option
  list and throws away the only thing case 9 asked for: an overlay with both scores on it, so "why
  did it go round" has an answer a person can read.
* **Price the ford at the character's own `wadePenalty` rather than at zero.** Then a character that
  already minds the water gets the same route back twice, and publishes one option at exactly the
  moment the choice is interesting. The probes have to bracket the taste, not sit on it.
* **Emit a single `Move` and trust the action layer.** Measured, in §3: the overlay said detour and
  the body forded.

## Revisit triggers

* A world where the driest route is genuinely unbounded — an island, or a river with no end inside
  the map. `detourPenalty` 40 will then return the same way as the ford and the considerer will
  publish one option, which is correct and is also silent about why.
* A second cost term worth bracketing the same way — slope, for a character that will not climb.
  The shape generalises and the two-request structure does not; a third probe is a third A\*.
* A character for which the double damping in §8 reads wrong -- one whose taste says "anywhere
  within 150 m" and whose routes are all long. The symptom is a flat option list.
* `maxDestinations` at 4 meeting a crowd. The cap is per character per decision tick and nothing
  budgets it across the world, which is the shape ADR-290 had to give occlusion.
