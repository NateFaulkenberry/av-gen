# Procedural Space POC: architecture decision (Phase 1)

*Engineering agent, 2026-09-29. Written from the code at `57c4c032` before any implementation. The
decision record is ADR-1000.*

## The finding that decides everything

**AV Gen already has an SDF ray marcher, and nothing uses it.** ADR-027 (2026-09-09) built it:

| layer | what exists | where |
|---|---|---|
| data | `spatial::SdfTree`: 8 primitives, 6 booleans (3 smooth), 8 domain ops (translate, rotate, scale, twist, bend, repeat with finite `count`, polarRepeat, mirror), 4 displacements (noise, voronoi, wave, field) | `src/spatial/sdf.{hpp,cpp}` |
| CPU reference | `evaluate`, tetrahedron `normal`, `validate`, a structural hash, JSON, surface-nets meshing, and `evaluatePacked`, the CPU copy of the GPU interpreter | same |
| GPU | a post-order bytecode interpreter (`sdfEvaluate`, 8-deep stacks, at most 128 records) | `shaders/sdf.wgsl` |
| pass | `SdfRenderer`: a screen quad over each object's projected bounds (the full screen when the camera is inside them), sphere tracing with relaxation and a distance-scaled epsilon, the shared PBR `shadeSurface` (lights, IBL, material programs, emission lanes, fog), `frag_depth` written, a depth-prepass entry and a reduced-step shadow-map entry, its own GPU timestamp (`"sdf"` on the frame timeline) | `src/rendering/sdf_renderer.{hpp,cpp}`, `shaders/sdf_raymarch.wgsl` |
| scene | `scene::SdfObject` (tree, transform, material, bounds, march settings); composition node kind `"sdf"`; `Scene::sdfs` | `src/scene/sdf_object.{hpp,cpp}`, `src/scene/composition.cpp` |
| parameters | every node member is a registered, modulatable parameter, `sdf/<node>/node/<i>/<field>`, re-packed every frame, so modulation is free | `registerSdfParameters` / `applySdfParameters` |
| tests | CPU unit tests, CPU/GPU parity at 1e-4 | `tests/unit/test_sdf*.cpp`, `tests/rendering/test_sdf_gpu.cpp` |
| docs | a full node reference, the packing scheme, and "how to add a node kind" | `docs/sdf.md` |

No example project, scene or asset places an `"sdf"` node (`grep -rl '"kind": "sdf"' examples` finds
nothing; the only uses are two emission-lane tests). This is the "built but unreachable" pattern that
recurs in this codebase. The POC's first job is therefore **to be the subsystem's first user**, not to
build a second one.

## The answers to the brief's questions

### A. Where should an experimental procedural-space renderer live?

It lives in the existing SDF path. The spatial rules are **data**: a composition scene file places one
or more `"sdf"` nodes, and a project file carries the routes, states and render settings. That data
goes in `examples/space/`. The engine changes are **generic SDF vocabulary and instrumentation**, and
they land in the files that already own them:

- `src/spatial/sdf.*` and `shaders/sdf.wgsl` get the domain operations the brief needs and the tree
  lacks: a plane **fold**, iterated fold-and-scale **recursion**, a **morph** combination that
  interpolates between child structures, and `count` as a parameter.
- `src/scene/sdf_object.*` gets **named nodes** (`node/<name>/<field>` instead of `node/17/size`), a
  march block exposed as parameters (steps, epsilon, relaxation, maximum distance) and a small look
  block (SDF ambient occlusion, edge emission, and a cheap soft shadow if it earns its cost).
- `shaders/sdf_raymarch.wgsl` and `SdfRenderer` get the maximum distance, the look terms and
  **sampled step statistics** (average and maximum steps, hit ratio), read back like the existing
  timestamps.

Nothing in the list is specific to "space". Each change is an SDF feature any scene can use.

### B. How can it be instantiated without becoming a permanent first-class feature?

