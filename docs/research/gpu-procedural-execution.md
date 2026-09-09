# GPU procedural execution: culling, LOD, indirect draws, hierarchy

Status: research (2026-09-09). Decisions: ADR-025 (GPU fields), ADR-029 (GPU point processing, culling, LOD, hierarchy).

## 1. GPU-driven rendering
- Haar & Aaltonen, "GPU-Driven Rendering Pipelines" (SIGGRAPH 2015, Ubisoft): instance culling in compute (frustum + occlusion), cluster culling, indirect multi-draw from compacted lists — the CPU sends camera and a handful of buffers. (https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf — accessed 2026-09-09.)
- Wihlidal, "Optimizing the Graphics Pipeline with Compute" (GDC 2016, Frostbite): triangle/cluster culling in compute, indirect args.
- Unreal PCG GPU nodes (5.5+): kernels operate on point data on the GPU; docs stress grouping GPU nodes and avoiding CPU readbacks. (https://dev.epicgames.com/documentation/en-us/unreal-engine/pcg-gpu-nodes — accessed 2026-09-09.)

## 2. WebGPU mechanics we already use and will reuse
- Storage buffers, compute dispatch, `drawIndexedIndirect` with args written by compute (particles since 0.5), stable stream compaction with prefix sums (1.0), `instance_index` reading a record buffer (procedural geometry). No `multiDrawIndirect` in core WebGPU (Dawn exposes an extension; not needed: one indirect draw per object/LOD).
- Buffer reuse: persistent per-object buffers grown by capacity doubling; a ring for per-frame uniforms; `writeBuffer` is fine for ≤ 1 MB per frame, larger updates should stay on the GPU.

## 3. Culling and LOD design
- Pass 1 (compute, per object): for each record compute the world bounding sphere (position, scale × source radius), test against the six frustum planes and the distance thresholds, pick a LOD (by projected radius or distance), write a flag + LOD index.
- Pass 2: stable compaction per LOD (reusing the particle scan) → `visibleIndices[LOD]`, `indirect[LOD].instanceCount`.
- Draw: one `drawIndexedIndirect` per LOD mesh with `instance_index` mapped through `visibleIndices`. Records stay on the GPU; the CPU never loops over instances.
- LOD meshes: the same primitive generator at lower segment counts (LOD1), a two-triangle billboard impostor (LOD2), a point sprite (LOD3), invisible (LOD4); thresholds as parameters.

## 4. GPU point processing
The point cloud's record buffer is processed by a compute pass running the effector list (fields via `fields.wgsl`) and the spatial ops that are per-element (transform, noise, randomise, attribute maths); filters produce flags and go through the same compaction. At 1M points this is a few hundred microseconds on M2-class GPUs; the CPU path stays for small clouds, authoring and tests, and both are tested for agreement.

## 5. Hierarchical generation and invalidation
- Generators form a DAG; each node caches its output keyed by a structural hash of its inputs (already the pattern for `ProceduralGeometry::rebuild`). Changing a material invalidates nothing upstream; changing a source invalidates the source mesh only; changing a distribution regenerates records but not the mesh.
- World scale: a *region* node (grid of cells with per-cell seeds) generates children lazily by camera distance (PCG "Hi-Gen"); cells beyond a radius drop their GPU buffers. Implemented later as a distribution kind + LOD gating; the cache/dirty rules are what make it possible.
