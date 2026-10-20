# ADR-1562: Pipelines compile on Dawn's workers, the heavy ones in parallel, and the window says so

**Status:** Accepted, 2026-10-20 · **Range:** 1560-1569 (eng/perf-reliability)

## Context

Every pipeline was created synchronously on the main thread, which is where Metal compiles its shaders. A cold launch
(ADR-1560's recipe) did 130 pipeline creations in a row while the window stayed blank. On 2026-10-19 that was
4 min 45 s, under load. ADR-1561 cut the compile work from 292 s to 119 s. Even so, a cold launch was still a blank,
unresponsive window for two minutes.

Measured while making this change:

- **Metal compiles two pipelines at a time per process.**
  - Sampling the compiler during a cold launch never showed more than two busy `MTLCompilerService` processes.
  - The process's requests wait on Metal's per-connection serial queues (`MTLCompilerScheduler::buildRequest`).
  - None of these raised the limit: `MTLDevice.shouldMaximizeConcurrentCompilation` (already 12),
    `MTL_FIXED_COMPILER_PROCESS_COUNT`, `MTL_MAX_COMPILER_TASKS`, `MTL_LEGACY_COMPILER_PROCESS_COUNT`,
    `MTL_MAXIMUM_CONCURRENT_COMPILATION_TASK_COUNT_API_OVERRIDE` and `MTL_COMPILER_SCHEDULER_VERSION`.
  - So parallel creation can at best halve the wall time.
- **Async creation by itself is no faster than sync.** Interleaved A/B, quiet machine:
  - `procedural-opaque`: 23.5 s sync, 23.8 s async;
  - `procedural-depth`: 19.7 s sync, 20.5 s async.

## Decision

1. **`gpu::createRenderPipeline` and `gpu::createComputePipeline` compile through Dawn's async creation.**
   - The calling thread waits in 30 ms slices and calls the **pipeline wait hook** between them.
   - The pipeline returned is the one the call always returned.
   - If an async creation fails, the call repeats the synchronous creation. That yields the error object it always
     returned and raises the error into whatever error scope the caller pushed, so every existing error path is
     unchanged.
   - The astral renderer's five direct `device.Create*Pipeline` calls and the procedural wire pipeline's call now go
     through the wrappers too.
2. **`gpu::PipelineBatch` with `startRenderPipeline` / `startComputePipeline`.**
   - While a batch is alive, `start*` begins a compile on Dawn's workers. `*out` is set when it lands, and
     `PipelineBatch::finish()` waits for all of them, naming any that failed.
   - With no batch, `start*` creates on the spot. A renderer initialised on its own, such as the astral test's
     `ParticleRenderer`, therefore behaves exactly as before.
   - The destructor waits for anything still in flight, so an early return cannot leave a callback writing into a
     dead member.
3. **`SceneRenderer::init` runs inside a batch and starts its heavy pipelines first:**
   - the PBR lit set;
   - the procedural lit, depth and wire pipelines, the effectors and the fiber strands;
   - the SDF raymarch set;
   - the particles' `cs_emit`, `cs_simulate`, `cs_latent` and `cs_latent_project`;
   - every simulation kernel;
   - the volume march.

   It waits at its end, before its GPU-error check. `sdfs_->setMeshPipelines` moves after the wait. Hot reloads
   still create synchronously, all or nothing.
4. **The editor shows a "Compiling shaders" screen.**
   - `Application::init` creates the ImGui layer before the renderer and installs a hook that pumps the window and
     draws a centred panel each tick.
   - The panel shows a progress bar (compiled, compiling), the elapsed seconds and one line saying that the first
     launch after an update compiles each shader once. The title bar shows the count too.
   - Closing the window during the compile ends the launch once the compile it is in returns.
   - After init the hook only pumps the system event loop (`platform::Window::pumpSystemEvents`), so a later
     synchronous creation, such as a project load or a shader reload, never makes macOS mark the app as not
     responding.
5. **`AVGEN_SYNC_PIPELINES=1`** puts every creation back on the calling thread, as an A/B switch and an escape hatch.

Headless runs, offline renders and tests install no hook. They block where they always blocked and get identical
pipelines.

## Consequences

Measured on a quiet M2 Max with avgen's Metal cache cleared:

| | Before (416d48f1) | ADR-1561 only | ADR-1561 + this |
|---|---|---|---|
| Headless cold launch | 305 s | 120 s | 75 s |
| Windowed cold launch | blank window | blank window | 77 s, progress screen throughout |
| Warm launch | 1.7 s | | 1.7 s |

- **The compiled-materials variant (ADR-1493).** Rainbow World's lands 8.5 s after the first frame, cold. With
  416d48f1's shaders it was 14.5 s.
- **The floor is Metal's two compile lanes.** About 119 s of compile work over two lanes is about 60 s.
  - `sdf-raymarch` alone is 17 s. Its fragment stage inlines the SDF tree evaluator at about ten call sites (march,
    density march, normal taps, occlusion, soft shadow, edge), the same shape ADR-1561 fixed for the procedural
    vertex stage. That would be the next cut.
- **Measuring with `AVGEN_PIPELINE_TIMING`.** A started pipeline's time is logged when its callback runs, at
  `finish()`, so it reads long. Use `AVGEN_SYNC_PIPELINES=1` for each pipeline's own compile time.