It is instantiated as a project. `examples/space/space.json` loads a composition with `"sdf"` nodes.
There is no new panel, no new node kind, no world-effect kind and no menu entry. The editor already
shows SDF objects: the World panel's "SDF" tree and the stats line (`control_panel.cpp:2606`). The
parameter inspector shows the node parameters like any other, and with names they read as
`sdf/space/node/columns/size`. That is the brief's §13 "inspector-based, parameter-driven" surface, and
it already exists.

### C. How can it receive audio and modulation?

It receives them through ordinary routes, with no new code. `audio.bass`, `audio.lowMid`, `audio.mid`,
`audio.highMid`, `audio.treble`, `audio.energy`, `audio.onset`, `beat.*`, `section.*` and `music.*`
events are signals on the existing bus (`src/signals/audio_signals.cpp`, `src/app/engine.cpp`).
Routes (`add | multiply | replace | min | max`, with attack and decay, curves, thresholds, envelopes
and remap) drive any modulatable parameter, including ints (rounded) and bools (≥ 0.5).

For the brief's "discrete events change discrete structure":

- **Scene states** (`src/app/scene_states.*`, `docs/scene-states-and-macros.md`) are a state machine
  whose states are presets. Its triggers are beat, bar, onset, signal, macro, phrase and section, with
  quantised transitions. A state change morphs a group of parameters, and integer parameters snap.
  That is "the drop changes the spatial configuration", built already.
- The new **`morph`** node turns "switch structure" into one float (`node/<name>/amount`, 0..k−1). A
  state, a timeline step key or a route can move it, and a transition morphs it continuously.
- `count` becomes a parameter, so "a beat changes the repetition count" is a scene state or a route
  into an int.

There is no second audio system.

### D. How does it take part in scene loading and rendering?

It takes part exactly as any composition node does. The composition registers the node's parameters,
applies finals every frame, and puts the live `SdfObject` in `scene.sdfs`. `SceneRenderer::render`
runs `SdfRenderer::update`, the depth prepass, the raymarch pass between the opaque and transparent
phases of the lit pass, and the shadow maps. After that come the existing post chain (GTAO, bloom,
exposure, tonemap, grade), the headless and offline render (`--headless --render`), the timeline,
scene states, presets and project save. Depth composition means meshes, particles and characters
compose with the SDF space in both directions. The brief's §32 "hybrid SDF + raster" architecture is
**therefore already the architecture**, and the POC does not have to build it.

### E. How can the experiment be removed or promoted later without damage?

- **Removal:** delete `examples/space/` and `docs/prototypes/procedural-space/`. The engine changes
  are generic SDF vocabulary with their own tests and `docs/sdf.md` rows. If the owner does not want
  them either, each is a self-contained node kind or field. Under ADR-441, cutting one means deleting
  its enum value, its two formulas and its rows, with no shim.
- **Promotion:** the main limit is the interpreter (see G). Promotion means **compiling** a tree to
  WGSL when its structure changes (as `shaders::generateModuleSource` already does for user shader
  layers), while the parameters stay live uniforms. A library of named space presets would follow,
  which is scene files. Neither changes the scene format or the parameter paths.

### F. Can the existing generator abstraction represent this cleanly?

`scene::ProceduralGeometry` (ADR-023) cannot represent it cleanly. It is source + distribution +
variation + deformer stack over **instanced meshes**. Its topology is fixed per rebuild, its deformers
are per-vertex, and a boolean or a domain fold has no meaning in it. The architecture of the Infinite
Temple and the Cathedral examples is built that way, and it cannot fold, recurse or subtract a doorway
from a wall. The SDF tree **is** the procedural-geometry abstraction for implicit geometry, and ADR-027
already placed it beside ProceduralGeometry as a sibling composition node. Nothing new is needed.

### G. Generator, render pass or scene type?

It is none of those on its own. The code already answers the question by splitting it into layers,
and each layer is the right home for its own part:

| concern | home | why |
|---|---|---|
| the geometry (rules, operators, parameters) | **scene data**: `SdfObject` in a composition | serialises, presets, routes, timeline, states, AI tools, all unchanged |
| evaluating it | **the `SdfRenderer` render pass** inside `SceneRenderer` | shares frame uniforms, lights, IBL, shadows, fog, depth and aux targets |
| an "experiment" | **a project**, `examples/space/` | removable, loads like any other example |

