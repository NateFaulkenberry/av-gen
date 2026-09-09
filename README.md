# av-gen

A native C++ real-time GPU audiovisual engine. Not a waveform visualizer: the goal is a small
scene engine in which audio analysis drives a general parameter/modulation system that in turn
drives GPU-rendered 3D scenes, in real time and as deterministic offline frame sequences.

Milestone 1.2 (current): load an audio file, play it, analyse it (bands, onsets, beat and tempo),
and render the built-in orb scene, any glTF 2.0 scene, or a composition of nodes (glTF instances,
orbs, grids, particle systems, nested scene files) saved as a scene file, with PBR materials,
textures, punctual lights, an HDR environment and GPU particle systems, behind or on top of user-written
WGSL shader layers, through a built-in post chain (bloom, colour grading, lens distortion and
chromatic aberration, depth of field, camera motion blur, five tone-mapping operators, vignette,
grain). Every parameter, including
shader inputs, can be driven by data-driven modulation routes from audio signals, the beat clock,
LFOs, envelopes, noise, random and timeline sources, and macros; a timeline keys any parameter
in seconds or beats and fires preset cues; presets snapshot and morph parameters; a project file saves
all of it plus the audio, scene and environment it belongs to, relative to the file, migrates
older versions, and exports as a self-contained bundle folder; offline renders are bit-identical
PNG sequences or ProRes/H.264 videos with the audio muxed, from the CLI, a queue, or in the
background of the live app. Live control: OSC (direct parameter addresses or bindings) and MIDI
become control signals or set parameters, and a microphone or line input can replace the file. Any number of output windows on any display
with crop, warp and edge blend, plus Syphon and NDI sharing to other applications.

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
./build/debug/src/avgen --scene DamagedHelmet.glb --env studio.hdr --audio /tmp/track.wav --play
./build/debug/src/avgen --headless --audio /tmp/track.wav --frames 300 --fps 30 --capture out.png
./build/debug/src/avgen --project show.json --play                 # restores audio, scene, environment and everything else
./build/debug/src/avgen --project show.json --export-bundle ./show_bundle   # self-contained folder
./build/debug/src/avgen --project show.json --render renders/show.mov --codec prores422   # offline video
./build/debug/src/avgen --project show.json --render renders/frames --size 3840x2160 --fps 60 --range 10:20
./build/debug/src/avgen --project show.json --render renders/exr --output exr                  # scene-linear EXRs
./build/debug/src/avgen --queue jobs.json                            # batch renders
./build/debug/src/avgen --project show.json --input --osc-port 9000 # live input + OSC/MIDI control (docs/control.md)
./build/debug/src/avgen --audio /tmp/track.wav --shader shaders/examples/feedback.wgsl --post my_post.wgsl
./build/debug/src/avgen --composition scenes/stage.json --audio /tmp/track.wav --play   # scene file (ADR-017)
```

Keys: Space play/pause, O open audio, S open scene, E open environment, Left/Right seek 5 s.
Drop an audio, .glb/.gltf, .hdr, .wgsl, project or scene .json file on the window to load it.
The Modulation window's Scene tab adds and removes composition nodes; File > Save Scene As
writes a scene file. `--help` lists every flag.

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

Milestones 0.1 to 1.2 complete on macOS 26 / Apple silicon. Windows and Linux are
architecturally supported (WebGPU via Dawn, SDL3) but not yet built or tested. Licence: MIT.
