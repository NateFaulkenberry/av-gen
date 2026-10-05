# ADR-1117: A generator is a distribution, and its CPU mirror is the rule

- Status: Accepted (gpu/productionization)
- Builds on ADR-023/025/029 (procedural geometry, effectors, deterministic GPU cull), ADR-035
  (identifier target), ADR-1094 (live levers).
- Evidence: the spike's Phase 3 B (Endless Meadow) and Phase 4 (compact vs expanded vs production at
  250 m-4 km); `docs/research/gpu-world-productionization.md` §Phase 1-3.

## Problem

Production expands every scatter layer at flatten into one 96 B record per instance, holds the records
in CPU and GPU memory, and culls all of them every frame. The spike's Phase 4 measured the same meadow
through production's real path:

| side | flatten | memory | cull | load |
|---|---|---|---|---|
| 250 m | ~0.3 s | 0.2 GB | 0.33 ms | 1.5 s |
| 2 km | ~6.0 s | 4.45 GB | 8.0 ms | 8.0 s |

The same meadow as a 144 B description, generated per frame on the GPU around the camera, cost the same
at 1,000 km as at 250 m. Production had no way to hold content that is a *function*: a world the camera
can travel through forever, or a population whose content changes per frame.

## Decision

1. **`DistributionKind::Generator`** (appended). A procedural object with this kind has no CPU records.
   Every frame a kernel, `shaders/generator.wgsl` `cs_generate`, writes the records of the cells of a
   window around the camera into the object's own record buffer. From there they go through the
   **existing** effector pass and the **existing** prefix-scan cull and LOD, so the ordering is
   deterministic and nothing uses atomics. An empty cell is written as a zero-scale record, which the
   cull pass now rejects from both lists.
2. **One kernel, "cells", and no registry.** A generator has a `name` and `version`, which are validated
   against the one kernel this build has. A registry, with a WGSL entry and a mirror per name, is
   deferred until a second kernel exists ("two use cases before a shared abstraction").
3. **The rule.** A world is a lattice of square cells, with at most one element per cell. Presence,
   jitter, size, yaw, tilt, the random lanes and the colour and emission variation come from hashes
   of the cell's **integer** coordinates and the seed (lowbias32). Presence compares the hash with an
   integer threshold made from the presence and a bilinear, integer cluster fertility. Height comes
   from a two-octave value-noise ground. A region is optional: integer cell bounds, plus an optional
   disc. The camera is brought into generator space; the window is (2⌈W/cell⌉+1)², clamped to the
   region.
4. **The CPU mirror is the rule, not the population** (`scene/generator.hpp`). It offers pure queries,
   one at a time: the element in a cell, the elements in a region (capped), the element a ray hits,
   the nearest element to a point, and how many cells of a window hold an element. It holds no records.
   - Identity is exact: integer arithmetic only. Over 8,844 present cells the GPU and the mirror
     disagreed on 0.
   - Floats agree within 16 ulps of the coordinate.
5. **Structure.** Only the window-sizing fields are structural: cell size, view distance and region.
   They size the record buffer once, and it only ever grows. Every other field is read by the renderer
   every frame, so presence, size, ground, tilt and clustering are parameters that routes and MIDI move
   **without a rebuild** (`distribution/generator/*`, registered for generators only).
6. **Picking without readback.** The identifier target resolves the object as it does today. The click's
   depth-buffer position goes into generator space, and `generatorNearest` names the element. The
   World inspector shows that element's cell, world position, size and random lanes. It also shows the
   derived state: every effector's field as the GPU samples it for that element, through the CPU
   reference.
7. **Live lever.** `drawDistanceScale` (ADR-1094), weighted by importance, shrinks the window, never the
   world.
8. **Refusals, not truncation.** A description whose largest window exceeds 4M cells (384 MB) is refused
   at load, with the numbers. A generator cannot recurse, reference another procedural or run point
   ops, because its records exist only on the GPU.
9. **What does not see it**, said rather than dropped:
   - The CPU path tracer notes "generator distribution: Unsupported" in its capabilities.
   - Navigation, ecology lights, the ADR-056 vegetation simulation and the LOD debug overlay read CPU
     records, so they skip generators.
   - The escape hatch is ADR-1118.

## Consequences

- Measured (`tests/rendering/test_generator_gpu.cpp`):
  - mirror parity cell for cell, bounded and unbounded, at two cameras;
  - the visible count is no larger than the present count, so empty cells are never drawn;
  - a renderer sent straight to (t, camera) draws the same bytes as one that played a path there
    first;
  - the window holds the same number of cells, and the buffer the same bytes, at 0 m, 1 km and 100 km;
  - effectors and audio fields act on generated records.
- f32 world positions quantise with distance: 7.8 mm at 100 km. Camera-relative rendering, which would
  remove this, touches every renderer and is not done. Hashing is integer, so *which* element sits where
  stays exact at any distance.
- `MaterialVariation` hue rotation is not applied to generated elements; value, emissive variation and
  sparsity are. The shader-side chroma drift (ADR-057) and colour fields work.
- Velocity AOV and motion blur of a generated element see object motion only. An element's own motion
  between frames (a window that slides, or a parameter change) is not in the velocity target.

## Rejected alternatives

- **A `GPUWorld` or "generated world" node kind.** Rejected: a generator is a distribution, so it
  inherits material, LOD, shadows, effectors, parameters, the inspector and serialisation for free.
- **Atomic append** (the spike's prototypes). Rejected: order-nondeterministic. The prefix scan already
  ships.
- **A per-instance id in the identifier target.** Rejected: 16 bits cannot address a population, and
  widening the target touches every pass. The mirror answers the question without one.
- **A CPU tile streamer** (the spike's untested alternative). Not built: for content that changes per
  frame (audio-driven layers) it would regenerate the window every frame on the CPU and upload it. The
  GPU kernel costs well under a millisecond (see the regression benchmarks).
