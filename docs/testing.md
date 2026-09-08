# Testing

Strategy: ADR-009. Tests are Catch2 v3, discovered into CTest with labels `unit` (GPU-free,
`avgen_tests`) and `gpu` (`avgen_render_tests`, skips itself when no adapter is available).

```sh
ctest --preset debug                 # everything
ctest --preset debug -L unit         # GPU-free only
ctest --preset debug -L gpu          # rendering tests
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
| scene | `test_scene.cpp` | mesh generator validity and counts, normals, TRS order, projection depth range 0..1, OrbScene registration/routes/determinism |
| integration | `tests/integration/test_pipeline.cpp` | synthetic audio → Engine (offline) → scene: bass raises scale, treble raises emissive, RMS raises brightness, onsets pulse impulse, bit-identical across runs, rotation integrates modulated speed, error path |
| rendering (GPU) | `tests/rendering/test_gpu.cpp` | headless context; shader errors with file/line; clear + readback exact; lit cube renders deterministically (hash equal), differs when the scene changes; resize; invalid meshes skipped without GPU errors |

Synthetic signals live in `tests/support/synth.hpp` (sine, silence, seeded noise, impulse train,
click track). Test WAV fixtures are generated at test time into the temp directory; no real
recordings are needed.

## Determinism requirements

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
