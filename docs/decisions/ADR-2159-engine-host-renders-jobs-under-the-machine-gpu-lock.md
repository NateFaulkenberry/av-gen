# ADR-2159: the EngineHost renders: a lazy device, a FIFO of render jobs stepped between pumps, the machine's GPU lock per job, and `render.capture`/`render.start`/`job.*`

- Status: Accepted (2026-11-25), eng/engine-api-s6 (Engine API stage S6; 113-engine-architecture, design in
  03-phase0-api-contracts/02-session-api.md sections 3.6, 4 and 8, boundaries in 03-boundaries.md section 2, stage
  plan in 04-poc-plan.md; owner spec 04-implementation/00-spec.md section 3 decisions 2 and 3)
- Code: `api/engine_host.{hpp,cpp}` (the GPU half: `submitJob`, `stepJobs`, `cancelJobs`, the job FIFO, the lock),
  `api/jobs.{hpp,cpp}` (`JobTable`, `JobRecord`, `RenderRequest`, the `RenderBackend`/`GpuRenderJob` seam),
  `api/gpu_lock.{hpp,cpp}` (`GpuLock`: `tools/gpu-lock.sh`'s protocol in C++), `api/render_tools.cpp`
  (`render.capture`, `render.start`, `job.status`, `job.wait`, `job.cancel`, `job.list`), `api/session.{hpp,cpp}`
  (the job table, the scratch directory, `job.wait` on the caller's thread, `Host::submitJob`), `api/cli_call.cpp`,
  `api/jsonrpc_server.{hpp,cpp}` (job notifications, capabilities, the generated protocol text),
  `app/offline_render.{hpp,cpp}` (`makeOfflineRenderEngine`, `makeOfflineRenderJob`, `makeGpuRenderBackend`),
  `app/application.cpp` (`makeRenderJob` calls the extracted function), `app/main.cpp` (the backend factory),
  `app/render_job.{hpp,cpp}` (`RenderHistory::Play`), `ai/main_thread_queue.{hpp,cpp}` (`waitForWork(timeout)`,
  `onPumpingThread`), `ai/engine_tools.cpp` (`render.probe {time|frame, width, height}`)
- Docs: `docs/api/protocol.md` (generated: render jobs, the GPU lock protocol), `docs/api/tools.md` (generated),
  `docs/help/reference-command-line.md`
- Tests: `tests/unit/test_api_jobs.cpp` (`[api][jobs]`, `[gpulock]`), `tests/rendering/test_engine_host_gpu.cpp`
  (`[gpu][api][jobs][adr2159]`)

## Context

After ADR-2155 an external client could build and edit a project through `avgen serve` and `avgen call`, but it
could not see it: the host had no GPU, and every GPU tool answered `UNAVAILABLE`. Rendering existed in three
owners (the Render panel's `Application::job_`, headless `--render`, `--queue`), all inside `Application`, whose
`init` creates a device unconditionally. A `--render` after an `--ai-script` rendered the FILE, so an unsaved edit
was not in the picture (the evaluation's runs 09/10). And GPU work on this machine is serialised by
`tools/gpu-lock.sh`, which a server process started by an agent would bypass unless it took the lock itself
(owner decision 2).

## Decision

1. **The GPU half is composed into `api::EngineHost`, not cut out of `Application`.** A `RenderBackend`
   (`app/offline_render.cpp`, injected by `main` as `EngineHostOptions::renderBackend`) creates the `gpu::Context`
   and `ShaderLibrary(defaultSearchDirs(exe))` on the first job that starts, under the lock, and never before. A
   host without a backend (the CPU suite, any embedder that passes none) cannot render: `canRender()` false, GPU
   tools `UNAVAILABLE`. `avgen call` with no render tool, and `avgen serve` until its first render, create no
   device. The Studio live loop is untouched.
2. **One FIFO of render jobs per host, capacity 4** counting the running one; a fifth is `BUSY`. One GPU job at a
   time, stepped in slices of about 50 ms between `session.pump()`s, so calls stay answered while it renders. The
   job's start (renderer init: the pipeline compile, 72 s measured cold by the evaluation) installs
   `gpu::setPipelineWaitHook`: about every 30 ms it reports `compiling pipelines: <landed> of <started>` into the
   record and pumps the host queue, so a cold compile is progress, not silence, and not a stall.
3. **Job life**: `queued -> preparing -> running -> succeeded | failed | cancelled`. Preparing loads a fresh
   Offline engine from the job's snapshot (CPU, before the lock), waits for the machine's GPU lock, creates the
   device on the first job, and starts the RenderJob. Cancel stops a queued job before it starts, and a running one
   after its current frame; the frames already written are kept and listed.
4. **The session owns the records, the host owns the GPU work.** `Session::jobs()` is a thread-safe `JobTable`
   (state, phase, progress, outputs, result, error); the host reports into it. `job.wait` blocks the CALLER:
   `Session::call` serves it on the caller's thread by waiting on the table, never through the host queue -- in
   `avgen serve` the reader thread waits and the host keeps rendering. When the caller is the host thread
   (`avgen call`), the body drives `Host::stepJobs` while it waits. `avgen call` also runs any job the calls did not
   wait for to the end before exiting.
5. **Every render renders the session's in-memory state.** The tool body snapshots the project with
   `Engine::writeProjectCopy` into the session's own scratch directory (`$TMPDIR/avgen-session-<pid>-<id>`,
   removed with the session). The user's file is not touched and nothing is saved; a render right after
   `parameter.set` shows it.
6. **Refusals cost no GPU work.** The output is resolved by `PathPolicy::forWrite` first (outside the write roots:
   `PERMISSION_DENIED`), then the snapshot, then the submission. Execution class `job`, permissions `gpu` and
   `write`, requirement `composition`. The S1 live policy already refuses the `job` class with `BUSY` while Studio
   runs a show (owner decision 3); a test holds it to that. A render job is exempt from "an explicit transaction is
   open and this call does not name it": it changes neither the project nor anything a rollback restores, so a
   client may preview a transaction's state. `job.*` are session tools (never in a transaction).
7. **A capture is a one-frame RenderJob** over `[t, t + 1/fps)`, written as a PNG (or EXR, by extension) through a
   staging folder in the scratch directory: the deliverable's render path, the same snapshot, warm-up, pre-roll and
   frame hash as a sequence. `render.start {range: "a:b"}` renders `[a, b)` (as `--range`) to a PNG or EXR
   sequence (`frame_000000.*`, numbered from the range's first frame) or a video.
8. **`history: play | seek`; `play` is the default.** `RenderJob::setHistory(Play)` warms up at, and plays from, the
   frame-aligned time `start - floor(start * fps) / fps` (0 for a frame-aligned start) and renders and discards
   every frame up to the range in `step()`'s slices -- the temporal pre-roll (ADR-1932) extended to the whole
   history, which it subsumes. `Seek` is today's behaviour and every existing caller's default. The measurement
   that chose `play` is below.
9. **Results** carry the per-frame hashes (`RenderJob::frameHashes`, 16 hex digits), the sequence hash, the last
   frame's hash, each output's sha256 and size, timing (queued, load, lock wait, start, render, total;
   `deviceCreated` for the job that created the device; `pipelinesCreated`), the lock (`path`, `waited`,
   `waitedSeconds`, `heldByAncestor`, `holderSeen`), the warnings the job logged on the host thread, and GPU errors.
10. **The machine's GPU lock, in-process (`api::GpuLock`), the script's protocol exactly**: the directory
    `${TMPDIR:-/tmp}/avgen-gpu.lock` (from the environment at each acquire, never from a source tree) made with
    `mkdir`; the pid written to `pid.<pid>` and renamed to `pid`; `holder` = `AVGEN_AGENT` or `avgen-<pid>`; a dead
    pid reclaimed only if `pid` still names it; a pid-less directory reclaimed after 30 s; a holder removes only
    its own. Held per job, from after the snapshot load to the end of the job's GPU work, released between jobs.
    A job waits up to `AVGEN_GPU_LOCK_TIMEOUT` (default 3600 s), polled every 250 ms between pumps, then fails
    `BUSY`. **Held by an ancestor**: when `pid` is this process or one of its ancestors -- the server was started
    under `gpu-lock.sh`, which exports nothing to its child -- the lock counts as ours: no wait, nothing created or
    removed. One deliberate difference: a pid of another user's process (EPERM) is alive here; the script would
    reclaim it.
11. **`makeOfflineRenderJob`**: `Application::makeRenderJob`'s steps (an Offline engine, `setLiveControl(false)`,
    `loadProject`, `requireProjectScene`, the warnings, the RenderJob, the debug overlays, `start`) moved verbatim
    into a free function; `Application` calls it. The API host uses its first half (`makeOfflineRenderEngine`)
    and starts the job later, under the lock.
12. **`render.probe` gains `time`/`frame` and `width`/`height`.** Without them it reads the camera the last frame
    used, which on a host that runs no frames is stale. With `time` it evaluates a copy of the in-memory project
    on a fresh Offline engine at that time, as a RenderJob positions its first frame; with `width` and `height`
    each rectangle is also given in pixels (`pixels`), for S8's changed-region check.

## Measured (2026-11-25, this branch, Apple M-series, under gpu-lock)

Scratch project outside every repository: the PoC art (`resonator.glb`, its skinned `Breathe` clip playing, and
`track.wav`), built through `avgen call --calls` (project.create, asset.import, scene.create_node, camera.set,
asset.import_audio, `modulation.create audio.bass -> nodes/resonator/emissiveBoost` with attack 20 ms / decay
250 ms), 480x270 at 24 fps. One `gpu-lock.sh` hold (`AVGEN_AGENT=engineapis6`).

- **The extraction changes nothing.** `--render R --range 0:2` (48 frames) with the binary built at the base's
  `application.cpp`/`render_job.*`/`main.cpp` and with this branch's: sequence hash `47cfd84be44c0eb7` both.
- **JSON-RPC end to end** (`avgen serve --stdio --root <scratch>`, a stdlib Python client): `initialize` answers
  `gpu`, `render`, `jobs` true; `project.open`, `render.capture {frame 24}` and `job.wait`. Cold (a new bundle id, so
  a cold Metal cache): 75.5 s, of which the start (device + 287 pipeline creations + warm-up) 74.1 s and the render
  (24 history frames + 1) 1.4 s; 329 `notifications/progress` reached the client, through the phases `loading the
  project snapshot`, `creating the GPU device`, `compiling pipelines: <n> of <m>`, `playing history`, `rendering`,
  `succeeded`. The same capture again in the same process (warm): 0.61 s (start 0.37 s). Both: frame hash
  `645df906acfb383c`, PNG sha256 `ca979fb8...` (the result's sha256 equals the file's). `job.status` was answered
  (0.4 s) while a sequence rendered. The lock was `heldByAncestor` (the run's own `gpu-lock.sh`, pid 52260): no
  wait, no deadlock.
- **The GPU suite's fixture** (a box whose emissive boost an `audio.bass` route with an envelope drives, under a
  timeline camera sweep, 160x90): capture frame 12 `play` equals frame 12 of `render.start 0:0.75`, byte for byte
  in the PNG too, and equals it again in a fresh host; `seek` differs in 6,492 of 14,400 pixels (45 %). At frame 0
  the two are the same render.
@@RESONATOR@@

## Alternatives considered

- **Extract the GPU half out of `Application` first** (the evaluation's Phase 1). Rejected for the PoC, as 03-boundaries
  section 2.2 argues: 4-7 engineer-weeks with live-loop timing risk, for nothing the PoC needs.
- **A GPU worker thread** running jobs off the host thread. Rejected: the RenderJob's engine is private, but the
  device and the pipeline wait hook are single-threaded today, and slices between pumps keep calls answered
  already (the compile hook pumps too).
- **Snapshot when the job starts** rather than when it is submitted. Rejected: a call made after the render was
  asked for would leak into it; the render renders what the client had when it asked.
- **`seek` as the capture default** (O(1)). Rejected by the measurement: it is not the sequence's frame.
- **No in-process lock; document that callers wrap `avgen serve` in `gpu-lock.sh`.** Rejected by owner decision 2.

## Consequences

- An external client renders and captures through the API, with hashes that equal the sequence's frame hashes, in
  a process that never created a device until it rendered and that waits its turn on the machine's GPU.
- A capture with `history: play` costs one rendered frame per frame before it (the end-to-end run above: 24 history frames plus the frame in 1.4 s cold, 0.19 s warm, at 480x270).
- Scene structure that the session changed is in the snapshot when the project saves it (`sceneNodes`, inline
  compositions, atmospherics); a file-backed scene document edited by a tool that writes only the scene file is
  rendered as the snapshot's project describes it, as the Render panel renders it.
- `docs/api/protocol.md` documents the job life and the lock protocol; `docs/api/tools.md` the six tools.

## Revisit triggers

- Stage S10: Studio adopts the EngineHost for the Render panel, the queue and `--render`/`--capture`; `--capture`'s
  output then changes (the probe below), which needs a release note.
- A client that needs a second request answered while it waits: `job.wait` then belongs on a request pool.
- A job class that is not a render (`render.pathtrace`, profiling): the FIFO and the record are generic; only the
  backend is render-shaped.
