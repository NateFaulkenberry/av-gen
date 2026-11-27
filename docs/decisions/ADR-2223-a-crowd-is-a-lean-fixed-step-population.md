# ADR-2223: a crowd is a lean, fixed-step population, not entities; Jacobi ORCA; counted amortisation

- **Status:** Accepted (2026-11-26), on `eng/115-crowds` (115 programme, Agent 3).
- **Code:**
  - `src/crowd/orca.{hpp,cpp}`;
  - `src/crowd/crowd_world.{hpp,cpp}` (`CrowdWorld`, `CrowdState`, `AgentPose`);
  - `Composition::installCrowd` and `driveNavigatedCrowd` (`src/scene/composition.cpp`);
  - the `crowd` debug layer (`DebugViewOptions::crowd`, `Scene::crowdDebug`, `--debug-draw crowd`);
  - the Crowd Navigation Lab registry entry (`src/labs/lab.cpp`).
- **Tests:** `tests/unit/test_crowd_world.cpp`.

## Context

An `explore` character costs about 89 µs a step and "exceeds the whole frame somewhere around 180" (`docs/character-ai-research.md` E.1). A crowd of a few hundred cannot be built of entities.

The 115 Stage A spike compared three ways of keeping agents apart in a 2 m bottleneck:

| Method | Cost per agent-step | Overlap | Oscillation |
|---|---|---|---|
| ORCA | 0.3–1.0 µs | ≤ 0.014 m | essentially none |
| Detour-style velocity sampling | 7–8 µs | small, but it pushed agents 0.4 m into walls | 30–90× ORCA's reversals |
| `crowdSeparation`'s positional push | — | 0.54 m of interpenetration | — |

## Decision

1. **An agent is a row in a structure of arrays** (`CrowdState`) holding:
   - place, velocity, cruise speed;
   - goal and face target;
   - mode (idle, departing, moving, arrived, dwelling, unreachable);
   - corridor and cursor, and the path status;
   - an `entity::Gait`, the activity, and the clip phase and rate.

   Perception, decision and behaviours stay with characters.
2. **Membership: one crowd concept (115 O2).** A crowd's agents are the scene nodes whose `animation.crowd` names one of its groups, in scene order. A navigated agent is also an ADR-2117 listening member.
3. **Every step is Jacobi.**
   - Each agent reads only the previous step.
   - Neighbours are taken in (distance, index) order, capped per group.
   - Static obstacles are gathered serially in the surface's order.
   - So 1, 4 and 8 threads give identical bits (test 802).
4. **Avoidance is ORCA** (van den Berg, Guy, Lin, Manocha 2011), self-written:
   - agents take half the avoidance;
   - static discs take all of it;
   - walls are hard constraints;
   - an infeasible program minimises the largest violation.
5. **All amortisation is counted, never timed:**
   - `pathsPerStep` queries per step, served from a cursor that is itself state;
   - a corridor shortcut is tried on one step in four per agent, staggered by index;
   - departures are spread by a seeded stagger.

   A time budget would make the crowd depend on the machine.
6. **Navigation drives the existing animation** (ADR-2226 in part).
   - The gait selects Idle, Walk or Run with its hysteresis.
   - `Gait::playbackRate` matches the clip to the speed, quantised to 1/32.
   - The crowd integrates the clip phase as state and pushes it with `setNodeAnimation`: rebased to the phase origin, cross-fading from the activity's start.
   - Cruise speeds come from a group's buckets, so pose sharing (ADR-2116) can still happen.
7. **The simulation never reads the view mode.** A test greps `src/crowd` and `src/nav` for it.

## Consequences

- Step cost and lab results are in the Stage B report (`11-stage-b/03-crowds/01-b0-b2/`).
- ORCA deadlocks when opposing flows meet at a doorway (spike). Portal controllers are ADR-2224 (phase B3). Until then, a schedule that sends two groups through one door in opposite directions at once will jam.
- Static obstacles are re-gathered every step for every agent. That is correct, and the first thing to cache if the 500-agent target needs it.
