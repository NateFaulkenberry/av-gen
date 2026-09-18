# The Character Intelligence Lab

**Question**: *Why is this character here, facing this way, and why did it decide to be?*

Registry entry: `LabId::Character`, key `character` (`src/labs/lab.cpp`).
Fixtures: `examples/labs/character/character-intelligence-lab.scene.json` (the explorers, frozen —
see §2), `examples/labs/character/guard-post.scene.json` (the decided characters),
`examples/labs/character/river-crossing.scene.json` (case 9's ford and detour) and
`examples/labs/character/perception-crowd.scene.json` (the occlusion budget).
Cases: `examples/labs/character/cases.json`, reachable as `avgen --lab-case character:<n>`.
Tests: `tests/unit/test_character_intelligence_lab.cpp`, `tests/unit/test_character_lab_sockets.cpp`,
`tests/unit/test_entity_perception.cpp`, `tests/unit/test_entity_decision.cpp`,
`tests/unit/test_decision_extraction.cpp`, `tests/unit/test_route_pricing.cpp`.

Companion reading, in this order: `docs/character-ai-research.md` (what already exists),
`docs/character-ai-plan.md` (the units and who owns which file), ADR-266 to ADR-275, then ADR-290
(perception, built), ADR-333 (the decision layer, built) and ADR-335 (route pricing, built).

---

## 0. What this lab owns, and what it does not

It owns **what a body knows, what it chooses, where it walks, and where its sockets are**:
perception, the decision layer above `ActionQueue`, navigation and stuck-detection, and the joint a
socket resolves to.

It does **not** own what the joints do once the entity is placed — that is the Animation Lab — or
whether the body reached a draw call — that is the Visibility Lab.

`decides` named `src/entity/behaviors.cpp:Explore` for as long as **nobody decided** (ADR-269), and
that entry carried its own expiry: "when P3 extracts the goal model out of `Explore`, the registry
test fails and this entry moves. That is the intended failure." It did, on 2026-09-18, and `decides`
is now `src/entity/decision.cpp:Selector` (ADR-333). `Explore` still *has* a goal model; it calls
`entity::goalWeight` for it, and the class that chooses between courses of action is the selector.

---

## 1. One of the fifteen cases cannot be run, and says so

The owner's brief asked for six: basic wandering, investigating a mushroom, crossing a river, a
social interaction, a dense environment, a long-running simulation.

| # | scenario | state |
|---|---|---|
| 1 | a socket on a hand is on the hand | runnable |
| 2 | three explorers, three seeds, three routes | runnable |
| 3 | a route round what is in the way | runnable |
| 4 | a penned character is stuck and nothing says so | runnable |
| 5 | two minutes, and the scrub against the play | runnable |
| 6 | every character *used to* see everything, and nothing scored an option | runnable — the before-arm, kept |
| 7 | investigating a mushroom | runnable — *unblocked by ADR-333* |
| 8 | a social interaction | runnable — *unblocked by ADR-333* |
| 9 | crossing a river | runnable — *unblocked by ADR-335* |
| 10 | two characters, two ranges, two different worlds | runnable — the after-arm (ADR-290) |
| 11 | the occlusion budget, and the arm at zero | runnable (ADR-290) |
| 12 | a head that turns while the legs keep walking | runnable — *unblocked by ADR-300* |
| 13 | half a metre of landing the engine throws away | blocked on **P9 root motion** |
| 14 | a guard, and no C++ class called Guard | runnable (ADR-333) |
| 15 | seven hundred lines moved and the route did not | runnable (ADR-333) |

Cases 7 and 8 were blocked on *two* units, then on one, and are now runnable. That is what a
`blockedBy` is for: the day a unit lands, the cases that were waiting on it are a list rather than a
memory, and unblocking half of one is deleting half of a string.

**Case 9 did not unblock when P3 landed, and its blocker was rewritten rather than deleted.** P3
landed a selector, an `Option` list and four stock considerers, so "there is nothing that scores"
stopped being true — but none of the four priced a *route*, and preferring a longer dry way to a
short wet one is exactly that. A case whose `blockedBy` is removed on the grounds that the unit it
named finished is a case that passes by asserting nothing, which is the failure ADR-275 exists to
prevent, so `docs/character-ai-plan.md` §P11 was added by P3 and the case waited for it. **P11
landed as ADR-335** and case 9 is now runnable: a fifth stock considerer, `route`, that asks
`Navigator::requestPath` twice at two `NavPathCost::wadePenalty` and scores both answers with the
character's own price on a wet metre. Thirteen of the fifteen cases were runnable before it and
fourteen are now; case 13 and P9's root motion are what is left.

A blocked case carries `blockedBy` naming the unit of `docs/character-ai-plan.md` that unblocks it
(ADR-275). Case 11 is the first blocked on something other than perception or a decider, and the
registry's own check widened with it: it asserts that `blockedBy` names *a* unit rather than one of
the two that happened to exist on the day it was written. `--lab-case character:7` refuses and prints the unit rather than opening the fixture: a
person who came to watch a character investigate something would otherwise be shown a scene in which
that is not happening, and left to work out why.

Case 9 was split on purpose and the split held all the way through. The *route* half of crossing a
river was answerable as soon as P7 landed — `NavGrid` prices a ford and `NavSample::waterDepth` is
carried for exactly that. The *choice* half was not, because preferring a longer dry route to a
short wet one is a score and nothing scored. So it was blocked on P3 and not on P7, and when P7 made
the route observable the first half unblocked without the second — exactly as the split predicted.
P3 then landed the scoring machinery and the second half *still* did not unblock, because scoring a
place is not scoring a way. That is three units the split kept honest, and §P11 is what it cost to
say so rather than to quietly let the case pass.

---

## 2. The fixtures

**Three of the four exist because the first one is frozen, and the reason it is frozen is the
golden trace.** `character-intelligence-lab.scene.json` is
frozen: `tests/data/explore-position-trace.txt` is 3,600 samples of its five bodies taken from the
build before ADR-333's extraction, and a sixth body in that scene changes what the other five
perceive and score. So the decided characters live in `guard-post.scene.json` — a flat world, four
cairns, a guard at a post, a courier patrolling a line under authored `move` actions, and an
explorer far enough away to be out of everybody's senses — and case 9's river crossing lives in
`river-crossing.scene.json`.

**How frozen the first fixture actually is, measured** (ADR-335 §5). The claim "a body added to it
changes what the other five perceive and score" is half true, and the half that is false is worth
knowing before the next case writes a fourth fixture:

| one hero stone added to the lab fixture | of the golden's 3,600 samples |
|---|---|
| at (−40, 92), 90 m away across the river | **0 differ**, worst 0.000 m |
| at (6, 10), 6 m from `scout` | **1,779 differ**, worst 13.685 m |

`goalWeight` rejects a point outside the taste's `maxRange` — 26 to 30 m for these bodies — before
it is ever weighed, and the explorers keep to a `homeRadius` of 28 to 34. So the fixture is frozen
against a body they can *reach*, and only against that. It is still the right call for case 9,
because a river case needs banks somebody walks on and a character standing on one of them.

The cost of the split is worth naming: guard-post has no ecology and therefore no `Glow` interest
point (its 8 points are 5 landmark and 3 character; the lab fixture's 141 are 28 landmark, 5
character and 108 water). Case 7's mushroom is a scripted percept for that reason, and its live arm
is a `Character` percept on the identical code path.

### 2.1 The original fixture

Twenty nodes, five entities, thirteen heroes, and every element in it earns its place as an arm or
a control.

* **`scout`** — `alien-scout.glb` at **3.61×**, the scale Glowmere draws its aliens at. Carries four
  sockets, and the scale is not decoration: a joint offset is in the asset's own units, so a socket
  that ignores the node scale is wrong by the scale, and a fixture at 1.0 could not have shown it.
  * `hand` → joint `hand.r` — **the arm.**
  * `head` → joint `head.x` — a second joint, so the answer cannot be one constant.
  * `chest` → no joint, authored offset — **the control**: it must resolve against the entity frame
    *with the skeleton installed*.
  * `tail` → joint `tail.x`, which no rig here carries — **the second control**: a typo used to be
    indistinguishable from a working socket.
* **`lantern`** — a node attached to `hand`. The end of the chain; where it is drawn is the result.
* **`rover`**, **`diver`** — two more explorers, two more seeds. The wandering arm.
* **`penned`** — the same `explore` behaviour, standing inside ten hero stones on a 6 m ring whose
  solids overlap by 0.26 m, with a home radius five times the pen. The stuck arm; `rover` is its
  control.
* **`watcher`** — the animation-layer arm (ADR-300). The same alien, walking, carrying two layers on
  top of whatever clip the gait machine picked: an **aim** layer masked to the five joints that are
  this rig's head, driven by `LocomotionState::lookTarget`; and an **additive** layer masked to the
  upper body playing `Fight_head_hit`, driven by `LocomotionState::reaction`. It runs `lookAt` at
  `boulder-b`, which while travelling publishes a look target and deliberately does not turn the
  body — the case the seam was written for and that nothing consumed until now. Five names rather
  than one because `head.x` has **zero children** on this asset and the eyes, mouth and antenna are
  its siblings: the mask "head.x and its descendants" covers 1 joint of 90.
* **`boulder-a/b/c`** — solids in the open, so a route has something to plan around.
* **Four sets of senses, one per body** (ADR-290). Every entity declares a `perception` block and
  no two are the same, because the question case 10 asks is whether what a character notices depends
  on the numbers its author wrote:
  * `scout` — 60 m, 200°. The far-sighted arm.
  * `rover` — **12 m**, 200°, the same capacity and the same cadence. The short-sighted arm; the
    only difference from `scout` is the range.
  * `diver` — 60 m, **90°**. The blinkered arm; its own control is the same body driven to 360°.
  * `penned` — 60 m, 360°, and the only body that pays for occlusion (2 tests a second). Ten
    seven-metre stones on a 6 m ring is the one place in this fixture where a sightline has
    something to be blocked by.
  Capacity is 24 rather than the default 8 on all four, deliberately: at 8 both `scout` and `rover`
  would hold full working sets and "different ranges" would be two lists of eight.
* **A flat world, authored explicitly.** `layers: []` and `features: [<one river>]` rather than an
  omitted `world` block, because an omitted one keeps the shipped world's designed landscape
  (`world_map.cpp:671`). Everything this lab measures is about what a body knows and chooses, and a
  procedural landscape would make every number a measurement of the terrain as well. The one feature
  is a 14 m river with 1.6 m of water. Case 9 ended up with a river of its own rather than this one
  — see §2.2 — but this one is why the world block is authored rather than omitted.

All four bodies run `explore` rather than `wander`, and that is not cosmetic: `wander` implements no
`navDebug`, so a fixture built on it can say nothing at all about why a character is going where it
is going. `Explore` is also the only autonomous mind in the engine, which makes it the thing the lab
is for.

### 2.2 The river-crossing fixture (case 9)

`river-crossing.scene.json`, added by ADR-335. The same flat 240 m world and the same authored
`world` block, with one difference that is the whole fixture: **the river ends inside the map**, at
x = −50, so there is a ford and there is a way round and the choice between them exists.

* **The channel** — 14 m wide (7 m half-width, which is the number case 9 has always quoted),
  **1.40 m deep** at the centre, tapering to nothing over about ten metres either side. Shallower
  than the original fixture's, deliberately: at 1.6 m of water over a 2.2 m channel the bed is
  3.80 m down, which is deeper than any plausible `navWadeDepth` and means there is *no* ford. A
  river a body cannot cross is a river the case cannot be about.
* **`navWadeDepth: 1.8`** — what makes the channel walkable at all. At the default of 0 every water
  cell is unwalkable, `NavCell::wade` is 0 everywhere, and both requests come back with the same dry
  detour.
* **`wader`** and **`drylander`** — the same C++ class, the same `decide` behaviour, the same
  destination and the same two considerers. One number differs: `route/wadePenalty`, 0.4 against
  12.0. That is the arm and the control in one run of one world, and it is also the shape the
  Glowmere Valley 3 showcase needs — a jetpack alien crossing the river while a walking-only one
  routes around it.
* **`plodder`** — the third body, 26 m west of the other two and out of their way. No decider at
  all: one authored `move` action straight at the far bank, which is exactly what an option whose
  single action named the destination would have emitted. Its deepest water is **1.40 m**, the full
  channel, against the drylander's 0.27 m. That is the control for a design decision rather than
  for a taste — it is why the winning option's actions are a `Move` per waypoint.
* **`north-cairn`, `south-cairn`, `ford-marker`** — three heroes, which are also the interest points
  the arm that names no destination scores. With `minRange` at 45 m they are the three nearest
  things worth walking to and all three are across the water, which is what makes that arm a probe
  that could have failed.

---

## 3. What the fixture measured that nothing had

**A socket answered the body's origin and called it a joint.** `Entity::setSkeleton` had zero call
sites, so every socket in this engine took the fallback and returned `true` on it. ADR-274. The hand
socket sits 2.878 m from the body's frame with the skeleton installed and 0.000 m from it without;
the two jointed sockets agree bit-for-bit when the skeleton is removed, which is the signature of a
fallback and is what the good arm must not have.

**The node's scale was never part of a socket.** Measured at two scales of the same pose: 2.8785 m
at 3.610× and 1.4392 m at 1.805×, exactly 2.000×.

**The GPU palette is not the pose.** `palette[k] = model[palette[k]] * inverseBind[k]`, and its
translation is not where the joint is — **0.9112 model units** apart on `hand.r`, on a 1.66-unit
character. ADR-260 warned; this is the number.

**A hero floating above the ground is an obstacle a walker passes underneath, and the log says it is
blocking.** This one cost a wrong conclusion. A hero's `position.y` is the obstacle's *base*
(`entity::obstaclesFromHeroes`), and the first version of this fixture authored its heroes at y = 0
over the shipped procedural world, whose ground there is **−7.46 m**. Thirteen blocking solids were
indexed, `terrain 'ground': 13 navigation obstacles (13 blocking)` was printed, and every one of them
sat 7.46 m over the walker's head. `penned` walked out of a sealed pen and **137.86 m** across the
map, and the honest-looking reading of that measurement — "obstacles do not contain a character" —
was false. The fixture is a flat world now for exactly this reason, and the cost of learning it is
the reason this paragraph exists rather than a one-line fix.

**Grounded, the grid does know.** 54×54 at 4 m: 2,916 cells, 2,517 walkable, 380 water, **19
blocked**, **3 regions**, largest 2,300. `NavGridStats::regions` is computed at build and read by no
UI (ADR-268); this lab is the first thing in the repository that asserts on it.

**There is no stuck detector, and the engine knows perfectly well.** `requestPath` from the middle of
the pen to the open world returns `PathStatus::Unreachable` with 0 waypoints; the same request from
just outside returns `Ok` with 2. The status is correct and is surfaced to no UI. `Explore` carries a
`stuckSeconds` watchdog and uses it to replan without publishing anything. And `penned` does not even
walk — every destination it could pick is outside the wall, `pickDestination` rejects all of them, and
it idles for two minutes. **Being stuck and being idle produce the same frame and the same log.**

**A percept is stale, and that is the feature.** The first version of the perception arm asserted
that a Character percept sits exactly on the body it names. It failed by **0.225 m** — one 4 Hz
cadence period of `rover` walking at 1.5 m/s — and the assertion was wrong rather than the engine.
Staleness is the only thing that lets a character be *wrong* about where something is, which is most
of what a sense model buys over the omniscient list it replaces (ADR-270 §4).

**The cost of a sightline is a property of the world function, not of the nine rays.** ADR-270 priced
`world::heroSightline` at 1720.779 µs at 20 m and built the whole occlusion budget on it. Measured
again here: **1800.6 µs at 20 m and 5876.0 µs at 60 m on `glowmere-valley-2`**, which confirms it —
and **0.008 ms on this lab's flat fixture**, two hundred times cheaper, because a flat world with no
ecology makes the march's per-sample world lookup nearly free. The budget is justified by the shipped
world and **a fixture cannot prove it**. Load average 6.6–7.9, minima of 5.
**Fixed 2026-09-18, ADR-296.** `NavDebug` now carries `confinedFor` and `stuckFor` and the route
overlay prints them; the selected walker's label also says which region it is standing in and whether
its destination is reachable from there. One line above needs correcting: "the status is correct and
is surfaced to no UI" was already out of date when it was written — ADR-197's route overlay prints
`PathStatus` in the label and flags the route failed. So a penned body that *plans* was visible. The
body that was invisible is the one that never plans, which is what the two new fields are for; the
stranded-cell count (`NavGridStats::stranded`) is in the panel beside the region count.

**The scrub does not match the play for an explorer.** 1.360136 m at 30 s, inside the 90 s replay
window where ADR-091 demands they agree and where ADR-267 measured 0.000022 m. Not a regression and
not a contradiction: `EntityWorld::seek` (`entity.cpp:739`) re-simulates a strict *subset* of
`update` — `behavior->update` and nothing else, no interest points, no crowd field, no gait, no
action queue, no `applyOffsets` — and leaves `BehaviorContext::self` at 0 for every body. `Explore`
reads all of those. ADR-267 §4 already lists these as P1's four seek defects; this is what they cost
on a cast of explorers. What does hold, and what nothing had checked, is that seeking twice to the
same second lands in the same place.

---

## 4. The overlay this lab still owes

`labs::overlaysFor(LabId::Character)` turns on entity origins, entity bounds, the transform trail and
the skeletons. What it cannot turn on is **the losing option scores beside the winner** — but the
reason has changed. There are options now: `IBehavior::decisionDebug` carries every option scored on
the last decision tick with its name, its score and which one was chosen, plus the tick, the dwell
and margin refusal counts and how many percepts the fade is holding (ADR-333). It is read by the
tests and by nothing in `src/ui/`, which is the same shape as the thing this section was written
about. Drawing it is an afternoon against a seam that exists. What it *could* also turn on, and does
not yet, is a body's working set:
`Entity::percepts()` carries what each character noticed, with a distance, a salience, a `seenAt`
and an honest `tested` flag (ADR-290), and nothing draws it. `NavDebug` already carries route, leg, destination, status, phase, goal name
and goal kind through `IBehavior::navDebug`; `decide` reports the queue's route through the same
seam, because a decider owns no route of its own. Drawing any of it is this lab's.

The navigation overlays are the editor's (`WorldEditor::showNavRoute` and friends) and are not part
of `rendering::DebugViewOptions`, so this profile cannot switch them on and does not pretend to.

---

## 5. Every arm has a control

ADR-182, applied here:

| arm | control |
|---|---|
| three seeds take three routes | the same world twice is bit-identical (0.000000 m) |
| a guard leaves its post for what it notices (13.442 m) | the same body with `investigate`'s registered weight at 0 (0.000 m) |
| the extraction changed no route (0 of 3,600 samples) | one metre of `maxRange` moves 621 of them |
| the percept fade keeps a guard walking (8.514 m) | the same guard with `memorySeconds` 0 turns back (1.497 m) |
| a dwell boundary is the same instant at 60 and 37 Hz (20 ms) | an accumulated half-second drifts 264 ms |
| a route round a solid is ≥ the straight line | a 12 m hop over open ground is exactly its 12.00 m line |
| a penned body goes nowhere (0.00 m) | an identical body outside the pen walks 72.4 m across 22 cells |
| a route out of the pen is `Unreachable` | the same route from outside the pen is `Ok` with 2 waypoints |
| the same seek twice is bit-identical | a seek past the 90 s window is 26.4 m from the play, inside it 1.36 m |
| play at 60 Hz | the same simulation at 30 Hz **must** disagree — 1.247820 m |
| a jointed socket is metres from the body | a jointless socket is not, with the skeleton installed |
| a socket naming a real joint answers `Joint` | one naming a missing joint answers `EntityFrame` |
| two bodies at 60 m and 12 m notice different things | the *same* body driven to 12 m notices a strict subset, from the same place to a micrometre |
| a 90° body misses what is behind it | the same body at 360° holds a strict superset |
| a crowd performs exactly 144 occlusion tests | the same crowd at `occlusionTestsPerSecond = 0` performs **0**, and reports `tested == false` on every percept of every body on every frame |
| 144 tests were performed | 405 of them came back genuinely occluded — without which "tested" would be consistent with a sightline that can only answer 1 |
| a 60 Hz replay senses 43,200 times | the same replay at 4 Hz senses 2,903 times, and the −1 from 2,904 is the per-body phase |
| the head group turns +32.71° while walking | the same layer with its weight pinned to 0 turns it 0.000° |
| the feet move 0.000000 while the head turns | the identical layer masked onto the **feet** moves `foot.r` 0.1447 and the head 0.0000° |
| a flinch moves the chest 0.0088 and the neck 0.0334 | the feet and toes move 0.000000 through the same flinch |
| a mask that names a joint answers `Applied` | one naming `Head01` on an alien answers `NoJoints`, not `Inactive` |
| the detour wins at `wadePenalty` 12.0 (0.2781 against the ford's 0.1745) | the identical pair of routes at 0.4 has the ford win (0.4055 against 0.3195) |
| a world with a river publishes two ways to a place | the same considerer over `guard-post.scene.json`, which has no water, publishes one |
| the dear body's deepest water is 0.27 m | the cheap body's is 1.40 m, in the same run of the same world |
| a detour expressed as a `Move` per waypoint stays dry | `plodder`, one authored `move` at the far bank and no decider, wades 1.40 m |
| a stone 6 m from `scout` moves 1,779 of the golden's 3,600 samples | the same stone 90 m away across the river moves **0** |

The determinism case prints its play-vs-seek figures rather than bounding them, and says why: a bound
that passes today would be loose enough to assert nothing, and P1 owns the fix. This is where the
before-number is kept, deliberately not in the unit that will move it.
