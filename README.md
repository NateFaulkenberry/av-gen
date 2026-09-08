# av-gen

A native C++ real-time GPU audiovisual engine. Not a waveform visualizer: the goal is a small
scene engine in which audio analysis drives a general parameter/modulation system that in turn
drives GPU-rendered 3D scenes, in real time and as deterministic offline frame sequences.

Milestone 0.1 (this release): load an audio file, play it, analyse it, and render a GPU scene
whose scale, rotation, emission, brightness and impulse respond to bass, mids, highs, RMS and
onsets through data-driven modulation routes.

```
Audio file -> AudioPlayer -> AnalysisRunner -> SignalBus -> Modulator -> ParameterSet
           -> OrbScene -> SceneRenderer (WebGPU/Dawn on Metal) -> tone map -> window / image
```

## Build (macOS, Apple silicon)

Requirements: Xcode 26 command-line tools, CMake >= 3.28 (4.0 tested), Ninja, Python 3 (only for
the test-audio generator). Everything else is fetched and pinned by CMake.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/src/avgen --audio path/to/track.wav --play
```

`cmake --preset release` for an optimised build. See [docs/build.md](docs/build.md) for options,
the Dawn from-source path, and platform notes.

## Run

```sh
python3 tools/make_test_audio.py /tmp/track.wav        # deterministic 120 BPM test track
./build/debug/src/avgen --audio /tmp/track.wav --play  # live window
./build/debug/src/avgen --headless --audio /tmp/track.wav --frames 300 --fps 30 --capture out.ppm
```

Keys: Space play/pause, O open file, Left/Right seek 5 s. Drop an audio file on the window to
load it. `--help` lists every flag.

## Documentation

- [docs/architecture.md](docs/architecture.md): modules, data flow, threading, seams for growth
- [docs/build.md](docs/build.md), [docs/testing.md](docs/testing.md), [docs/performance.md](docs/performance.md)
- [docs/audio.md](docs/audio.md), [docs/rendering.md](docs/rendering.md), [docs/shaders.md](docs/shaders.md),
  [docs/assets.md](docs/assets.md), [docs/project-format.md](docs/project-format.md)
- [docs/dependencies.md](docs/dependencies.md): every third-party library, licence and reason
- [docs/decisions/](docs/decisions/): Architecture Decision Records
- [docs/research/](docs/research/): the Phase 0 technology research with sources
- [docs/development-log.md](docs/development-log.md): what was built, tested, and measured

## Status

Milestone 0.1 complete on macOS 26 / Apple silicon. Windows and Linux are architecturally
supported (WebGPU via Dawn, SDL3) but not yet built or tested. Licence: MIT.
