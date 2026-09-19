# Offline rendering: what is already here, what the brief assumes wrongly, and the order to build in

**Branch:** `agent/mbackend` · **Date:** 2026-09-19 · **Base:** `8f23d2ec`

This is the audit the brief's own section 1 demands, and it is deliberately written before any
refactor. Everything below was read in this tree or measured on this machine. Where a number is
quoted, the command that produced it is quoted with it. Where the brief is wrong, this document
says so plainly rather than working around it, which is what the owner asked for.

## 0. The short version

The brief asks for a multi-backend offline rendering architecture, a Metal ray-tracing backend, a
hybrid renderer, a production video pipeline, viewport suspension, and an ImGui fix. Of those:

| Brief asks for | State in this tree |
|---|---|
| A renderer-agnostic **video output** system, ProRes/H.264/HEVC, MOV/MP4, audio muxed | **Built and shipping** (ADR-020, `src/assets/video_writer.*`). Its research section is answered. |
| A **mode selector** in the UI (Raster / Hybrid / Full PT) | **Half-built**: the Render panel already has `Renderer: (o) Realtime ( ) Path trace`. |
| A **full path tracer** with AOVs, denoising, determinism, capability reporting | **Built** (`src/pathtrace/`, ADR-351/352/353), reachable from the Render panel and the CLI. |
| **EXR / HDR image output** | **Built** — including multi-layer AOV EXR with correct `normal.X/Y/Z` naming. |
| **Bounded producer/consumer frame queue**, backpressure, progress, cancellation | **Built** for the raster job (`RenderJob`, `queueLimit_ = 8`, `ReadbackRing`, encoder threads). |
| **Viewport suspension** during an offline render | **Does not exist, at all.** No render state, no throttle, no suspend. The panel advertises the opposite. |
| **HDR end-to-end to video** | **Does not exist.** `VideoWriter::writeFrame` takes RGBA8 and the AVFoundation pool is `kCVPixelFormatType_32BGRA`. |
| **Metal ray-tracing backend** | Does not exist. Feasible — but see section 5, the target machine has no hardware RT. |
| **Backend abstraction** (Embree / Metal / OptiX / Vulkan) | Does not exist; `PathTracer` owns `EmbreeScene` directly. |

The two things the brief is most wrong about are the video system (already decided, already built)
and the value of a Metal backend on this machine (measured below, and it is not what the brief
assumes). The one hard requirement the brief names that is genuinely absent is **viewport
suspension**, and it is also the cheapest large win available.

## 1. What exists

### 1.1 Two renderers over one scene contract

`scene::Scene` holds CPU-side triangles (`std::vector<MeshData>`) and texture pixels permanently;
`SceneRenderer::uploadMeshes` only *copies* from them. Both renderers read the same memory.

* **`avgen::rendering`** — the WebGPU/Dawn rasteriser. Right-handed, +Y up, metres, depth 0..1,
  glTF's convention.
* **`avgen::pathtrace`** (`src/pathtrace/`, 3,628 lines, ADR-351) — a CPU path tracer over Embree
  4.4.0. Phases 0-6 and 8 are done: glTF metallic-roughness BRDF, textures, MIS, Russian roulette,
  OIDN denoising (opt-in), CPU skinning, Embree instancing, and six AOVs. Phase 7 (Glowmere) is
  not started.

`pathtrace::Snapshot` is the transient render snapshot; `PathTracer::render` fills a
`pathtrace::Framebuffer` of `glm::vec3` scene-linear radiance plus feature buffers. The tracer
never tone maps — it stops at scene-linear radiance and the colour pipeline is downstream of the
EXR.

`Snapshot::capabilities` already carries a per-feature capability report with nine rows of
"unsupported / degraded / supported" and prints it at startup. **The brief's section on explicit
feature tracking is already implemented for the CPU backend**, and any new backend should extend
that table rather than invent a rival one.

### 1.2 The offline batch pipeline (ADR-020) is not the path tracer

`app::RenderJob` + `app::RenderSettings` are the frame-sequence pipeline: a dedicated
`EngineMode::Offline` engine with a fixed-step clock, a `gpu::ReadbackRing` so the copy rides in
the frame's command buffer, per-frame hashes in frame order, and encoder threads. Output is
`PngSequence | Video | ExrSequence`. It already has:

* a **bounded** `std::deque<Pending> queue_` with `queueLimit_ = 8` and backpressure — exactly the
  producer/consumer design the brief's "BOUNDED VIDEO BUFFERING" section asks for;
* `RenderProgress` with frames rendered / read back / written, elapsed, fps, ETA and a sequence
  hash;
