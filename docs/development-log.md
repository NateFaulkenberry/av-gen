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

Sanitizers: the `asan` preset (ASan + UBSan) builds warning-free and passes all 132 tests with
no sanitizer reports (includes the concurrent ring-buffer, analysis-stream, runner-thread and
audio-device tests).

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

## 2026-09-08 — Milestone 0.2: scene system (glTF, PBR, lights, image-based lighting)

### What was implemented and why

The roadmap's "better scene system": imported assets instead of one procedural orb, so the
engine renders real content and every later milestone (modulation sources, particles, post) has
something worth looking at.

- **Scene model** (`src/scene/scene.hpp`): CPU textures (RGBA8 sRGB/linear, RGBA32Float),
  glTF metallic-roughness materials with five texture slots, alpha modes, double-sided and unlit
  flags; punctual lights (directional/point/spot); imported cameras; an environment block
  (equirect map, intensity, rotation, skybox blur); matrix decomposition, bounds, smooth normals.
- **Assets** (`src/assets/`): stb_image/stb_image_write wrappers and a fastgltf 0.9 importer
  (external buffers/images, hierarchy flattening, materials, samplers, `KHR_lights_punctual`,
  `KHR_materials_emissive_strength`, cameras) that builds into a local scene and merges only on
  success. Developed by a parallel agent against the fixed headers, with an in-memory GLB fixture
  and Khronos sample checks.
- **Rendering**: PBR shader (GGX, height-correlated Smith, Schlick; derivative-based normal
  mapping; occlusion; emissive; alpha mask/blend; unlit), material bind groups with 1x1 defaults,
  CPU mip generation, up to 8 lights, blended draw sorting, skybox; `EnvironmentProcessor`
  (ADR-013) producing irradiance and GGX-prefiltered cubes and the BRDF LUT from an HDRI on the
  GPU in fragment passes with fixed Hammersley sequences; HDR uploads as RGBA16Float.
- **Controllers**: `SceneController` interface; `OrbScene` (0.1 preset) and `GltfScene`
  (orbit camera framed from bounds, root transform, emissive/roughness scaling, per-light
  intensity, env parameters, default routes). `Engine::loadScene`/`loadOrbScene`/
  `loadEnvironment`/`loadFile` rebuild parameters and routes; a failed load changes nothing.
- **App/UI**: `--scene`, `--env`, PNG capture, File menu entries and S/E keys, file drop by
  extension, route list generated from the modulator, scene/environment readout.

### Files changed

`src/scene/{scene.hpp,scene.cpp,scene_controller.hpp,gltf_scene.*,orb_scene.*}`,
`src/assets/*` (new), `src/gpu/texture.*` (new), `src/rendering/{scene_renderer.*,environment.*}`,
`shaders/{common,pbr,grid,skybox,environment}.wgsl` (`mesh.wgsl` removed), `src/app/{engine.*,
application.*}`, `src/ui/control_panel.*`, `src/platform/window.*`, `cmake/Dependencies.cmake`
(fastgltf, stb), `src/CMakeLists.txt`.

### Tests

146 cases (was 132), all passing in Debug and Release. New: scene model (3), image I/O and glTF
loader (9 incl. 2 sample-asset tests that skip without `AVGEN_SAMPLE_ASSETS`), GltfScene
controller and engine scene swap (3), IBL GPU test (1). Existing GPU tests updated for the new
shader files and for lights being scene data.

### Results

- DamagedHelmet + studio HDRI + test track, headless: 0 GPU errors, deterministic hashes; textures,
  normal map, metallic reflections, emissive HUD and IBL all visible in the capture.
- MetalRoughSpheres: the metal/roughness grid reads correctly from mirror-like to diffuse.
- Live window (Release, 2880x1800): 120 fps, CPU work 0.3 ms, GPU 1.0-1.2 ms; helmet load 161 ms;
  environment preprocessing 18 ms; 0 GPU errors.
- Debug: helmet load 835 ms (PNG decode), environment 127 ms.

### Known limitations

- No MSAA; no shadows; no specular occlusion or multi-scatter compensation; single UV set;
  `KHR_texture_transform` parsed but ignored; no animation/skinning; no KTX2/WebP.
- Per-entity parameters are not registered (only the curated root/material/camera/env/light
  surface), which keeps large scenes manageable but means individual objects are not yet
  addressable by modulation.
- Environment preprocessing blocks the main thread at load time.
- Blended materials are sorted per entity, not per triangle.
- Large HDRIs (4k+) cost noticeable CPU time in the half-float conversion and mip generation.

### Next step

Milestone 0.3 (modulation): LFO, envelope, noise and timeline sources on the `SignalBus`;
per-route polarity; macros; presets; beat tracking and tempo signals from the analyser.

## 2026-09-08 — Runtime crash fix (reported by the user)

**Symptom.** Crash while adjusting sliders during playback. The macOS crash report showed an
ImGui assertion in `SliderBehavior` from `ControlPanel::drawResponse`.
**Cause.** The route-amount slider's range was derived from the value being dragged (±3×|amount|),
so dragging to the end enlarged the range each frame and the value grew exponentially until it
exceeded `FLT_MAX/2` (ImGui asserts in Debug; Release would reach infinity).
**Fix.** Constant range (`ui::routeAmountBounds`), non-finite sanitising, regression test
`test_ui_logic.cpp` that replays 10,000 drag-to-the-end frames.
**Also found** by AddressSanitizer while reproducing: `GpuTimer`'s asynchronous readback callback
could fire into a destroyed slot when the app closed with a readback in flight; the destructor
now waits for in-flight mappings. **Tooling added:** `--stress <seed>` (random slider-like
actions every frame in the real app), an engine stress test, a `tsan` preset. Debug, Release,
ASan and TSan stress runs are clean.

## 2026-09-08 — Milestone 0.3: general modulation

### What was implemented and why

The roadmap's "Source → Processor → Modulator → Parameter" generalisation, so visuals can be
driven by more than raw audio features and a project can be saved and recalled.

- **Sources** (`src/signals/source.*`): LFO (five shapes, free-running as a function of render
  time or beat-synced), envelope (ADSR with hold, any event trigger), noise (seeded value noise),
  random (sample-and-hold with slew), timeline (keyframes, interpolation, loop), macros. Settings
  are parameters under `sources/<name>/...`, so sources modulate each other through routes. The
  `SourceRack` serialises and survives scene swaps.
- **Beat and tempo** (`src/analysis/beat_tracker.*`): live tempogram + phase-locked predictor and
  the offline Ellis 2007 DP tracker; frames carry tempo/beat fields; `audio.tempo`, `audio.beat`,
  `audio.beatPhase`, `audio.beatCount` on the bus; the engine extrapolates a per-frame beat clock
  (`beat.phase`, `beat.pulse`, `beat.count`, `beat.bpm`, `beat.bar`) and publishes `time.*`.
- **Routes**: polarity (bipolar); editable in a new Modulation window (add/remove, op, polarity,
  attack/decay, curve, envelope).
- **Presets** (`src/params/preset.*`): capture, recall, blend/morph; bank in the project.
- **Projects**: format version 2 with sources and presets; `--project`, `--save-project`, File
  menu open/save dialogs; loads validate everything before mutating.
- Developed in two parallel worktrees (beat tracking; sources/presets/serialisation) against fixed
  headers, plus the engine/UI integration on main.

### Tests

208 cases (was 152), all passing in Debug and Release: 13 beat-tracker cases, 30 source/preset
cases, serialisation v2, parameter removal, UI regression, engine stress, and 4 integration
cases (LFO without audio, modulators of modulators, project round trip, beat clock).

### Results

- Beat tracking on a 120 BPM click track: offline 119.94 BPM with beats within 10 ms; live locks
  at 2 s, 119.97 BPM, beats within 20 ms; a 120→150 BPM change is followed within 3 s.
- Project round trip through the CLI: `--save-project` then `--project` reload, 20 parameters,
  5 routes, sources and presets restored.
- Windowed runs with the Modulation window and `--stress` are clean.

### Known limitations

- Source parameter modulation has one frame of latency by design.
- Bus signals of removed sources stay declared and hold their last value.
- The timeline source has no keyframe editing UI yet (JSON only); macros have knobs but no
  mapping UI beyond ordinary routes.
- The live tracker holds the last tempo for about 9 s after onsets stop and handles octave
  ambiguity only through the prior.
- A project's source parameter values are skipped with a warning if the rack is attached to a
  different parameter set than the one loaded into.

### Next step

Milestone 0.4 (shader system): runtime WGSL hot reload, the ISF-style user shader contract with
parameters exposed from the header, render-to-texture passes.

## 2026-09-08 — Milestone 0.4: shader system

### What was implemented and why

Custom shaders as a first-class feature (roadmap 0.4): drop a shader into the project, expose
its parameters, modulate them, hot reload, render to texture.

- **User shader contract** (`src/shaders/shader_format.*`, ADR-014): ISF-style JSON header
  (INPUTS float/long/bool/color/point2D/event with DEFAULT/MIN/MAX/LABEL; PASSES with TARGET,
  PERSISTENT, FLOAT, WIDTH/HEIGHT expressions) + WGSL body defining `mainImage`. The engine
  generates the module: `sys` standard uniforms (time, sizes, audio levels, beat clock), the
  `Inputs` struct with WGSL layout rules, sampler, input image, audio spectrum texture, one texture
  per named target, and the entry points. Developed by an agent against the fixed header with 21
  parser/layout/generator tests and two example shaders.
