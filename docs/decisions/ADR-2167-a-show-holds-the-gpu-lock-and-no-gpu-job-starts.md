# ADR-2167: the show is a host state with reasons; while it runs Studio holds the machine GPU lock (never waiting for it) and no GPU job starts

- Status: Accepted (2026-11-26), eng/engine-api-s10. **Its definition of the show (decision 1: the reasons and
  "transport alone counts") is superseded by ADR-2176**; the rest stands. (Engine API stage S10, the addition the orchestrator decided;
  05-ui-readiness/05-live-safety.md G1 and G4, sections 2.3, 2.7, 4.1, 4.2)
- Code: `api/show_state.{hpp,cpp}` (`ShowReason`, `ShowState`, `ShowFacts`, `showStateFor`, `ShowLockState`),
  `api/session.{hpp,cpp}` (`Host::showState`, `Host::showLock`, `LocalHost::setShowState`; `session.info.show`),
  `api/session_tools.cpp` (its schema), `api/engine_host.{hpp,cpp}` (`updateShow`, the show gate in `submitJob` and
  `stepJobs`, the lock shared between a show and a job), `api/gpu_lock.{hpp,cpp}` (`setHolderName`),
  `ai/engine_tools.cpp` (the profiler tools), `app/application.cpp` (`currentShowState`, `updateShow` in the frame)
- Tests: `tests/unit/test_api_jobs.cpp` (`[show]`, `[adr2167]`), `tests/rendering/test_engine_host_gpu.cpp`
  (`[gpu][api][jobs][show][adr2167]`)

## Context

05-live-safety.md found that nothing protected a show from GPU work: Studio's live loop, its Render panel job and its
assistant's `--live-profile` children took no lock, so an agent's `gpu-lock.sh` run or an `avgen serve` render could
share the GPU with a projector, and Studio's own render could start in the middle of a set. It also found the
question itself wrong: "a show is running" was `Engine::isPlaying()`, which misses a live-input show (live input never
sets the transport playing), a paused frame on a projector (the parked clock keeps the world moving, ADR-2154) and a
Syphon/NDI feed. Once Studio's renders are EngineHost jobs (ADR-2165) the host is where all of it can be enforced.

## Decision

1. **The show is a host state with reasons** (`Host::showState()`): a closed set, `transport` (playing), `liveInput`
   (a capture device or live Sonic input), `output` (an output or projection window open), `share` (Syphon/NDI).
   `active` is true when any of them holds. `remainingSeconds` only for a file that plays to its end (no loop, no
   live input). Studio fills it from facts it already has (`isPlaying`, `hasLiveInput`/`liveSonic`,
   `outputs_.openCount()`, `share_.isOpen()`, the transport's loop); the rule is the pure `showStateFor`. One meaning
   drives the live policy (`showRunning()` is `showState().active`), the lock and the jobs.
   - **Transport alone counts.** An owner who mirrors the editor's display onto a projector has no output window open;
     the safe reading of "the transport plays" is that somebody may be watching. That is today's predicate kept as a
     subset (05-live-safety Q2 stays the owner's: dropping `transport` from the set is one line in `showStateFor`).
2. **While a show is active no GPU job starts.**
   - A new API render (`render.capture`, `render.start`, from any session including Studio's assistant, which the
     live policy exempts) is refused `BUSY`, `details {field: "show", reasons, retryAfterMs?}`, before any snapshot or
     GPU work.
   - A job queued before the show stays queued, phase `held: show active (<reasons>)`, and starts when it ends. A held
     render-queue entry stays held.
   - A job already rendering pauses between frames (phase `paused: show active (...)`) and resumes when the show stops;
     the output is the output it would have had (tested on the fake and on the GPU: identical sequence hashes). A
     cancel still reaches it. Studio's viewport is not suspended for a paused job, and the frame cap and late latch
     behave as with no job (`jobInLoop()` is false).
   - The Render panel's Render press submits a job that is held the same way; the status says so.
   - `performance.profile_scene` and `performance.optimize_scene` (the tools that start an `avgen --live-profile`
     child) are refused `BUSY` with the same details, before the child starts.
3. **While a show is active Studio holds the machine GPU lock** (`EngineHost::updateShow`, once per frame before
   the job step). It takes it with **one `tryAcquire`**, never a wait, on the show's first frame, holder
   `avgen-studio-show`; while held it does not touch the lock again; on the show's last frame it releases it. Under
   `tools/gpu-lock.sh` the lock is held by an ancestor and counts as ours. Agents' `gpu-lock.sh` runs and other
   processes' EngineHost jobs then wait for the show (their phase names the holder).
4. **If another process holds the lock when the show starts, the show runs anyway.** The attempt costs one round of
   the protocol (measured below the frame noise, ADR-2169); `updateShow` logs `show: the machine GPU lock <path> is
   held by '<holder>' (pid n); the show runs anyway, sharing the GPU` once per holder, reports `contended` and the
   holder, and tries again about once a second, taking the lock when it frees.
5. **A show and a job share one lock object.** A job that ends (or is cancelled) during a show leaves the lock to the
   show; a show that starts while a job holds it takes it over (rewriting `holder`), and one that ends while a paused
   job holds it hands it back under the job's name.
6. **`session.info.show`** (additive): `{active, reasons[], remainingSeconds|null, gpuLock: null | {held, contended,
   holder: null|{pid, name}, path, attempts, lastAttemptMs, maxAttemptMs}}`. `showRunning` stays as it was (the
   same fact, on a Live host).

## Not covered

- Director stills (`StillsSession` on the host's device) and the Director's own scratch renders (their threads) are
  not EngineHost jobs; a show does not hold them.
- No operator override ("render anyway") and no "armed" reason: there is no UI for either yet.
- An external `avgen serve` in another process is not told about the show; it simply waits for the lock.

## Alternatives considered

- **Block, or wait briefly, for the lock when the show starts.** Rejected: a show cannot wait for an agent's suite.
- **Refuse the Render press during a show.** Rejected for the operator's own render: holding it is what a queue
  entry already does, and the status line says why; API clients, which cannot see the stage, are refused.
- **Cancel a running job when a show starts.** Rejected: an hour-long render would be lost to a press of Play.
- **The audience subset only (output, share, live input) for the lock** (05-live-safety 4.1). Rejected for now, see
  1; it is one line to change.

## Consequences

- An agent's GPU work waits while the owner plays music in the editor, as well as during a projected show.
- `AVGEN_GPU_LOCK_TIMEOUT` (default 3600 s) bounds how long an agent waits behind a show.

## Revisit triggers

- An owner decision on 05-live-safety Q2 (does the transport alone count) or Q5 (a running job when a show starts).
- An operator override in the Render panel.
