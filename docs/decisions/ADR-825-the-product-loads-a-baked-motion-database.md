# ADR-825: The product loads a baked motion database through the slot

**Status:** Accepted
**Date:** 2026-09-25
**Related:** ADR-623 (runtime matching), ADR-650 (one database per skeleton), ADR-700 (checkpoints);
Phase C §37 (offline/runtime separation), §39 (database loading), §40 (hot-swap safety), §68 (debug
visualisation), §76 (hot reload), §81 (UI responsiveness)
**Implemented by:**
- `MotionMatchingDesc::database` / `databaseResolved`;
- `Composition::motionSlotFor`, `motionLoadStatus` and `setBlockingMotionLoads`;
- `AnimationSink::matchSlot_`, with `prepareChain` rebuilding the chain when the slot publishes;
- the slot in `replayInputKey`;
- the offline engine blocking on loads;
- `MotionDebug::matching` and `databaseSamples`;
- `tools/make_scout_motion_db.sh`.

**Tests:** `tests/unit/test_motion_db_loading.cpp` (`[motion][matching][database]`)

## Context

The recount found Phase C §37, §39, §40, §76 and §81 partial for one reason: the product never
loaded a baked `.motiondb`.
- `Composition::matchAssetFor` extracted features synchronously when a composition was built. That
  is runtime work, on the thread that draws.
- `MotionDatabaseSlot` (async load, validation, atomic publish, keep the old asset on failure,
  last request wins) was called by nothing but its own tests.

The owner will not review the matcher on the Glowmere film until this path exists.

## Decision

- **Naming a baked database.** An entity names one with `motionMatching.database`, beside the
  `pack` it was built from. Naming one without the pack is refused.
- **Loading it.** The composition keeps one `MotionDatabaseSlot` per (pack, database). The slot is
  requested on first use and loaded off-thread.
- **The fallback while loading.** Until the slot publishes, the body's chain holds the clip
  provider alone, so it plays its clips rather than standing unposed.
- **Arrival and hot swap.**
  - `prepareChain`, which already runs before the entities advance each frame, rebuilds the chain
    when the slot's asset differs from the one the body holds. That covers the first arrival and
    any later publish.
  - The matcher's memory from another database is treated as a first selection (existing
    `MotionMemory` behaviour).
- **Failure.** A failed load publishes nothing. The body stays on its clips, and
  `motionLoadStatus()` carries the reason.
- **Checkpoints.** Each slot's publish count, and whether it has an asset, are part of the replay
  key, so a newly published database drops stale checkpoints.
- **Offline engines block.** An offline engine (renders, headless runs, tests) waits for the
  request to finish, so no rendered frame depends on how long a load took. A live session does not
  wait.
- **The fallback stays.** With no `database` named, the synchronous in-memory build remains.
- **Debug fields.** `MotionDebug` gains `matching` and `databaseSamples`. A chain index alone
  cannot tell the matcher from the clip provider, because with no matcher index 0 *is* the clip
  provider.

## Consequences

- **Measured on the scout.** The baked database poses the body exactly as the runtime build of the
  same pack does: the same motion-memory generation, the same position, and the joint palette to
  under 1e-5.
- **What the recount rows now have:**

  | Row | Status | Reason |
  |---|---|---|
  | §37 | done | runtime work is load and validate, not feature extraction |
  | §39 | done | the slot has a product caller |
  | §40 / §76 | done | the product path is arrival and swap, with fallback on failure |
  | §81 | done | the load is off the draw thread |
  | §68 | still partial | the in-app view is text fields, not a panel |

- **A live session changes when the load lands.** While the load is in flight, the body plays
  clips. A later scrub replays with the matcher from zero, so a scrub over those first frames of a
  live session differs from what played. Offline, and every test, block and have no such window.
- **Output is gitignored.** The scout's pack and database are gitignored. They are rebuilt by
  `tools/make_scout_motion_db.sh`, which is under 1 s, and every alien shares the scout's skeleton.
