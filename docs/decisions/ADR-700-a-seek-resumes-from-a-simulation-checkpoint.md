# ADR-700: A seek resumes from a simulation checkpoint, so a scrub is exact at any second

**Status:** Accepted -- the owner's ruling, 2026-09-21 ("#2 it is": checkpoints, not a whole-history
replay on every click)
**Date:** 2026-09-21
**Supersedes:** the ninety-second replay window of ADR-671 ("The replay window still bounds
exactness") and of ADR-273 (the window as the product; its body-step budget survives, re-based)
**Related:** ADR-671 (a scrub replays the director), ADR-670 (the awareness layer), ADR-623 (provider
memory is replayed), ADR-620 (the limiter's previous speed), ADR-273/ADR-267 (the replay's cost),
ADR-360 as the project cites it ("a render must be reproducible and a scrubbed frame must equal a
played one"), ADR-091 (two-tier determinism; this is the "fixed-step checkpointing for the live
tier" it deferred)
**Implemented by:** `EntityWorld::seek`/`seekExact`/`seekWindow`/`replayStep`/`publishSeek`,
`EntityWorld::Checkpoint`, `captureCheckpoint`/`restoreCheckpoint`/`thinCheckpoints`,
`checkpointInputKey`, `BehaviorList`, `IBehavior::clone`/`assignState` via `CheckpointedBehavior<T>`
(`src/entity/behaviors.cpp`), `Staging::checkpoint`/`restore`/`epoch`,
`Composition::seekWithDirector`'s `capture`/`restore` hooks and `replayInputKey`,
`SeekMode`, `CheckpointSettings`, `SeekBudget::kEditorBodySteps`, `Engine::seekSeconds`
(`AVGEN_SEEK_MODE`), `avgen_behavior_trace --scrub T [--warm S] [--seek-mode M] [--interval S]`
**Tests:** `tests/unit/test_seek_checkpoints.cpp`, `tests/unit/test_glowmere_scrub.cpp`,
`tests/unit/test_entity_seek.cpp` (the window, kept as a control)

---

## Context

A seek reconstructs the simulation by replaying it. Until now the replay started from a reset state
at most 90 s before the target. That was exact while a body's motion was a function of the clock.
Since ADR-670 and ADR-671 it is not: an alien's choices depend on everything it has seen and heard,
and the director's abduction cycle depends on every cycle before it. On
`glowmere-valley-2-multicam` every body was exact at 30, 45 and 90 s, and past the window it was
not. Measured on this branch's base before the change (`test_glowmere_scrub.cpp`, the late-time
arm, run on the pre-change binary):

| t | worst body | director beat, play / scrub |
|---|---|---|
| 150 s | `ember` 69.2 m | `approach` / `depart` |
| 200 s | `visitor` 185.9 m | `depart` / `depart` |
| 226.28 s (last frame) | `visitor` 185.9 m | `depart` / `depart` |

A render started past 90 s therefore did not reproduce the same frames from a full render. The
owner rejected replaying the whole history on every click (about 12 ms per film-second: 2 to 3 s to
scrub to the end, on every click) and chose checkpoints.

## Decision

1. **A seek runs on a fixed grid.** Step 0 is the instant t = 0 with no elapsed time (a play's first
   frame, ADR-521). Step k > 0 is the instant k / rate with dt = 1 / rate. The instant is spelled
   `k / 60.0`, the way a play spells `frame / 60.0`, and not `k * (1/60)`. So a state recorded on
   the way to one target is exactly the state any other target's replay passes through. The old
   window counted back from the target (`target - j * dt`). Its instants depended on where the seek
   landed, so nothing it passed through could be reused. A target between two grid instants gets
   one short step from the last instant, and that step is never recorded.

2. **A checkpoint is taken every second of film as a replay passes it.** A seek restores the
   nearest checkpoint *strictly before* its target step and replays forward, so it always replays
   at least one step. An off-grid target may use the checkpoint at its last grid instant, because
   its short step is replayed anyway. Landing on a checkpoint and replaying nothing left the
   parameter finals as the reset left them rather than as the target step wrote them. The digest
   test caught that on its first run.

3. **What a checkpoint holds, and how we know it is all of it.**
   - **Every `Entity`, whole, by its implicit copy constructor.** This became possible when
     `std::vector<std::unique_ptr<IBehavior>>` became `BehaviorList`, a value type that clones on
     copy and copies *into* the live behaviours on assignment, so their addresses survive a restore.
     Every behaviour copies itself through `CheckpointedBehavior<T>`, which uses the compiler's copy
     constructor and copy assignment. `clone`/`assignState` are pure virtual, so a new behaviour
     does not compile until it opts in. A member added to `Entity` or to any behaviour is therefore
     in every checkpoint without anyone listing it. That covers: entity state, the rng, the
     measured velocity and its last position, the limiter's previous speed (it lives in
     `EntityState::speed`), provider memory (ADR-623), gait hysteresis, the locomotion plan, root
     motion samples, the action queue and schedule with their serials and drained lists, properties,
     claims, attachments, field and arc state, the sense tick and working set, the director hold,
     and inside `decide`: the selector, percept memory, object memory and novelty, attention,
     habituation, the plan lifecycle, the variety window, the trace and the stall breaker. Deciders'
     considerers are shared between a live decider and its copies. They hold no per-character state
     (decision.hpp's rule), so sharing them is sound, and `unique_ptr` became `shared_ptr`.
   - **The director, whole** (`Staging`'s copy constructor, private and reachable only through
     `checkpoint()`): runs, cues, per-run rngs, bindings, claims, the gate's memory, the log, retired
     animals and readings. It also records the **current base of every parameter the director has
     written**, the one piece of its state that lives outside the object. `restore` first puts back
     the authored values of what the live director wrote (as `reset` does), then becomes the copy,
     then writes the copy's values.
   - **`ReplayPlacement`**, the one-step-old placement the director asks about.
   - **The world-level list**, the only hand-written part: world events, the event sequence, the
     signal-event clock, and the action events pending and published. It is guarded by a `sizeof`
     tripwire (`test_seek_checkpoints.cpp`). The guard's message says how to classify a new member:
     state (add it here), configuration (bump the epoch) or scratch.
   - **Not held, on purpose:** scratch rebuilt before it is read (the crowd grid, the body grid, the
     sense index), reports (`SeekWork`, the perception counts), parameter *finals* (rebuilt by the
     first replayed step, which is why rule 2 replays at least one), and two append-only registries
     (event-type names and semantic tags). An id is assigned at first sight and never reassigned, so
     the ids a checkpoint carries stay valid for the session. The order in which a session first
     meets the names is not itself replayed, which is the pre-existing limit a seek already had.

   **How we know it is complete:** by construction for everything above except the world-level
   list, and by test for the whole. The Glowmere digest test restores into a world that has first
   been to the film's end (every checkpoint recorded, every piece of state as unlike the target's as
   the film allows) and then through 30 ordinary played frames (the state only a play touches). It
   then scrubs back to 37.5, 150, 200.28 and 121.004 s and compares a digest of everything public
   with a whole-history replay. The digest covers every body's simulation and drawn position,
   facing, speeds, velocity, acceleration, activity, look target, director hold, properties, action
   queue, sense tick and percepts. It covers every decider's choice, tick, counters, plan, attention
   and full trace, the world events, the director's log, retired animals and cue readings, and every
   parameter's base and final. The digests must be identical, and must stay identical over 20 more
   played frames.