* `cancel()`, which stops after the current frame and keeps partial output;
* `step(maxFrames, budgetSeconds)` so the job can be pumped from the UI frame loop;
* a live frame preview of the deliverable's own pixels (ADR-320);
* AOV export as separate EXR sequences (ADR-242) and post-stage capture (ADR-277).

ADR-351 is the thing to cite when "offline" is ambiguous: **"offline" is a quality tier and a batch
pipeline; "path trace" is a renderer.** The brief uses "offline" in the ray-tracing sense
throughout. That is the single largest source of confusion in the document and this audit does not
adopt it.

### 1.3 Video output: already decided, already built

ADR-020 chose, and `src/assets/video_writer.*` implements:

* a **native AVFoundation backend** (`video_writer_apple.mm`, 740 lines): ProRes 4444, ProRes 422,
  H.264, HEVC into `.mov` / `.mp4`; audio muxed with `AVAssetReader` over the sample range, re-timed
  so `audioOffsetSeconds` maps to movie time 0, kept ahead of the video by a lead window, trimmed at
  the video's end, and `markAsFinished` when exhausted so a short track does not stall the writer;
* an **ffmpeg backend** using a *user-supplied* executable started with `posix_spawn` — never
  linked, never shipped — with raw RGBA on stdin and the last 2 KB of its stderr quoted into any
  error;
* `backend = "auto"`, which prefers native and falls through to ffmpeg.

**The brief's "VIDEO CODECS / CONTAINERS" research section is therefore already answered**, and its
"Do not introduce FFmpeg blindly" concern was decided the way it wanted, three years of ADRs ago.
Restating that research would be the duplication the brief's own Code Quality section forbids.

### 1.4 The Render panel already distinguishes mode

`ControlPanel::drawRender` (`src/ui/control_panel.cpp:2926`) opens with
`Renderer: (o) Realtime ( ) Path trace` and dispatches to `drawPathTrace()` at `:2802`. Resolution
is drawn by a single shared control so the two halves cannot disagree about output size. The
path-trace half exposes samples, bounces, denoise (greyed with an explanation when the build has no
OIDN), AOVs and the ADR-352 albedo probe, with an honest progress rule: a bar only for the one
stage that can measure itself.

The panel is reached from **View → Render** only. It is not open by default and has no shortcut.

## 2. What the brief assumes that is wrong

Numbered so they can be cited individually.

### W1. "Do not introduce FFmpeg blindly. Evaluate licensing, ProRes support, hardware acceleration…"

Already done, ADR-020, and the outcome is the one the brief would have reached. See 1.3. **This
research section should be struck from the plan**, not re-run.

### W2. "The output pipeline must be independent of renderer/backend" — it already is, in the one place that matters, and is not in another

`assets::VideoWriter` and `assets::writeExr` / `writeExrLayers` take plain pixel spans and know
nothing about a renderer. They are already backend-agnostic. What is *not* agnostic is that
**`RenderJob` is the only thing that drives them over a frame range.** The path tracer's
`TraceJob` writes exactly one EXR and cannot produce a sequence at all — the UI tooltip says so:
*"One frame, not a sequence -- a path-traced sequence is a queue of these and is not built yet."*

So the gap is not an abstraction over output sinks. It is **a frame-range driver that is not
welded to the WebGPU rasteriser.**

### W3. "The real gap is HDR through to video" — correct, and here is exactly where it breaks

Confirmed in three places, all of which must change together:

1. `VideoWriter::writeFrame(std::span<const std::uint8_t> rgba)` — 8-bit, exactly `w*h*4` bytes.
2. `video_writer_apple.mm:143,417` pins the CVPixelBuffer pool to `kCVPixelFormatType_32BGRA`.
   **ProRes 4444 is a 12-bit format being fed 8-bit source.**
3. `video_writer.cpp` feeds ffmpeg `-f rawvideo -pix_fmt rgba`, then `yuv420p` for the lossy
   encoders.

And a fourth thing nobody has written down: **the files are written with no colour tags at all.**
There is no `AVVideoColorPropertiesKey` in the native backend and no `-color_primaries` /
`-color_trc` / `-colorspace` in the ffmpeg arguments. Every deliverable this project has ever
produced is untagged and every player is guessing. Today the guess is usually right (BT.709), which
is why it has never been noticed, and it is a prerequisite for any HDR work.

### W4. The colour pipeline cannot currently be shared, because half of it does not exist on the CPU

The brief says "Maintain one coherent colour pipeline … Do not apply AgX/tone mapping twice."
Agreed — but **there is no CPU implementation of the tone map.** ACES, AgX, Reinhard and Khronos
PBR Neutral live only in `shaders/tonemap.wgsl`. `render_job.cpp:804-810` states this deliberately
and refuses to make a second copy, on the grounds that a CPU twin would be free to drift from the
picture it claims to be of.

