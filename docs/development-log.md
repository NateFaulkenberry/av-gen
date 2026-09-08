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