4. **Invalidation is a key, and it is conservative.** The key hashes the world's structure epoch,
   every entity's borrowed providers, the step, the host's key, and **every parameter base together
   with which parameter holds it**. The epoch is bumped by `setEntities`, `clear`, `setBindings`,
   the navigator (so every flatten), landmarks, terrain landmarks, extra interest points, fields,
   event profiles, parameter registration, `bind`, the path provider and the sense stage. The host's
   key covers the staging epoch (description and registration), the root fold and centre, and the
   node/mesh counts. The bases are hashed *after* the director and the entities have put back what
   they write, so what is hashed is the authored scene plus whatever a person has changed. Any
   change drops the whole set, whether or not it matters to the simulation: a light slider costs one
   replay from zero. We cannot tell which edits the simulation reads, so every edit drops the set,
   not only those after the edit time. Timeline, keyframe and route edits do not enter the key
   because the replay does not read them (a seek has no signal history, and the replay resets
   finals to bases). A project load builds a new `Composition`, and with it an empty set.

5. **No checkpoint yet** means replaying from the latest one before the target, or from zero, and
   recording as the replay passes each second. An offline render runs in a fresh process, so its
   opening seek replays the whole film to its start, exactly, once.

6. **The window is retired as the product and kept as controls.** `SeekMode::Checkpointed` is the
   default everywhere. `SeekMode::Window` is the old replay (its shallow-body classification and
   budget tests in `test_entity_seek.cpp` ask for it by name). `SeekMode::FullHistory` replays every
   step from zero and touches no checkpoint; it is the reference every checkpointed seek is compared
   with. `AVGEN_SEEK_MODE=window|full` switches the engine for an A/B from one binary.

