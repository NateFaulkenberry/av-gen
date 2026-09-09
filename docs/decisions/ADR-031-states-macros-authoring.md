# ADR-031: Scene states, world macros, layered authoring, inspection and debug drawing

- Status: Accepted (2026-09-09)
- Research: `docs/research/procedural-authoring.md`

## Decision
- **States** reuse presets: `app::StateMachine` holds states {name, preset, transition {duration, easing linear|smooth|ease|bezier}, triggers {timeline cue, beat, bar, onset threshold, OSC, MIDI, macro threshold, manual}}; a transition is a preset morph run by the engine (like cues) with easing and optional beat/bar quantisation. Stored in the project under `"states"`.
- **World macros** reuse macro sources: a `WorldMacro` is a macro knob plus a target map {path, min, max, curve} stored as ordinary routes with remap (`macro.<name> → target`), grouped in the UI; the beginner layer shows only world macros.
- **Authoring layers** are a UI filter over the one parameter set (beginner: world macros/atmosphere/camera; intermediate: generators/fields/materials/deformers; advanced: graph/attributes/GPU/simulation), never a second parameter system.
- **Inspector**: per object, the parameters and every route, track, cue, state and macro that writes to them ("why is it moving"), attributes (min/max/mean), fields and deformers affecting it, GPU stats. **World overview**: a navigational tree (architecture, fields, particles, materials, atmosphere, lighting, camera, post).
- **Debug drawing**: `rendering::DebugDraw` (lines and points, depth-tested or not) with modes for points, bounds, density/attribute colouring, field vectors and falloff, normals, splines, SDF slices, instance ids, LOD and culling; a **profiling capture** records per-frame CPU/GPU/system timings, buffer sizes and counts to a JSON report.

## Consequences
- Positive: performers get a few meaningful controls; long-form compositions become states on a timeline; every motion is discoverable.
- Negative: transitions of structural parameters (counts) snap; the inspector's "why" is only as complete as the routing data (custom shader layers stay opaque).
