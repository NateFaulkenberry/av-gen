# ADR-023: Procedural geometry — sources, instances, distributions, deformers as data

- Status: Accepted (2026-09-09)
- Research: `docs/research/procedural-geometry.md` (audit, instancing, deformation, how Notch,
  TouchDesigner, Houdini, Blender and Unreal separate the stages), `rendering-techniques.md` E.1
  (noise), `particles.md` §3 (instancing vs indirect)

## Problem

The engine can render scenes but not generate worlds: geometry comes from glTF files or three
fixed generators, every object is one draw with its own uniform slot, and nothing deforms per
vertex. The next phase needs monumental, repetitive, continuously deforming architecture that
the existing modulation, timeline, preset, project, composition and offline systems drive
unchanged, and it must not become a pile of special-case scene classes.

## Alternatives considered

1. Special scenes (`TempleScene`, …) with hand-written animation code.
2. A parallel "procedural scene graph" with its own parameters and files.
3. One scene component (`ProceduralGeometry`) = source + distribution + variation + deformer
   stack + material, evaluated by an instanced renderer, parameterised through the existing
   registrar pattern, placed by compositions (chosen).
4. Generating everything on the GPU (compute instance generation, indirect draws) now.

## Decision

- **Data, not classes.** `scene::ProceduralGeometry` holds `SourceSpec` (box, cylinder, sphere,
  torus; mesh later), `Distribution` (single, linear, grid, radial, spiral), `Variation`
  (seeded random position/rotation/scale), transforms, an ordered `Deformer` stack (bend, twist,
  sine, noise, displacement; ≤ 8), a `Material` and material variation. It lives in
  `Scene::procedurals`, serialises with `toJson/fromJson`, and is placed by a `procedural`
  composition node. Showcase scenes are scene files plus presets.
- **Generators versus deformers versus fields.** Generators answer where geometry comes from
  (primitives, distributions); deformers change existing geometry per vertex; fields (later)
  modulate instance attributes by position. Instance transforms and vertex deformation are
  separate mechanisms with separate costs.
- **CPU builds structure, GPU animates.** The CPU generates the source mesh on source change
  and the instance records (`InstanceRecord`: position, rotation, scale, id, normalised index,
  four hashed randoms, colour and emissive multipliers) on structural change; the GPU applies
  instance transforms and the deformer stack in the vertex shader every frame and recomputes
  normals by finite differences of the whole stack. One `DrawIndexed(indexCount, instanceCount)`
  per object; instances come from a storage buffer indexed by `instance_index`.
- **Transform order** (documented, tested): `world = parent × distribution × placement(i) ×
  variation(i) × source`. Local-space deformers run before the instance transform, world-space
  ones after.
- **Parameters** are registered per object under `procedural/<name>/…` (source, distribution,
  transform, variation, `deform/<slot>/…`, material) with the rest/finals pattern used by
  particles; modulation, timeline, presets, projects, OSC and MIDI need nothing new.
- **Determinism**: instance randoms are `hash(seed, index, channel)`; GPU noise is hash-based
  value noise of position and render time; no wall clock, no state.
- **Shader strategy**: one `procedural.wgsl` with a fixed deformer loop over a uniform array
  (kind switch), sharing the PBR fragment with entities via an include. No shader variants, no
  generated WGSL yet; a custom-deformer kind can later splice a user function at one include
  point without changing the pipeline layout.

## Rationale

Every surveyed system converges on "instances are attribute records, deformation is a vertex
function, fields come later"; building that as data on top of the existing scene, parameter and
renderer pieces gives modulation, timeline, presets, projects and offline determinism for free
and makes the acid test pass by construction: a new world is a new scene file. Keeping the CPU
to structural work bounds per-frame cost by instance count (microseconds), not vertex count.
Compute-generated instances and indirect draws add nothing until counts are produced on the
GPU, and would only obscure the first vertical slice.

## Consequences

- Positive: worlds are files; ten thousand instances are one draw; every knob is a modulation
  target; offline renders match live.
- Negative: eight deformer slots; alpha-blended instances unsorted; per-instance material
  variation limited to colour/emissive multipliers; distribution changes re-upload the instance
  buffer (cheap, but still an upload).
- Follow-ups: mesh/glTF sources, spline/surface/volume distributions, fields/effectors, custom
  WGSL deformers, GPU instance generation with indirect draws, fog volumes.