Consequence the brief does not anticipate: **a path-traced frame cannot be turned into a video
frame without a GPU.** The tracer's selling point is that it needs no GPU at all. Either
(a) path-traced video acquires a GPU dependency for the tone map, or (b) somebody ports five
operators to C++ and owns a parity test against the WGSL, in the style
`tests/rendering/test_material_gpu.cpp` already uses for `color.wgsl` (matched to 1e-4). This is a
real decision and it belongs to the owner. **The conservative choice is (b) with a parity test**,
because it keeps the tracer's "no GPU" property, which is the reason it is usable on this shared
machine at all.

### W5. "Hidden viewport does NOT continue rendering underneath" — it does, and by design, and the panel says so

There is **no render-state enum, no `paused` flag, no `needsRedraw`, no idle throttle, no frame
limiter** anywhere in the application. The only three cost-shedding behaviours are: skip when the
window is minimised (`application.cpp:3476`), a 16 ms backoff on a failed swapchain acquire
(`:3670`), and `PresentMode::Fifo` hardcoded at `gpu/surface.cpp:70`.

An in-app render is pumped from the main loop as `job_->step(4, 0.010)` (`application.cpp:3802`) —
up to 4 frames or 10 ms per UI frame — and the Render panel's closing line advertises
*"renders load the saved project; the live view keeps playing."* This is a deliberate feature, not
an oversight: it is what lets a person keep working. **The brief's hard requirement therefore
conflicts with a shipped, documented behaviour**, and the resolution has to be a user-visible
choice rather than a silent reversal. See R3.

### W6. "Metal is the first GPU ray-tracing backend … the practical high-quality GPU rendering mode"

Measured on this machine, not assumed. A twelve-line Objective-C++ probe
(`MTLCreateSystemDefaultDevice`, compiled and run 2026-09-19):

```
device: Apple M2 Max
supportsRaytracing: 1
supportsRaytracingFromRender: 1
family Apple7: 1
family Apple8: 1
family Apple9: 0
family Metal3: 1
hasUnifiedMemory: 1
recommendedMaxWorkingSetSize: 55.66 GB
maxBufferLength: 41.75 GB
```

`supportsRaytracing` is true, so the API is available and a backend is buildable. But
**`MTLGPUFamilyApple9` is false**: Apple's hardware ray-tracing units arrive with M3, and this is an
M2 Max. Metal ray tracing here runs `intersect()` on the shader cores against a GPU-built BVH — it
is compute, not dedicated silicon.

It may still beat a 12-core Embree, because the GPU has far more ALUs. It may not, because the CPU
path is already SIMD-wide, cache-friendly and has 64 GB of the same unified memory. **The brief
treats the win as given. It is not given, and it is cheap to find out.** The good news is in the
same numbers: 55.66 GB recommended working set and unified memory mean the Tree of Life's 3.16M
triangles are not a memory problem, and no CPU/GPU geometry upload is needed in the usual sense.

### W7. "Do not attempt to force this through the existing WebGPU/Dawn abstraction" — agreed, and the interop already exists

The brief is right that the offline ray tracer should use native Metal. What it does not know is
that **this project already does Dawn↔Metal interop, in production, and there is a working
pattern to copy.** `src/share/syphon_share.mm` creates its own `MTLDevice` and exchanges frames
with Dawn through `wgpu::SharedTextureMemoryIOSurface` plus `SharedFenceMTLSharedEvent`, entirely
GPU-side, with no CPU wait. It is gated on `context.capabilities().sharedTextureIOSurface`.

`enable_language(OBJC OBJCXX)` is already on and `-framework Metal` is already linked into
`avgen_gpu`. **A Metal RT backend needs no new dependency and no new build machinery.**

### W8. "Ray-traced shadows / reflections composited over the raster base" — the G-buffer is already there, minus one channel

`SceneRenderer` already writes, every frame, as real targets with public accessors: world-space
normal + roughness (`normalRoughnessTexture`, RGBA16F, octahedral-encoded), linear depth in metres
(`linearDepthTexture`, R32F), emission (RGBA16F), screen-space velocity (RG16F), object id (R32U),
and the HDR scene colour (`hdrOutputTexture`, RGBA16F). Six of the brief's seven debug
visualisation modes are therefore already renderable.

**The one channel that is missing is base colour / albedo.** Ray-traced shadows and specular
reflections can be composited from what exists; diffuse GI cannot, because a bounce needs an albedo
to multiply by. That is a concrete, bounded piece of new renderer work and it should be costed
before "hybrid GI" is promised.

### W9. Naming

