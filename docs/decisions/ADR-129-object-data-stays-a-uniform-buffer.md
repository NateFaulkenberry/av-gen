# ADR-129: Object data stays a uniform buffer until multi-draw indirect exists

**Status:** Accepted
**Date:** 2026-09-13

## Problem

`docs/renderer-upgrade/04-target-architecture.md` names the Phase E replacement explicitly:

> Replace the 256 × 512-byte dynamic-offset uniform layout with a storage buffer indexed by
> instance (core WebGPU; 8 storage buffers available in the vertex stage).

ADR-128 lifted the cap without doing that. This records why, because the difference is not laziness
and the conditions that would reverse it are worth writing down.

## What the storage-buffer path actually costs

The direction is sound in the abstract: a `storage` array of `ObjectUniforms` read at
`@builtin(instance_index)`, with `firstInstance` carrying the object index on each draw, has no
compile-time object count anywhere. What it costs in *this* codebase is not one declaration.

**The fragment stage reads `object`.** `pbr_shade.wgsl` alone takes 20 fields off it — `ids.x` for
the pick index, `ids.y` for the material id, `flags` for the alpha mode and the texture mask —
and `procedural.wgsl` another 20. `@builtin(instance_index)` does not exist in the fragment stage.
So every vertex→fragment interface in the object-reading shaders (`pbr`, `pbr_shade`,
`pbr_skinned`, `procedural`, `water`, `grid`, `gtao`, `sdf_raymarch`, plus `common.wgsl`'s shared
`VertexOutput`) would gain a `@interpolate(flat)` index, threaded through by hand, in nine WGSL
files — in a wave where another agent owns `shaders/`.

**And the payoff is not there yet.** Without the bindless and multi-draw-indirect facilities the
rewrite is designed to feed, the per-draw encode does not collapse: the material bind group and the
draw call stay, one per object. All the storage buffer removes is the group-1 bind and its dynamic
offset. `docs/renderer-upgrade/04-target-architecture.md` already defers GPU-driven submission on
exactly this ground — *"CPU encode time exceeds ~10% of frame, **and** Dawn-on-Metal ships
multi-draw indirect"* — and Dawn-on-Metal does not.

## The measurement that settles it

`glowmere-extreme` (840 entities, 315 draws — the heaviest object-count content in the repo), 90
frames at 1280×720, headless, M2 Max / Metal / Release, 78 measured frames after 12 warm-up:

| | ms |
|---|---|
| GPU frame p50 | **18.94** |
| Wall p50 | 25.53 |
| CPU `sceneEncode` (the lit pass) | 0.159 |
| CPU `depthEncode` (prepass + linear depth + AO) | 0.168 |
| CPU `objects` (traversal, uniforms, caster selection) | 0.246 |
| CPU `queueWait` (blocking on the GPU) | 20.07 |

The entire CPU encode of the two passes that bind object slots is **0.33 ms**, and the storage
buffer removes some fraction of that — not all of it. Against an 18.94 ms GPU frame that is an
upper bound of **1.7%**, and against the 25.53 ms wall p50, **1.3%**. Wave 2's own measurement rule
says below 2% GPU / 4% wall is not a result. The frame is not CPU-bound in the first place: 20.07 ms
of the 23.90 ms CPU frame is the offline path blocking on the queue.

So the storage-buffer rewrite would touch nine shaders and every VS→FS interface in them, to buy a
change that is under the threshold at which this project agrees to call something a change.

## Decision

Object data stays a uniform buffer of 512-byte dynamic-offset slots. ADR-128's growth policy removes
the cap without touching the ABI, so the two questions — *how many objects can we address* and *how
cheaply can we submit them* — are separated. The first is now answered. The second is deferred.

## What would reverse this

Any one of:

1. **Dawn-on-Metal ships multi-draw indirect.** Then the draw loop itself collapses and the storage
   buffer is its prerequisite rather than a marginal saving. (It is commented out in Dawn today.)
2. **CPU encode exceeds ~10% of the frame** on a canonical scene — the condition the target
   architecture already wrote down. At 0.33 ms of 18.94 ms it is at 1.7%.
3. **A scene needs more than one object record per draw** — instanced authored entities, or
   per-draw data that will not fit 512 bytes. The uniform slot is a fixed-width record; the storage
   buffer is not.

None of these is close, and the first is not ours to schedule.

## What was *not* rejected

The 64-bit-atomics wall (`docs/renderer-upgrade/04-target-architecture.md`'s rejection of Nanite and
virtual shadow maps) is not what blocks this — a storage buffer of object records needs no atomics.
This is a cost/benefit call against a measured frame, not a capability wall, and it should be
revisited when the numbers move rather than treated as settled.
