# ADR-2165: Studio runs on an EngineHost: the device, the shaders, the Render panel, the render queue, `--render` and `--queue` are the host's; the live loop is not touched

- Status: Accepted (2026-11-26), eng/engine-api-s10 (Engine API stage S10; 113-engine-architecture, plan in
  03-phase0-api-contracts/03-boundaries.md section 2 and 04-poc-plan.md S10; owner spec 04-implementation/00-spec.md
  section 4 S10)
- Code: `api/engine_host.{hpp,cpp}` (a host over a borrowed engine; `ensureDevice`, `submitLocal`, `releaseJob`,
  `cancelJob`, `jobInLoop`, `pumpWhileCompiling`, `gpuLockPollSeconds`; API capacity counts API jobs only),
  `app/offline_render.{hpp,cpp}` (`GpuRenderBackend`, `createLocal`, `LocalRenderJob`, the surface layer and label
  options; `makeOfflineRenderJob` removed), `app/application.{hpp,cpp}` (`host_`, `initLiveRenderer`,
  `submitRenderJob`, `runRenderToEnd`, `servicePanelJob`; `makeRenderJob` and `uiQueue_` removed),
  `ai/control_plane.{hpp,cpp}` (the plane takes Studio's EngineHost), `gpu/pipeline_compile.{hpp,cpp}`
  (`pipelineWaitHook()`), `app/main.cpp`
- Tests: `tests/unit/test_api_jobs.cpp` (`[adr2165]`: held queue entries, capacity, cancel one; a borrowed engine's
  clock and live control), `tests/rendering/test_engine_host_gpu.cpp`; measured gate in ADR-2169

## Context

After ADR-2159 the engine had two owners of GPU work: `api::EngineHost` (a lazy device, a FIFO of render jobs, the
machine GPU lock), used by `avgen serve`/`call`, and `Application`, which created a device unconditionally in `init`,
stepped the Render panel's `RenderJob` itself (`job_->step(4, 0.010)` in the frame loop), kept the render queue as a
deque of scratch copies (`uiQueue_`), and ran `--render` and `--queue` through `RenderJob::run()`. 03-boundaries.md
section 2.2 chose to compose the host first and have Studio adopt it as a separate, measured step: this one.

## Decision

1. **Studio's host is an `api::EngineHost` over `Application::engine_`, borrowed.** A second constructor takes an
   engine someone else owns and changes nothing about it (live control, time policy, offered policies). The host is
   made right after the engine, before the control plane, which now takes it (`ControlPlane(EngineHost&, ...)`;
   the old constructor makes a GPU-less host of its own, which is what every test gets). The hooks, the show and the
   offered time policies are set through `host.local()`, as they were on the LocalHost.
2. **The device and the shader library are the host's.** `GpuRenderBackend` (app/offline_render) makes them as
   `Application::init` did: the window's CAMetalLayer (asked for when the device is made), Dawn's pipeline cache
   where the application keeps it, `AVGEN_PIPELINE_CACHE`, `defaultSearchDirs(argv[0])`, the default label. Studio calls
   `host->ensureDevice()` at the point where `init` used to call `gpu::Context::create` -- before the imgui layer and
   the renderer, without the GPU lock, as before -- and keeps raw pointers (`context_`, `shaders_`). The live
   renderer, the output mapper, the compositor, outputs, sharing, imgui and the stills session are built from them
   and stay `Application`'s (03-boundaries 2.1). Teardown resets the host where it reset the context: after
   everything built from the device, before the window.
3. **Render jobs are the host's.** The operator's jobs are host *local* jobs: a record in Studio's own `JobTable`
   (`studioJobs_`), and a `prepare` step -- `GpuRenderBackend::createLocal` -- that loads the project file with
   `makeOfflineRenderEngine` and builds the RenderJob over the operator's full `RenderSettings`, the debug overlays and
   the output base, exactly what `makeRenderJob` built. They do not count against an API client's capacity of four.
   - **The Render button** submits one (`submitRenderJob`); it loads its scratch copy (and removes it) when it starts,
     at the host's next step -- the frame after the press, which is the frame the panel's job always stepped in.
   - **The render queue** is held local jobs. Enqueue writes the scratch copy and submits a held job ("queued: Run
     queue starts it"); Run queue releases one; when it succeeds the next is released; a failure stops the queue.
     The order and the stop-on-failure are the deque's.
   - **The panel reads the RenderJob** through a `shared_ptr` the job hands over when it starts (`onStarted`), so
     progress, the preview frame and the last frame's status read exactly what they read before. Cancel cancels the
     panel's job wherever it is (`cancelJob`).
   - **The slice** is the one it was: Studio's host steps 10 ms per UI frame and the panel's job caps itself at four
     frames (`LocalRenderJob::maxFramesPerStep`); `--render`/`--queue` one frame per step as `RenderJob::run` did,
     with its once-a-second progress line, and its exit codes (2: never started; 7: failed; 5: failed with GPU errors).
   - **Studio's pipeline wait hook survives a job.** A job's start installs the host's progress hook in front of the
     process's own and puts the old one back (`gpu::pipelineWaitHook()`); before, it cleared it. Studio's host does
     not pump the assistant's queue from inside a compile (`pumpWhileCompiling = false`); its window hook keeps
     pumping system events, as it did.
4. **The live loop is not restructured.** Its three reads of "a render job exists" -- the frame cap ("never while a
   render job shares the loop"), the late latch, and the ADR-364 viewport suspension -- read `host_->jobInLoop()`: a
   job is running or can start at the next step. With no job it is false, as `job_` was null; held entries and a job
   paused by a show (ADR-2167) do not count. The job step sits where `job_->step` sat, under the `render.job` phase.
5. **The machine's GPU lock now covers Studio's renders and the command line's.** Each local job takes it, as an API
   job does (ADR-2159): under `tools/gpu-lock.sh` it is held by an ancestor and costs nothing; otherwise a render
   waits while another process holds it, polled four times a second, with the phase on the Render panel's status
   line and in the `--render` log.
6. **Studio's assistant can render.** Its session is on a host with a GPU half, so `render.capture`/`render.start`
   are available in Studio and in a headless `--ai-script` run (they were `UNAVAILABLE`); the jobs step in the frame
   loop, and a batch run finishes the ones its script did not wait for, as `avgen call` does.

## Alternatives considered

- **Extract the GPU half out of `Application` into the host** (the evaluation's Phase 1). Rejected again for the same
  reason as in ADR-2159: the live loop's timing is the risk, and the move needs none of it.
- **Keep Studio on a LocalHost and give it a pointer to a separate EngineHost.** Rejected: two hosts over one engine,
  two queues and two notions of the show, and the assistant still could not render.
- **Run the render queue as unheld jobs** (submitted all at once). Rejected: Enqueue would start rendering, and a
  failure would no longer stop the queue.
- **Make the panel's job an API job** (`RenderRequest`). Rejected: a request carries size, range, rate, tier and
  format, not the panel's codec, AOVs, post stages, supersample, pattern and output base.

## Consequences

- One owner of GPU work in every process: a device, a FIFO, one job at a time, the machine lock.
- `--render` and `--queue` no longer build and discard a second renderer (`Application`'s, which they never used):
  the start-up cost of its pipeline compile is gone from them (ADR-2166).
- The Render panel's status says `render waiting for the GPU lock (held by ...)` when an agent holds the lock; the
  owner's render waits for it. A render started under the lock is unchanged.
- A job's load error reads as it did ("render failed: ..." on the panel, `render: ...` from the command line).
- Director stills (`StillsSession`, on the host's device) and the Director's scratch renders (their own threads and
  engines) are not host jobs: they neither wait for the lock nor are held by a show (unchanged; listed in ADR-2167).

## Revisit triggers

- A windowless live host (03-boundaries 2.1): the renderer, compositor, outputs and sharing move then.
- Director scratch renders as host jobs.
