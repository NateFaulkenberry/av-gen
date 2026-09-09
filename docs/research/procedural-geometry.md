# Procedural geometry: audit, research and architecture proposal

Status: proposal for the procedural-geometry phase (2026-09-09). Decision: ADR-023.

## 1. Repository audit (what exists, what to extend)

| Concern | Today | Extend or reuse |
|---|---|---|
| Scene model | `scene::Scene` holds `meshes` (`MeshData`: position/normal/uv + 32-bit indices), `entities` (transform, mesh id, `Material`, style), `lights`, `particles` (settings only), `environment`, `post` | Add `Scene::procedurals` next to `particles`: settings on the CPU, evaluation in the renderer |
| Draw path | `SceneRenderer` uploads meshes on `meshVersion`, draws each entity with one `DrawIndexed` and a 256-byte dynamic-offset `ObjectUniforms` slot (256 entities max) | Procedural objects get their own instanced pipeline and one object slot per *object*, not per instance |
| Instancing | none in the PBR path; particles draw instanced quads from a storage buffer | Same idea for meshes: one source mesh, a storage buffer of instance records, `DrawIndexed(indexCount, instanceCount)` |
| Materials | `Material` struct + a bind group of five textures per material (defaults when untextured) | Procedurals use the same `Material`, the same bind group, the same fragment shading |
| Shaders | `ShaderLibrary` with `#include`, hot reload, `pbr.wgsl` + `common.wgsl` | Factor the PBR fragment into an includable file; a new `procedural.wgsl` vertex stage with the deformer stack |
| Mesh generators | icosphere, cube, plane | Add box (subdivided), cylinder (caps, segments), UV sphere, torus |
| Parameters | `ParamDesc`, `ParameterSet`, path = address; `registerParticleParameters`/`applyParticleParameters` pattern (rest values + finals every frame) | Same pattern: `registerProceduralParameters` / `applyProceduralParameters` |
| Modulation, timeline, presets, OSC/MIDI | all resolve by parameter path | Free once parameters exist |
| Compositions | nodes of kind gltf/orb/grid/particles/scene, flattened; node params under `nodes/<name>/…`; particle nodes under `particles/<name>/…`; scene files are JSON | New node kind `procedural`; parameters under `procedural/<node>/…`; showcase scenes are scene files + presets, no C++ |
| Offline | `RenderJob` = offline engine + renderer, frame f at `start + f/fps`, hashes per frame | Unchanged; procedurals must be pure functions of parameters and render time |
| Determinism | PCG on the CPU, `pcg3d` hashes in WGSL, no wall clock | Instance randomness = hash(seed, id); noise = hash-based value noise of (position, time) |
| UI | generated panel grouped by the first path segment; label = last segment by default | Group `procedural/<name>`, labels `distribution/count` etc. |
| Tests | Catch2 unit/integration (GPU-free) + `avgen_render_tests` (GPU, readback, hashes), `[.perf]` probes | Same split |

Smallest clean extension: **one new scene component (`ProceduralGeometry`), one new renderer (`ProceduralRenderer`), one new shader, one new composition node kind, one new parameter registrar.** No second parameter, serialisation or modulation system.

## 2. Research