- **Layers** (`src/shaders/shader_layers.*`): inputs become parameters at
  `shader/<layer>/<input>`; values survive reloads and scene swaps; project JSON `shaders` array.
- **GPU side** (`src/rendering/shader_layer.*`): per-layer pipelines cached by target format,
  named pass targets (persistent ones double-buffered for feedback), bind groups per pass, error
  fallback (magenta stripes) with Tint diagnostics, `ShaderStack` mirroring the set by version.
- **Renderer**: background layers draw inside the scene pass before geometry; post layers chain
  through ping-pong HDR targets before tone mapping; an RGBA16F spectrum texture is uploaded per
  frame; `reloadEngineShaders()` rebuilds engine pipelines individually.
- **Hot reload**: `core::FileWatcher` (polling mtimes) for user and engine shaders.
- **App/UI**: `--shader`, `--post`, `.wgsl` file drop, Shaders tab (enable, reorder, reload,
  remove, errors), File menu entries.

### Bugs found and fixed during the milestone

- `std` is a WGSL reserved word: the original contract named the standard uniform `std`, so every
  generated module failed to parse (caught by the GPU tests on first run). Renamed to `sys`.
- Scene swaps cleared the parameter set while shader layers still held parameter pointers
  (segfault in the project round-trip test). Layers now `detach()` before the clear.

### Tests

238 cases (was 208), all passing in Debug and Release: 21 shader-format, 2 file-watcher, 2 layer
set/integration, 5 GPU shader-layer cases (background input colour, post inversion, persistent
accumulation, broken → fallback → fixed reload, engine reload).

### Results

- `shaders/examples/plasma.wgsl` as background behind the orb, `feedback.wgsl` trails driven by
  the beat clock, a post vignette over DamagedHelmet: all captured headless with 0 GPU errors.
- Live window with a shader edited twice mid-run: both reloads applied within the polling
  interval, 0 GPU errors, 120 fps maintained.

### Known limitations

- WGSL bodies only (no GLSL/ISF translation yet); full-screen `mainImage` shaders only.
- Background layers ignore depth; non-persistent targets keep stale content between their passes.
- Post layers run at full HDR resolution with no size expression for the output pass.
- The polling watcher checks mtimes; editors that write via rename are detected as
  removed+recreated (handled) but a change within the same second on coarse filesystems may be
  missed.

### Next step

Milestone 0.5 (GPU particles): compute-driven particle systems with indirect draw, emitters
and force fields as parameters; the renderer already exposes storage buffers, indirect dispatch
and 3D textures through WebGPU.

## 2026-09-08 — Milestone 0.5: GPU particles

### What was implemented and why

Roadmap 0.5: particle systems that live on the GPU and are driven by the parameter system, the
first "millions of particles" building block for festival-scale visuals.

- **Model** (`src/scene/particles.*`, ADR-015): `ParticleSystem` (emitter shape/extent/rate/
  burst/lifetime/direction/spread/speed, gravity/drag/curl turbulence/attractor/orbit, size/
  colour/emissive/blend) plus `registerParticleParameters` exposing 20 `particles/<name>/…`
  parameters with rest-relative scaling.
- **Simulation and drawing** (`shaders/particles.wgsl`, `src/rendering/particle_renderer.*`):
  fixed pool, dead-list stack, per-frame alive list, indirect draw args written by compute;
  `cs_reset → cs_emit → cs_simulate` in one compute pass per system; camera-facing quads with
  soft falloff, additive premultiplied or alpha blend, depth-tested; pcg3d hashing for
  deterministic emission; curl of value-noise potentials for divergence-free turbulence.
- **Scenes**: the orb gains "sparks" (bass → spawn rate, treble → turbulence, onset → burst);
  glTF scenes gain "dust" sized from the bounds (bass → spawn, onset → repulsion).
- Hooked into `SceneRenderer` (compute before the scene pass, draw after the grid) and into the
  engine shader reload path.

### Bugs found during the milestone

- WGSL rejects `read_write` storage in the vertex stage; the draw entry point now uses read-only
  declarations at the same bindings (caught by the renderer-init check in the GPU tests).
- My first GPU test used a point emitter with near-zero speed, so every particle landed on the
  same two pixels and the average-brightness assertion could not move; the test now spreads them.

### Tests

241 cases (was 238): parameter registration/application (1), GPU emission/lifetime/disable and
burst/capacity clamping (2), route/parameter counts updated in the scene tests, plus a hidden
`[.perf]` one-million-particle probe.

### Results

- Sparks orbiting the orb and dust around the helmet render with 0 GPU errors; 120 fps at
  2880x1800 with GPU 0.79 ms in the Debug window.
- 1M-particle pool: 3.4 ms GPU per frame at 1280x720.

### Known limitations

- Alpha-blended systems are unsorted; no collisions, trails or sub-frame emission; soft-particle
  depth fade is parameterised but not applied; alive-list order is not bit-stable across runs
  (additive blending makes this invisible in practice); pools reset when capacity changes.

### Next step

Milestone 0.6 (post-processing): bloom, tone-mapping options, colour grading, distortion, motion
blur and depth of field as built-in post layers on the existing post chain, and the transient
resource pool / frame graph that comes with them.

## 2026-09-08 — Milestone 0.6: post-processing

### What was implemented and why

Roadmap 0.6: the built-in effects that shape the final image, parameterised like everything else,
and the transient-resource machinery that a multi-pass pipeline needs.

- **Settings** (`src/scene/post_settings.*`): `PostSettings` with 23 `post/*` parameters
  (bloom, grading, lens, DoF, motion blur, tone operator, vignette, grain); owned by the Engine,
  re-registered across scene swaps, copied into `Scene::post`; default routes RMS → bloom
  intensity and onset → chromatic aberration are added by the engine.
- **Transient pool** (`src/gpu/transient_pool.*`, ADR-016): scratch textures by (size, format,
  usage), reused across passes and frames, aged out after 60 idle frames.
- **Chain** (`shaders/post.wgsl`, `src/rendering/post_processor.*`): depth of field (CoC from
  reconstructed view distance, 24-tap gather), camera motion blur (depth reprojection with the
  previous view-projection, neighbourhood-max velocity), bloom (soft knee, 13-tap down, tent
  up), composite (distortion, chromatic aberration, white balance, hue, contrast, saturation,
  lift/gamma/gain). Output pass (`tonemap.wgsl`): ACES fitted, AgX, extended Reinhard, Khronos
  PBR Neutral, clamp; vignette; seeded grain.
- Renderer order: user post layers → built-in chain → tone map; depth targets are now
  sampleable; the previous view-projection is tracked per frame.

### Bugs found during the milestone

- WGSL has no ternary operator (`select`), `textureSample` is forbidden in non-uniform control
  flow (`textureSampleLevel`), and depth targets needed texture-binding usage; all caught by the
  GPU tests' renderer-init check and validation-error counting.
- First motion blur only smeared inside moving silhouettes because static background pixels
  returned early; replaced with neighbourhood-max velocity gathering (test: edge sharpness).
- A member name clash (`post_`) with the existing ping-pong targets.

### Tests

246 cases (was 241): post settings (1), GPU bloom / grading + operators / DoF + motion blur /
transient pool (4). Scene route counts updated (post routes moved to the engine).

### Results

- Orb + sparks + bloom + onset chromatic aberration, and DamagedHelmet with DoF, AgX, vignette and
  grain via a project file: both captured headless with 0 GPU errors.
- 2880x1800 window: 120 fps, GPU 3.3 ms with the default chain (bloom at native resolution).

### Known limitations

- Motion blur is camera-only (no per-object velocity); DoF is single-layer (background bleeds
  onto in-focus edges); bloom runs at native resolution and dominates GPU time on Retina;
  the pass list is explicit code, the pool is the only frame-graph piece so far.

### Next step

Milestone 0.7 (scene composition): reusable scenes, nested scenes, presets and asset management.

## 2026-09-08 — Milestone 0.7: scene composition

### What was implemented and why

- `assets::AssetRegistry` (`src/assets/asset_registry.*`): cached, versioned glTF and image
  loads keyed by resolved path, base-directory path resolution and relativisation, `reload`.
  One decode per asset however many instances reference it; the version counter is the hook for
  asset hot reload later.
- `scene::Composition` (`src/scene/composition.*`, ADR-017): a `SceneController` made of nodes
  (`gltf`, `orb`, `grid`, `particles`, `scene`) flattened into one `scene::Scene`: meshes and
  textures shared per asset, one entity per instance with pre-multiplied transforms, particle
  systems and lights appended, a key light when none exists, camera fitted to the bounds. Every
  node registers `nodes/<name>/position|rotation|scale|visible|emissiveBoost|roughnessScale`;
  nested scene files prefix their parameters with `nodes/<name>/`; particle nodes expose
  `particles/<name>/…`. The camera, environment, brightness, grid and root parameters and default
  audio routes match the orb and glTF scenes, so projects and presets carry over.