7. **The budget keeps its unit and changes its base.** `SeekBudget::maxBodySteps` bounds the replay
   *from the checkpoint*. Past it, a checkpointed seek falls back to the window and reports
   `SeekWork::exact == false` and `fellBack == true`. The live default rose from 180,000 to
   `kEditorBodySteps` = 400,000. The first scrub to the multicam's last frame is 19 entities x 13,578
   steps = 257,982 body-steps, so 180,000 would have made it inexact. 400,000 still bounds ADR-267's
   250-character cast to 27 s of exact history per click.

## Numbers

Checkpoint size and interval were measured on `glowmere-valley-2-multicam`: 19 entities, namely the
craft, its beam, five aliens and twelve farm animals. The brief's "nine animals" predates three more.
Measured with `avgen_behavior_trace --warm 226.28 --interval N`. The size is **measured, not
estimated**: it is the heap bytes allocated while copying (the process's interposed allocation
counter) plus the inline size of the copies.

| interval | checkpoints over the film | memory | worst scrub after the first (1 interval of replay) |
|---|---|---|---|
| 5 s | 45 | 2.42 MB | 104.6 ms |
| 2 s | 113 | 6.08 MB | 29.5 ms |
| **1 s** | **226** | **12.16 MB** | **17.9 ms** |

One checkpoint is **55.1 KB**. It takes 0.1 to 0.4 ms to take and 0.05 to 0.17 ms to restore, so the
cost of recording is under 1% of a first-time replay. The interval is **1 s**. The set is capped at
**256 MB** (about 77 minutes of this film at 1 s). Past the cap, every other checkpoint is dropped
and the interval doubles, so the set thins evenly and a scrub stays bounded by the new interval.

Scrub cost, minima over repeats, headless, same scene. The *before* is this branch's base (window
plus director replay). *First* is a fresh load with no checkpoint. *Warm* is after one scrub to the
end has recorded the set. The two rounds were interleaved; the load average is printed per row:

| t | before (window) | after, first scrub (no checkpoint) | after, warm | load avg (1 min), rounds 1 / 2 |
|---|---|---|---|---|
| 30 s | 426.9 / 424.7 ms, exact | 426.3 / 426.2 ms, exact | 11.1 ms | 15.6 / 17.9 |
| 90 s | 1278.6 / 1279.8 ms, exact | 1285.1 / 1287.4 ms, exact | 16.6 / 16.6 ms | 10.2 / 11.3 |
| 150 s | 1309.4 / 1292.1 ms, **69.2 m off** | 2142.9 / 2145.0 ms, exact | 14.5 / 14.1 ms | 12.6 / 6.6 |
| 226.28 s (last frame) | 1301.8 / 1421.9 ms, **185.9 m off** | 3248.1 / 3217.3 ms, exact | 4.4 / 4.2 ms | 3.9 / 5.5 |

Minima of 5 (7 for warm). Each *first* repeat is a fresh load. Each *warm* repeat scrubs the same
loaded film after one untimed scrub to the end, which took 3.2 to 3.4 s and left 226 checkpoints
(12.16 MB). The warm 30 s row's round-1 value (0.2 ms) came from a stale tool binary still on the
old 5 s interval and is not quoted. The last-frame rows were re-taken at the exact frame
(13,577 / 60 s): the first pass used 226.28 s, which is between two frames, so its play and its
scrub compared different instants (1 cm apart, `vane`).

Read it this way. Up to 90 s the first scrub costs what the window did, because both replay from
zero. Past 90 s the first scrub pays for the history the window skipped, about 14 ms per
film-second (the window's fixed 102,600 body-steps, against 257,982 to the end), and is exact where
the window was not. Every scrub after the first costs one second of replay at most: 4 to 17 ms,
against 0.4 to 1.4 s before. The warm cost varies with where the target falls inside its second
(the 226.28 s target is 17 steps past a checkpoint; the 90 s target is 60).

## Found on the way

The digest test's first run failed, and not because of checkpoints. **A replay from zero after an
earlier run did not equal a fresh load's.** Two defects in the reset every such replay relies on:

- `Decide` kept its variety window as a `span` into `recentKinds_`, taken in `sense()` *before* the
  same step's plan-completion branch pushed onto that vector. When the push reallocated (the second
  completion of a run, growing from a capacity of one), the span pointed at freed memory, and that
  tick's choice read whatever was left there. After an earlier run, `clear()` had kept the capacity,
  so there was no reallocation and a different answer. `rook`'s glow errand scored 0.44 where a
  fresh load scored 0.74, and the aliens were 28 m apart by 90 s. Now re-pointed after the push, as
  `dctx.visited` already was two lines above. This changes what the aliens choose on those ticks
  compared with the previous binary, which was reading freed memory.
