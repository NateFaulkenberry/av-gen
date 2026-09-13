# ADR-114: Cluster occupancy is measured before the cap, not after it

Status: accepted
Date: 2026-09-13

## Context

The clustered-forward path ([ADR-033](ADR-033-lighting-architecture.md)) divides the
view into a 16×8×24 froxel grid — 3072 clusters — and gives each one an index list holding at most
`kMaxLightsPerCluster` = 32 lights, out of `kMaxSceneLights` = 256 in the scene.

The audit's limit inventory listed the 32-light cap as a "quality setting" whose replacement need
was **"measure first"**, and the measurement-capability audit recorded cluster occupancy as
"⚠️ cluster buffer readable; no occupancy statistic". Nobody could say whether the cap was ever
reached, whether lights were being silently dropped, or whether the grid was mostly empty and the
clustered path's cost was somewhere else entirely.

The brief is explicit that the 32 cap must not be raised until someone knows whether it binds.

## Decision

`rendering::clusterOccupancy` (light_data.hpp) reports, for one frame's grid: lights per cluster
min / mean / p50 / p90 / p99 / max, how many clusters are empty, how many overflowed, and how many
light-to-cluster assignments the cap dropped.

Three choices in that, each load-bearing:

**Every count is the uncapped demand.** A statistic gathered from the capped index lists saturates
at 32 and therefore can never show that 32 is the problem — a grid where every cluster wants 200
lights and a grid where every cluster wants exactly 32 produce identical numbers. `max` is
therefore how many lights actually *reach* the busiest cluster, and `dropped` is the difference the
cap makes. A unit test pins this by measuring one scene under two different caps and requiring the
demand statistics not to move.

**It is computed on the CPU, from the same inputs the compute pass is given.** The grid is built by
`shaders/clusters.wgsl`, and reading its buffer back would stall the one frame a performance run
must not stall. So this replays the pass on the CPU using `assignClusters` — the reference
implementation that `tests/rendering/test_shadows_gpu.cpp` already checks the GPU's own cluster
buffer against, index for index. It is a replica of the pass, not an approximation of it, and a
unit test requires the two to agree on demand, emptiness and the busiest cluster.

**It is off by default** (`--cluster-stats`). It is `3072 × lights` sphere-against-box tests on the
submitting thread, inside the measured frames. A diagnostic that silently taxes the measurement is
worse than no diagnostic, so the record it writes carries
`conditions.clusterStatsPerturbsWallClock: true` and the GPU numbers — which are unaffected — are
the ones to read from such a run. Measured on Glowmere: wall median 23.29 ms with it against
22.20 ms without, GPU unchanged at 18.48 ms.

## The measurement it was built to make

**Glowmere** (`examples/world/glowmere-stylized.scene.json`, 1280×800, 120 frames):

| | |
|---|---|
| froxels | 3072 |
| local lights offered to the grid | 222 |
| cap | 32 |
| min / p50 / p90 / p99 / max lights per cluster | 0 / 0 / 14 / 24 / **29** |
| mean | 5.70 |
| empty clusters | 1720 (**56.0%**) |
| overflowed clusters | **0 (0.00%)** |
| lights dropped by the cap | **0** |

**Constellation** (`examples/constellation/constellation.json`, same settings): **0 local lights.**
Every one of the 3072 clusters is empty, and the cluster compute pass runs — and reports 0.00 ms —
over an entirely empty grid.

## Consequences

**Light overflow is not happening, on either canonical scene, and the 32 cap must not be raised.**
Glowmere's busiest froxel wants 29 of a possible 32. Not one light is dropped anywhere in the grid.
Raising the cap would enlarge the cluster buffer — `3072 × (1 + cap)` words — and change nothing
about what is drawn.

**The margin is three lights, and that is worth knowing too.** 29 of 32 is not comfortable. A
denser lighting pass on this scene, or a camera that puts more of the valley's emissives into one
froxel, would start dropping lights — silently, because nothing in the engine reports an overflow at
runtime. This instrumentation is now the thing that would catch it, and it should be run again after
any change that adds local lights to Glowmere.

**56% of the grid is empty, which bounds what the clustered path can be costing.** More than half
of the froxels pay nothing per-light. The `clusters` pass itself measures 0.07 ms of Glowmere's
18.5 ms frame. Clustered lighting was named in the brief as "a large uninstrumented cost"; it is now
instrumented, and it is not large. The scene pass's 15.4 ms is not the froxel grid's doing, which is
consistent with what the audit's resolution sweep and fragment-cost experiment already concluded.

**Constellation does not use clustered lighting at all.** Its cost is volumetrics (64%) and its
grid is empty. Nothing the froxel grid does can help it, and the two scenes need separate budgets —
which is the same conclusion the audit reached from the pass split, now confirmed from the other
direction.

**A counter was found wrong while this was being recorded.** `RenderStats::lights` counts slots in
the 8-long *uniform fallback* array, so Glowmere — which shades with 230 lights — reported `8`.
That field is left in place for its existing reader and documented for what it is;
`shadedLights` and `directionalLights` now answer the question anyone reading "lights" was actually
asking.

## Alternatives considered

**Read the cluster buffer back from the GPU.** Correct in principle and the direct measurement, but
it stalls the frame being measured, which makes it useless in exactly the run where occupancy
matters. The CPU replica is already verified against the GPU's output by an existing test, so the
readback would be measuring the same thing at a much higher price.

**Report occupancy from the capped lists and infer overflow from lists of length 32.** Rejected: it
gives a boolean where the question needs a magnitude. "Some cluster is full" does not distinguish a
scene one light over the cap from one twenty over it, and the decision about whether to replace the
cap depends on exactly that.

**Average occupancy over the measured frames.** Rejected: occupancy is a property of one camera
position, and a mean over a moving camera describes no camera. The last measured frame's grid is
reported, and the camera pose that produced it is in the record's conditions.