### 2.1 GPU instancing and instance data
- WebGPU `draw`/`drawIndexed` take `instanceCount` and `firstInstance`; the vertex stage reads
  `@builtin(instance_index)`. Instance data can be a vertex buffer with `stepMode: instance` or a
  storage buffer indexed by the builtin (spec: https://www.w3.org/TR/webgpu/#dom-gpurendercommandsmixin-drawindexed, accessed 2026-09-09).
  A storage buffer wins for us: arbitrary record size, readable by compute later, no vertex-layout permutation per attribute set.
- Dawn on Metal supports `firstInstance` and indirect draws (research/rendering.md R3, particles.md §3); indirect
  draw is only needed once instance counts are produced on the GPU — not in this phase.
- Buffer updates: `queue.writeBuffer` copies through a staging ring inside Dawn; writing a few hundred KB per frame is
  cheap (our particle uniforms and object slots already do this). Upload only when the structure changes.

### 2.2 Vertex deformation on the GPU
- Bend/twist/sine/noise/displacement are all *position functions* `p' = f(p, t)`; the exact normal is `n' = normalize(cofactor(J) n)`. Evaluating the Jacobian analytically per deformer is fragile when they stack, so the standard technique is **numerical differentiation of the whole stack**: deform `p`, `p + ε·t1`, `p + ε·t2` (a tangent basis around the original normal) and take the cross product (Unity/Unreal vertex-animation practice; Blender's displace modifier recomputes normals the same way). Cost: 3 evaluations per vertex, fine for our sizes.
- Order matters and must be explicit: deformers apply in stack order in the declared space (object-local before the instance transform, world after).
- Noise: hash-based value noise (`pcg3d`) with smooth interpolation is textureless, deterministic and already in `particles.wgsl`; fBM with 2–3 octaves is enough for architecture "breathing" (rendering-techniques.md E.1).

### 2.3 Distributions, radial and spiral geometry
- Radial: `θ = start + (end − start)·i/(N or N−1)` (closed circle divides by N, an arc by N−1); orientation modes are rotations about the plane normal: outward = `θ`, inward = `θ + π`, tangent = `θ + π/2`.
- Spiral (helix): `θ = 2π·turns·u`, `r = r0 + growth·u`, `y = height·u`, `u = i/(N−1)`; tangent orientation follows the derivative.
- Grid and linear are trivial; all of them are pure functions of `(index, count, parameters)` — no accumulated state, so they are frame-rate independent by construction.

### 2.4 Deterministic randomisation
- Per-instance random values come from a hash of `(seed, instanceIndex, channel)` (PCG-style mix, already used for particles). Same seed → same values on any machine, any frame rate, live or offline. No `std::rand`, no state carried between frames.

### 2.5 How other systems separate the stages
| System | Source | Instances | Transform variation | Deformation | Notes |
|---|---|---|---|---|---|
| Notch | Shape/Mesh nodes | Clone nodes (grid/radial/spline/…) | Clone node parameters + Effectors (fields) | Deformers (bend, twist, noise, …) as child nodes, ordered | Effectors are fields that modulate clones by position; deformers stack top-down |
| TouchDesigner | SOPs | Copy SOP / instancing on the Geometry COMP from CHOP/DAT tables | per-instance attributes from channels | SOPs (Twist, Noise) on the source | instancing is data-driven: any table becomes instances |
| Houdini | any SOP | Copy to Points / Instance | point attributes (`pscale`, `orient`, `Cd`) | deform SOPs before or after copy | "points carry attributes" is the universal instance record |
| Blender Geometry Nodes | mesh | Instance on Points | per-point attributes, random value with seed and ID | Set Position + noise texture on the source or the instances (realised) | explicit realise vs keep-instanced distinction |
| Unreal Niagara / PCG | static mesh | instanced static mesh components from PCG points | point attributes | material WPO (world position offset) in the vertex shader | deformation lives in the material vertex stage, exactly our GPU split |
(Sources: Notch manual "Cloners/Deformers/Effectors" https://manual.notch.one/2026.1/en/docs/nodes/; TouchDesigner "Instancing" https://docs.derivative.ca/Instancing; Houdini "Copy to Points" https://www.sidefx.com/docs/houdini/nodes/sop/copytopoints.html; Blender "Instance on Points" https://docs.blender.org/manual/en/latest/modeling/geometry_nodes/instances/instance_on_points.html; Unreal "Procedural Content Generation" https://dev.epicgames.com/documentation/en-us/unreal-engine/procedural-content-generation-overview — all accessed 2026-09-09.)

The consistent lesson: **instances are records of attributes; deformation is a per-vertex function; fields/effectors are a later, separate layer that modulates records by position.**

## 3. Proposal (answers to the fifteen questions)

1. **Representation in the scene.** `scene::ProceduralGeometry` (a data struct) in `Scene::procedurals`, exactly like `ParticleSystem` in `Scene::particles`: the CPU holds settings and structural outputs; the renderer holds GPU state per object, keyed by name. A composition node kind `procedural` owns one and places it with the node transform; the Lab/showcases are scene files.
2. **Source geometry.** `SourceSpec { kind: Box|Cylinder|Sphere|Torus (later Mesh), parameters }` generated by `makeSourceMesh(spec) -> MeshData` (deterministic, unit-tested), cached by a structural hash; `MeshData` is the existing mesh type, so a source can also be added as a plain entity.
3. **Instances.** `InstanceRecord { position, rotation (quaternion), scale, id, normalizedIndex, random[4], color (rgba multiplier), emissive (multiplier) }` — 96 bytes, `std::vector<InstanceRecord>` on the CPU, a storage buffer on the GPU. Room for custom attributes is the `random`/`color` lanes and a future `extra` block.
4. **Instance transforms.** `Distribution { kind: Single|Linear|Grid|Radial|Spiral, parameters }` evaluates `i -> Transform`, then `Variation { randomPosition, randomRotation, randomScale, uniformScale, seed }` perturbs it with hashed randoms. Order (documented and tested): `world = parent × distributionTransform × placement(i) × variation(i) × sourceTransform`.
5. **CPU work.** Source mesh generation (on source change), instance record generation (on distribution/variation change, ~µs per instance, so also fine when audio modulates a distribution parameter every frame), material variation, bounds. Never per-vertex work per frame.
6. **GPU work.** Instance transform application, the deformer stack, normal recomputation, shading. One instanced draw per procedural object.
7. **Deformation representation.** `Deformer { kind: Bend|Twist|Sine|Noise|Displacement, enabled, amount, space Local|World, speed, phase, axis, center, plus kind fields (frequency, scale, seed, falloff, displacement axis) }`, ordered in `deformers` (max 8). Packed into a uniform array of fixed 64-byte records for the shader.
8. **Stack evaluation.** The vertex shader runs `for d in deformers: p = apply(d, p, t)` in order; Local-space deformers run before the instance transform, World-space ones after; normals by finite differences of the whole stack (three evaluations). The same functions exist on the CPU (`deformPoint`) for tests and for tools that need CPU results.
9. **Parameters.** `registerProceduralParameters(params, rest, prefix)` registers `<prefix>source/<field>`, `<prefix>distribution/<field>`, `<prefix>transform/…`, `<prefix>variation/…`, `<prefix>deform/<n>/<field>` (n = 1-based slot, label carries the kind), `<prefix>material/…`; `applyProceduralParameters` copies finals into the live struct every frame; structural fields (kinds, counts, segments) are integer/enum parameters too, and changing them marks the object dirty.
10. **Modulation targets** resolve by path like everything else; nothing special. `audio.bass → procedural/columns/distribution/radius` is an ordinary route.
11. **Serialisation.** `ProceduralGeometry::toJson/fromJson` inside scene files (composition node) and, because every field is a parameter, in project `parameters` too. The rest values come from the scene file, the finals from parameters/modulation.
12. **Offline.** Deformation time is `RenderTime` (the frame's time), instance randoms are hashed; the render job renders the same project at the same time → identical hashes (tested).
13. **Deterministic randomisation.** `hashInstance(seed, index, channel)` (PCG mix) on the CPU; `pcg3d` value noise on the GPU with time from the frame; no state.
14. **No per-frame CPU mesh regeneration.** The mesh is generated on source-spec change only; the instance buffer on structural change only (dirty flag from a hash of the structural inputs); per-frame changes go through uniforms.
15. **Future generators.** `SourceSpec::kind = Mesh` (any `MeshData`, glTF submeshes), `Distribution::kind = Spline|Surface|Volume`, and a `Field` layer that scales instance attributes by position are additive: new enum values + generator functions, same record and buffer contract. GPU-side instance generation (compute) can replace the CPU generator without changing the vertex shader. Custom WGSL deformers become a `Custom` kind whose function is spliced into the shader by the existing shader-layer generator.

## 4. Shader strategy

One `procedural.wgsl` vertex stage with a fixed deformer loop over a uniform array (kind switch), sharing `pbr_shade.wgsl` with the entity pipeline for the fragment. Chosen over shader variants (combinatorial pipelines) and generated WGSL (no need yet): eight deformer slots with a `kind` switch cost a few ALU per vertex and keep one pipeline per material variant; hot reload keeps working. A generated path for custom deformers can be added later by emitting a function into the same include point.

## 5. Risks
- Normal recomputation by finite differences at silhouette-thin geometry: mitigated by scaling ε with the source bounds.
- 256 object slots: procedurals use one slot each, so hundreds of procedural objects are fine; tens of thousands of *instances* are one slot.
- Alpha-blended instances are unsorted (opaque first in this phase).
