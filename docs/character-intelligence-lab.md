# The Character Intelligence Lab

**Question**: *Why is this character here, facing this way, and why did it decide to be?*

Registry entry: `LabId::Character`, key `character` (`src/labs/lab.cpp`).
Fixture: `examples/labs/character/character-intelligence-lab.scene.json`.
Cases: `examples/labs/character/cases.json`, reachable as `avgen --lab-case character:<n>`.
Tests: `tests/unit/test_character_intelligence_lab.cpp`, `tests/unit/test_character_lab_sockets.cpp`.

Companion reading, in this order: `docs/character-ai-research.md` (what already exists),
`docs/character-ai-plan.md` (the units and who owns which file), ADR-266 to ADR-275.

---

## 0. What this lab owns, and what it does not

It owns **what a body knows, what it chooses, where it walks, and where its sockets are**:
perception, the decision layer above `ActionQueue`, navigation and stuck-detection, and the joint a
socket resolves to.

It does **not** own what the joints do once the entity is placed — that is the Animation Lab — or
whether the body reached a draw call — that is the Visibility Lab.

`decides` names `src/entity/behaviors.cpp:Explore` and not any file with "decision" in its name,
because **nobody decides** (ADR-269). The only autonomous mind in this engine is hardcoded inside
one 700-line behaviour class, which is why every autonomous character in Glowmere is an explorer.
When P3 extracts the goal model out of `Explore`, the registry test fails and this entry moves. That
is the intended failure.

---

## 1. Three of the six scenarios cannot be run, and say so

The owner's brief asked for six: basic wandering, investigating a mushroom, crossing a river, a
social interaction, a dense environment, a long-running simulation.

| # | scenario | state |
|---|---|---|
| 1 | a socket on a hand is on the hand | runnable |
| 2 | three explorers, three seeds, three routes | runnable |
| 3 | a route round what is in the way | runnable |
| 4 | a penned character is stuck and nothing says so | runnable |
| 5 | two minutes, and the scrub against the play | runnable |
| 6 | every character *used to* see everything | runnable — the before-arm, kept |
| 10 | two characters, two ranges, two different worlds | runnable — the after-arm (ADR-290) |
| 11 | the occlusion budget, and the arm at zero | runnable (ADR-290) |
| 7 | investigating a mushroom | blocked on **P3 decision** |
| 8 | a social interaction | blocked on **P3 decision** |
| 9 | crossing a river | blocked on **P3 decision** |

Cases 7 and 8 were blocked on *two* units and are now blocked on one. That is what a `blockedBy` is
for: the day a unit lands, the cases that were waiting on it are a list rather than a memory, and
unblocking half of one is deleting half of a string. All three remaining blocks are the same block —
**nobody decides** (ADR-269) — and `Option` and `IConsiderer` still have no implementations.

A blocked case carries `blockedBy` naming the unit of `docs/character-ai-plan.md` that unblocks it
(ADR-275). `--lab-case character:7` refuses and prints the unit rather than opening the fixture: a
person who came to watch a character investigate something would otherwise be shown a scene in which
that is not happening, and left to work out why.

Case 9 is split on purpose. The *route* half of crossing a river is answerable today — `NavGrid`
already prices a ford and `NavSample::waterDepth` is carried for exactly that. The *choice* half is
not, because preferring a longer dry route to a short wet one is a score and nothing scores. So it
is blocked on P3 and not on P7, and when P7 makes the route observable the first half unblocks
without the second.

---

## 2. The fixture

Nineteen nodes, four entities, thirteen heroes, and every element in it earns its place as an arm or
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
  is a 14 m river with 1.6 m of water, which is what case 9 will need.

All four bodies run `explore` rather than `wander`, and that is not cosmetic: `wander` implements no
`navDebug`, so a fixture built on it can say nothing at all about why a character is going where it
is going. `Explore` is also the only autonomous mind in the engine, which makes it the thing the lab
is for.

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
the skeletons. What it cannot turn on is **the losing option scores beside the winner**, because
there are no options. What it *could* now turn on, and does not yet, is a body's working set:
`Entity::percepts()` carries what each character noticed, with a distance, a salience, a `seenAt`
and an honest `tested` flag (ADR-290), and nothing draws it. `NavDebug` already carries route, leg, destination, status, phase, goal name
and goal kind through `IBehavior::navDebug`; drawing them is this lab's, and drawing a score beside
them waits on P3.

The navigation overlays are the editor's (`WorldEditor::showNavRoute` and friends) and are not part
of `rendering::DebugViewOptions`, so this profile cannot switch them on and does not pretend to.

---

## 5. Every arm has a control

ADR-182, applied here:

| arm | control |
|---|---|
| three seeds take three routes | the same world twice is bit-identical (0.000000 m) |
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

The determinism case prints its play-vs-seek figures rather than bounding them, and says why: a bound
that passes today would be loose enough to assert nothing, and P1 owns the fix. This is where the
before-number is kept, deliberately not in the unit that will move it.
