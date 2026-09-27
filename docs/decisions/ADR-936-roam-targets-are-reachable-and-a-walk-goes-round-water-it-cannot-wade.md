# ADR-936: Roam targets are reachable, and a walk goes round water it cannot wade

**Status:** Accepted
**Date:** 2026-09-27
**Follows:** ADR-093 (the navigation graph, its regions and its planner), ADR-096 (the action tier and
its path seam), ADR-195 (wading), ADR-333 (the decider's considerers) and ADR-336 (the `route`
considerer), ADR-908 (walk-through turns), ADR-932 (routes that respect connected regions; its "Open"
item), ADR-933 (an urgent option's one attempt)
**Found by:** navfix's trace of GV3 (its report's "Defects found, not fixed"), and this stream's traces
of the films
**Implemented by:**
- `IPathProvider::reaches`, and `NavigatorPath::reaches`, `crossesDeepWater`, `acrossDivide` and
  `route` (`src/entity/action.{hpp,cpp}`);
- the `move` verb's routes of several waypoints: `kLegReach`, `routeLeft`, arrival on the last leg,
  and a route round collapsed when the body already stands within its tolerance (`ActionQueue::update`,
  `src/entity/action.cpp`); `kMoveTolerance` (`action.hpp`);
- `InterestConsiderer::consider`'s filter and `reachableStand` (`src/entity/decision.{hpp,cpp}`).

**Tests:** `tests/unit/test_reachable_roams.cpp` (`[adr936]`: no place across a divide is offered,
against the same place where the banks are one region; a place whose stand-off is in the water is
walked to standable ground beside it, against the raw stand-off's refused route and a place with no
standable ground in reach; a move across a channel in one region walks round its head, against the
straight line; a scrub part way round); `tests/unit/test_reachable_goals.cpp` (`[adr932][adr936]`, the
river that ends inside the world, re-baselined); `tests/unit/test_urgent_retries.cpp` (the stall case,
re-baselined)

## Context

ADR-932 made a walk to a goal across a divide end at the bank, and left two things open that GV3 went
on showing.

**`interest` offered places a walk cannot end at.** It is GV3's roam, graze and watch, and it scored
every place its body had noticed with no question about the way there. ADR-932's `route` then answered
the walk, and two answers were failures the decider had chosen:

- **Across a divide** the walk went as near as it could and failed "unreachable: no way across to it":
  ember, three such errands at 22-52 s on navfix's trace of gv3-cast's iteration 2, and four on
  `gv3/production` `a6020f39` (30.5, 37.4, 42.5, 156.2 s).
- **Where the stand-off was not ground a body can stand on** the move failed on its first step
  ("unreachable": `route` refuses a goal no walker can stand on): ember, four in its first 33 s on
  navfix's trace -- two glow patches by the bloom, then the bloom's own spores and cap, whose
  stand-offs fall in the hero's footprint and the scatter round it -- and on `a6020f39` ten in the
  film (ember nine, vane one).

Each is a walk that goes nowhere, and with ADR-935's "before" each left the legs running on the spot
for half a second. The `route` considerer has never offered a destination the planner cannot reach
(ADR-336); `interest` was never asked.

**A same-region goal across water was walked straight at.** Within one region `route` answered with the
straight line, and local steering cannot get round a lake (the planner's header says so). On ADR-933's
fixture -- a channel whose banks join round its head, walkers wading to 0.8 m -- a `move` from (0, 0) to
(40, 0) walked into the channel's margin at 9.5 s, dithered at its wade limit (0.87 m deep) turning,
braking and pivoting, and gave up "stuck" at 18.2 s: the stuck clock resets whenever the body gets 5 cm
nearer the goal, and each creep deeper was such a step. ADR-932 named the fix ("a searched route")
and left it.

## Decision

**1. `interest` offers only a place its walk can end at.** Each candidate's stand-off is asked the
action tier's own questions before it is an option: a walker can stand there, and it is not across a
divide from the body (`IPathProvider::reaches`, which is `route`'s two refusals without building the
route, so the considerer and the walk it pushes cannot disagree). A stand-off that is not standable
moves onto standable ground within the walk's own arrival tolerance -- rings a metre apart, twelve
bearings a ring, the first ring with any, the point in it nearest the walker -- because arriving within
that tolerance is what the walk already means by arriving. The look at the end of the errand is still
at the thing itself. A candidate with no such ground, or across a divide, is not offered.

- **Why not offered, rather than walked to the bank.** A roam is chosen for its place, and the bank is
  not the place. ADR-932's walk to the bank is right for a reaction -- see the beam from the water's
  edge -- and stays: it is `react`, not `interest`.
- **Cost.** Two analytic samples and two grid lookups per candidate per decision tick, and up to 48
  more samples for a stand-off that is not standable. `candidates()` -- the goal model `explore` shares --
  is untouched.

**2. Within one region, a walk goes round water it cannot wade.** `NavigatorPath::route` asks
`crossesDeepWater`: the straight line sampled every metre against the world's own rule for this walker
(`Submerged`, and water deeper than its wade band where the slope rule fired first). When it does, the
answer is the planner's route (`Navigator::requestPath` at its default price, so a ford it would take is
taken and a channel it would walk round is walked round): several waypoints, `Ready`. A dry straight
line is the straight line exactly as before, and so is every answer the planner cannot better (no
route inside its budget, an end it cannot place, a route that is the straight line after all).

**3. The `move` verb walks a route of several waypoints.** Until now `route` only ever returned one.

- A corner is passed within a leg's reach -- 2 m, half a grid cell, or two of the body's turning
  circles -- not within the goal's `tolerance`, which for a post is 15 m and would cut the corner back
  into the water.