- Scene files: `"format": "avgen-scene"` v1 (`docs/project-format.md`), nested up to four
  levels, self-inclusion refused, missing assets skipped with a warning so a scene still opens.
  Asset paths are written relative to the scene file.
- Engine: `loadComposition` / `saveComposition` / `newComposition` / `addNode` / `removeNode`;
  `.json` files are routed by their `format` field (project vs scene) for `--project`, drag and
  drop and the File menu; `--composition <file>`; environment maps go through the composition
  (which owns its texture list) instead of being appended to the scene.
- UI: a Scene tab (node list, add glTF/scene/orb/grid/particle nodes, remove) and File > Save
  Scene As.
- Implementation was split: the registry and composition (GPU-free, ~1400 lines + 900 lines of
  tests) were written in a worktree by a subagent against fixed headers; engine, UI, CLI, docs
  and ADR on main; merged with private-member header changes only.

### Bugs found during the milestone

- Headless progress logging dereferenced `root/scale` / `material/emissiveBoost` unconditionally
  and crashed (SIGSEGV) for compositions, which have neither `orb/*` nor `material/*` parameters;
  now reads whichever headline parameter exists.
- `camera.distance: 0` in a scene file was taken literally (camera inside the model); 0 or
  negative now means "fit to the bounds" as documented.
- The dark blurred shape that appeared while orbiting a composition turned out to be a softbox
  in the studio HDRI's skybox, not geometry (verified by rendering without the environment).
- Euler-angle round trips through quaternions lost ~0.02° at 90° with `glm::eulerAngles`; the
  composition uses an `atan2` extraction matching `glm::quat(vec3)`.
- macOS temp paths (`/var` vs `/private/var`) broke relative-path expectations until the registry
  canonicalised its base directory.

### Tests

260 cases (was 246): asset registry (6), composition (7: flattening of every node kind with
shared assets, key light and particle framing, parameters driving instances/materials/particles,
JSON and file round trips, malformed files and missing assets, nested scene files with prefixed
parameters, cycle and depth refusal, determinism, kind names) and an engine integration test
(format-routed `.json` open, audio driving `root/scale`, node add/remove keeps the modulator
bound, save with relative asset paths then reopen, broken file leaves the scene, fresh
composition from the orb scene). The GLB fixture writer moved to `tests/support/gltf_fixture.hpp`.

### Results

- Sample scene (`stage.json`: two DamagedHelmet instances, orb, grid, BoxTextured, a nested
  `backdrop.json` with MetalRoughSpheres at 0.12 scale and a particle node, studio HDRI):
  headless 1280x720 renders with 0 GPU errors and a bit-identical hash across runs
  (`7eb2d99fa8271618` at frame 59); one helmet decode is shared by both instances.
- Windowed 2880x1800 Release: 120 fps (vsync), GPU 1.9 ms, CPU work ~2 ms with the default
  post chain.
- Self-including scene file: refused at load with a clear error; `--stress 7` headless against
  the composition: 0 GPU errors.
- Debug and Release: 260/260 tests pass; zero warnings.

### Known limitations

- No node parenting inside one composition (use a nested scene file to group); bounds and the
  camera fit are computed at rebuild, not when node parameters move things; lights from glTF
  instances and nested scenes are placed at rebuild and do not follow per-frame node motion;
  particle settings edited via parameters are saved as authored values, not the edited ones.
- The bright studio HDRI at intensity 1 renders near-white with the default exposure in both
  the glTF scene and compositions (a look setting, not a composition issue).

### Next step

Milestone 0.8 (timeline): keyframes and automation curves alongside modulation, scene switching
over time, and per-section presets.

## 2026-09-08 — Milestone 0.8: timeline

### What was implemented and why

- `params::Timeline` (`src/params/timeline.*`, ADR-018): tracks key one parameter (all
  components or one) against audio seconds or beats with step, linear, smooth (clamped
  Catmull-Rom), ease-in/out/in-out and Bezier (Hermite tangent) interpolation, optional looping
  (a 4-beat pattern), and replace/add/multiply modes. Evaluation is a pure function of a
  `TimelineClock`, so offline renders are deterministic and seeks are exact.
- Evaluation order: `resetFinals` → `Timeline::apply` → `Modulator::applyRoutes` (the modulator
  gained the route-only half of `evaluate`). Automation writes *finals*, so the user's base
  values are never overwritten and audio routes still add on top. The parameter panel marks
  automated parameters with `[A]` and offers "Key at current time".
- Cues: time-stamped preset recalls with a morph length (from the values current at that moment).
  The engine owns the cue state (`cueState()`), applies the preset once at full weight so later
  user edits stick, and re-syncs after seeks.
- Project format version 3 adds `"timeline"` (and documents `"shaders"`); older projects load.
- UI: Timeline tab (add key for any parameter at the current time in seconds or beats, per-track
  enable/mode/loop, key table with time/value/interp, ImPlot curve preview with a play head, cue
  list with preset and morph).
- Implementation split as in 0.7: the GPU-free timeline and its unit tests in a worktree by a
  subagent against a fixed header; engine, UI, docs and integration test on main.

### Bugs found during the milestone

- The two serialization tests that pinned the project version to 2 (and rejected 3) now use
  `kProjectFormatVersion`.
- While checking determinism over 240 frames (previous milestones checked 60-90) the orb scene
  diverged between runs from about frame 100 even *without* a project. Bisected to the sparks
  particle system: with `particles/sparks/enabled = false` two runs are bit-identical for all
  240 frames; with it, they are not. Cause: the dead/alive lists are compacted with atomics, so
  slot assignment (and with it the per-slot random seeds) and draw order depend on GPU
  scheduling. Not a timeline issue; recorded as a 1.0 (deterministic offline rendering) task:
  stable stream compaction (prefix sums) for emit and simulate.

### Tests

280 cases (was 260): timeline unit tests (19: names, sorted insert and epsilon replace, every
interpolation, looping, vector and single-component tracks, apply modes write finals only,
bind/unbind and unknown targets, recordKey, isAutomated, cues and morph progress in both time
bases, JSON round trip and malformed input, determinism and purity) and four engine integration
tests (keyed values at exact times with routes/add tracks stacking and disable restoring the
base; a beat-based 1-beat loop follows the click track; cues morph a preset, keep later user
edits, and re-sync after a seek; project v3 round trip, scene swap unbinds then rebinds tracks,
recordKey through the engine).

### Results

- Project with three tracks (easeInOut/smooth/bezier/step scale curve, a colour ramp, a
  beat-looped add on rotation speed) and two cues, headless 240 frames at 30 fps: scale hits
  1.2 / 1.8 / 1.45 / 1.0 / 1.2 / 1.4 at whole seconds as authored; emissive 4.0 after the
  "drop" cue morph and 0.3 after "calm"; 0 GPU errors; frames 0-90 bit-identical across runs
  (the later divergence is the particle issue above).
- Windowed 2880x1800 Release with the same project: 120 fps, GPU 1.4 ms; Debug `--stress 3`
  headless: 0 GPU errors.
- Debug and Release: 280/280 tests pass; zero warnings.

### Known limitations

- No curve editor beyond the key table and preview; keys are recorded one at a time (no live
  automation recording); cues morph from the values at the moment they take effect (after a
  backward seek that is the seek-time state); smooth curves are not C1 across a loop wrap.

### Next step

Milestone 0.9 (project system): asset references relative to the project, versioning and
migration, presets and scene files bundled, recent files; then 1.0 offline rendering with the
particle determinism fix.

## 2026-09-08 — Milestone 0.9: project system

### What was implemented and why

- Project format version 4 (ADR-019): `"assets"` (audio, environment, scene = orb | glTF path |
  composition path | inline composition) and `"app"`. Every path, shader layers included, is
  written relative to the project file and resolved against it, so a project folder can move.
  `Engine::loadProject` restores the assets first (they define the parameter surface), then the
  rest; a missing asset is a warning (`projectWarnings()`, shown in the transport window) rather
  than a failure. `--project` now recalls a whole session; explicit flags override it.
- Explicit migration: `params::migrateProject` upgrades one version at a time with a report of
  the steps (1→2 route polarity and empty sections, 2→3 shaders, 3→4 assets/app); the loader
  works on a migrated copy.
- Bundles: `Engine::exportBundle(dir)` / `--export-bundle` / File > Export Bundle copy every
  referenced file (audio, environment, glTF with `.bin`/image sidecars, scene files rewritten
  recursively, shaders) into `<dir>/assets` and write `<dir>/project.json`.
- `Engine::newProject` (everything but the audio), `referencedFiles()`, `RecentFiles` persisted
  in the SDL preferences directory with File > Open Recent, Save Project (to the current path),
  Save Project As, New Project.
- Split as before: migration and recent files (GPU-free) in a worktree by a subagent; engine,
  CLI, UI, docs and the integration test on main.

### Bugs found during the milestone

- A splice error while rewriting the engine's project code duplicated a block of functions;
  caught by the compiler (redefinitions) before anything ran.
- The timeline's "unknown targets" warning is logged twice on a project load (bind runs on the
  scene swap and again after the document is applied); harmless, left as is.

### Tests

296 cases (was 280): migration (5: v1 to v4 in three recorded steps, v2 gains sections without
overwriting, current documents report nothing, too-new and malformed envelopes rejected, the
caller's document is untouched), recent files (6), and five engine integration cases (relative
references and full session restore after moving the folder, missing assets as warnings with
parameters still applied, composition projects by path or inline, bundle export that reopens
after the originals are deleted, new project resets everything but the audio).

