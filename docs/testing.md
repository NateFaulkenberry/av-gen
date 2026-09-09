# Testing

Strategy: ADR-009. Tests are Catch2 v3, discovered into CTest with labels `unit` (GPU-free,
`avgen_tests`) and `gpu` (`avgen_render_tests`, skips itself when no adapter is available).

```sh
ctest --preset debug                 # everything
ctest --preset debug -L unit         # GPU-free only
ctest --preset debug -L gpu          # rendering tests
cmake --preset asan && cmake --build --preset asan && ctest --preset asan   # ASan + UBSan
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan   # ThreadSanitizer
./build/debug/src/avgen --audio track.wav --play --frames 900 --stress 7    # random UI-like actions
./build/debug/tests/avgen_tests "[analysis]"   # Catch2 tag filter
./build/debug/tests/avgen_tests "[audio][device]"  # tests needing an output device (SKIP if none)
```

## What is covered (milestone 0.1)

| Area | File | Highlights |
|---|---|---|
| core | `tests/unit/test_core.cpp` | fixed-step and realtime clocks, seek; PCG32 reference values; SPSC ring incl. concurrent producer/consumer; triple buffer |
| headers | `tests/unit/test_headers.cpp` | every public header compiles standalone; `Parameter<T>` for all supported types |
| audio | `test_audio_file.cpp`, `test_analysis_stream.cpp`, `test_audio_player.cpp` | WAV round trip via miniaudio, mono downmix, zero-padding, missing/invalid files, odd sample rates; discontinuity markers are exact under concurrency; device playback: position advance/pause/seek/end-of-file/restart (skipped without a device) |
| analysis | `test_fft.cpp`, `test_analyzer.cpp`, `test_analysis_track.cpp`, `test_analysis_runner.cpp` | 440 Hz peak bin and magnitude scale, Parseval, silence; band dominance for 60 Hz/440 Hz, RMS of a sine, white-noise centroid, impulse-train and click-track onsets, chunking invariance, bit-identical determinism, frame stamping; offline track lookup; runner thread |
| signals/params | `test_signal_bus.cpp`, `test_parameters.cpp`, `test_processor.cpp`, `test_modulation.cpp`, `test_serialization.cpp` | defaults, clamping, soft ranges, duplicates; every chain stage, time-constant accuracy, frame-rate independence, envelopes; route binding errors, all ops, priority, component targeting, master gain, reset; JSON round trips, version/format validation, unknown paths, malformed files |
| scene | `test_scene.cpp`, `test_scene_model.cpp` | mesh generator validity and counts, normals, TRS order and matrix decomposition, projection depth range 0..1, bounds, textures/lights/clear, OrbScene registration/routes/determinism |
| assets | `test_image.cpp`, `test_gltf_loader.cpp` | PNG/HDR round trips, sRGB tagging, error paths; in-memory GLB fixture: hierarchy transforms, vertex data, material factors and texture refs, embedded PNG decode, punctual lights, cameras, bounds, failure leaves the scene untouched, id offsets on repeated loads; Khronos samples when `AVGEN_SAMPLE_ASSETS` is set |
| beat tracking | `test_beat_tracker.cpp` | tempo estimation on synthetic envelopes (120/90/160 BPM, flat input), offline Ellis tracker accuracy on a click track, live tracker convergence, tempo-change following, determinism, reset; AnalysisTrack stamping |
| sources/presets | `test_sources.cpp`, `test_presets.cpp` | LFO shapes and seek exactness, beat sync, ADSR timing, noise determinism/continuity, random sample-and-hold sequences, timeline interpolation and looping, macros, rack JSON round trips, modulators of modulators; preset capture/apply/blend, bank JSON |
| timeline | `test_timeline.cpp`, `tests/integration/test_timeline_engine.cpp` | key insertion and sorting, every interpolation, looping, vector and single-component tracks, apply modes write finals not base, bind/unbind and unknown targets, recordKey, cues and morph progress, JSON round trip and malformed input, determinism; engine: offline keyed values at exact times, routes add on top of automation, beat-based loop follows the click track, cue recalls a preset and morphs, seek re-syncs cues, project v3 round trip, scene swap rebinds tracks |
| project system | `tests/integration/test_project_system.cpp`, migration cases in `test_serialization.cpp`, `test_recent_files.cpp` | relative asset references and full session restore after moving the folder, missing assets as warnings with parameters still applied, composition projects by path or inline, bundle export reopens after deleting the originals, new project resets; v1/v2 documents migrate step by step, too-new rejected, caller's document untouched; recent list order/dedupe/limit/persist/prune |
| offline rendering | `test_render_settings.cpp`, `tests/rendering/test_render_job.cpp`, `test_video_writer.cpp` | range/frame-count resolution, pattern and JSON validation; GPU: an exact PNG sequence at the requested size, bit-identical sequence hash on a fresh engine + renderer, bounded stepping and cancellation with partial output, bad settings rejected, video with audio when a backend exists; native/ffmpeg video writers probed after writing |
| ui logic | `test_ui_logic.cpp` | regression: route slider bounds independent of the value (0.2 crash) |
| stress | `test_engine_stress.cpp` (`[device][stress]`) | rapid seeks/param/route/volume/transport edits during live playback; run under ASan and TSan |
| modulation integration | `tests/integration/test_modulation_sources.cpp` | LFO drives the orb without audio, seek exactness, modulators of modulators, project round trip through the engine (sources, routes, presets, values, morph), beat clock from a click track |
| shader format | `test_shader_format.cpp` | header parsing (all input types, passes, defaults, errors), WGSL layout offsets, packing, generated module structure, size expressions, example shaders stay valid |
| shader layers | `tests/integration/test_shader_layers_set.cpp`, `tests/rendering/test_shader_layers.cpp` | parameter registration/packing/reload/remove, project round trip and scene swap; GPU: background colour from an input, post inversion, persistent feedback accumulation, broken shader → error pattern + diagnostics → fixed on reload, engine shader reload |
| particles | `test_particles.cpp`, `tests/rendering/test_particles_gpu.cpp` | parameter registration and rest-relative application; GPU: emission brightens the frame, particles die after their lifetime, disabled systems are skipped, bursts flash immediately and are clamped to capacity; hidden `[.perf]` one-million-particle probe |
| post-processing | `test_post_settings.cpp`, `tests/rendering/test_post_gpu.cpp` | 23 `post/*` parameters register/apply; GPU: bloom spreads light beyond a bright object, zero saturation yields grey, vignette darkens corners, all five tone operators stay in range and differ, DoF softens an out-of-focus edge, camera motion blur smears edges, transient pool reuse and ageing |
| file watcher | `test_file_watcher.cpp` | modified/removed/recreated files, poll interval |
| asset registry | `test_asset_registry.cpp` | path resolve/relativise against a base directory, scene and image caching (same object twice), errors not cached, reload bumps the version and yields a new object, per-sRGB image keys, loaded path listing, clear |
| composition | `test_composition.cpp`, `tests/integration/test_composition_engine.cpp` | nodes of every kind flatten correctly (shared meshes per glTF asset, entities per instance), unique names, node transform/visibility/emissive parameters, root transform, particle node parameters, nested scene parameter prefixes, remove unregisters, JSON/file round trips, nested scene files, self-inclusion and depth errors, malformed JSON, missing asset skipped with a warning, determinism, detach/reattach; engine: `.json` routing by `format`, composition swap keeps audio driving, save then reload |
| scene controllers | `tests/integration/test_gltf_scene.cpp` | GltfScene import + parameter surface + root transform maths; engine scene swap keeps audio driving the new surface; failed loads keep the old scene |
| integration | `tests/integration/test_pipeline.cpp` | synthetic audio → Engine (offline) → scene: bass raises scale, treble raises emissive, RMS raises brightness, onsets pulse impulse, bit-identical across runs, rotation integrates modulated speed, error path |
| rendering (GPU) | `tests/rendering/test_gpu.cpp` | headless context; shader errors with file/line; clear + readback exact; lit cube renders deterministically (hash equal), differs when the scene changes; resize; invalid meshes skipped without GPU errors; image-based lighting from a synthetic sky brightens and tints a rough white cube, skybox shows the sky, deterministic |

Synthetic signals live in `tests/support/synth.hpp` (sine, silence, seeded noise, impulse train,
click track). Test WAV fixtures are generated at test time into the temp directory; no real
recordings are needed.

## Determinism requirements

GPU particle systems use stable stream compaction since 1.0 (ADR-015 revision), so headless
hashes are bit-identical with particles on; the render job's sequence hash is the check.

- `Analyzer`: bit-identical output for identical samples regardless of chunking or run.
- Offline `Engine` runs: bit-identical parameter values and matrices across runs (tested).
- Renderer: identical image hash for identical scene + time on the same GPU (tested). Cross-GPU
  equality is not promised; a perceptual comparison is planned for visual regression.

## Conventions

- A bug fix adds a test that failed before the fix.
- Tests that need hardware (audio device, GPU) call `SKIP()` rather than fail when it is absent.
- No test reads a system clock for correctness; timing-based tests (`[device]`) only check
  monotonic progress with generous margins.
- Benchmarks: `Analyzer` hop time and modulation evaluation are reported, not asserted
  (`docs/performance.md`).