- The walk is over on its last leg only, and what is left of it is the route's length (`routeLeft`):
  the braking into the goal and the stuck clock read that, not the straight line. A walk round a
  channel heads away from its goal for a while and is getting somewhere all the time; measured by the
  straight line it would have been "stuck" after four seconds.
- A route round to a goal the body already stands within its tolerance of is not walked: the straight
  line had it arrive on its first step, and still does.
- A route of one waypoint -- every route before this ADR -- is walked exactly as before.

**Why round, not "fail fast".** Refusing a wet straight line would turn every walk that local steering
did get round (a small pond, a bend in a stream) into a failure, and would call unreachable a goal the
planner can reach. The planner exists (ADR-093) and already prices wading (ADR-195); the `route`
considerer has walked its routes since ADR-336, one `move` per leg. A single `move` now can.

**What this does not do.** `social`'s greeting, `investigate`, a post and `react` still push walks
without asking `reaches` first: across a divide they walk to the bank (ADR-932), which for a reaction is
the point. A reaction's approach or flee to ground no walker can stand on still fails at once (on GV3,
sage's flee at 171.1 s before this ADR; tide's approach to E5 at 171.3 s after it, with "go and see").
An entity-target walk is still routed once, when it starts.

## Consequences

- **On the fixtures** (`test_reachable_roams.cpp`; the three new cases fail on the code before this
  ADR, 10 assertions -- `build/behave-runs/behave-adr936-control.log`):
  - A river edge to edge: the glow on the walker's bank is offered with the walk it always had; the
    glow across it is not offered (before: offered, and its route is ADR-932's `Nearest`). Control:
    where the river ends inside the world the far glow is reachable round the head and is offered.
  - A glow whose stand-off, 6 m this side of it, is in water no walker stands in (its route
    `Unreachable`, the old option's failure on its first step): offered, its walk ending on standable
    ground 3.0 m nearer the walker, within the walk's 3 m tolerance, its look still at the glow. With no
    stand-off the same place holds no standable ground within 0.75 m and is not offered.
  - A `move` across a channel in one region: 120 m round the head, never deeper than 0.31 m (the
    walkers wade to 0.8), arrived at 60.35 s and never stuck, although for 35.5 s of it the walk took
    it no nearer the goal in a straight line. The straight line: into the channel to 0.87 m, 78 s in
    water over 0.5 m, "stuck" at 18.2 s. A scrub part way round lands on the play.
- **Re-baselined, with the reason in each:** `test_reachable_goals.cpp`'s river that ends inside the
  world (its route goes round the head, every leg dry of the channel; the straight line is the control)
  and `test_urgent_retries.cpp`'s stall (its subject is the retry memory after a stall, and the stall
  is the straight line's, so both arms walk it).
- **On the films** (the runs ADR-935 describes; the numbers are both ADRs together):
  - **The aliens' failed errands:** GV3 as generated 17 -> 5 ("unreachable" 11 -> 0, "no way across
    to it" 4 -> 0); GV3 go and see 18 -> 9 ("unreachable" 12 -> 1, tide's reaction to E5, which is
    `react`'s; "no way across" 4 -> 0); GV2-multicam 17 -> 0. What is left on GV3 is three to four
    greetings that chase an alien walking away and give up "stuck", and ember's "blocked" in one
    pocket of shallows (below).
  - **No route round water was needed on GV3:** a diagnostic replay of the film as generated, to
    180 s, planned none. Its river divides the valley (ADR-932's `Nearest`), and none of its
    same-region straight lines crosses water deeper than 0.85 m. The route round matters where banks
    join: GV3 before gv3-world's closure, the fixtures, the labs' channels.
  - **Trajectories diverge from the first place no longer offered** (GV2-multicam: ember at 98.4 s
    took a reachable glow instead of vane, whose stand-off no walker could stand on). The aliens'
    totals, before -> after: GV3 as generated reversals 5 -> 4, turned back 22 -> 22, A->B->A 4 -> 2;
    go and see 4 -> 3, 20 -> 22, 2 -> 2; GV2-multicam 6 -> 5, 32 -> 35, 6 -> 4. The longest pacing
    runs (ADR-933's measure): GV3 rook 6 over 64 s from 148 s -> 1 (as generated), sage 1 -> 2 and
    tide 2 -> 3 in both GV3 variants, GV2-multicam ember 4 -> 6 and rook 3 -> 4. Each new run is a post
    pulling the body home after an errand it completed on its own bank (ADR-909's ground, and
    GV2-multicam's frozen file cannot set `maxStillSeconds`), not a place it could not reach.
  - **The longest stands** mostly fall where they were long (GV2-multicam: ember 10.1 -> 3.6 s, rook
    7.1 -> 3.8 s, tide 14.9 -> 9.1 s, vane 19.9 -> 14.0 s; GV3 sage 13.4 -> 8.3 s) and rise a little
    where they were short (GV3 rook 4.2 -> 6.8 s, tide 4.3 -> 6.9 s, ember 6.1 -> 7.4 s).
  - **A pocket this stream did not fix:** on the trajectory ADR-936 gives it, ember walks home at
    159 s along the west bank's shallows, into a pocket at (-54, 57-59) between the bank's foot and the
    channel, and every walk from there fails "blocked" until 180 s (two errands as generated, four with
    "go and see"). These are straight-line walks, and the pocket is walkable ground from which the
    steering fan finds no clear way; `refuge` only helps a body off the walkable set.
- **Scenes whose behaviour changes:** every `interest` decider in a world with a navigation graph --
  the GV2 family, GV3, the Character Intelligence Lab's roamers, the autonomy demo -- no longer
  chooses places across a divide or with nowhere to stand near them; and every walk in one region
  whose straight line crosses water deeper than its walker wades goes round it.
