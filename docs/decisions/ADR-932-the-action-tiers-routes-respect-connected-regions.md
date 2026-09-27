# ADR-932: The action tier's routes respect connected regions

**Status:** Accepted
**Date:** 2026-09-27
**Follows:** ADR-093 (the navigation graph, its regions and `PathStatus::Unreachable`), ADR-096 (the
action tier and its path seam), ADR-296 (the reachability report), ADR-908 (the refuge walk), ADR-909
(the decider's habits)
**Found by:** the GV3 revision's gv3-cast stream (`phase3/cast.md`, "Found" 2): ember paced a river
bank through the drop. This stream's trace of the same film with `avgen_cast_trace --decisions`.
**Implemented by:** `NavigatorPath::route`, `RouteStatus::Nearest`, `edgeToward` and the `move` verb's
`Progress::shortOf` (`src/entity/action.{hpp,cpp}`); `NavGrid::regionNear` and `NavGrid::nearestInRegion`
(`src/entity/nav_grid.{hpp,cpp}`); the Director's goal check, `SceneFacts::walkable`
(`src/app/directing_context.hpp`)
**Tests:** `tests/unit/test_reachable_goals.cpp` (`[adr932]`: the route across an edge-to-edge river
against the planner's own verdict; a river that ends inside the world; a `move` to the far bank against
the old straight line); `tests/unit/test_urgent_retries.cpp` (`[adr932][adr933]`: a reaction to a beam
across the river)

## Context

Every `move` runs through `IPathProvider::route`, and the only provider is `NavigatorPath`. It
answered a goal a walker could stand on with the straight line, whatever lay between (`(void)from`).
Its own comment said why: the navigator had "no graph and no search". It has had both since ADR-093:
`NavGrid` labels the walkable set's connected regions and its search refuses a goal in another one.
The action tier used neither.

On GV3 the river runs from edge to edge, so it divides the valley's walkable ground: the nav grid logs
"4 disconnected pieces; 9805 of 20214 walkable cells (49%) cannot be reached from the largest". So a
goal on the far bank was `Ready`. The body walked straight at it, waded in to its limit (GV3 wades to
0.85 m), stalled, and gave up "stuck". This stream traced the film with every decision and action result
(`avgen_cast_trace --decisions`, engine `ec515c8b`, gv3-cast's iteration 2 with ember's reaction to E5
set back to "go and see"). From 170.9 s ember:

- went to see E5's beam across the river, and stalled at the water;
- was pulled home by its post (`range`) at 176.8 s, while stalled, so no turn-back rule applied;
- went to a glow patch across the water at 180.9 s, then to horse-11 across the water at 183.4 s, which
  failed "stuck" at 197.4 s;
- went home at 199.0 s, back to the glow at 203.4 s, home at 209.3 s, the glow at 212.6 s, and home at
  220.1 s.

Six stops running were turn-backs: at the water 121, 116 and 129 degrees, at the south end 172, 173 and
176 degrees (ADR-933 explains the angles). The same trap made it dither on that bank at 22-52 s.

gv3-world met the same fact from the other side (`phase3/world.md` W2b, W2c). Ending the river in a
pool joined the banks round its head, and every alien's route changed.

## Decision

**With a navigation graph, a goal in another connected region is answered with a route to the nearest
point of the walker's own region, `RouteStatus::Nearest`.** Within one region, and in a world with no
graph, the answer is the straight line, exactly as before.

- **Regions are placed the way the planner places them.** `NavGrid::regionNear` takes the region of the
  point's own cell, or else the nearest walkable cell's within reach. The reach is 3 cells for the
  walker and 4 for the goal, the snaps `NavGrid::path` makes for its start and its goal. So "different
  regions" here and `PathStatus::Unreachable` there are one statement. If the graph cannot place an end
  (for example, a body far off the walkable set, which ADR-908's refuge walk brings back), that is not a
  claim about regions, and the route stays the straight line.
- **The nearest point, not the nearest-looking point.** `NavGrid::nearestInRegion` searches outward in
  rings and stops only when no further ring can hold a nearer cell centre. (`nearestWalkable` stops at
  the first ring that holds one, and can miss by up to a third of the distance.)
- **Dry ground first.** The search takes the region's nearest cell with no water over it. Only a region
  with no dry cell falls back to a wet one. `edgeToward` then steps from that cell centre toward the
  goal every half metre, for at most one cell, while the ground stays walkable and dry. The body
  therefore stops at the water's edge, not in the middle of the last dry cell, and a walker that may wade
  does not end an errand it cannot finish standing in the river that stopped it.
- **A walk that goes as near as it can says so.** The `move` verb walks to the substitute. Its last
  waypoint does not track the goal (`Progress::shortOf`). When it arrives, the move **fails** with
  "unreachable: no way across to it; went as near as it could". The rest of the action list still runs,
  as it does after any failed action. So a reaction's face and look happen from the bank, which is the
  brief's "walks to the bank and watches from there".

**Why walk to the bank and then fail, rather than refuse outright or report success.** The brief allows
either: route to the nearest reachable point, or refuse with a reason when there is none. Refusing at
once would stop a reaction dead, so a body that hears a beam across the river would turn to it where it
stands and never walk toward it. That is less alive than going to the water's edge, and the brief asks
for the walk. Reporting the arrival as a success would be a lie to every reader that takes a completion
as arrival:

- staging's `Walk` step (`actionFailed`) would read "the saucer walked into the lake" as "it got there";
- an action's `onComplete` and a Director goal's `goal.arrived` would fire at the wrong bank;
- a decider would mark an errand it never reached as investigated.

A failure after the walk tells each of them the truth and keeps the walk. It also feeds the memories
that stop a retry: the decider's failure exclusion, and ADR-933's attempts. The one case with no nearer
point is a body already standing at its region's nearest point. Its "route" is zero metres long, so the
move fails at once with the same reason. That is the "refused with a reason" half of the brief, which
falls out of the same rule.

**Everything that calls `route` inherits this:**

- **The `move` verb** (`ActionQueue`), whatever pushed the move:
  - the decider's options on the Routine tier: `interest` errands (GV3's roam, graze and watch),
    `investigate`, `react`'s approach and flee, `social`'s greeting (a body target) and stepping away,
    `holdPost`'s return, `goal` (ADR-824's runtime goals), each leg of `route`'s errands (each leg is
    already within a region, so these are unchanged), and ADR-909's `stroll`;
  - schedules (Routine) and one-off actions (Action tier), including a sequence's section actions;
  - the Director tier: staging's `Travel::Walk` steps and directed performances' orders.
