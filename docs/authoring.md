# Authoring a world

This is the practical guide: how to build a world by hand, what the windows do, and where each
system's reference lives. Everything here edits ordinary parameters, so anything you can do in the
UI can also be automated, saved in a preset, driven by audio and rendered offline.

## 1. The three ways in

| Route | Use it for | Reference |
|---|---|---|
| Scene JSON | precise, diff-friendly authoring; what the shipped worlds use | [procedural-geometry.md](procedural-geometry.md) |
| The graph | exploring, reusable subgraphs, wiring audio to structure visually | [procedural-graph.md](procedural-graph.md) |
| The UI | adjusting a loaded world live, performing, capturing presets | this document |

A scene file is either graph-driven or hand-made. Installing a graph replaces the nodes it
installed last time, so hand edits to those nodes are overwritten; hand-added nodes are left alone.

## 2. The windows

**Control** carries transport, response gains and the performance readout.

**Parameters** lists every registered parameter, grouped by its path's first segment, filtered by
the authoring layer chosen in the World window. A modulated parameter shows its final value next
to the slider, and an automated one is marked. Right-click any parameter to reset it or to key it
at the current time.

**Modulation** holds the routes, sources, presets, shaders, scene nodes, timeline and control
bindings.

**World** is the authoring hub:

- *Overview* lists the flattened scene by family with counts. Select an object to inspect it.
- *Inspector* answers "why is this moving": for every parameter of the selected object it lists
  the routes with their source signal and last output, timeline tracks, cues, states and world
  macros writing to it, plus the base and final values. It also shows the object's counts, bounds,
  effectors and per-attribute statistics.
- *States* runs the scene state machine: go to a state, snap to it, edit transition time, easing
  and beat or bar quantisation, and capture the current look as a new state.
- *Macros* edits world macros: turn a knob, add targets with their ranges, remove a macro.
- *Debug* toggles the inspection modes described below.

**Graph** is the node canvas. Click a pin then a compatible pin to connect, alt-click an input to
disconnect, middle-drag to pan, and edit the selected node's parameters in the side panel. Any
change re-evaluates the graph and re-installs what it emits.

**Assets** catalogues projects, scenes, graphs, models, environments, shaders and audio found in
the example directories, filtered by kind and a search box. Opening an entry loads it.

## 3. A world from nothing, in seven steps

1. **Start from a shape.** Add a `procedural` node: pick a source primitive and a distribution.
   Radial for colonnades, spiral for towers and corridors, grid for fields of objects, spline for
   anything that follows a path, grammar for structured repetition.
2. **Break the uniformity.** Give the object a `variation` seed with small random position,
   rotation and scale, then add point operators: a probability filter thins a grid, a distance
   filter carves a clearing, sort and duplicate build rhythm.
3. **Add a field.** A `field` node is a spatial signal, not an effect. Radial and wave fields
   read as pulses, curl noise as turbulence, vortex and attractor as flow.
4. **Connect the field to the geometry.** An effector applies it to the object's instances every
   frame: position offset for motion, scale for breathing, emission for light, colour for palette.
   A `field` deformer bends the vertices instead, and `emissiveField` lights an object by where it
   sits in the field.
5. **Light and frame it.** Set `scene/keyLight` and `scene/brightness`, then the camera: mode 0
   orbits, mode 1 is a free camera you can key on the timeline, mode 2 rides a spline.
6. **Give it macros.** Add a world macro per idea, not per parameter: energy, architecture,
   distortion, colour, atmosphere. Point each at the handful of parameters that idea should move.
7. **Give it an arc.** Capture presets for the moments you like, promote them to states with
   transition times, and either trigger them from audio or place them as cues on the timeline.

## 4. Connecting audio without making a visualiser

Route audio to macros and states rather than to everything:

- `audio.rms` to an energy macro, with a slow attack and release, is the backbone.
- `audio.bass` to structural scale, `audio.lowMid` to deformation, `audio.mid` to rotation.
- `audio.treble` to emission, `audio.spectralCentroid` to hue, `audio.spectralFlux` to turbulence.
- `audio.onset` through a peak-hold or linear-fall envelope to impulses: a wave field's origin, a
  particle burst, a brief chromatic aberration.
- `beat.pulse` and `beat.bar` to cyclic motion; a macro threshold or a beat count to state changes.

Every one of those is an ordinary route with its own processing chain, so the same map drives the
live window, an offline render and a remote controller identically.

## 5. Inspection

The Debug tab draws into the world: instance points, object bounds, per-instance normals, spline
paths with their frames, field frames with falloff radii and sampled vectors, density and
attribute colouring, instance identifiers, level-of-detail and culling colouring, and a horizontal
slice through an SDF. Point size, the field sampling grid, the point budget and depth testing are
adjustable. It is the fastest way to see why something is not where you expect.

The profiling capture records per-frame timings and counts and writes a JSON report with
percentile summaries, for comparing builds or finding the frame that costs too much.

## 6. Where the details live

- Geometry, distributions, deformers: [procedural-geometry.md](procedural-geometry.md)
- Point clouds, attributes, operators, effectors: [spatial-data.md](spatial-data.md)
- Fields on the GPU: [gpu-fields.md](gpu-fields.md)
- Splines: [splines.md](splines.md); recursion and grammars: [grammar-and-hierarchy.md](grammar-and-hierarchy.md)
- Signed distance fields: [sdf.md](sdf.md); materials: [procedural-materials.md](procedural-materials.md)
- The graph: [procedural-graph.md](procedural-graph.md)
- States, macros, layers: [scene-states-and-macros.md](scene-states-and-macros.md)
- Culling and level of detail: [gpu-culling-lod.md](gpu-culling-lod.md)
- Live control: [control.md](control.md); offline rendering: [rendering.md](rendering.md)
