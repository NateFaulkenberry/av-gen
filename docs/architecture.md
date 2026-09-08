# Architecture

This document describes the system as built for milestone 0.1 and the seams through which it
grows. Decisions are recorded in `docs/decisions/`; the research behind them in `docs/research/`.

## 1. Data flow

```
                 main thread                          audio thread          analysis thread
  ┌──────────────────────────────────────┐   ┌─────────────────────┐   ┌─────────────────────┐
  │ Application::runLive                 │   │ miniaudio callback  │   │ AnalysisRunner      │
  │  Window::pollEvents ─┐               │   │  read AudioFile     │   │  AnalysisStream.read│
  │  Engine::tick(clock) │ FrameTime     │   │  volume, mix mono   │──▶│  Analyzer.push/pop  │
  │  Engine::update ◀────┘               │   │  AnalysisStream.write│  │  TripleBuffer.publish│
  │   ├ AnalysisRunner::acquire  ◀───────┼───┼─────────────────────┼───┤                     │
  │   ├ AudioSignals::publish → SignalBus│   │  playhead (atomic)  │   └─────────────────────┘
  │   ├ Modulator::evaluate → ParameterSet│  └─────────────────────┘
  │   └ OrbScene::update → Scene         │
  │  ImGuiLayer/ControlPanel (reads Scene, params, routes, frames)
  │  SceneRenderer::render(encoder, Scene, FrameTime, target)
  │   ├ pass 1: lit meshes + grid → HDR RGBA16F + depth
  │   └ pass 2: ACES tone map → swapchain (or RGBA8 texture for capture)
  │  ImGuiLayer::render(encoder, target)  (pass 3)
  │  Queue::Submit, GpuTimer::collect, Surface::Present
  └──────────────────────────────────────┘
```

Offline mode (`--headless`) replaces the audio and analysis threads with `AnalysisTrack` (the
same `Analyzer` run over the decoded file ahead of time, indexed by time) and the wall clock
with `FixedStepClock`. Everything from `SignalBus` downwards is identical.

## 2. Modules and ownership

| Module (`src/`) | Library | Responsibility | Depends on |
|---|---|---|---|
| `core` | avgen_core | logging facade, `Result<T>`/`Error`, `FrameClock`, PCG32 `Rng`, SPSC ring, triple buffer | fmt, spdlog |
| `audio` | avgen_core | `AudioFile` (decode to float32 in memory), `AudioPlayer` (miniaudio device, atomic play-head), `AnalysisStream` (SPSC samples + discontinuity markers) | miniaudio |
| `analysis` | avgen_core | `RealFFT` (KissFFT), Hann window, `Analyzer` (streaming STFT features), `AnalysisTrack` (offline), `AnalysisRunner` (thread) | KissFFT |
| `signals` | avgen_core | `SignalBus` (named float channels + events), `AudioSignals` (the `audio.*` vocabulary) | analysis (struct only) |
| `params` | avgen_core | `Parameter<T>`/`IParameter`, `ParameterSet`, `ProcessorChain`, `ModRoute`/`Modulator`, JSON serialisation | glm, nlohmann/json |
| `scene` | avgen_core | `Scene` data model (camera, light, material, meshes, entities), mesh generators, `OrbScene` (the 0.1 scene as parameters + routes) | glm, params |
| `gpu` | avgen_gpu | `Context` (Dawn instance/adapter/device/surface), `ShaderLibrary` (WGSL files + includes + diagnostics), `RenderTarget`, `GpuTimer`, readback | Dawn |
| `rendering` | avgen_gpu | `SceneRenderer`: pass list, pipelines, uniform layout, mesh upload, tone mapping | gpu, scene |
| `platform` | avgen_platform | `Window` (SDL3, Metal layer, events, file dialog) | SDL3 |
| `ui` | avgen_platform | `ImGuiLayer` (SDL3 + WebGPU backends), `ControlPanel` (transport, response, generated parameter panel, analysis plots, performance) | ImGui, ImPlot |
| `app` | avgen | `Engine` (the pipeline; also compiled into the test binary), `Application` (live/headless loops, CLI) | everything |

Rules enforced by the target graph: `avgen_core` has no GPU or windowing dependency and is what
most tests link; only `src/gpu/` includes `webgpu/*.h`; only `src/platform/` and `src/ui/`
include SDL; the renderer never sees a signal and the analyser never sees a parameter.

