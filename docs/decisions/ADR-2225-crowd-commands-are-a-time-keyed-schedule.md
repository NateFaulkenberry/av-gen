# ADR-2225: crowd commands are a time-keyed schedule; the crowd at t is a pure function of t; checkpoints make a seek cheap

- **Status:** Accepted (2026-11-26), on `eng/115-crowds` (115 programme, Agent 3). The owner **confirmed** 115 decision C3 on 2026-11-27: crowd commands are a time-keyed schedule in document data.
- **Code:**
  - `src/crowd/crowd_desc.{hpp,cpp}` (the scene file's top-level `crowd` block);
  - `CrowdWorld::advanceTo`, `advanceToStep`, `recordCheckpoint` and `pose`;
  - `Composition::updateBehaviour`, which drives the crowd before the entity early-out.
- **Tests:** `tests/unit/test_crowd_world.cpp`:
  - 801: a checkpointed seek equals a from-zero replay at 10 random seconds;
  - 803: a 30 fps play equals a cold seek, and the node is drawn where the crowd says;
  - schema round-trip.

## Context

ADR-091 split the world into a baked tier (pure, seekable) and a live tier (stateful, reset on seek). ADR-700 made the live tier exact with checkpoints.

A navigated crowd is history-dependent: avoidance has no closed form. If commands arrived as live calls, its state would also depend on when somebody clicked, and no seek could reproduce it.

## Decision

1. **The scene's `crowd` block holds:**
   - `groups`: radius, speed range and buckets, avoidance settings, seed, gait, clips;
   - `walls`;
   - `schedule`: entries `{at, group, command: goto|wander|hold, points|area, policy: spread|index, arriveRadius, face, stagger, dwell}`.

   The Session tools, after API level 3, edit this block. They never push commands into a running simulation.
2. **The crowd at timeline second t is the state after `floor(t × 60)` fixed steps.**
   - A play, a seek and a range render all call `advanceTo(t)`. It steps forward, or restores the nearest checkpoint at or before the target and replays.
   - Checkpoints are taken every second of film. The set is thinned evenly past 2,048, as ADR-700 does.
   - Their validity is the input key: the block, the agents and the surface's content key. A changed key resets them.
   - The drawn pose between grid instants is extrapolated by at most one step, and never fed back. So frame rate cannot change the simulation, which ADR-990 had to fix separately for entities.
3. **Inputs are pure:**
   - every random choice is a hash of (group seed, schedule entry, member, counter);
   - the crowd reads no signal bus, no entity and no route.

   Parameters under `crowds/<group>/…` arrive in phase B3, and integrated routes onto them will be refused.
4. **The crowd keeps its own checkpoints, behind the one seek entry point.**
   - The crowd reads nothing the entity replay produces. So its seek is independent, and it works in scenes with no entities, where `updateBehaviour` returns before the entity pass.
   - **There is one entry point.** `Composition::seekWithDirector`, the only seek `Engine::seekSeconds` calls (Studio, the API, `RenderJob` and `--range` all go through it), seeks the crowd first, to the same second on the same 1/60 grid. No caller can seek one without the other.
   - **The coupling rule.** The day the crowd reads an entity (entity obstacles, ADR-2223 §7.7 of the plan), or an entity reads the crowd (density, avoidance of the crowd), the crowd must join `EntityWorld::seek`'s `SeekHooks.capture/restore`, so one replay drives both. Short of that, the two seeks must be proven to agree.
   - **Enforced by** `tests/unit/test_crowd_world.cpp`, "crowd lab 804". In a scene with both wandering entities and a crowd, one `seekWithDirector` from a warmed state must equal a play from zero, for the crowd's state hash and every entity's position. The targets fall inside schedule entries: mid-departure-stagger, mid-walk, mid-dwell. The test asserts that all three states were in fact hit.

- **Schema.** `/crowd` is registered `x-avgen-untyped` in `src/api/schema_types_scene.cpp` (a data-only line, under the L3-gate exception the orchestrator granted). It must be typed before the phase B3 `crowd.*` Session tools land.

## Consequences

- A scrub replays at most one second of crowd (warm). A cold first seek deep into a film replays from the nearest checkpoint, or from zero. A background pre-roll at load is phase B3 work.
- The rig's cross-fade is player state. A seek that lands inside a 0.2 s idle/walk fade draws the incoming clip without the fade. The crowd's own state is exact.