The brief's `OfflineRenderManager` / `HybridOfflineRenderer` / `FullPathTracer` collide with
`QualityTier::Offline` and `app::RenderJob`. ADR-351 already refused those names once for the same
reason. Section 6 proposes names that do not collide.

## 3. Gaps the brief does not mention, found while auditing

### G1. The path tracer's settings are not settings (ADR-350)

`pathtrace::TraceSettings` — samples per pixel, bounces, denoise, AOVs, the timeline second, the
albedo probe, the seed, the thread count — is held **only** in `Application` member fields
(`application.hpp:349-353`). Nothing writes it to the project and nothing reads it back. There is no
`"pathtrace"` key in any project file, scene file or `AppSettings`. Grep for it returns nothing.

Set 512 spp and 8 bounces, save the project, reopen it: you get 32 spp and 3 bounces, because
`application.cpp:1515-1516` hard-codes those two and defaults the rest.

This is the exact defect ADR-350 records against `DayNightSettings`: **no reader and no writer.**
`app::RenderSettings` by contrast round-trips properly through `doc["render"]`
(`engine.cpp:1234`, `:1877`), so the pattern to follow is one file away. Any backend selection,
codec choice or mode selection added by this work needs both halves and a save/reload/save test —
ADR-350's second prescribed test, which asserts a non-default value survives **two** round trips,
not one.

Not persisted in `RenderSettings::toJson` either, and worth deciding rather than inheriting:
`encoderThreads`, `disablePasses`, `qualityArms`, `postStages`. The last three are deliberate
(they are diagnostics). `encoderThreads` looks like an oversight.

### G2. The path tracer's output path is unreachable while the path tracer is selected

`drawRender` returns at `control_panel.cpp:~2947` before it draws the output-path `InputText` and
the `Choose...` button, but `startPathTraceFromUi` reads `uiRender_.outputPath`
(`application.cpp:4437`). A user who has never selected Realtime and set a path gets their trace
silently written to `$TMPDIR/avgen_pathtrace.exr`. This is a small bug with the ADR-350 shape and it
should be fixed in the same pass as the ImGui work.

### G3. Six raster render settings are CLI-only

`aovs` (ADR-242), `supersample` (ADR-212), `postStages` (ADR-277), `disablePasses`, `qualityArms`
and `encoderThreads` have no widget. The brief's UI PRINCIPLE section covers at least `aovs` and
`supersample` — both are properties of the deliverable, not diagnostics. `supersample` in
particular is the documented answer to foliage undersampling (2.80% chroma noise at 720p against
1.86% at 1440p) and a person cannot reach it.

Related and worse: the Performance panel's forensic arms *look* like they apply to a render and do
not — they are never copied into `uiRender_`.

### G4. The codec combo advertises codecs the machine may not have

`control_panel.cpp:3081` is a hardcoded array of eight codec ids including four ffmpeg-only ones
(`libx264`, `libx265`, `prores_ks`, `libvpx-vp9`). It is not filtered by `nativeCodecs()` or by
whether `findFfmpeg()` found anything. The brief's "Do not expose unavailable backends" rule is
already violated by the shipping UI, one layer below where the brief is looking.

### G5. Three unrelated job abstractions, no common progress type

`app::JobSystem`/`JobStatus` (world building, AI control plane, song analysis — 2 workers),
`app::RenderProgress` (raster renders) and `pathtrace::TraceProgress` (traces) share no type and no
UI. `JobSystem::pause`/`resume` exist and no UI exposes them. Adding a third renderer must not add a
fourth progress type.

### G6. The render-suite bus error, and the half of it that is cheap

ADR-358 records it: `avgen_render_tests` dies with a bus error in a full-suite run; the last thing
printed is `tests/rendering/test_wind_gpu.cpp:272 FAILED`, then SIGABRT while Catch2 stringifies a
four-megabyte byte vector. It reproduces at the merge base, so it is on `main`.

The reporter half is a one-line pattern. `test_wind_gpu.cpp` has four assertions of the form
`REQUIRE(a.rgba == b.rgba)` over ~4 MB `std::vector<uint8_t>`; `test_post_gpu.cpp:398` and
`test_motion_gpu.cpp:191` have two more. Catch2 stringifies both operands on failure. Every other
GPU test in the suite already does the right thing — `test_gpu.cpp:1346-1349` and
`test_resource_lifetime_gpu.cpp:136` compare sizes and then count differing bytes. Converting the
six outliers to that form costs nothing, makes the failure survivable, **and makes it diagnosable**,
which is what is needed to tell GPU contention from state leaking between tests.

This is not this branch's defect and fixing the reporter half does not fix the underlying
non-determinism. It is listed because it is cheap and because the underlying bug cannot be
investigated while the reporter kills the process that found it.

## 4. The architecture this codebase can actually support

