# ADR-355: Five frames a second was a bounding box, rescanned forty million times

Status: accepted
Date: 2026-09-19
Follows ADR-348 (runtime LOD for imported assets), which was built to make this scene faster and
was not what was making it slow.

## Context

The owner reported **~5 fps in both Tree of Life scenes**. I had measured the floating island at
11.86 ms and reported a 41.8% GPU saving from runtime LOD. Both statements were true and neither
was the frame time.

Two things had to be separated before anything could be fixed:

* **LOD was switched off in both shipped scenes.** Neither scene file carried a `lod` block. Every
  measurement I quoted had been taken on my own arms in `examples/assetlod/`, which did. The system
  was built, tested, measured and unreachable — the fourth thing in one day with that shape, after
  the ocean world missing from the examples index, the day/night cycle registering no parameters,
  and the path tracer with no caller outside its own tests.
* **The frame was not GPU-bound and LOD could not have fixed it.** 11.86 ms of GPU against a
  reported 200 ms frame is a 17× gap that the tree's triangle count does not reach.

## What was actually wrong

`MeshData::bounds()` scans every vertex. `Scene::meshBounds()` caches the answer against
`meshVersion` and says so in its own comment: *"MeshData::bounds() scans every vertex... so the
answer is cached against the version that already says when meshes changed."*

**Three call sites had the Scene in hand and called the uncached one**, per entity, per frame:

| site | calls per entity per frame |
|---|---|
| `scene::entityCullBounds` — via `Composition::cullEntityNodes` | 1 |
| `scene::entityCullBounds` — via `SceneRenderer`'s diagnostic bounds | 2 |
| `rendering::buildDebugGeometry` | 2 |

The Tree of Life's 45 entities carry **39.9 million vertices** between them. So each frame rescanned
that geometry five times over to recompute numbers that had not changed since the asset loaded.

The amplification is ADR-348's other finding: those 39.9M vertices are **4.4M distinct ones**,
duplicated ninefold because a multi-material glTF shares one POSITION accessor between its
primitives and the importer copies it per primitive. That turned a merely wasteful loop into a
250 ms one.

## Decision

Two one-line substitutions: `mesh.bounds()` → `scene.meshBounds(entity.mesh)` in
`scene::entityCullBounds`, and the same at both sites in `rendering::debug_visualizer.cpp`.

`Scene::meshBoundsRebuilds()` is added so this is testable — the same shape
`MeshMetricsCache::rebuilds()` already has, and for the reason its comment already gives.

## Consequences

**Live editor**, which is where the owner is and where I had never measured. 150 frames, the
owner's actual canvas read from the `canvas:` log line rather than assumed — **3408×1786 px,
6.09 Mpx, backing scale 2.00** inside a 5120×2754 window — load average 2.69, medians in ms:

| state | FRAME | engine.update | debug.geometry | render.record | gpu |
|---|---:|---:|---:|---:|---:|
| A — neither fix (as shipped) | **350.33** | 87.25 | 86.69 | 175.65 | 24.58 |
| B — cull bounds cached | 90.52 | 0.06 | 87.78 | 2.11 | — |
| C — both cached | **8.64** | 0.08 | 0.003 | 3.48 | 13.89 |

**350 ms to 8.6 ms. Forty times.**

The attribution is not what finding the first site suggested. That one line was worth **261 ms**,
not the 87 ms I first attributed to it: 87 out of `engine.update`, where I found it, and another 174
out of `render.record`, because the renderer calls `entityCullBounds` twice per entity for its
diagnostic bounds. I only know that because the editor profiler breaks the frame into phases and the
headless one does not.

**Headless**, 1920×1080, tier high, three interleaved repeats, minima, both scenes:

| scene | | CPU p50 | GPU | wall |
|---|---|---:|---:|---:|
| floating island | before | 201.96 | 18.74 | 382 |
| | + bounds cache | 24.77 | 18.87 | 115 |
| | + LOD @ 16 px | 14.38 | 10.16 | 103 |
| ocean world | before | 203.53 | 19.73 | 381 |
| | + bounds cache | 24.70 | 19.53 | 114 |
| | + LOD @ 16 px | 18.54 | 14.16 | 107 |

Both scenes, near-identical before and after, which is the point: the cost was proportional to total
vertices and both scenes contain the same tree. The ocean world's water, day/night cycle, HDRI swap
and 256 chunks are not involved — the owner deduced that before I measured it, from the fact that
the floating island has none of them and is equally slow.

**LOD is a separate, smaller, real saving.** At 6.09 Mpx with both fixes the GPU frame is 13.89 ms
without LOD and 11.01 ms with it. The CPU frame is 8.6 ms either way, so at this canvas on this
display LOD buys **headroom, not frames**. It should not be credited with the 40×, and it should not
be blamed for having existed while the frame was slow.

**The floor moves from 8 px to 16 px on this asset**, against the owner's "the LOD for the hero tree
needs to be much lower". Rendered at the hero camera: 8 px draws 32.4% of the source, 16 px draws
16.8% and still reads as the same tree — full canopy, intact silhouette, slightly larger leaves. 24
px (10.7%) and 32 px (9.2%) show sky through the canopy and are past the line. `maxScreenError` is a
per-node override and the global default stays 8.

## What this says about the process

**The headless path printed no CPU breakdown, and that is why this survived for months.** `probe2`
has recorded these stage timings since the ui-responsiveness work and `--profile-cpu` prints them —
from the live editor only. Every diagnosis made from a headless run has been blind to them,
including all of mine. The headless statistics block now prints `cpu(update)` with the same stages
and the upload pass counts beside their times.

**I reported a GPU number for a CPU-bound frame and set the CPU column aside** as "a fixed cost of
the headless path and not a property of any scene". It was 90 ms, it was a property of the scene,
and it was the entire complaint. A frame time has at least three numbers in it and quoting the
healthy one is not a measurement.

## Revisit triggers

* Any new caller of `MeshData::bounds()` that has a `Scene` in scope. Three of them have now made
  this mistake; the fourth will not be caught by a test that only covers the three.
* The importer learning to share a vertex buffer between primitives of one glTF, which would make
  every remaining uncached scan nine times cheaper and is the real fix for the class.
* A scene with enough *distinct* geometry that even the cached path costs something.