- A **generator** in the shader-layer sense (ISF user shaders, `src/shaders/shader_format.hpp`) would
  be a full-screen image with no scene camera, lights, depth, shadows, fog or composition with meshes.
  It could only mimic the camera through `INPUTS`. It is the right home for a 2D effect, not a space.
- A **new render pass** would duplicate `SdfRenderer`.
- A **new scene type** would be a parallel scene format, which §28 forbids and nothing requires.
- A **world effect** (ADR-500 registry) would be a first-class kind with an Add button in the World
  Effects panel. That is exactly the permanent product surface §12 forbids, and the registry's buckets
  are per atmospheric GPU payload besides.

## What the existing path costs, and the risk this decision accepts

1. **Interpreter overhead.** Every march step runs the packed program: a loop over up to 128 records
   with a branch per record and 8-deep private stacks. A hand-written WGSL scene does the same work in
   straight-line code. An architectural tree of 30-50 nodes is 40-80 records per step. This is the
   decision's risk, and Phase 3 measures it first. **If the interpreter cannot hold an interactive
   frame at render scale 0.5 on this tree, the fallback is not a new renderer.** It is a compiled
   variant of the same pass: generate a WGSL `sdfEvaluate` from the tree, recompile on a structural
   change, and keep the parameters in the storage buffer. That is the promotion path in E, pulled
   forward.
2. **The depth prepass marches as well.** `fs_sdf_depth` marches with the same steps as the lit pass,
   so a full-screen SDF is marched twice, plus the shadow cascades at reduced steps. That will be
   measured, not assumed.
3. **One material per object.** A dark structure and an emissive accent layer are two `sdf` nodes, so
   two marches. Emissive edges come from the new look block or from a material program (`curvature` /
   `localPosition` masks, ADR-030), so they do not need a second object.
4. **Limits:** 64 nodes, depth 8 and 8 stack entries per tree. The recursion node puts "nested
   architecture" in one node, so the depth limit does not bite for it.

## Camera, render scale and instrumentation (what exists, what the POC adds)

- **Camera:** the composition camera has `mode` 0 orbit (`orbitSpeed`), 1 free (position and target),
  and 2 spline (`splineT`, `lookAhead`). All are routable parameters (`camera/*`). The stationary
  observer is mode 1 with constant keys. Forward movement is mode 1 with a keyed or routed position,
  or mode 2 on a straight spline. Orbit is mode 0. The editor's own orbit gestures cover free
  inspection. **Nothing to add.** The example project ships all three as scene states or timeline
  choices.
- **Render scale:** `QualitySettings::renderScale` sizes the HDR scene targets. In the editor it is the
  canvas scale (settings `general.canvasRenderScale`, 0.25-1; CLI `--canvas-scale <f>`; adaptive
  `--adaptive-scale on`). The brief's 1.0, 0.75, 0.5 and 0.33 are all reachable, and the POC measures
  each. Offline pins it to 1.0, which is correct for final frames.
- **Timing:** GPU per-pass medians come from `--headless --frames N --bench-json f` (the raymarch pass
  is labelled `sdf`). CPU frame time comes from `--profile-cpu` and `--profile-csv`. The POC adds
  **sampled step statistics** (average and maximum steps, hit ratio) to `SdfStats`, the stats line and
  the bench JSON.

## Decision

Host the Procedural Space POC on the existing ADR-027 SDF path, as composition `"sdf"` nodes in an
example project. Extend the tree and the pass generically: fold, recursion, morph, named nodes,
`count` as a parameter, march and look parameters, and step statistics. Drive it with the existing
routes, scene states and timeline. Build no panel, scene type, world effect or renderer. If
measurement condemns the interpreter, compile the tree; do not write a second marcher.

## Why no "Euclidean Space" panel

The inspector, the World panel's SDF tree, scene states and routes already give a
"developer-oriented, parameter-driven, inspector-based" surface (§13). A dedicated panel would be the
only part of the POC that the removal in E could not undo cleanly. With named nodes the parameter list
reads as the architecture (`columns/size`, `hall/twist/amount`), which is all a panel would have
added.