The brief's separation of **mode** from **backend** is right and is kept. What changes is where the
seam goes, because the codebase already has one seam and the brief proposes a different one.

```
                        mode                      backend
                          |                          |
  Render panel:  Realtime | Path trace     Auto | CPU (Embree) | Metal
                    |          |
                    v          v
            app::RenderJob   pathtrace::TraceJob        <- exist today
                    \          /
                     \        /
                  FrameRangeDriver                      <- the missing piece
                          |
                          v
                 HDR or LDR frame
                          |
                 output transform
                          |
              +-----------+-----------+
              v                       v
        assets::writeExr      assets::VideoWriter       <- exist today, backend-agnostic
```

Three points where this differs from the brief's diagram:

1. **There is no `OfflineRenderManager` above the renderers.** The thing that is actually missing
   is *below* them: something that walks a frame range, asks a renderer for frame *n*, and hands
   the result to a sink. `RenderJob` contains one welded to the rasteriser. Extracting that driver
   is the highest-value refactor in the whole brief, and it is what makes "Embree can use the new
   video-output pipeline" true.

2. **The ray-tracing backend seam goes inside `pathtrace`, at `EmbreeScene`**, not around
   `PathTracer`. `EmbreeScene` is already the only file that includes Embree, behind a pimpl, with
   `embree` a PRIVATE link dependency. It already answers exactly two questions — intersect and
   occluded — and everything else (materials, BSDFs, sampling, the integrator, AOVs, accumulation,
   output) is AV Gen's. That is already the backend abstraction the brief asks for; it needs an
   interface extracted from it, not a new hierarchy above it.

   This matters: a Metal backend that only answers intersect/occluded reuses the entire integrator,
   which is where ADR-352's carefully-measured BRDF behaviour lives. A Metal backend that
   reimplements shading in MSL forks that and the two will disagree.

3. **"Hybrid" is not a third renderer.** It is the existing rasteriser plus one or more ray-traced
   contribution passes composited into the HDR target. It belongs in `src/rendering/`, consuming
   the G-buffer that already exists (W8), not in a new parallel renderer.

### Proposed names (ADR-351's rule: name the technique, not the schedule)

| Brief | Here | Why |
|---|---|---|
| `OfflineRenderManager` | `app::FrameRangeDriver` | says what it does; "offline" stays a tier |
| `RayTracingBackend` | `pathtrace::TraceBackend` | lives beside `EmbreeScene` |
| `EmbreeRayTracingBackend` | `pathtrace::EmbreeBackend` | the existing `EmbreeScene`, behind the interface |
| `MetalRayTracingBackend` | `pathtrace::MetalBackend` | |
| `HybridOfflineRenderer` | `rendering::RayContributionPass` | it is a pass, not a renderer |
| `FullPathTracer` | `pathtrace::PathTracer` | already exists |

## 5. The Metal question, stated as a decision rather than a task

The brief treats "implement a Metal ray-tracing backend" as settled. On the measured hardware (W6)
it is not, and the honest framing is:

**Option A — prototype first, decide after.** Build the smallest possible Metal RT program outside
AV Gen (the same discipline ADR-351's Phase 0 used for Embree): build an acceleration structure
over the Tree of Life's real triangles, trace primary rays, shade nothing, and time it against
`EmbreeScene` doing the same on the same geometry under the GPU lock. Two numbers, minima over
repeats, with a control. Cost: a day or two. If Metal is not decisively faster on Apple8 silicon,
the entire Metal branch of the brief is deferred and nothing has been wasted.

**Option B — build the backend and find out.** Weeks, and the measurement arrives at the end.

**Recommendation: A.** It is the conservative choice, it is what this repository's own ADRs did for
Embree and for OIDN, and the brief's own "METAL PROTOTYPE FIRST" section already asks for it. The
only thing added here is that the prototype's *purpose* is to decide whether to proceed, not to
prove that Metal works — Metal works; the question is whether it is worth anything on an M2.

Flagged as the owner's decision: if the answer is "the target machine for delivery is an M3/M4",
that changes the arithmetic completely and should be said before the prototype is built.

## 6. Revised implementation order

The brief's order is 1 audit, 2 ImGui, 3 renderer abstraction, 4 output pipeline, 5 video,
6 viewport suspension, 7 Metal prototype, 8 benchmark, 9 hybrid, 10 features, 11 validate.

Steps 4 and 5 are largely already done (1.3, W2) and step 3 is the biggest change in the list while
being the one with the least user-visible payoff. That is the wrong order for a codebase in this
state. Revised, with the reason for each move:

| # | Work | Why here |
|---|---|---|
| **1** | **Audit** — this document | unchanged |
| **2** | **ImGui ID conflict** + audit the surrounding dynamic UI | unchanged; self-contained, the owner hits it |
| **3** | **Make the path tracer's settings settings** (G1, G2) — reader, writer, round-trip test | ADR-350's defect, present today, cheap, and it is a precondition for *every* later setting this work adds. Adding a backend selector to a settings object that is never saved repeats the defect at a larger scale. |
| **4** | **Render-state + viewport suspension** (W5) | The brief's one hard requirement that is genuinely absent. Independent of every renderer question. Needs an owner decision on the *default* (see R3). |
| **5** | **Extract `FrameRangeDriver` from `RenderJob`** | The actual missing abstraction (W2). Small, testable without a GPU, and it is what makes step 6 possible at all. |
| **6** | **Path-traced sequences** — a frame range of traces to EXR, then to video | The first genuinely new user-facing capability, and it falls out of step 5. Forces the W4 colour decision. |
| **7** | **Colour tagging on video output** (W3.4) — **done, ADR-365** — plus the W4 tone-map decision, which is not | Small, overdue, and a prerequisite for anything HDR. |
| **8** | **Expose the CLI-only render settings** (G3, G4) | UI PRINCIPLE, and they already exist below the UI. |
| **9** | **Extract `pathtrace::TraceBackend` from `EmbreeScene`** | Only now, when there is a second backend candidate worth measuring. Doing it earlier is an abstraction with one implementation. |
| **10** | **Metal RT prototype, outside the tree, to decide** (section 5) | Under `tools/gpu-lock.sh`, minima over repeats, with a control. |
| **11** | **Metal backend, or defer** | Gated on 10 |
| **12** | **Hybrid ray contribution pass** — shadows first, then reflections | Gated on 11, and on costing the albedo target (W8) |
| **13** | **HDR through to video** (W3.1-3) | Large, and the deliverable it buys should be confirmed as wanted before it is built |

Steps 2, 3, 4, 7 and the G6 reporter fix are all independently shippable and none of them depends
on a single architectural decision. That is deliberate: the brief's own step 3 would have had the
branch touching the path tracer's internals before anything a person can see had changed.

## 7. Decisions that are the owner's, with a recommendation

**R1 — the CPU tone map (W4).** Path-traced video needs a display transform, and the only one that
exists is a WGSL shader. Options: (a) route path-traced frames through the GPU for tone mapping,
losing the tracer's "needs no GPU" property; (b) port the five operators to C++ with a 1e-4 parity
test against the WGSL, in the style `test_material_gpu.cpp` already uses for `color.wgsl`.
**Recommended: (b).** Conservative, keeps the property that makes the tracer usable on a shared
machine, and the parity test is the thing that stops the drift the existing comment warns about.

**R2 — Metal (section 5).** Prototype and measure before committing. Recommended: prototype.

**R3 — the viewport-suspension default (W5).** "The live view keeps playing during a render" is a
shipped, documented, deliberate feature. The brief demands the opposite as a hard requirement.
Recommended: make it a **setting with a reader and a writer**, defaulting to **suspend**, because
that is what the brief asks for and what a person doing a final render wants — but keep
"keep the viewport live" reachable in one click, because it is what a person iterating wants, and
silently removing it would be its own ADR-225 failure in reverse. Both states need the ADR-182
treatment: a test that measures frames actually submitted, with a control that could have come out
the other way.

**R4 — HDR video (W3).** It is a real gap and it is also a large piece of work whose deliverable
nobody has asked for by name. Recommended: do the colour tagging now (step 7, cheap, fixes a real
defect) and defer the 10/12-bit pipeline until somebody names the delivery that needs it.

## 8. The ImGui ID conflict — root cause and fix

Done on this branch. ADR-361 has the full record; the short version:

* **The error message's "Empty label" line is not a clue.** All three lines are static bullets
  ImGui always prints (`imgui.cpp:12031-12042`). There are **zero** empty-label call sites in
  `src/`, and every repeated-row loop in `src/ui/` already pushes an id. Sixteen `##`-literals
  appear more than once in the codebase and all sixteen are separated by a `PushID` or by mutually
  exclusive branches.
* **The cause** is `HelpPanel::drawSidebar`: "Recently viewed" and the category tree both live in
  one `BeginChild("help-sidebar")` and both keyed a row on `PushID(doc->id)` + `Selectable(title)`.
  `CollapsingHeader` carries `NoTreePushOnOpen` and `Indent` has no id effect, so the two lists sat
  at the same id-stack depth. Any topic that has been opened is in both lists. Two items, which is
  the number the owner sees.
* **The fix** makes a row's identity (which list, which document), and moves the grouping into
  `src/ui/help_sidebar.cpp`, which is ImGui-free and on the test target's list.
* **The control fires**: over the shipped `docs/help` content with eight recently-viewed topics,
  identifying a row by document alone produces **8 collisions** and identifying it by
  (scope, document) produces **0**.
* `io.ConfigDebugHighlightIdConflicts` was not touched. Nothing in this repository ever enabled it;
  it is Dear ImGui's own default and it stays on.

Second candidate, **not** this bug but the same shape and left alone deliberately:
`world_edit_panel.cpp:659` keys an object row on `PushID(node.name)` and two sibling nodes with the
same name would collide across five widgets. `Composition::uniqueName` normally prevents that;
hand-edited or imported scene JSON bypasses it. Same pattern at `control_panel.cpp:2584`, `:1700`,
`:3524`, `graph_editor.cpp:153`, `world_builder_panel.cpp:188`, `:221`.

## 9. Things measured on this machine, with the command

| Measurement | Value | How |
|---|---|---|
| Metal RT capability | `supportsRaytracing: 1`, `MTLGPUFamilyApple9: 0` | standalone `MTLCreateSystemDefaultDevice` probe, 2026-09-19 |
| Unified memory budget | 55.66 GB recommended working set, 41.75 GB max buffer | same probe |
| Help sidebar collisions, shipped content, 8 recent topics | 8 without scope, 0 with | `avgen_tests "[help]"` |
| Viewport frames not drawn during a 400-frame in-app render | 307 with the setting on, 0 (and no log line) with it off | `avgen --render-in-app`, both arms, under the GPU lock |
| Quit with a render in flight | SIGSEGV 3/3 before, exit 0 3/3 after; reproduces on `main` at `1cdfb84a` | `avgen --render-in-app --frames 60` |

Not measured, and deliberately not: any Embree or renderer timing. Two other agents are running,
one of them taking performance measurements, and a contended timing is worse than none (ADR-170).
The benchmark table the brief asks for belongs to step 10 and needs a quiet machine.

## 9b. Where this leaves the brief's acceptance criteria

Honest accounting, because the brief asks for a checklist and a half-true tick is worse than a
blank. Of its ~45 boxes:

**Met before this branch started** (and the brief did not know): Embree remains functional; the
output system is renderer-agnostic at the sink; EXR output works with HDR preserved; image mode
exists in the UI; MOV/MP4 output works; the timeline renders frame by frame at the correct rate,
resolution and range; audio can be included and is synchronised.

**Met by this branch**: the ImGui conflict is fixed at its source with detection still enabled;
dynamic UI uses stable ids; offline rendering suspends the viewport; the hidden viewport does not
continue rendering underneath; the UI stays responsive and progress stays visible; cancellation
works; the viewport resumes; colour management is now at least *stated* on the video output.

**Not met, and not attempted**: everything Metal. Rendering mode and backend are still one concept;
there is no `TraceBackend` interface, no Metal prototype, no hybrid pass, no benchmark table. Those
are steps 9-13 and step 10 is a decision point, not a task (section 5).

**Not met, and newly understood as harder than the brief thinks**: "no code-only controls for
user-facing features" — `aovs`, `supersample` and `encoderThreads` are still CLI-only (G3), and the
codec combo still advertises codecs the machine may not have (G4). "Colour management is correct"
is only true at SDR; the HDR path does not exist end to end (W3) and cannot without the tone-map
decision (W4/R1).

## 10. Test baseline for this branch

`./build/release/tests/avgen_tests "~[gpu]"` at `8f23d2ec` with no source change, verbatim:

```
test cases:    2452 |    2447 passed | 4 skipped | 1 failed as expected
assertions: 3799731 | 3799730 passed | 1 failed as expected
```

The one "failed as expected" is `tests/unit/test_character_lab_slopes.cpp:187`, tagged
`[!shouldfail]` (ADR-260). It prints `FAILED:` and it is a pass; three agents have now misread it.

A small correction to the figures in the briefing that started this work: it gave
"2452 test cases, 2446 passed, 4 skipped, 1 failed-as-expected", which sums to 2451. The measured
split is **2447** passed. Recorded because a baseline that is off by one is a baseline nobody can
use to prove they broke nothing.

`avgen_render_tests` has no green baseline to quote: it aborts, on `main`, for the reason ADR-358
records and ADR-362 half-fixes.

**After this branch's work, merged with `main` at `1cdfb84a`:**

```
test cases:    2470 |    2465 passed | 4 skipped | 1 failed as expected
assertions: 3800104 | 3800103 passed | 1 failed as expected
```

The only `FAILED:` line is `test_character_lab_slopes.cpp:187`, with its `with expansion:` present,
which is the `[!shouldfail]` pass.

### An instrument that was built, could not be shown to fire, and was removed

Recorded because the negative result is the useful part. ADR-361's conflict was invisible to every
automated run because Dear ImGui reports it **only as a tooltip**. The obvious improvement is to log
it instead, and it was written: six lines in `ImGuiLayer::render` reading
`ImGuiContext::DebugDrawIdConflictsId`, once per process.

Then the control (ADR-182): two deliberately identical `Button("dupe##adr361")` calls were injected
into an always-drawn panel and the editor was run for 600 and then 900 frames under
`--ui-script hover`, `hover,tabs,panels`. **The line never appeared.** Not once, with the conflict
guaranteed present.

The reason is in ImGui's own detection (`imgui.cpp:5841-5843`): it keys on
`g.HoveredIdPreviousFrame == id`, so the pointer must be over the *same* item on two consecutive
frames. `--ui-script hover` is a Lissajous sweep that writes a new pointer position every frame, so
it essentially never satisfies that — and the `click` arms park somewhere else.

So the instrument cannot be demonstrated to work, and this project's whole discipline is that a
probe which has not been seen to fire proves nothing. It was **removed rather than shipped with a
caveat**, because a caveated no-op is how the last four unreachable subsystems got merged. The
finding to carry forward: **ImGui ID conflicts cannot be detected by any scripted run this repo
has**, and the only way to catch the next one is a person hovering it, or a static check over the
call sites — which is what `ui::helpSidebarGroups` and its test are, for the one place that had
one.

## 11. What this branch has done so far

* This document.
* **ADR-361** — the ImGui ID conflict, fixed at its source. `src/ui/help_sidebar.cpp` (new,
  ImGui-free, on the test target), `src/ui/help_panel.{hpp,cpp}`, `tests/unit/test_help.cpp`,
  `tests/CMakeLists.txt`.
* **ADR-362** — the Catch2 reporter that could not survive printing the failure it found.
  `tests/support/image_diff.hpp` (new), four test files converted.
* **ADR-366** (step 3) — the path tracer's settings become settings: `app::PathTraceSettings`
  under the project's `"pathtrace"` key with a reader, a writer and ADR-350's two tests; its own
  output path, which was unreachable while a trace was selected; `SaveKind::Exr`; and `--pt-*`
  options made optional so a project can be the base.
* **ADR-364** (step 4) — viewport suspension. `src/app/render_state.hpp` (new),
  `AppSettings::suspendViewportDuringRender` defaulting to on with a Settings control, the count of
  undrawn frames in the Render panel, and a shutdown segfault on `main` fixed on the way past.
* **ADR-365** (step 7, brought forward because it is cheap) — W3.4: both video backends now tag
  BT.709, `VideoInfo` reads the tags back from the track's format description, and the control
  writes a deliberately untagged file and requires the probe to notice.

### G8. An in-app render rewrites the project it renders, and the integrity checker does not mind

Self-inflicted and therefore well measured. `--render-in-app` goes through
`startRenderFromUi`, which **saves the project first** — deliberately, because a render loads from
the file rather than from live state, and that is what makes it reproducible (ADR-020). So five
verification runs against `examples/chamber/chamber.json` rewrote it: 88 lines became 3,268, every
default parameter the engine registers written out as an explicit value, plus `control/`, `midi/`,
`osc/`, an asset hash and a reordered document.

Nothing about the scene changed and nothing looks wrong, which is the problem — it is a photograph
of the run, not an edit of the piece. **`python3 tools/check_project_integrity.py` passed on it
twice**, because the file is valid, its parameters resolve and its asset exists. The check that
would catch it is "an example project is not modified by a run that only read it", and nothing
asserts it.

Relevant beyond this branch: the brief's validation workflows are exactly these commands, and
`--pathtrace` has the same rule for the same reason. Copy the project to a scratch directory first,
or expect to revert it.

### G7, found by ADR-364's control arm and added to this list after the fact

`Application::~Application` carries an explicit, commented destruction order and **`job_` and
`ptJob_` were not on it**. `unique_ptr` members destroy in reverse declaration order, `job_` is at
`application.hpp:346` and `context_` at `:394`, so quitting with a render in flight destroyed the
GPU context and then had `ReadbackRing::~ReadbackRing` wait on a Dawn instance that no longer
existed. `--render-in-app --frames 60` segfaults every time, **including on a pristine `main` build
at `1cdfb84a`**, so it is not this branch's. Fixed here: three runs of three exit 0.

This is worth generalising. The brief's error-handling section asks for cancellation to shut the
renderer, the encoder, the temporary resources and the background jobs down cleanly, and the first
thing that was actually tried in that area was already broken. **Before any new backend is added,
its teardown wants the same one-line check**: quit while it is running.