### Results

- `--save-project` from a composition + HDRI + audio + timeline session wrote a v4 project with
  `../track.wav`, `../assets/…hdr` and `../scenes/stage.json` references; `--project` alone
  recalled all of it (113 parameters, 6 routes, 3 tracks, 2 cues, 0 warnings, 0 GPU errors).
- `--export-bundle` copied 7 files (audio, HDRI, three glTFs, two scene files rewritten to
  bundle-relative references) and the moved bundle loaded with 0 warnings.
- A hand-written version 1 project migrated in three logged steps and loaded.
- Windowed Release from the project alone: 120 fps; the recent-files store was written under
  the SDL preferences directory.
- Debug and Release: 296/296 tests pass; zero warnings.

### Known limitations

- glTF sidecars are collected by extension from the `.gltf`'s folder, not by parsing the file;
  no content hashes (a renamed asset is reported missing rather than relinked); no autosave or
  unsaved-changes flag; recent files are per machine.

### Next step

Milestone 1.0 (offline rendering): frame sequences and video export from the timeline range,
a render queue, render settings in the project, and the particle determinism fix (stable
compaction) so headless hashes stay bit-identical with particles on.

## 2026-09-08 — Milestone 1.0: offline rendering

### What was implemented and why

- `app::RenderSettings` (size, fps, range, PNG sequence or video, path, pattern, codec, backend,
  quality, audio mux, threads) saved in the project under `"render"` and overridable from the
  CLI (`--render`, `--size`, `--fps`, `--range`, `--codec`, `--quality`).
- `app::RenderJob` (ADR-020): an Offline engine loaded from the project file plus its own
  `SceneRenderer`; frame f at `start + f / fps`, synchronous readback, per-frame and sequence
  hashes, encoder threads (PNG in parallel; video in order to one writer). `step()` renders a
  bounded number of frames so the live app renders in the background with a progress bar and a
  cancel button; `run()` loops headless with progress every second.
- Video: `assets::VideoWriter` with a native AVFoundation backend (ProRes 4444/422, H.264,
  HEVC; audio muxed from the project's file) and an external ffmpeg backend (raw RGBA piped to a
  user-supplied binary; nothing linked or shipped, per the research's licensing posture).
- Render queue: `--queue jobs.json` (projects with per-job render overrides) and an in-app queue.
- Particle determinism: stable stream compaction replaces the atomic dead/alive lists
  (ADR-015 revision), closing the gap found in 0.8.
- Split: particle compaction (GPU) and the video backends (ObjC++/process) by two subagents in
  worktrees; render settings/job, CLI, headless path, queue, Render window, tests and docs on
  main.

### Bugs found during the milestone

- The render job's renderer was created but never `init()`ed (every frame failed with
  "renderer not initialised"); caught by the first GPU test.
- `FixedStepClock::seek` makes the *next* tick land one step later, so frame 0 of a render was
  at start + 1/fps; added `restartAt` (next tick returns the start itself, frame index 0) and a
  core test for it. With it, the job's per-frame hashes equal the classic headless loop's
  exactly (20 of 20 frames compared).
- The very first renderer in a process produced a 1-LSB difference in 27 pixels on its second
  frame only (bisected with a three-job queue: job A frame 1 differed, jobs B and C agreed with
  each other and with separate processes). Root cause unknown (cold pipeline or driver state);
  the job now warms up with a throwaway renderer (one dt-0 update rendered twice, no state
  drift) and the in-process re-render test is bit-identical.
- The native video writer stalled on renders longer than ~1 s with audio: AVAssetWriter
  interleaves its inputs and stops accepting video until the audio input has caught up, and the
  audio was only pumped at `finish()`; fixed by feeding audio incrementally alongside the video
  frames (regression test: a 3 s ProRes render fed from another thread with a deadline).

### Tests

316 cases (was 296): render settings (3), GPU render job (4: exact PNG sequence and
bit-identical re-render in one process, bounded stepping and cancellation with partial output,
bad settings rejected before the GPU, video with audio), particles (2: 200-frame determinism
across fresh renderers and contexts with heavy slot recycling; exact CPU-predicted alive/dead
counts), video writer (11: every native codec and container, non-integer fps, audio muxing with
offset, sticky errors, odd sizes, ProRes-in-mp4, backend selection, an ffmpeg stand-in script for
the process plumbing, the 3-second cross-thread stall regression), and `restartAt` in the clock.

### Results

- Six-node composition + HDRI + timeline project rendered from the CLI: ProRes 422 `.mov` and
  H.264 `.mp4` at 1280x720, 30 fps, 6 s with the audio muxed, both at 120 fps and with the same
  sequence hash; probed with AVFoundation: 6.00 s, apcn + LPCM and avc1 + AAC.
- PNG sequences: 1920x1080 composition at 50 fps, 1280x720 orb at 102 fps (Release).
- Determinism: the render job's frames equal the classic headless loop's frame for frame; the
  orb scene with sparks is bit-identical over 240 frames (agent-verified) and three identical
  queue jobs in one process agree; the GPU particle change cost +15% on the 1M-particle probe.
- Windowed Release from a project: 120 fps with the Render window present.
- Debug and Release: 316/316 tests pass; zero warnings.

### Known limitations

- Readback is synchronous (a staging ring would overlap GPU and CPU); no EXR/16-bit output;
  motion blur and DoF are the real-time approximations; native video is macOS only and the
  ffmpeg backend is untested against a real ffmpeg on this machine; the first-renderer 1-LSB
  quirk is worked around (warm-up), not explained; the UI render runs from the saved project,
  so unsaved edits are not rendered until Save.

### Next step

Milestone 1.x (live control): OSC and MIDI input as modulation sources and parameter targets,
a hot-reloadable control map, and a staging-buffer ring for faster offline readback.

## 2026-09-09 — Milestone 1.1: live control (OSC, MIDI, live audio input)

### What was implemented and why

- `signals::ControlSource` ("control", always in the rack): named `control.<channel>` signals
  (continuous 0..1 or event pulses) fed from outside the frame loop, so controllers are
  modulation sources with the same chains, curves and polarity as audio.
- `control::ControlMap` (project `"control"` block, ADR-021): MIDI bindings (cc, note, noteEvent,
  pitch bend, pressure, program; source/channel/number filters, toggle) and OSC bindings
  (address patterns, argument index, input range) to a control channel and/or a parameter base
  value with a range; pure `matchMidi`/`matchOsc`. The direct OSC scheme (`/avgen/param/<path>`,
  `/signal`, `/pulse`, `/preset/recall|morph`, `/transport/*`) needs no binding.
- Transports with no third-party dependency: an in-house OSC 1.0 implementation (encode/decode,
  bundles, patterns, UDP receiver thread with a bounded inbox, sender) and MIDI over CoreMIDI
  (stub elsewhere), both with `inject` paths for tests. `app::ControlHub` drains and applies
  both on the engine thread each frame and keeps "learn" state.
- `audio::AudioInput` (miniaudio capture) feeds the same `AnalysisStream` as the player, so the
  analyser, beat clock and routes run on a microphone or line input; `--input [device]`,
  `--list-audio-devices`, `--list-midi`, `--osc-port`; transport input selector with a peak
  meter; `audio/inputGain` parameter.
- UI: Control tab (OSC/MIDI status and settings, learn/bind last message, bindings list).
- Split: OSC library and MIDI/audio-input backends by two subagents in worktrees against fixed
  headers; control source, map, hub, engine/CLI/UI integration, docs and integration tests on main.

### Bugs found during the milestone

- `Modulator::bind` cleared a route's `enabled` flag when its source or target did not resolve,
  so a route to a control channel created later (or to a parameter of the next scene) stayed
  dead after a rebind. Unresolved routes now keep the user's flag and are skipped until a bind
  resolves them; two modulation tests updated.
- The OSC binding matcher passed the pattern and address to `matchAddress` in the wrong order
  (patterns never matched); caught by the unit test.
- The agent's first recursive OSC pattern matcher was exponential on `*a*a*a…`; replaced by
  the iterative glob with one backtrack point (patterns are per segment).
- CoreMIDI hot-plug notifications need a CFRunLoop on the creating thread; the client lives on a
  small dedicated run-loop thread.

### Tests

349 cases (was 316): OSC (11: every type tag round trip, spec byte vector, bundles and nesting,
every truncation of a valid packet, pattern table, UDP loopback, queue limit), MIDI (11: parser
table with running status, interleaved real-time and sysex, inbox, CoreMIDI virtual-source round
trip and hot-plug), audio input (capture device when present), control map (4: MIDI/OSC match
rules, direct scheme, JSON), engine integration (4: injected MIDI drives routes and parameters
and pulses events, direct OSC sets parameters/signals/presets and counts unmatched, real UDP into
the hub, project round trip and scene swaps keep the control source).

### Results

- `--list-audio-devices` / `--list-midi` enumerate the Mac's inputs (five capture devices, an
  SE49 keyboard's two ports).
- Windowed Release with `--osc-port 9009`: a Python sender set `root/scale` to 2.0 and
  `scene/brightness` to 0.5, created the `control.energy` channel and paused the transport;
  the project saved on exit carried the values and the channel.
