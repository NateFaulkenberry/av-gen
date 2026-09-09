# ADR-029: GPU point processing, culling, LOD and hierarchical generation

- Status: Accepted (2026-09-09)
- Research: `docs/research/gpu-procedural-execution.md`

## Decision
- Point clouds above a threshold (default 16k) are processed on the GPU: `points.wgsl` runs the object's effector list and per-element spatial ops over the record buffer with `fields.wgsl`; filters set flags and go through the existing stable compaction; the compacted records feed the instanced draw without readback. The CPU path (same operations) remains for small clouds, authoring previews and tests; both are tested for agreement.
- Culling: a compute pass tests each record's bounding sphere against the frustum and distance limits and picks a LOD; per-LOD compaction writes `drawIndexedIndirect` args; the draw reads `instance_index` through the visible list. Enabled per object; off for tiny objects.
- LOD: LOD0 source mesh, LOD1 reduced segments, LOD2 billboard impostor, LOD3 point, LOD4 culled; thresholds by projected radius or distance as parameters.
- Memory: persistent per-object buffers with capacity doubling and reuse; per-frame uniforms through a ring; allocation/used/peak/capacity tracked and shown in the performance UI.
- Hierarchy: a `Region` distribution kind partitions space into cells with per-cell seeds and generates children lazily by camera distance; cells beyond their radius release GPU buffers. Invalidation follows the structural-hash rules of ADR-028.

## Consequences
- Positive: 100k instances and 1M points without CPU loops; consistent CPU/GPU semantics.
- Negative: two implementations of each per-element op; culling adds a compute pass per object; impostors are a later quality step.