- **The Director's goal check.** `SceneFacts::walkable` asks `route`, as the goal's walk will.
  `Nearest` answers "no walking route", so a goal across water now draws the validator's warning "the
  goal may never be reached". Before, it said nothing.
- **Not affected:** `wander` and `explore` choose their own destinations through the navigator (`explore`
  already routes with the planner), and the world editor's route overlay only draws what the queue
  walks. It now draws the line to the bank.

## Consequences

- **A goal across an edge-to-edge river** (the test fixture: a 12 m, 2.5 m-deep channel, walkers wading
  to 0.8 m, a 4 m grid):
  - The route is `Nearest`. Its end is on the near bank, dry, across from the goal, and within half a
    metre of the water.
  - A `move` there walked 17.3 m in 9.13 s, stood at (17.26, -1.90) in no water at all, failed
    "unreachable: ...", and the `face` after it turned the body to the far bank.
  - The old straight line walked into the channel to 0.87 m and gave up "stuck" at 18.32 s.
  - The planner's verdict on the same pair is `Unreachable`, and now the action tier agrees.
- **Same-region routes are unchanged to the bit.** The fixture's goal on the same bank, and the same
  river ending inside the world, both still get the straight line. The Character Intelligence Lab's
  river crossing (one region, a ford) is untouched: `[route]` passes unchanged (2825 assertions).
- **On the films** (whole film, 226 s at 60 fps, `avgen_cast_trace --decisions` and
  `avgen_character_quality`; before `ec515c8b` plus this stream's measurement-only commit, after this
  branch; GV3 is a scratch copy of gv3-cast's iteration-2 project, and "go and see" is the same copy
  with ember's reaction to E5 back at `approach` 18):
  - **GV3, go and see:**
    - ember's loop on the bank is gone: reversals 3 -> 0; stops walked out back the way they came
      10 -> 3; its longest run of those, 6 over 28 s from 176 s -> 2 over 5 s; A->B->A 3 -> 1;
    - all five aliens together: reversals 5 -> 1, turn-backs 25 -> 20;
    - 6 errands now end at a bank, failing "unreachable: no way across to it", where before an errand
      across the river walked into the water; failures "stuck" 4 -> 3.
  - **GV3 as generated** (E5 heard as "stop and watch"): aliens' reversals 3 -> 2, turn-backs 20 -> 19;
    5 errands end at a bank; ember's reversals 1 -> 0.
  - **GV2-multicam:** 4 errands end at a bank. Past the first abduction the aliens' decisions diverge
    (ADR-934 changes what they perceive from 14.9 s), and their totals moved both ways: reversals
    7 -> 6, turn-backs 27 -> 32, A->B->A 2 -> 6. ember's longest turn-back run went 2 -> 4 (149-203 s).
    It is its post pulling it home against errands on its own bank, with no errand across the river in
    it (its one reaction, at 158.4 s, was taken once and not again). That is ADR-909's ground, and
    GV2-multicam's frozen file cannot set `maxStillSeconds`.
  - **The shape it leaves:** a roam errand to something across the river now walks to the bank and ends
    there. On GV3 as generated, ember at 22-52 s made three errands across the water (failing at 30.5,
    37.4 and 42.5 s), each a short walk ending at the bank. Before, it waded in: its easternmost point
    in those 30 s was x -49.0, and is now -50.4. The decider still offers such destinations, because
    `interest` does not ask the graph. `route` already refuses them through `requestPath`. That is a
    follow-up, not done here.
- **Scenes whose behaviour changes:** any scene whose navigation graph is in more than one region
  (its log says "the walkable ground is in N disconnected pieces"), where something is sent across a
  divide. Checked: GV3 and GV2-multicam, which share the valley world and log the same 4 pieces (9,805
  of 20,214 walkable cells cut off from the largest). The rest of the GV2 family is built on the same
  world and presumably changes the same way. Scenes with one region are unchanged.
- **Also visible to authors:** the Director's validator now warns about goal performances across a
  divide, and a staging walk across one fails at the bank instead of in the water.
- **Open:** a goal in the walker's own region whose straight line crosses water (the fixture's river
  ending inside the world, GV3 before gv3-world's W2c) still walks the straight line into the water.
  The fix for that is a searched route (`Navigator::requestPath`), which moves every same-region route
  in every scene; it is not done here. ADR-933 is what keeps a reaction from pacing there. An
  entity-target `move` (a greeting) is routed once, so a target that crosses the divide after the walk
  starts is still walked toward in a straight line.
