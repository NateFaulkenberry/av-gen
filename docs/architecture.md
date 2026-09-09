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
| `audio` | avgen_core | `AudioFile` (decode to float32 in memory), `AudioPlayer` (miniaudio device, atomic play-head), `AudioInput` (capture device feeding the same stream), `AnalysisStream` (SPSC samples + discontinuity markers) | miniaudio |
| `analysis` | avgen_core | `RealFFT` (KissFFT), Hann window, `Analyzer` (streaming STFT features), `AnalysisTrack` (offline), `AnalysisRunner` (thread) | KissFFT |
| `signals` | avgen_core | `SignalBus` (named float channels + events), `AudioSignals` (the `audio.*` vocabulary), sources incl. `ControlSource` (`control.*` from MIDI/OSC) | analysis (struct only) |
| `params` | avgen_core | `Parameter<T>`/`IParameter`, `ParameterSet`, `ProcessorChain`, `ModRoute`/`Modulator`, presets, `Timeline` (keyframe tracks + cues, ADR-018), JSON serialisation | glm, nlohmann/json |
| `assets` | avgen_core | image decode/encode (stb), OpenEXR write/read (tinyexr), glTF 2.0 import (fastgltf) into a `Scene`, `AssetRegistry` (cached, versioned, path-resolving loads), `VideoWriter` (AVFoundation native / external ffmpeg) | fastgltf, stb, tinyexr, AVFoundation (macOS) |
| `shaders` | avgen_core | user shader contract: ISF-style header parsing, WGSL module generation, inputs layout/packing, `ShaderLayerSet` (layers, parameters, hot-reload watching, project JSON) | params, core |
| `scene` | avgen_core | `Scene` data model (cameras, punctual lights, materials with textures, meshes, entities, environment, particle systems), mesh generators, particle parameter registration, `SceneController` interface with `OrbScene` (built-in preset + sparks), `GltfScene` (imported file + curated parameters + dust) and `Composition` (nodes of any kind, nested scene files, flattened into one `Scene`; ADR-017) | glm, params, assets |
| `control` | avgen_core | OSC 1.0 (messages, bundles, patterns, UDP receiver/sender), MIDI input (CoreMIDI on macOS, byte parser, virtual source), `ControlMap` (bindings + direct OSC scheme; ADR-021) | POSIX sockets, CoreMIDI |
| `gpu` | avgen_gpu | `Context` (Dawn instance/adapter/device/surface), `ShaderLibrary` (WGSL files + includes + diagnostics), `RenderTarget`, `GpuTimer`, readback (synchronous helpers and `ReadbackRing`) | Dawn |
| `rendering` | avgen_gpu | `SceneRenderer` (pass list, PBR/grid/skybox/tonemap pipelines, material bind groups, lights, background/post user layers, engine shader reload), `EnvironmentProcessor` (IBL), `ShaderStack`/`ShaderLayerGpu` (user layers), `ParticleRenderer` (compute pools, indirect draw), `PostProcessor` (built-in effect chain over `gpu::TransientPool`) | gpu, scene, shaders |
| `platform` | avgen_platform | `Window` (SDL3, Metal layer, events, file dialog) | SDL3 |
| `ui` | avgen_platform | `ImGuiLayer` (SDL3 + WebGPU backends), `ControlPanel` (transport, response, generated parameter panel, analysis plots, performance) | ImGui, ImPlot |
| `app` | avgen | `Engine` (the pipeline; also compiled into the test binary), `Application` (live/headless loops, CLI), `RecentFiles`, `RenderSettings`/`RenderJob` (offline renders; ADR-020), `ControlHub` (applies the control map every frame, learn state) | everything |

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

**Sources (milestone 0.3).** `signals::Source` implementations publish onto the same bus every
frame and register their settings as parameters under `sources/<name>/...`, so routes can target
them (modulators of modulators, with one frame of latency): `LfoSource` (sine/triangle/saw/
square/sample-hold, free-running as a pure function of render time or beat-synced),
`EnvelopeSource` (ADSR with hold, triggered by any event signal), `NoiseSource` (seeded value
noise over time), `RandomSource` (sample-and-hold per trigger with slew), `TimelineSource`
(keyframes with step/linear/smooth interpolation and looping), `MacroSource` (UI knobs
`macros/<knob>` mirrored as `macro.<knob>`). The `SourceRack` owns them and survives scene swaps.
The engine also publishes `time.seconds`, `time.progress`, `time.playing` and a per-frame beat
clock extrapolated from the analyser's tempo: `beat.phase`, `beat.pulse` (event), `beat.count`,
`beat.bpm`, `beat.bar`. Routes have a polarity (bipolar maps 0..1 to -1..1 before the chain).

**User shader layers (milestone 0.4).** Each layer's INPUTS are parameters at
`shader/<layer>/<input>`, so shaders are modulated exactly like scene properties. Layers survive
scene swaps (values captured by `detach()` and restored by `reattach()`), reload on file change,
and are stored in projects as `{path, stage, enabled}`.

**Post-processing (milestone 0.6).** `scene::PostSettings` lives in the Engine, is exposed as
`post/*` parameters (re-registered across scene swaps with their values kept), and is copied into
the scene each frame for the renderer's built-in chain.

**Presets and projects.** `params::Preset` is a path-keyed snapshot of base values; the
`PresetBank` stores, recalls and morphs them. A project (`docs/project-format.md`, version 2)
holds parameters, routes, sources, presets, shader layers, the timeline and, since 0.9, the asset
references (audio, scene, environment) relative to the file (ADR-019); `Engine::loadProject` validates everything before
mutating and re-attaches the rack. Growth path: MIDI/OSC sources, per-route blend, keyframe
editing UI.

**Timeline (milestone 0.8, ADR-018).** `params::Timeline` keys parameters against audio time
or beats. Per frame the engine runs `resetFinals` → `Timeline::apply` (writes finals: replace,
add or multiply) → `Modulator::applyRoutes`, so automation is the first modulation layer and the
user's base values stay untouched. Cues recall presets (base values) at a time, morphing from the
current values; the engine keeps the cue state and re-syncs it after seeks. Tracks bind to
parameter paths and rebind after scene swaps, so a track on a path the new scene does not have
simply waits.

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