- `Selector::reset` forgot `commitment_`. It now resets by assignment, keeping only its settings.

Every earlier scrub test used a fresh load for each scrub, which is the one history in which a
forgotten reset is invisible (testing.md #41).

## Alternatives considered

- **Replay the whole history on every seek.** Exact and simple, and it is kept as
  `SeekMode::FullHistory`. The owner rejected it as the product on cost: about 2 to 3 s per click
  at the film's end, against a warm checkpointed scrub of under 20 ms.
- **Record checkpoints during playback.** The brief suggested it. **Rejected, because a play is not
  the simulation a seek computes.** The live clock's `deltaTime` is wall time (`RealtimeClock::tick`),
  so a played step is 1/57 s on one frame and 1/63 s on the next. A play also runs the modulation
  routes against the live audio bus, which a seek never replays (ADR-671's stated limit). A
  checkpoint recorded from a play would carry that history into every later scrub, so the same scrub
  would give different frames depending on whether and how the owner had played past it. That
  breaks the property this ADR exists for. Checkpoints are recorded only by the fixed-step,
  signal-free replay, which is the definition of "where a scrub lands". A play at exactly 60 Hz with
  no audio would agree, and the tests' `Film` is exactly that, but the editor never runs in that
  configuration.
- **Fill checkpoints in the background while idle.** Considered and **not built**. The replay mutates
  the one live `EntityWorld`, `Staging` and `ParameterSet`, so it cannot run on another thread
  without a second copy of the simulation, including the navigator, terrain queries and nodes. On
  the main thread, a slice would have to save the live state, restore the fill cursor, step, save
  the cursor and restore the live state: about 0.5 ms of copying per slice before any stepping,
  inside a frame the Glowmere renderer already fills to about 13 ms. The first-time cost it would
  hide is paid once per edit (under 5 s to the end of this film on a loaded machine), and the owner
  accepted that ("exact, just slower the first time"). **Revisit** if first scrubs of long films
  become the complaint. The right shape is a second, headless simulation owned by a worker, fed the
  same inputs, that publishes checkpoints the main thread can adopt when the keys match.
- **Hand-written save/load per class.** The obvious implementation, and the one that forgets a
  field. The reset path, hand-written field by field, had forgotten two, which is the argument.

## Consequences

- Scrub = play to the bound `test_entity_seek` holds (0.000000 m) at 30, 45, 90, 150 and 200 s and
  at the film's last frame. The comparison covers all bodies, the director beat, and, on the frame
  after, every particle system's enabled flag, spawn rate, size and emitter position.
- A render started at 150 s (through the beam, the lift and the beam's cut-out) and one started at
  154 s (a seek that lands mid-beam) draw the same frames as a render from zero: bodies, particle
  states, and the cut-out itself.
- An edit that changes the simulation drops the set, and the next scrub equals a fresh replay with
  the edit. With the key ignored (`CheckpointSettings::ignoreInputKey`, a test control) the same
  sequence puts the bodies 87 m away on the film and gives a different digest on the scene-free
  world.
- Every seek now replays every body from its checkpoint. The window's shallow-body saving only
  mattered when the window was 90 s and does not apply to a replay of at most 1 s. Director-less
  scenes pay up to 60 steps where a clock-driven craft used to pay one.
- **Shown failing:** the late-time arm on the pre-change binary (table above). The `sizeof` guard
  with a member added to `EntityWorld` (2064 against 2056). The digest test with world events left
  out of the restore (first difference `event:` at 37.5 s; bodies apart after 20 frames). The digest
  test with the director's base writes left out of its restore (first difference in `params:`). Both
  invalidation tests with the key check disabled (the multicam 87 m off, `invalidated` false,
  restored from 149 s).

## Revisit triggers

- A first scrub reaches the fallback on a scene someone cares about (`SeekWork::fellBack`): raise
  the budget or build the background fill above.
- The checkpoint size grows past about 1 MB (a big cast, or a behaviour holding a large history):
  consider a lower interval cap or storing a delta.
- A new source of history outside `Entity`, `Staging` and the listed world state, such as a new
  replay hook: it needs a `capture`/`restore` half and a place in the key, and the digest test is
  what will tell you.