- `--input` on the MacBook microphone: analysis runs on the live stream at 120 fps, 0.7 ms CPU.
- Debug and Release: 349/349 tests pass; zero warnings.

### Known limitations

- IPv4 only; no OSC feedback/query; MIDI clock is ignored (no tempo sync); MIDI backends exist
  only for macOS (stub elsewhere); live input has no latency compensation and no transport;
  the Control tab edits bindings by deletion and learn only (no field editor yet).

### Next step

Milestone 1.2 (live performance outputs): multi-output windows and displays, Syphon/NDI
texture sharing where appropriate, projection/display workflows; then MIDI clock sync and OSC
feedback.

## 2026-09-09 — Milestone 1.2: outputs, sharing, and the remaining follow-ups

### What was implemented and why

- Outputs (ADR-022): the renderer draws into an offscreen final texture; the main window shows
  it under the UI and every additional output (`app::OutputManager`, project `"outputs"`) is
  its own SDL window on a chosen display with its own `gpu::Surface`, presented through
  `rendering::OutputMapper` (crop, four-corner projective warp, soft-edge blend margins with
  gamma, brightness/gamma, flips). Outputs tab, `--output <display>[:fullscreen]`.
- Sharing (`share::TextureShare`): Syphon through Dawn's IOSurface interop and the Syphon
  framework built from source; NDI through a runtime-loaded `libndi` fed by an async readback
  ring. Share section in the Outputs tab, `--syphon <name>`, `--ndi <name>`.
- Control follow-ups: MIDI clock as a tempo source (`control::MidiClockTracker`, project
  `tempoSource`), OSC feedback and query, editable bindings in the Control tab.
- Composition node parenting, asset relinking by size and SHA-256 when a referenced file moved.
- Offline follow-ups: `gpu::ReadbackRing` (async staging buffers) in the render job, EXR
  sequences of the scene-linear HDR frame (tinyexr), and a bounded investigation of the
  first-renderer quirk.
- Timeline curve editor: keys are draggable points on the track preview.
- Split across four subagents (outputs; sharing; control/scene/asset follow-ups; offline
  follow-ups) in worktrees, with the UI editors, application wiring, ADR-022 and docs on main.
## 2026-09-09 — Milestone 1.2 (part): texture sharing to other applications (Syphon, NDI)

### What was implemented and why

- `share::TextureShare` (`src/share/`, in `avgen_gpu`): one output per instance, `open(kind,
  context, name)`, `publish(texture, w, h)` once per frame after the frame's submit, `stats()`;
  `available()`/`describe()` say what the machine supports. Two transports behind a small
  internal interface (`share_backend.hpp`).
- Syphon is native: Dawn writes straight into an IOSurface (`wgpu::SharedTextureMemory`,
  `SharedTextureMemoryIOSurfaceDescriptor`; the device now requests
  `SharedTextureMemoryIOSurface` + `SharedFenceMTLSharedEvent`, `Capabilities::
  sharedTextureIOSurface`), and a `SyphonMetalServer` on a second Metal device blits from it.
  Ordering is GPU-only in both directions with `MTLSharedEvent`s: Syphon's command buffer waits
  for the value `EndAccess` exports, and the next `BeginAccess` waits on the event Syphon's
  buffer signals. No CPU wait, no added latency, one IOSurface.
- Syphon-Framework is compiled from source (BSD-2, pinned commit, Metal subset only, no OpenGL
  framework linked). Its Metal renderer loads shaders from the framework bundle, which a static
  library lacks; `cmake/patches/syphon-metal-library-from-source.patch` adds a compile-from-source
  fallback and CMake verifies the patch is present in the CPM cache.
- NDI is runtime-loaded only (`dlopen`/`dlsym`, `share/ndi_runtime.*`, own declarations of the
  three SDK structures with size checks); frames come from a three-deep `MapRead` staging ring
  and go to `NDIlib_send_send_video_async_v2`; a saturated ring drops the frame instead of
  blocking.

### Bugs found during the milestone

