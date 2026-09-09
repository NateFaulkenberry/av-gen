# Procedural authoring: graphs, subgraphs, presets, inspection

Status: research (2026-09-09). Decisions: ADR-028 (graph and dirty propagation), ADR-031 (states, world macros, authoring layers).

## 1. Graph models
- **Houdini**: pull-based cooking of a DAG; each node caches; only dirty nodes recook; nodes are typed by data family.
- **Blender GN**: typed sockets; fields (lazy per-element functions) vs geometry sockets; node groups expose inputs as a panel.
- **Unreal PCG**: graphs of typed pins; subgraphs; graph parameters become instance-editable; loops via "loop subgraph"; execution partitioned.
- **TouchDesigner**: cook-on-demand, operators reference each other by path, COMPs as subgraphs with custom parameters; UI is the graph.
- **Notch**: a node tree (parent/child), parameters keyframed, nodes reference others by connection.

## 2. What av-gen needs (and no more)
- The graph is an **authoring layer that emits the flat scene data** the engine already renders: procedural objects, fields, splines, SDFs, material programs, routes and parameter bindings. The runtime keeps rendering flat data; the graph is evaluated when it changes (dirty nodes only) and re-emits.
- Typed pins: float, vec2, vec3, colour, transform, point cloud, spline, field, mesh(source), sdf, material, particles, volume. Invalid links refused at connect time.
- Subgraphs = graph files with exposed parameters; a subgraph instance registers its exposed parameters under `graph/<instance>/…` so they are ordinary parameters; recursion by a subgraph referencing itself with `depth` and per-level transform, expanded deterministically and bounded.
- Dirty propagation: each node has a structural hash of its parameter *bases*; modulation of a parameter marks only "per-frame" (uniform) work unless the parameter is structural (count, kind, segments), in which case the node and its downstream re-emit.

## 3. Presets, states, macros
- Presets are parameter snapshots (exists). A **state** is a named preset plus transition settings and triggers; the cue mechanism (timeline) already morphs presets, so states reuse presets + cues with beat/bar/onset/OSC/MIDI/macro-threshold triggers evaluated by the engine.
- A **world macro** is a macro knob (exists) with a *map* of targets (path, min, max, curve): stored as ordinary routes with remap so nothing new is evaluated; the UI groups them.

## 4. Inspection
Per object: parameters, effective routes/tracks/cues affecting each ("why is it moving"), attributes (min/max/mean per attribute), fields affecting it, deformers, GPU timings; scene-wide: performance breakdown by system; debug draw modes for points, bounds, field vectors, splines, SDF slices, normals, LOD/culling colouring. The Notch, Houdini and PCG viewports all provide the equivalents (PCG's debug/inspection mode draws points coloured by attribute).