## 3. Time (ADR-012)

`FrameTime { renderTime, deltaTime, frameIndex }` is produced once per frame by a `FrameClock`
and passed to every update. Live mode: `RealtimeClock` (clamped wall delta); while audio plays,
`Engine::tick` overrides `renderTime` with the audio play-head (audio is the master clock) and
keeps the wall delta for smooth integration. Offline mode: `FixedStepClock(fps)`, so frame N is
always evaluated at N / fps. Analysis frames carry the PCM frame index of their window centre;
`AnalysisTrack::at(time)` and the runner's discontinuity markers both derive from that index, so
seeking and offline evaluation never depend on wall time. Randomness goes through the seedable
`Rng`; `std::random_device` and `rand()` are banned.

## 4. Parameters and modulation (ADR-011)

Every animated scene property is a `Parameter<T>` with a path (`orb/scale`), default, hard and
soft ranges, flags, and two values: `base` (authored/UI) and `final` (after modulation). The UI is
generated from parameter metadata. `ModRoute { source signal, ProcessorChain, amount, op, target
(path, component) }` is data owned by the scene document; the `Modulator` resets finals, then
applies routes in op priority (Replace, Multiply, Add, Min, Max). The chain order is fixed:
gain, offset, curve, clamp, threshold, attack/decay smoothing, envelope, remap. Events (onsets)
enter as impulses and become envelopes in the chain. The 0.1 scene declares five routes; the
"response" sliders in the UI are those routes' amounts, and "master gain" scales all of them.

Growth path: more sources (LFO, envelope, timeline, MIDI/OSC) publish onto the same `SignalBus`;
more processors extend `ProcessorChain`; macros and presets are path-keyed snapshots; the
serialiser already writes routes and parameters as a versioned JSON document.

## 5. Rendering (ADR-001, ADR-006)

WebGPU through Dawn on Metal. The frame is an explicit ordered list of passes encoded into one
command buffer owned by the application: scene (HDR RGBA16F + Depth24Plus), tone map (ACES +
sRGB into the target), UI. The scene always renders offscreen first, so capture and offline
output read the same HDR result. Object uniforms live in one buffer with 256-byte dynamic
offsets; meshes are uploaded when `Scene::meshVersion` changes. Shaders are WGSL files loaded at
runtime with a textual `#include`; compile errors carry file/line diagnostics.

Growth path: the pass list becomes a frame graph (transient resource pool, declared reads/writes)
when post-processing arrives; compute passes are peers of render passes in the same encoder;
the `gpu` module exposes storage buffers, indirect draw/dispatch and 3D storage textures through
plain WebGPU, which the particle research (§11) requires.

## 6. Threading

| Thread | Owns | May touch |
|---|---|---|
| main | window, GPU, UI, Engine, SignalBus, ParameterSet, Scene | everything except the audio callback's state |
| audio (miniaudio) | play-head, volume, seek flags (atomics), `AnalysisStream` producer side | nothing else; no allocation, locks, or logging |
| analysis (`AnalysisRunner`) | `Analyzer`, `AnalysisStream` consumer side, history ring (mutex) | `TripleBuffer` producer side |

Hand-offs: audio → analysis via the lock-free `AnalysisStream` (samples + discontinuity markers);
analysis → main via `TripleBuffer<AnalysisFrame>` (main always reads the newest frame, never
blocks). Dawn callbacks run inside `Instance::ProcessEvents`/`WaitAny` on the main thread.

## 7. Error handling and logging

Fallible boundaries return `Result<T>` (`std::expected<T, Error>`) and callers decide: the
application logs and shows the message in the UI (bad audio file), or exits with a distinct code
(GPU init, shader compile). GPU validation errors are counted by `Context` and fail the headless
run (exit 5) so automated checks catch them. Logging goes through `core/log.hpp` (spdlog); the
audio callback never logs.

## 8. Directory layout

```
src/{core,audio,analysis,signals,params,scene,gpu,rendering,platform,ui,app}
shaders/           WGSL, loaded at runtime (AVGEN_SHADER_DIR overrides the search path)
tests/{unit,integration,rendering,support}
tools/             make_test_audio.py
docs/{research,decisions}
```
