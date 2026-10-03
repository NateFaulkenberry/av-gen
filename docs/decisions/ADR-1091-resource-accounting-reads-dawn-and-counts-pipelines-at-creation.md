# ADR-1091: Resource accounting reads Dawn's own memory figures and counts pipelines at creation

**Status:** Accepted (live optimizer, Stage 1f). **Date:** 2026-10-03

## Context

The profiler must report texture and render-target memory, the largest textures, buffers, and pipeline and
shader-variant counts. `TransientPool` knew only a count; nothing knew bytes. The plan proposed counting bytes at
creation and destruction in the gpu layer, but there are about 150 texture/buffer creation sites in this repository.

## Decision

- **Memory comes from Dawn,** which already tracks every allocation: `dawn::native::ComputeEstimatedMemoryUsageInfo`
  (textures, depth-stencil, buffers, total) and `DumpMemoryStatistics` (per object: label, size, format, usage) for
  the largest-N list and the render-attachment total. Read once after measuring, so it costs the frame nothing.
  Reported as "Dawn's estimate". `gpu/resource_stats.cpp` is compiled with `-fno-rtti` because the prebuilt Dawn has
  no RTTI and the dump interface is subclassed.
- **Pipelines and shader modules are counted** where they are created: every `CreateRenderPipeline` /
  `CreateComputePipeline` in the renderer goes through `gpu::createRenderPipeline` / `createComputePipeline`, and
  `ShaderLibrary` counts modules. Counts are "created since start"; the profiler reports their delta across the
  measured window as compiles during measurement (a touring frame needs 0). SDF compiled variants are counted by
  `SdfRenderer::compiledVariantCount`, material programs and their ops from the scene.
- New per-frame counters: particle simulation steps, post-chain size, shadow casters below the floor, shadow pass
  skipped.

## Consequences

A count of bytes that already existed is read rather than duplicated. Particle *alive* counts stay on the GPU and are
not read back; the record says capacity, not population.
