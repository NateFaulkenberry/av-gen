# ADR-266: Nine of the twelve layers were already here, and the three that were not are not the three anyone expected

**Status:** Accepted
**Date:** 2026-09-17

The Autonomous Character Intelligence brief describes a twelve-layer stack — perception, memory,
needs, emotion, decision, navigation, steering, social, group, animation selection, animation
layering, directed override — and asks for it to be built. Phase 0's job was to find out what
building it would mean. The answer is that most of it is built, and that a Phase 1 which started
from the brief's layer list would have produced a second copy of nine subsystems.

This is the fourth time in a fortnight. ADR-261 found that four of the six pieces the Engineering
Lab brief asked for already existed. ADR-210 found that four fifths of the director brief was
already in `entity/action.hpp`. The shape is consistent enough to be a rule: **in this repository,
the first Phase 0 question is not "how should this be built" but "what is it called here".**

---

## 1. The inventory

| the brief's layer | what it is called here | evidence it is real |
|---|---|---|
| navigation | `Navigator`, `NavGrid`, `PathRequest/Result/Status` | 23,716 cells built in 243.9 ms; A* at 24.969 µs |
| steering | `Navigator::steer`, `crowdSeparation`, `resolvePenetration` | 59.135 µs a call; ADR-240 §5 fixed its worst defect |
| group / crowd | `EntityWorld::crowd_`, rebuilt per frame as a snapshot | `entity.cpp:873-890` |
| decision *execution* | `ActionQueue`: 3 tiers, preemption, resumption with elapsed time, `Condition`, `otherwise`, `ActionResult` | `entity/action.hpp:281` |
| routines | `Schedule`, pause-aware | a routine paused at 10 s and resumed at 400 s fires its 12 s entry two seconds later |
| smart objects | `InteractionDesc`, authored on the prop | `entity/action.hpp:167` |
| memory (flat) | `PropertyDesc` → `entity/<name>/state/<prop>` as real parameters | keyframeable, presettable, a legal modulation target |
| animation selection | `Gait` with hysteresis bands and a dwell | `entity/gait.hpp` |
| directed override | `stage::Staging`: 13 step kinds, queries, roles, beats, cycles | the shipped abduction is 24 parameters, 5 beats, 0 lines of saucer-aware C++ |
| AI level of detail | three bands per entity, **live** | `entity.cpp:940-996` |

Nine. What is not here:

**Perception.** Nothing at all. `EntityWorld::interestPoints()` is one omniscient list — 505
entries on `glowmere-valley-2` — with no range, no facing, no occlusion, no memory of having
noticed. Two characters in the same world walk different routes only because they weight the same
omniscient list differently.

**A decider.** `ActionQueue` is a thing that can be *told* what to do. `stage::Staging` tells it,
for authored scenarios. The only autonomous decider is fused inside `Explore` — 700 lines carrying
the goal model, five affinities, a six-phase FSM, a replanner, a stuck detector, a jump arc and a
novelty memory in one class. It works. It is also why every autonomous character in Glowmere is an
explorer: there is one autonomous mind in the engine and it has one personality.

**Animation above a single cross-fade.** No additive, no masks, no layer stack, no IK, no morph
targets, no root motion extraction, and `ISkeletonQuery` has zero implementations, so every
`Entity::socketTransform` returns the entity frame — and returns `true` while doing it.

---

## 2. Three things the assignment said that the code contradicts

Recorded because each would have sent an implementation agent the wrong way.

**"`world::TerrainQuery` has an `ObstacleField` seam that is not populated."** It is populated.
`composition.cpp:3650` fills it from every scatter layer, `:3798` adds the heroes, `:3799` builds
it, `:3804` installs the bridge. Measured: **1,238 solids, 1,238 blocking.** An agent told the seam
was empty would have built the population pass that exists.

**"Entity LOD is not live."** *Representation* LOD is not — `RepresentationSelector` has tests and
no call sites. *Behaviour* LOD is live, is read from the camera position, and is the largest single
source of non-determinism in the entity layer (ADR-267). The two share a word and nothing else.

**"ADR-091 demands that a scrub equal a play."** ADR-091 demands it of the baked tier and concedes
the opposite for the live tier in as many words: "Stateful, reset on seek, and explicitly not
frame-accurate under scrub" (`ADR-091:64-66`). `EntityWorld::seek`'s own header promises only that
the same seek time gives the same state. The concession turns out to undersell the code by a lot —
see ADR-267 — but an agent designing around "this is impossible today" would have designed a
checkpoint system to solve a problem that is already solved.

---

## 3. Consequence

**Four layers, not twelve.** Two new (sense, decide), one extended (express), one left alone (act).
Needs, emotion, social and episodic memory are each a term in a utility score or a blend weight on
a pose, not a subsystem; they are deferred until there is a pose layer that could consume one.
That is the owner's §55 preference — a small number of extremely solid capabilities over a large
collection of shallow ones — applied rather than quoted.

**The shared interfaces file refuses to define three of the five it was asked for.**
`src/entity/character_ai.hpp` §0 is a table naming the existing type and the rule that goes with
it, because the way eight agents build eight incompatible systems is not that nobody wrote the
interface down — it is that somebody wrote a second one.