- Syphon swaps its output IOSurface at encode time (`prepareToDrawFrameOfSize:`) but announces
  frames from Metal completion handlers, so on a size change a handler of an older frame pushed
  the new, still blank surface to clients. `ensureSurface` now waits until the last frame has
  been announced (our own completion handler, registered after Syphon's) before recreating
  anything; the burst-then-resize test caught it.
- `MTLSharedEvent` values must stay monotonic: the read-done counter is no longer reset when the
  surface is recreated.

### Tests

353 cases (was 349): `test_texture_share.cpp` with an in-process `SyphonMetalClient`
(`tests/support/syphon_test_client.mm`) discovered by name through `SyphonServerDirectory`:
solid colour then gradient from RGBA8 and BGRA8 sources match within 2/255, stats, close/reopen,
30-frame burst plus size change, publish rejected when closed; the NDI case skips without the
runtime. Hidden `[.perf]` probe: `publish()` at 1920x1080 BGRA8 over 300 frames blocks the caller
0.07 ms on average, 0.55 ms worst (Debug, client attached, 267 frames observed by the client
because notifications coalesce).

### Results

- Clean Debug rebuild: zero warnings (ObjC++ included); 353/353 tests pass (NDI skipped).
- Syphon verified end to end in-process. No NDI runtime on the development machine, so the NDI
  sender has only been exercised through its loader diagnostics.

### Known limitations

- No alpha premultiplication or colour-space metadata; one output per instance; NDI is CPU-bound
  (readback) and untested against a live receiver; no Windows/Linux transport (Syphon is macOS
  only, NDI loader handles dylib/so names but was built only on macOS); the app does not yet
  expose the outputs (next: window/display outputs and a Sharing section in the UI).
## 2026-09-09 — Offline follow-ups: readback ring, EXR output, first-renderer investigation

### What was implemented and why

- `gpu::ReadbackRing` (ADR-020 revision): three staging buffers; frame f's texture-to-buffer
  copy is appended to the frame's own command buffer, the ring submits it and starts the map,
  and the render thread goes on to f+1 and f+2. `poll()` returns completed frames in submission
  order without blocking; `enqueue()` blocks only when all three slots are still on the GPU.
  `RenderJob` renders through it (one persistent RGBA8 target with `CopySrc`), hashes in frame
  order, keeps the per-frame hashes (`frameHashes()`), flushes on finish and on cancel (frames
  already on the GPU are still written) and logs how long the render thread waited for the GPU
  vs. for the encoders. `renderToImage` stays for tests and captures.
- EXR: tinyexr v3.2.0 (classic `tinyexr.h` + bundled miniz; SHA256-pinned release archive so the
  ZFP submodule is never fetched; `docs/dependencies.md`), `assets::writeExr/readExr`,
  `gpu::ImageF`, `readTextureF16`, `halfToFloat` (+ a 64K table for whole images),
  `SceneRenderer::renderToImageFloat` and `hdrOutputTexture()` (the RGBA16F image the tonemap
  pass sampled: HDR target, last post layer or the post chain's composite; those targets and the
  transient pool's default usage gained `CopySrc`). `RenderOutput::ExrSequence`
  (`"output": "exr"`, `--format exr`, UI radio, default pattern `frame_{:06d}.exr`) writes half
  EXRs from the ring's float path on the encoder threads.
- PNG deflate now goes through miniz (level 2) instead of stb_image_write's compressor: once the
  readback no longer stalled, the encoders were the bottleneck. Encoder threads clamp at 16.

### Bugs found during the milestone

- The first ring design mapped the staging buffer before the copy was submitted; WebGPU needs
  the map to start after the submit, so `enqueue()` finishes and submits the frame's encoder.
- My first "improvement" of PNG encoding (miniz at zlib's default level 6) was slower than stb's
  weak compressor (63 vs 103 fps at 720p) although 30% smaller; level 1–2 is both faster and
  smaller than stb. Measured, not assumed, from then on.
- The scalar half-to-float conversion made the EXR path render-thread bound (37 fps at 720p);
  a lookup table and a per-value FNV over the float bits doubled it.

### Results (Release, Apple M2 Max, orb scene, 300 frames at 60 fps, `--range 0:5`)

| Output | 1280x720 | 1920x1080 |
|---|---|---|
| PNG, synchronous readback (before) | 96 fps | 48.7 fps |
| PNG, readback ring, stb deflate | 103 fps (+7%) | 53.5 fps (+10%) |
| PNG, readback ring, miniz deflate (after) | 184–189 fps (1.9x) | 88–89 fps (1.8x) |
| EXR half, readback ring (new) | 83 fps | 41 fps |

The ring alone removed the GPU stall (the render thread waits 0.07–0.10 s in total for the ring
over 300 frames) but exposed PNG encoding as the limit (0.9–1.6 s waiting for a free encoder
slot at 8 threads); with miniz the encoder wait is 0.00 s and the remaining cost is the render
thread itself (engine update, pass encoding, row copy and hashing). PNG files are ~19% smaller
than before (1080p frame: 0.87 MB vs 1.13 MB). Sequence hashes are unchanged by the ring
(`47ae193a93ac6ab0` at 720p, `bb0160373da6b8ef` at 1080p, identical to the synchronous build)
and the ring-vs-`renderToImage` test compares 20 frames hash for hash. EXR at 720p writes
1.34 MB frames (ZIP); the emissive orb reads > 1.0 in linear light where the PNG is clipped.

### Tests

353 cases (was 349): EXR round trips (half exact, float, errors), the EXR settings kind and JSON,
ring vs. synchronous path (20 frames), EXR render job (readable, brighter than 1.0, equal to the
synchronous float readback, byte-identical re-render). Debug and Release: all pass, zero warnings.

### Investigation: the first renderer's 1-LSB difference

Reproduction (`AVGEN_NO_WARMUP=1 avgen --queue jobs.json --log debug`, three identical jobs on
the orb scene at 96x64, 10 frames each): job A's frame 1 hashes differently from jobs B and C,
which agree with each other; with the warm-up all three agree. Bisection by project variants
(`post/bloom/enabled` false; `particles/sparks/enabled` false; both; everything off including
DoF, motion blur, grain, vignette, lens):

| Variant | Differing frames (job A vs B/C) |
|---|---|
| defaults (bloom + sparks) | 1 |
| bloom off | 1 |
| sparks off | 3 |
| bloom + sparks off | 3, 4, 5 |
| everything off, 96x64 | 3, 4, 5 |
| everything off, 640x480 | 1, 2, 3 |
| everything off, 48x32 | none |

So no pass is responsible: the difference survives with only the scene pass (lit PBR, grid) and
the tonemap left, and *moves* with frame cost (slower frames: earlier indices; fewer passes:
later indices), i.e. it is a window of wall time after the pipelines are created, not a frame
count. The pixels: 1–4 per 6144, isolated, ±1 in a single channel, scattered (mostly on the
grid), never a region or a row: a shader arithmetic (rounding/contraction) variant, not a stale
texture, uninitialised uniform or missing clear. Confirmation: a 1 s sleep after creating the
job's renderer, with no warm-up renderer and no draws, makes job A identical to B and C; 100 ms
does not for the fast variant; a warm-up that only creates a renderer (pipelines compiled,
nothing drawn) does not help, the two-frame warm-up does. Conclusion: Metal replaces the GPU
binaries of freshly compiled pipelines shortly after creation/first use with a variant that
rounds differently in a few pixels; Dawn caches pipeline objects device-wide by descriptor, so
a throwaway renderer that draws two frames warms exactly the pipelines the real renderer gets.
No engine-side fix is cheap and clearly correct (a sleep would be timing-dependent), so the
warm-up stays, now documented at the call site with the `AVGEN_NO_WARMUP=1` switch to
reproduce. The live application is unaffected in practice (its first frames are never captured).

### Known limitations

- EXR is encoder- and render-thread-bound (41 fps at 1080p): ZIP through miniz, half data only,
  no AOVs; the per-frame float hash is FNV over the float bits (not byte-wise like PNG).
- The render thread still copies rows and hashes on its own thread; moving the hash to the
  encoder threads (folding in frame order at finish) would raise the PNG ceiling further.
- The first-renderer quirk is a driver behaviour observed on one machine (macOS 26, M2 Max,
  Dawn v20260907); the warm-up costs two small frames per job.

### Next step

Milestone 1.2 (live performance outputs), then AOVs (depth, velocity, emissive) as extra EXR
channels and temporal supersampling for motion blur in the offline path.

### Tests

385 cases (was 349): output mapping (homographies, blend weights, JSON) and GPU mapper cases
(identity, crop, flip, blend ramps, warp), window smoke; Syphon self-receive through a real
`SyphonMetalClient` (solid and gradient frames, RGBA and BGRA, burst + resize), NDI skipped
without the runtime; MIDI clock tracker (jittered 120 BPM, dropped ticks, tempo change) and the
engine tempo source; OSC query/feedback over UDP; composition parenting; SHA-256 vectors and
project relinking; EXR round trip and EXR render sequences; readback ring determinism.

### Results

- Second output window from the CLI (`--output 0:640x360`) alongside the main window: 120 fps,
  0 GPU errors, the output saved in the project and restored.
- Syphon server from the CLI (`--syphon avgen`) with the composition project: 120 fps; the
  self-receive test verifies pixels through a real client; `publish()` costs 0.07 ms average.
- Offline: PNG sequences 96 → 186 fps at 720p and 49 → 89 fps at 1080p (readback ring plus
  miniz deflate); EXR half sequences 83 / 41 fps; sequence hashes unchanged by the ring.
- The first-renderer quirk was bisected to no pass: it follows frame cost and disappears after a
  one-second idle, so it is attributed to driver-side pipeline binary swapping; the warm-up
  stays, with `AVGEN_NO_WARMUP=1` to reproduce.
- Debug and Release: 385/385 tests pass; zero warnings.

### Known limitations

- No mesh warp or mask images per output; a single homography and per-side blends. Syphon and
  IOSurface are macOS only; NDI is untested against a live receiver (no runtime here); Spout
  (Windows) does not exist. MIDI clock cold start hands the first downbeat to the analyser.
  EXR is half/ZIP only, no AOVs. Windows and Linux builds remain unexercised (Dawn/SDL keep the
  door open; MIDI, Syphon and native video have stubs there).

## 2026-09-09 — Procedural geometry phase: generators, instancing, deformers, showcase worlds

### What was implemented and why

- Audit and design first (`docs/research/procedural-geometry.md`, ADR-023): one new scene
  component instead of scene classes. `scene::ProceduralGeometry` = source primitive (box,
  cylinder, UV sphere, torus) + distribution (single, linear, grid, radial, spiral) + seeded
  variation + ordered deformer stack (bend, twist, sine, noise, displacement; ≤ 8) + material and
  per-instance material variation. Instance records (96 bytes) are generated on the CPU only when
  structure changes; the GPU applies instance transforms and the deformer stack per vertex every
  frame and recomputes normals by finite differences of the whole stack. One instanced draw per
  object; the PBR fragment is shared with entities through `pbr_shade.wgsl`.
- Transform order `world = node × distribution × placement(i) × variation(i) × source`; local
  deformers before instancing, world deformers after. Deterministic: `hashInstance(seed, index,
  channel)` on the CPU and the same `pcg3d` value-noise fBM in WGSL and C++.
- Parameters under `procedural/<node>/…` through the particle-style register/apply pattern, so
  modulation, timeline, presets, projects, OSC and MIDI need nothing new. Compositions gain a
  `procedural` node kind (JSON `"procedural": {…}`), a free camera mode
  (`camera/mode|position|target`) for fly-throughs, and distance fog (`scene/fogDensity|fogColor`).
- Examples browser (File > Examples, `--example <name>`, `examples/index.json`): the Lab, Temple,
  Cathedral, Helix, Impossible Chamber, Hyperspace and Benchmark are scene + project files; no C++
  knows about any of them.
- Split: primitives/distributions/variation/deformers/parameters (GPU-free) and the instanced
  renderer + shader + fog + benchmark by two subagents against fixed headers; composition node,
  camera mode, examples browser, showcase files, presets, timeline demo, docs on main.

### Bugs found during the milestone

- The CPU and GPU noise deformers disagreed (scalar × mask versus three decorrelated channels);
  aligned on the shader's per-axis form and documented in the header.
- Relative `--composition` paths were resolved twice (against the scene's own folder); the
  engine now absolutises the path first.
- Compositions computed their bounds from entities only, so scenes made purely of procedural
  objects framed to a radius of 1 and the far plane clipped everything beyond 100 units: a free
  camera looking across a world saw nothing. Bounds now include procedural instance bounds and
  the far plane has a 2,000-unit floor.
- Nested scene files dropped their procedural nodes when flattened; they are now copied with
  the node transform folded in, at rebuild and every frame.
- Compositions inherited the glTF scene's default root rotation (0.15 rad/s), which swung
  whole worlds past a free camera; the default is now 0 and the examples set it explicitly.
- Route amounts that looked fine on the orb were far too strong for emissive architecture (the
  Temple's columns washed out under treble); the showcase presets use modest amounts and let
  the deformers' own speeds provide the idle motion.

### Tests

421 cases (was 385): primitives, distributions, variation, transform order, each deformer, stack
order, CPU/GPU noise identity, JSON, parameter registration and application (30 CPU cases);
GPU instancing coverage, deformer effects, determinism across renderers, 4,096 instances, fog
(5); engine: procedural node registration and structural rebuilds, audio routes driving
distribution radius and twist, project round trip, every example project loads with no
warnings (3); showcase frame-hash regression: Temple, Helix, Chamber, Hyperspace and Lab
render bit-identically across fresh engines and renderers and differ across time (1).

### Results

- Live loop and render job agree frame for frame on the Temple (20 frames), and two render runs
  are identical; the showcase regression test covers radial, spiral, noise, combined stacks,
  nested scenes and audio-reactive routes.
- Windowed Release, the Temple at 2880x1800 (five procedural objects, ~1,000 instances, dust):
  120 fps, GPU 2.7 ms, CPU 3.1 ms.
- Release throughput (agent's probe, 1280x720, 24-segment cylinders): 1,000 instances 1.0 ms
  (2.0 ms with noise); 10,000 instances 3.6 ms (4.5 ms one deformer, 5.6 ms three, 8.6 ms
  noise); CPU instance regeneration 1–10 µs. See `docs/performance/procedural-geometry.md`.
- The acid test: every showcase is a scene file plus a project; deleting the Temple and writing
  the Cathedral, Helix, Chamber, Hyperspace and Worlds needed no C++. The Worlds project morphs
  Cathedral → Temple → Helix → Hyperspace → Cathedral on a 60-second cue timeline.

### Known limitations

- Eight deformer slots; opaque instances only (blend materials draw opaque); one homography of
  material variation (colour/emissive multipliers); no mesh or glTF sources, no spline/surface
  distributions, no fields yet (all additive per ADR-023); the composition's per-node transform
  is folded into the distribution transform, so moving a 10,000-instance node re-uploads its
  records; the per-instance hue tint is baked at rebuild time.

### Next step

Fields/effectors that modulate instance attributes by position, mesh and glTF sources, spline
distributions, and custom WGSL deformers spliced at the shader's include point.

## Procedural world engine (2026-09-09)

The brief: turn the engine into a general-purpose procedural audiovisual world-building
instrument, where worlds are built from spatial data, fields, effectors, splines, SDFs, materials
and simulation without writing C++. Research first (eight documents under `docs/research/`), then
ADR-024 to ADR-032, then implementation in waves with fixed headers so parallel agents could work
against a stable contract.

### What was built

- **Spatial data** (`src/spatial/`, ADR-024): typed `AttributeSet` columns over a point domain,
  `PointCloud` with the conventional core columns (position, rotation, scale, id, seed, density,
  colour, emissive, velocity, normal, bounds, index), thirteen attribute operations and eighteen
  point operators (transform, noise, randomise, scatter, five filters, sort, duplicate, sample,
  merge). The renderer's 96-byte instance record became a projection of a cloud.
- **Fields and effectors** (ADR-025): twenty-five field kinds with ten falloff curves, transforms,
  animation and compounds, sampled identically in `src/spatial/field.cpp` and `shaders/fields.wgsl`
  (a parity test compares every kind); eight effector operations with six blend modes, applied on
  the GPU each frame over the record buffer; field forces in the particle simulation; a `Field`
  vertex deformer; field-driven emission.
- **Splines** (ADR-026): four spline kinds, seven generators, rotation-minimising frames, used by
  a spline distribution, a path deformer, a spline particle emitter and camera mode 2.
- **Hierarchy and grammar**: self-recursion with per-level transforms, a procedural object as
  another object's source, and a seven-operation shape grammar (place, repeat, branch, alternate,
  mirror, choice, conditional) with deterministic choices.
- **SDFs** (ADR-027): a 26-kind node tree with constructive geometry, domain operations and
  displacement, evaluated identically on both sides through a packed post-order interpreter,
  raymarched with depth writes so it composes with rasterised geometry, or meshed by surface nets.
- **Procedural materials** (ADR-030): a nineteen-operation interpreted program over a register
  file with position, normal, attribute, time, audio and field inputs, plus an OKLab colour layer.
- **GPU execution** (ADR-029): an effector compute pass, frustum, distance and screen-size culling
  with stable compaction into indirect draws, and four levels of detail per object.
- **States and macros** (ADR-031): scene states as preset morphs with easings, beat and bar
  quantisation and seven trigger kinds; world macros that expand to ordinary remap routes; three
  authoring layers; a world overview; an inspector that answers "why is this moving"; an asset
  browser; debug view options; a profiling capture with percentile summaries.
- **A procedural graph** (ADR-028): 102 typed node types that emit flat scene data, with
  subgraphs, incremental evaluation by structural hash, and a graph library.

### Results

- 676 tests pass in Debug and Release, zero warnings.
- CPU and GPU agree within 1e-4 for every field kind, falloff, effector operation and SDF tree.
- Performance (M2 Max, 1080p): 1M point instances draw in 3.1 ms, with a curl-noise effector pass
  at 4.6 ms; 100k boxes at 1.1 ms; the cull pass costs 0.06 ms at 100k and 0.38 ms at 1M and
  removes 80 percent of a half-off-screen scene; particles at 256k simulate in 0.83 ms with two
  field forces. SDF raymarching is the expensive path at roughly 100 ms for sixteen blended
  spheres filling 1080p, which is why objects are bounded and it stays opt-in.
- Ten example worlds ship as data, including the Living Machine (fields, effectors and particle
  forces) and the Infinite Temple (seven macros, twenty audio routes, eight states, a four-minute
  cue arc). A golden frame-hash test renders every world twice and walks the flagship along its
  arc.

### Bugs found and fixed

- The output manager drained the shared SDL event queue with no handler at the end of every frame,
  so mouse events never reached the UI while the panels kept redrawing. The window that pumps the
  queue is now the only one that drains it, with a regression test.
- Panels were positioned in framebuffer pixels where ImGui expects points, pushing them off the
  right edge of a scaled display.
- A file whose entire symbol set was also defined weakly elsewhere was never pulled out of the
  static archive, so the stubs won at link time. All temporary stubs are now gone.
- The scene path was moved into the composition before later parsing read it, so relative graph
  paths never resolved.

### Known limitations

- Volumetric fog and simulated grid fields are specified (ADR-032) and being implemented.
- SDF raymarching has no per-tree specialisation; cost scales with pixels times steps times nodes.
- Culling uses the source bounds through the instance transform, so a large world-space deformer
  can pop at the frustum edge; it is opt-in per object for that reason.
- The graph is an authoring layer: a scene is either graph-driven or hand-made, since re-evaluation
  replaces what the graph installed.

## 2026-09-10 — The 2D composition: text, shapes and layers above the render (ADR-083)

The engine could make a picture. It could not make a finished piece: no titles, no lyrics, no
captions, no framing. The gap between "this looks extraordinary" and "this is a music video" was
entirely made of things that live above the render.

### What was built

`avgen::comp::LayerStack` — an ordered stack of 2D layers drawn over the tone-mapped frame,
knowing nothing about the scene behind it. Named `LayerStack` and not `Composition` because
`scene::Composition` already means the 3D authoring tree and the CLI already spends
`--composition` on it; the word keeps its artist meaning in the UI and the project file.

The coupling to the renderer is one virtual call — `rendering::FrameOverlay`, six lines in
`scene_renderer.cpp`, null by default — placed after the tone map and before the frame timeline
resolves. Because it is inside `render()`, every path the renderer already has gets the
composition: the editor window, the offline job, `renderToImage`, screenshots, projection outputs.

Layers composite **after** the tone map, in display-referred space. Compositing into the HDR
buffer would put every layer through AgX, so `#FFFFFF` would come out a desaturated off-white
whose value depended on the scene's exposure that frame. The price of the choice is that layers
cannot bloom and EXR output does not contain them; the render job says so when asked for that
combination, rather than shipping a sequence somebody finds has no captions in a grade.

Text is shaped by CoreText and rasterised from glyph **outlines** rather than drawn — drawing goes
through hinting and font smoothing, which are tuned for a screen and vary with system preferences,
and an atlas that depends on appearance settings is an atlas that breaks determinism. The coverage
is rasterised at 4x and turned into a signed distance field by an exact Euclidean distance
transform, with the distances averaged down rather than the coverage. Fields are cached by face
and glyph id and **not by size**, so an animated font size rebuilds nothing, and the fill, the
outline, the glow and the drop shadow all read one field in one pass.

Every animatable property is an ordinary parameter under `layers/<id>/<property>`, registered
before the timeline binds. So layers keyframe on the existing timeline with the existing
interpolation, and follow the bass through the existing modulation routes. No text-specific
animation code was written, and no second timeline exists.

### Results

- 995 unit tests and 171 of 173 GPU tests pass. The two failures are pre-existing and unrelated
  (an ADR-077 triangle-count expectation, and a shadow-pass timing ratio measuring at the
  timestamp resolution floor); both fail identically with the composition hook removed.
- 1920x1080 GPU pass cost: 1 layer 0.016 ms, 10 layers 0.016 ms, 100 layers 0.101 ms, 200 layers
  0.219 ms. All of them one draw call, because runs that are normally switched off are packed at
  the end of the vertex buffer so skipping them does not break contiguity. With no layers the
  pass is not encoded at all.
- A frame produced by the real `RenderJob` is byte-identical to the same second produced through
  the interactive encode path. `examples/composition/glowmere-lyrics.json` renders to the same
  sequence hash on repeated runs of the process.

### Bugs found and fixed

- A stroke-only shape's glow was measured from the shape's outline rather than from what it
  actually draws, so a border's glow filled the whole picture with a milky wash. Found by looking
  at the first showcase render, not by a test.
- `Timeline::bind` could only log its unresolved targets. `Timeline::unboundTargets()` now keeps
  them, and the Composition panel shows them — the silence here is what made two earlier features
  do nothing without saying so (ADR-075, ADR-080).

### Known limitations

- Layers do not bloom, and EXR output does not carry them.
- Font portability is not solved, only made honest: a project moved to a machine without the face
  renders with different type and says so at error level.
- Tracking, line spacing, alignment and the text itself rebuild geometry and therefore do not
  keyframe. Position, scale, rotation, anchor, opacity, colour, size, outline, glow and shadow do.
- No viewport direct manipulation: the inspector is the only way to place a layer.
- Image, Video, Shader and Nested Composition layers are not implemented; the abstraction that
  would carry them is the one Text and Shape already use.

## 2026-09-11 — Entities: behaviour, navigation and reactions declared in data (ADR-088)

A UFO over Glowmere was the brief; the layer underneath it was the work. An entity drives a node the
scene has already placed, so the same thing configures an imported craft, a procedural rock and a
skinned character. Behaviours (hover, drift, bank, spin, orbit, wander, lookAt, interest) are code
because aperiodic motion has to be; reactions are data, and compile to ordinary modulation routes —
the chain in `params/processor.hpp` was already the whole "signal → curve → smoothing → depth →
property" pipeline and building a second one beside it would have been the fifth parallel system
this codebase has grown.

What made the data spelling possible was addressing. An imported asset's materials became parts
ordered by surface area, so a scene file could only say `parts/2`. `scene::Entity` now carries the
material name its source asset gave it, and a reaction may write `parts/Blue/emissiveGain`.

The craft's position was chosen by `entity::findPlacement`, which samples candidates in screen space,
unprojects them, and rejects each against the scene's own camera matrices, the WorldMap's slope and
water, and the ClearanceField's canopy and hero capsules. It also says *why* things failed, which is
how the right-of-centre sky turned out to be unusable (77,380 of 120,000 samples on the east ridge)
rather than merely unlucky.

Navigation has no navmesh. `WorldMap::sample` and `ClearanceField` already answer every question a
walker has, analytically; a baked second description of the same ground would be wrong the first
time somebody moved a hill.

### Found by looking at the picture

- `params::loadProject` called `Modulator::clearRoutes()` and installed only the document's routes,
  so anything a subsystem had installed was destroyed a few hundred lines after it was installed.
  Entity reactions bound, logged, and vanished — and so had every route a procedural graph emitted
  since ADR-028. `saveProject` had the mirror of it, writing subsystem routes into the file so a
  project gained a duplicate of each one every time it was saved.
- `lookAt` turned the body towards its subject while `wander` walked somewhere else. Locomotion
  scales its pace by alignment, so the two multiplied to a standstill rather than averaging.
- `interest` treated a strong audio event as a state change, so on a percussive track every impact
  interrupted the one before it and the character stood still for ninety seconds looking startled.
- An unleashed wander is a random walk, and a random walk leaves.

### Numbers

- 64 entities carrying four behaviours each: 35.3 µs per frame, 551 ns per entity (minimum of five
  runs). Glowmere carries two.
- The whole feature — craft, beam, behaviours, reactions — costs +0.33 ms of GPU frame time at
  1920×1080 (28.05 → 28.38 ms, minimum of four interleaved runs); the beam's particle pass is below
  the 65,536 ns timestamp quantum.
- Between the score's quietest bar and its loudest the craft is 1.73× brighter, the beam 1.94×, and
  the bloom around it 2.19×.

### Known limitations

- A part is one *material*. An asset whose belly lamp and dome ring share a material is one
  addressable part; separating them means editing the asset or changing how parts are grouped,
  which would move every existing part index.
- No path planning and no inter-entity avoidance: steering is a fan of local deviations.
- Sockets resolve against the entity's own frame until something implements `ISkeletonQuery`.
- A behaviour profile is take-it-or-extend-it. There is no override-by-name, because an override
  that could not survive a save would be worse than not having one.

## 2026-09-11 — The cinematic sequence: choreography through time (ADR-089)

The engine could render a world, analyse a song, animate a camera, pose a character, draw type over
the frame and export a video. It could not say **when**. Every one of those systems had its own idea
of time and there was no object that could be handed a second and answer *what does the piece look
like now*.

There was also, in the working tree, an uncommitted `src/seq/` from an earlier attempt: 2,615 lines,
in no build file, referenced by nothing. Its design was right — a sequence *bakes* into ordinary
timeline tracks — and two of its seams described a repository that no longer existed. `seq/rig.*`
built a stand-in articulated character out of composition nodes on the stated grounds that "there is
no skeletal animation in av-gen"; there is (ADR-086). `seq/layers.hpp` described the composition
system as "being built separately"; it landed as ADR-083. The first was deleted, the second was
connected, and the rest was kept.

### What a sequence is

Shots, scene slots, actors, overlay cues, markers and piece-level tracks — a value. `bake()` is a
pure function from that value to the JSON `params::Timeline::fromJson` reads. After the bake there
is no sequencer left to run, which is what buys determinism, scrubbing and cost at once.

The one thing that is not baked is which animation clip an actor is in. A track carries numbers; a
clip's phase needs a *time origin*. So `animationAt(t)` returns the active cue per actor and
`applyAnimation` pushes it — the clip and the cue's own absolute second — into the composition every
frame.

### Found by writing the tests

- **A cut was eating a shot.** Two shots meet at one second, and `Track::addKey` replaces a key
  within a microsecond of an existing one — so the outgoing shot's final camera pose and the
  incoming shot's opening pose became *one* key, and the first shot spent its whole twenty seconds
  gliding toward the second shot's opening frame. The last key of a shot that is cut away from now
  lands a millisecond early.
- **A saved project held both the sequence and the tracks baked from it**, so a load read them *and*
  re-baked them. Two tracks writing `camera/position` is not a blend; it is whichever one the
  timeline applies second. Derived tracks, derived layers and their parameter values are no longer
  written.
- **A scene swap left every baked track correct and bound to nothing** — ADR-075's failure again,
  found by a test that swaps the scene and then asks whether the cut still happens.
- `OverlayCue::fromJson` read `presetSeconds` through a `float`, so `0.45` came back as
  `0.44999998807907104` and a project re-saved immediately after loading differed from itself.

### Found by writing the proof-of-concept

- **Animating a procedural sky's own parameters rebuilds it every frame.** `SkyRuntime::hash()`
  covers the zenith and horizon colours, the sun's colour, intensity, size and direction, and the
  sky's own intensity; a changed hash rebuilds a 256-pixel cube with nine mips, a 32-pixel
  irradiance probe and a six-mip prefiltered chain — 93 to 146 ms of CPU, per frame. Keying those
  four parameters with `Step` at the section boundaries instead took the same six hundred frames
  from **66.5 s to 30.7 s** (minimum of two interleaved runs each, 1280×720), and the whole
  hundred-and-five-second piece from **364.0 s to 49.7 s** — 8.7 fps to 63.3, with 8 sky rebuilds
  instead of 3,150. The dusk now arrives at a cut, which is where a cutter would have put it anyway,
  and everything that should move continuously still does: none of the key light, the fog, the
  ambient or the practicals is in the sky's hash.
- **A wide scene of small objects gets almost no cast shadows.** 220 nodes over 260 metres reports
  `draws=95 shadowDraws=2`. Not the rig, not the sun's elevation, not instancing — a `single`-
  distribution probe box in the middle of frame is culled too. `shadowFar` is `sceneRadius * 3`, so
  a large ground plane pushes the cascade splits past the geometry. Recorded in
  `docs/renderer-2-backlog.md` rather than fixed: it is renderer work.
- **Two districts must share the same ground.** Putting them side by side tripled the scene radius
  for no gain — they are never both visible.

### Numbers

- **Determinism.** The same project rendered twice produces the same sequence hash:
  `1a31c825fdc049aa` twice before the sky change, `32dbb3a940c95aa2` twice after. The integration
  test makes the stronger claim — two engines, one played forward and one seeked in an order no
  playback would produce, agree on every parameter the sequence writes and on the composition frame.
- **The sequencer's own cost**, minimum of many runs, `avgen_tests "[seqcost]"`:
  bake 213 µs, install (bake + sixteen layers realised + bind) 433 µs, `animationAt` **26 ns per
  frame**, and the 25 tracks a five-shot piece bakes evaluate in **0.45 µs per frame** above the
  `resetFinals` every frame pays anyway.
- **The proof-of-concept**: 48 tracks, 230 keys, 18 layers, 0 unresolved targets; 9,279 parameters;
  227 composition nodes; ~430,000 triangles. 105 seconds at 1280×720 ProRes 422 with the audio
  muxed renders in **49.7 s** (63.3 fps), sequence hash `059c6e621acdbfe6`.
- **The score analyses.** `tools/make_city_score.py` was written to be sequenced rather than merely
  heard, and the engine agrees: 96.0 BPM at 0.92 confidence over 165 beats (the true count is 168),
  and the musical fold puts section boundaries at 23.55, 40.00, 46.12, 56.12, 66.07, **80.00**,
  86.15 and 96.14 seconds — 40.00 and 80.00 being exactly the two chorus starts the arrangement
  was blocked out to. Pinned by `avgen_tests "[poc]"`, which is hidden because the score is
  generated rather than committed.

### Known limitations

- Entity behaviours (ADR-088) integrate `dt` and are not scrub-deterministic. A sequence's actors are
  the deterministic alternative and the piece uses them; `EntityWorld::reset()` exists and nothing
  calls it, and even if something did it would return entities to *t = 0* rather than to the seeked
  time.
- No crossfade between two 3D scenes; a dip to black is what exists, and it is two keys on
  `scene/brightness` rather than a renderer change.
- `lookAtWeight` is per shot, not animated, so a shot whose subject *changes* has to author its
  targets. The proof-of-concept's reveal does exactly that.
- Every scene slot is resident.
