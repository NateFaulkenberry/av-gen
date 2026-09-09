# Procedural world systems: how the established tools are structured

Status: research for the procedural-world phase (2026-09-09). Decisions: ADR-024 … ADR-031.

## 1. Audit of av-gen before this phase

| Layer | What exists | What this phase adds |
|---|---|---|
| Scene | `Scene` with meshes, entities, lights, particles, `procedurals` (ADR-023), environment, post | `fields`, `splines`, `sdfs`, atmosphere parameters, procedural material programs |
| Procedural | `ProceduralGeometry`: primitive source → distribution → seeded variation → deformer stack → instance records (96 B) → one instanced draw; parameters via register/apply | records become a projection of a typed **point cloud**; spatial operators and **effectors** (fields) between distribution and instancing; spline distributions; hierarchical sources (a procedural object as the source of another); recursion and grammars; GPU point processing, culling and LOD |
| Particles | compute pool, deterministic compaction, curl noise, attractor | field forces sampled in the simulate pass; particles as an instance source |
| Materials | `Material` struct + textures, shared `pbr_shade.wgsl` | an interpreted procedural material program with position/normal/attribute/field/audio inputs |
| Parameters / modulation / timeline / presets / projects / control | authoritative, unchanged | every new object registers parameters through the same registrar pattern; states and world macros reuse presets, cues and macro sources |
| Renderer | HDR lit pass, post chain, output mapper, offline job, determinism hashing | SDF raymarch pass writing depth, volumetric fog pass, debug-draw layer, GPU culling/LOD passes |
| Tests | Catch2 unit + GPU readback, showcase hash regression | the same split for every new module; showcase scenes are integration tests |

The extension boundary that already works and must be kept: **data structs + pure generators + a registrar + a renderer that consumes the struct**. Every new system below follows it.

## 2. Systems surveyed

### Unreal Engine PCG (5.4–5.6)
- Points are *spatial data*: transform, bounds min/max, density, steepness, seed, colour and arbitrary metadata attributes on a per-data "metadata" table; graphs move `PCGData` (point, spline, surface, volume, spatial, param) between nodes; subgraphs expose parameters; generation is partitioned into grid cells with hierarchical generation ("Hi-Gen") so world-scale graphs only regenerate the cells and levels affected; GPU nodes (5.5+) group work into compute kernels and warn against CPU↔GPU round trips. (Sources: https://dev.epicgames.com/documentation/en-us/unreal-engine/procedural-content-generation-overview ; …/pcg-hierarchical-generation ; …/pcg-gpu-nodes — accessed 2026-09-09.)
- Take-aways: attributes-as-columns with a small fixed core; density as a first-class attribute used by filters; deterministic seeds per point; hierarchy and partitioning as the scalability mechanism; explicit invalidation.

### Notch
- Node hierarchy of Geometry, Cloners, Deformers, Effectors, Fields, Particles, Materials; **Fields** carry voxel colour and motion vectors and expose *affectors* (primitive, turbulence, curl noise, vortex, point, procedural, mesh); particles read fields; procedural (SDF) geometry combines primitives with CSG and displacement and can take particles and meshes as inputs. (Sources: https://manual.notch.one/2026.1/en/docs/nodes/fields/ ; …/nodes/procedurals/ ; …/nodes/particles/ — accessed 2026-09-09.)
- Take-aways: fields as a general spatial control signal (particles, deformers, materials all sample them); effectors = field + operation; SDF procedurals with CSG and displacement as a peer of meshes.

### Houdini
- Everything is geometry with attributes on points/vertices/primitives/detail; SOP networks are pure functions with dependency-driven cooking (dirty propagation per node); Copy to Points reads `pscale`, `orient`, `Cd`; VEX/VOPs = per-element programs; Solver SOP for simulation over frames. (Sources: https://www.sidefx.com/docs/houdini/model/attributes.html ; …/nodes/sop/copytopoints.html ; …/nodes/sop/solver.html — accessed 2026-09-09.)
- Take-aways: attribute conventions (`P`, `N`, `pscale`, `orient`, `Cd`, `id`) that downstream nodes agree on; cooking = dirty propagation over a DAG; generators are pure.

### Blender Geometry Nodes
- Typed sockets (float, vector, colour, geometry, …), fields as *functions of context evaluated per element*, instances kept as instances until "Realize Instances", node groups with exposed inputs. (Source: https://docs.blender.org/manual/en/latest/modeling/geometry_nodes/index.html — accessed 2026-09-09.)
- Take-aways: keep instances unrealised as long as possible; typed sockets make invalid links obvious; node groups = subgraphs with parameters.

### TouchDesigner
- Operators by family (SOP/CHOP/TOP/…), instancing from CHOP/DAT tables (any table = instances), cook-on-demand dependency graph. (Source: https://docs.derivative.ca/Instancing — accessed 2026-09-09.)

### Processing / openFrameworks / three.js
- Immediate-mode drawing and per-frame CPU loops; the opposite of what we want for 100k+ elements but the right *mental model* for authoring: a small number of powerful primitives.

## 3. Cross-cutting conclusions

1. **Points with typed attributes are the universal currency.** Every system passes points around; av-gen's instance records become a fixed projection of a point cloud.
2. **Fields are spatial control signals**, not effects; effectors are field × operation on data; particles, deformers and materials all sample the same fields.
3. **Generators are pure functions of parameters and seeds**; invalidation is per node, downstream only.
4. **Keep instances as instances**; realise only for meshing/SDF.
5. **GPU work is grouped**: small parameter buffers in, large buffers processed on the GPU, indirect draws out.
6. **Hierarchy and partitioning** are how world-scale content stays cheap.
7. **Subgraphs with exposed parameters** are how authored structures become reusable assets.

## 4. Mapping to av-gen (the shape of this phase)

```
Source (primitive | mesh | procedural | particles | sdf)
  ↓
PointCloud (distribution | spline | grammar | recursion)       [spatial/point_cloud]
  ↓
Point operators + Effectors(field) → attributes                 [spatial/ops, spatial/effector]
  ↓
Instance records (projection) → GPU (points.wgsl: fields, cull, LOD)
  ↓
Vertex deformers (+ field displacement, path deform)            [procedural.wgsl]
  ↓
Material program (position/normal/attribute/field/audio inputs) [material.wgsl]
  ↓
Lit pass + SDF raymarch + particles (field forces) + volumetric fog + post
```
Parameters, modulation, timeline, presets, states, macros, OSC/MIDI and offline rendering are unchanged layers on top of the same data.
