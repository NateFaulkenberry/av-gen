# Development log

## 2026-09-08 — Milestone 0.1: research, architecture, and the first vertical slice

### Stage 1 — Research (Phase 0)

Ten research documents under `docs/research/` (5,800 lines, 686 unique cited URLs, every claim
with source/URL/date/relevance/confidence), produced by six parallel research passes and
consolidated by the lead: rendering backends (15 candidates + windowing), audio I/O and analysis,
shaders, assets, particles, rendering techniques, offline rendering, audiovisual systems (20+
systems), tooling. `architecture-options.md` compares five rendering architectures with a
weighted scoring and records the decision.

Key evidence that drove decisions:
- Dawn now ships prebuilt macOS arm64 archives with CMake package config (inspected: static lib,
  headers, Tint; `minos 26.0`; no SPIR-V reader), removing the historical build objection.
- sokol_gfx has no indirect draw and no readback API; SDL_GPU caps colour targets at 4; bgfx
  requires an offline shader dialect; Diligent's Metal backend is commercial; Filament owns the
  frame. Raw Metal would mean writing a 10-20k-line RHI before any portability.
- KissFFT/pffft/miniaudio/libebur128 cover the analysis stack permissively; aubio, Essentia, FFTW,
  KFR, JUCE were rejected on licence or maintenance.
- TouchDesigner CHOPs, Notch modifiers, Synesthesia's audio uniforms, Milkdrop's raw/`_att`
  pairs, and DAW modulation matrices converge on: typed signal channels, fixed-order processing
  chains with attack/decay smoothing, data-driven routes with amounts and ops, path-addressed
  parameters.

### Decisions

Twelve ADRs (`docs/decisions/`): WebGPU via Dawn behind a thin `gpu` module; SDL3; miniaudio;
in-house STFT over KissFFT; glTF canonical (loaders in 0.2); WGSL files at runtime with an ISF-
style user-shader plan; Dear ImGui + ImPlot; CPM.cmake pinned; Catch2; C++23 allow-list with
spdlog/fmt, GLM, nlohmann/json; the parameter/modulation model; the injected time model.

### Stages 2-8 — Implementation

Built in this order, each step with tests before moving on: CMake skeleton and core (clock,
RNG, SPSC ring, triple buffer) → GPU context, shader library, readback, render target, GPU timer,
scene renderer and WGSL shaders with headless GPU tests → audio decode/playback and the
analysis stream → STFT analyser, offline track, runner thread → signal bus, parameters,
processor chain, modulation, JSON serialisation → scene data model, mesh generators, the orb
scene → engine (live + offline modes), SDL3 window, ImGui layer, control panel, application loop
with CLI → integration tests → visual tuning.

Three module implementations (audio playback, analysis DSP, params/signals/scene) were developed
in parallel git worktrees against fixed headers written by the lead and merged; two add/add
conflicts (`analysis_stream.cpp` stub, `scene.cpp` interim copy) were resolved by keeping the
owning module's version.

Files: `src/` 11 modules, 4 WGSL shaders, `tests/` 27 test files, `tools/make_test_audio.py`,
12 docs, 12 ADRs, 11 research documents. Dependencies: 12, all pinned (`docs/dependencies.md`).

### Tests

132 test cases (Catch2, CTest labels `unit` and `gpu`), all passing in Debug and Release on the
development machine (Apple M2 Max, macOS 26.6.2, Apple clang 21, CMake 4.0.1, Dawn
v20260907.201642):

- unit: core (7), headers (1), audio file (5), analysis stream (8), audio player (7 incl. 5
  device tests that SKIP without an output device), FFT (7), analyser (14), analysis track (4),
  analysis runner (4), signal bus, parameters, processor, modulation, serialisation, scene (74
  across those six files);
- integration (4): synthetic audio → Engine (offline) → scene: bass raises scale, treble raises
  emissive, RMS raises brightness, onsets pulse the impulse envelope; bit-identical across runs;
  rotation integrates modulated speed; error path;
- gpu (5): headless context, shader diagnostics, clear + exact readback, deterministic lit-cube
  render (hash equality, stats), resize and invalid-mesh robustness.

Sanitizers: the `asan` preset (ASan + UBSan) was built and run at the end of the milestone (see
"Results" below for the outcome recorded at commit time).

### Results (Task 9 verification)

Synthetic 24 s, 120 BPM test track (`tools/make_test_audio.py`: kick, snare, hats, bass, pad,
with a two-bar break).

Headless, deterministic: `avgen --headless --audio track.wav --frames 300 --fps 30 --capture`
- bands, RMS and onsets follow the music (e.g. t=4.0 s bass 0.74 mid 0.95 treble 0.50 rms 0.48
  onset 1 → scale 1.90 emissive 3.19; break at t=9.97 s bass 0.00 rms 0.07 → scale 1.50);
- GPU 0.07-0.13 ms per frame at 1280x720; 0 GPU errors; identical hashes when re-run.

Live window, Release: `avgen --audio track.wav --play --frames 900 --capture`
- window 1440x900 points / 2880x1800 pixels, Metal via Dawn, timestamps available;
- audio device opened at the file's 48 kHz; playback position advanced 1:1 with wall time
  (t=2.17 s at frame 240 → 7.17 s at frame 840);
- 120 fps vsync-locked, CPU work 1.4-1.8 ms, GPU 0.39 ms, 0 GPU errors, exit code 0, capture
  written. Debug build: 60 fps on the same display, same behaviour.

Seeking, play/pause, end-of-file restart, volume, and discontinuity re-stamping are covered by the
device tests; the UI's seek slider, Space/O/Left/Right keys and file drop use the same calls.

Visual result: a luminous orb floating over a procedural grid in a dark space. Bass lifts and
scales the orb, mids spin it, highs make it glow, RMS raises exposure, onsets pulse it. Default
route amounts were tuned after the first captures blew the orb out to white (treble→emissive 6.0
→ 2.0, rms→brightness 1.0 → 0.5, bass→scale 1.2 → 0.9), and the orb now floats 0.5 m above the
grid regardless of scale so it never sinks into the floor.

### Known limitations

- macOS 26 / Apple silicon only as built. The prebuilt Dawn archive has a 26.0 deployment floor
  and is arm64; the from-source Dawn path is configured but not yet exercised. Windows/Linux need
  a surface-source branch and a build pass.
- Onsets are stamped at the window centre of the first frame that sees the transient, so they
  lead the impulse by up to half a window (10-20 ms). Acceptable visually; documented in
  `docs/audio.md`.
- Per-band running-max normalisation drives any steady tone to 1.0 in its band (auto-gain by
  design); `bandsRaw` carries absolute levels.
- No MSAA; the grid relies on a distance fade to avoid aliasing at far range.
- `AnalysisTrack` keeps display vectors for every frame (~0.8 MB per second of audio).
- The seek slider is driven by the play-head every frame, so dragging it fights playback
  slightly (it seeks on every change). Fine for 0.1; a "seek on release" mode is trivial later.
- ImGui layout is not persisted; no project save/load UI yet (serialisation exists and is tested).
- `AudioPlayer::positionFrames()` may be overwritten by one in-flight callback buffer right after
  a seek during playback (documented in the source).

### Next step

Milestone 0.2 (scene system): glTF loading via fastgltf, multiple entities, PBR materials with an
HDR environment, camera/light parameters; then 0.3 generalised modulation (LFO, envelope, timeline
sources; macros; presets) on the existing `SignalBus`/`Modulator`.
