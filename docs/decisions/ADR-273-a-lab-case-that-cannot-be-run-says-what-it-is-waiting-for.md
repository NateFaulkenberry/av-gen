# ADR-273: A lab case that cannot be run says what it is waiting for

**Status:** Accepted
**Date:** 2026-09-18

The Character Intelligence Lab was asked for six scenarios: basic wandering, investigating a
mushroom, crossing a river, a social interaction, a dense environment, a long-running simulation.

Three of them require a perception layer and a decider. Neither exists. ADR-270: every character in
this engine reads one global list of interest points with no range, no facing, no occlusion and no
notion of having noticed something. ADR-269: nobody decides — the only autonomous mind is hardcoded
inside `Explore`, which is why every autonomous character in Glowmere is an explorer.

There were three ways to handle that and two of them are worse than doing nothing.

**Write the case and let it pass.** A case asserting a decider that does not exist passes by
asserting nothing, and reads to whoever comes next as a decider that works. This is exactly the §37
failure the lab suite was built to prevent, committed by the lab built to prevent it.

**Write the case and let it fail.** A permanently red suite is a suite people learn to ignore, and
the repository already carries three deliberately-failing invariants that are load-bearing. A fourth
that means "not built yet" rather than "this is wrong" devalues the other three.

**Write the case and say what it is waiting for.** `LabCase::blockedBy` names a unit of
`docs/character-ai-plan.md` — `"P2 perception"`, `"P3 decision"`. `--lab-case character:7` refuses
and prints the unit rather than opening the fixture, because a person who came to watch a character
investigate something would otherwise be shown a scene in which that is not happening and left to
work out why. `LabCase::runnable()` is the predicate. The registry test requires that every blocked
case names something, and that the file is not *all* blocked — a lab whose every case was waiting
would satisfy the first check and be a lab that does nothing.

A blocked case is a commitment, not a placeholder. The question and the expectation are the useful
half and they are the half that is ready now; the day the unit lands, the cases waiting on it are a
list rather than a memory.

---

## 1. Splitting a case on what actually blocks it

Case 9, crossing a river, is blocked on **P3 decision** and not on P2 or P7, and the split is the
point of the mechanism.

The *route* half is answerable today: `NavGrid` already prices a ford and `NavSample::waterDepth` is
carried for exactly that. The *choice* half — preferring a longer dry route to a short wet one — is a
score, and nothing scores. So when P7 makes the route observable, the first half unblocks and the
second does not, and the case says which is which rather than being marked "not supported" as a
whole.

---

## 2. What the lab measures now, and what it cost

Six cases run. The findings are in `docs/character-intelligence-lab.md`; three belong here because
each one corrects something that was believed.

**A hero floating above the ground is an obstacle a walker passes underneath, and the log calls it
blocking.** A hero's `position.y` is the obstacle's *base* (`entity::obstaclesFromHeroes`). The
first version of the lab fixture authored its heroes at y = 0 over the shipped procedural world,
whose ground there is −7.46 m. Thirteen solids were indexed, `13 navigation obstacles (13 blocking)`
was printed, and every one of them sat 7.46 m over the walker's head. The penned character walked
out of a sealed pen and 137.86 m across the map, and the reading that suggests itself — "obstacles do
not contain a character" — is false. **Cost: one wrong conclusion, nearly written down.** The fixture
is an explicitly flat world now, because an omitted `world` block keeps the shipped landscape
(`world_map.cpp:671`) and every number would otherwise be a measurement of terrain as well.

**Being stuck and being idle produce the same frame and the same log.** Grounded, the pen holds
absolutely: `penned` walks **0.00 m** in 120 s and stands in one cell, against `rover`'s 72.4 m
across 22. `Navigator::requestPath` out of the pen returns `PathStatus::Unreachable` with 0
waypoints and the same request from just outside returns `Ok` with 2 — so the engine knows exactly,
and says nothing. The status reaches no UI (ADR-268), `Explore`'s `stuckSeconds` watchdog replans
silently, and the body never even tries, because `pickDestination` rejects every goal it could pick.

**The scrub does not match the play for an explorer: 1.360136 m at 30 s**, inside the 90 s replay
window where ADR-091 demands they agree and where ADR-267 measured 0.000022 m. This is not a
contradiction of ADR-267 and not a regression. `EntityWorld::seek` (`entity.cpp:739`) re-simulates a
strict *subset* of `update` — `behavior->update` and nothing else: no interest points, no crowd
field, no gait, no action queue, no `applyOffsets` — and leaves `BehaviorContext::self` at 0 for
every body. `Explore` reads all of those. ADR-267 §4 already lists these as P1's four seek defects;
this is what they cost on a cast of explorers, and the before-number now lives outside the unit
that will move it. What *does* hold, and what nothing had checked: seeking twice to the same second
is bit-identical.

---

## 3. Registry corrections

`decides` for this lab is `src/entity/behaviors.cpp:Explore`, not a file with "decision" in its
name. Naming a layer that does not exist would be the registry sending somebody nowhere. Naming
`Explore` sends them to the code that is actually choosing today, and P3's extraction will fail the
registry test — which is the intended failure, and how the ownership map stays true.

The lab is registered on the `character` key that already existed, retitled from "Character Runtime
Lab": a second lab owning the same subject would be the §34 boundary failure the registry exists to
prevent. The `animation` lab's two drifts are fixed in the same table — its `doc` was the general
document rather than `docs/character-animation-lab.md` and its `fixture` was
`examples/characters/alien.json` rather than the lab's own — both of which the registry test could
not catch, because both targets existed.
