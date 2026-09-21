# av-gen

A native C++ real-time GPU audiovisual engine. Not a waveform visualizer: the goal is a small
scene engine in which audio analysis drives a general parameter/modulation system that in turn
drives GPU-rendered 3D scenes, in real time and as deterministic offline frame sequences.

Procedural world engine (current): load audio, analyze it (bands, onsets, beat and tempo) and
build a world from data. Worlds are made of procedural objects (primitives, points, or another
object used as a source) instanced by linear, grid, radial, spiral, spline and grammar
distributions with seeded variation and recursion; typed point clouds with named attributes that
spatial operators filter, sort, scatter and transform; fields (radial, box, sphere, gradient,
noise, Voronoi, curl noise, vortex, attractor, wave and compounds) sampled identically on the CPU
and the GPU; effectors that apply a field to an object's instances every frame; GPU vertex
deformers (bend, twist, sine, noise, displacement, field, spline path); signed distance fields
with constructive geometry, raymarched with depth or meshed by surface nets; procedural material
programs; GPU particles that read the same fields; splines used for distributions, deformation,
emission and camera rails; and GPU frustum culling with levels of detail for large scenes.

A node graph authors all of it and emits ordinary scene data, so the runtime stays a flat data
path. Scene states morph presets on beat-synced transitions, world macros fan one knob out to
many parameters as ordinary routes, and an inspector answers "why is this moving" for any
parameter. Everything else the engine already had still applies to all of it: glTF scenes, PBR
materials and image-based lighting, user WGSL shader layers, the post chain, modulation from
audio, LFOs, envelopes, noise and macros, keyframe automation with cues, presets, projects with
migration and bundling, deterministic offline renders to PNG, EXR or video, OSC and MIDI control,
multiple output windows with warp and blend, and Syphon and NDI sharing.

Ten example worlds ship as data, from the Geometry Lab to the flagship Infinite Temple with its
seven macros and four-minute state arc. None of them required a line of C++.

```
Audio -> AnalysisRunner -> SignalBus -> Timeline -> Modulator -> ParameterSet
      -> generators (points, fields, splines, SDFs, grammars) -> Scene
      -> SceneRenderer (WebGPU/Dawn on Metal): cull/LOD -> effectors -> instanced draws
      -> SDF raymarch -> particles -> post -> window / output windows / image / video
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
./build/debug/src/avgen --project show.json --render renders/exr --format exr                  # scene-linear EXRs
./build/debug/src/avgen --queue jobs.json                            # batch renders
./build/debug/src/avgen --project show.json --input --osc-port 9000 # live input + OSC/MIDI control (docs/control.md)
./build/debug/src/avgen --audio /tmp/track.wav --shader shaders/examples/feedback.wgsl --post my_post.wgsl
./build/debug/src/avgen --composition scenes/stage.json --audio /tmp/track.wav --play   # scene file (ADR-017)
./build/debug/src/avgen --example "The Infinite Temple" --audio /tmp/track.wav --play   # a shipped world
```

Keys: Space play/pause, O open audio, S open scene, E open environment, Left/Right seek 5 s.
Drop an audio, .glb/.gltf, .hdr, .wgsl, project or scene .json file on the window to load it.
The Modulation window's Scene tab adds and removes composition nodes; File > Save Scene As
writes a scene file. `--help` lists every flag.

## Documentation

- [docs/architecture.md](docs/architecture.md): modules, data flow, threading, seams for growth
- [docs/build.md](docs/build.md), [docs/testing.md](docs/testing.md)
- Performance is two documents, deliberately: [docs/performance.md](docs/performance.md) is the
  GPU's frame, [docs/application-performance.md](docs/application-performance.md) is the main
  thread -- the UI, the engine update, threading, allocation and latency
- [docs/audio.md](docs/audio.md), [docs/rendering.md](docs/rendering.md), [docs/lighting.md](docs/lighting.md),
  [docs/shaders.md](docs/shaders.md),
  [docs/assets.md](docs/assets.md), [docs/project-format.md](docs/project-format.md)
- [docs/authoring.md](docs/authoring.md): how to build a world, window by window
- [docs/sequencer.md](docs/sequencer.md): choreography through time -- shots, scene cuts,
  characters, lyrics and markers, and how a piece becomes ordinary timeline tracks
- [docs/song-direction-manual.md](docs/song-direction-manual.md): the artist's guide -- import a
  song, analyze it, and turn its sections into an edit you control. Written for someone who has
  never opened the camera controls
- [docs/song-analyzer.md](docs/song-analyzer.md): audio to sections -- the detection pipeline,
  confidence, manual refinement, and what pressing Analyze again does to your work
- [docs/section-shot-language.md](docs/section-shot-language.md): the section and shot-intent
  vocabulary -- built-in types, your own types, and why a shot intent never names a camera
- [docs/composition.md](docs/composition.md): the 2D layers over the frame
- [docs/output-preview.md](docs/output-preview.md): seeing what the render will produce -- the
  output frame in the canvas, safe areas, preview quality, and the two things a preview cannot show
- [docs/art-direction.md](docs/art-direction.md): the World Director, looks, composition and phrasing
- [docs/visual-cookbook/scale-and-silhouette.md](docs/visual-cookbook/scale-and-silhouette.md): why a hero cannot supply its own scale, and three things behind it that fight its silhouette
- The procedural world: [docs/procedural-geometry.md](docs/procedural-geometry.md),
  [docs/spatial-data.md](docs/spatial-data.md), [docs/gpu-fields.md](docs/gpu-fields.md),
  [docs/splines.md](docs/splines.md), [docs/grammar-and-hierarchy.md](docs/grammar-and-hierarchy.md),
  [docs/sdf.md](docs/sdf.md), [docs/procedural-materials.md](docs/procedural-materials.md),
  [docs/procedural-graph.md](docs/procedural-graph.md),
  [docs/gpu-culling-lod.md](docs/gpu-culling-lod.md),
  [docs/scene-states-and-macros.md](docs/scene-states-and-macros.md)
- [docs/dependencies.md](docs/dependencies.md): every third-party library and dataset, its licence
  and reason -- including the credit line that published work must carry
- [docs/quality-lab/](docs/quality-lab/): the Render Quality Lab -- objective, repeatable measurement
  of rendered output. [reconnaissance](docs/quality-lab/repository-reconnaissance.md),
  [research](docs/quality-lab/research.md) (what was adopted and what was rejected, with reasons),
  [architecture](docs/quality-lab/architecture.md), [metrics](docs/quality-lab/metrics.md),
  [artifact detection](docs/quality-lab/artifact-detection.md),
  [reference rendering](docs/quality-lab/reference-rendering.md),
  [benchmark scenes](docs/quality-lab/benchmark-scenes.md),
  [experiments](docs/quality-lab/experiments.md)
- [docs/decisions/](docs/decisions/): Architecture Decision Records
- [docs/research/](docs/research/): the Phase 0 technology research with sources
- [docs/development-log.md](docs/development-log.md): what was built, tested, and measured

## Status

Milestones 0.1 to 1.2, the procedural geometry phase and the procedural world engine are complete
on macOS 26 / Apple silicon. Windows and Linux are
architecturally supported (WebGPU via Dawn, SDL3) but not yet built or tested. Licence: MIT.
