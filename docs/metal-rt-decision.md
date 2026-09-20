# Metal ray tracing on this machine: the measurement, and what it does and does not settle

Status: **measured, contended, and NOT a recommendation to build a backend yet**
Date: 2026-09-19 · Branch `agent/mbackend2` · Probe: `tools/metal_rt_probe.mm` (temporary)
Relates to: `docs/offline-backend-audit.md` §5 and W6, ADR-351 (the Embree tracer), ADR-170
(one GPU, one run; minima over repeats)

## The question

The brief treats "implement a Metal ray-tracing backend" as settled. The audit did not, for one
measured reason: **this machine has no hardware ray-tracing unit.**

```
device: Apple M2 Max
supportsRaytracing: 1        MTLGPUFamilyApple7: 1    Apple8: 1    Apple9: 0
```

Apple's RT hardware arrives with `MTLGPUFamilyApple9` — M3 and later. On an M2 the API works and
`intersect()` runs on the shader cores: compute against a GPU-built BVH, not dedicated silicon. So
the question is not "does Metal ray tracing work" (it does) but "is it worth weeks of backend on
*this* hardware", and that is answerable in a day.

## What was built

`tools/metal_rt_probe.mm`, a temporary tool that takes **AV Gen's own geometry** — the same
`pathtrace::Snapshot` the Embree backend consumes — builds a Metal primitive acceleration structure
over it, casts the rays `pathtrace::cameraBasis`/`generateRay` generate, shades `|N·L|` and writes
a scene-linear EXR. It does not shade materials, sample lights, bounce, or handle instancing:
those are the backend, and this is the decision about whether to write one.

## The numbers

`examples/treeisland/tree-of-life-floating-island.json`, 1920x1080, minima over 5 repeats.

**Every number here was taken on a machine at load 5.2 to 68.3 with two other agents rendering.**
They are therefore an *order of magnitude*, not a benchmark, and the probe prints the load itself so
a contended run cannot be quoted as a clean one.

| | Metal | Embree | |
|---|---|---|---|
| geometry | 3,102,341 triangles | 3,102,349 triangles | the same snapshot |
| acceleration structure build | **136 ms** | **1,669 ms** | **12.3x in Metal's favour** |
| AS memory | 546 MB (+600 MB scratch) | not reported | of a 55.7 GB working set |
| trace | 1.5 ms — 1,365 Mrays/s | 58,890 ms | **NOT COMPARABLE — see below** |
| coverage | 19.5% of pixels hit | — | the control: it did not measure an empty scene |

## What this settles, and what it does not

**Settled: Metal ray tracing works on real AV Gen geometry.** Three million triangles of the Tree of
Life go into a primitive acceleration structure, the camera rays agree with the tracer's own
generator closely enough to hit the subject, and an image comes out. 19.5% coverage is the control —
a probe that measured 0% would have been timing the cost of missing, and it says so and exits 7 if
that happens.

**Settled, and the most useful thing here: the acceleration-structure build is about twelve times
faster on the GPU.** 136 ms against 1,669 ms over the same triangles. That is the number that
matters for an *animated* sequence, where the BVH is rebuilt every frame: at Embree's rate a
240-frame path-traced shot spends **6.7 minutes building BVHs alone**, before a single ray. At
Metal's it spends 33 seconds. `docs/offline-backend-audit.md` lists "BVH reuse across frames" as
known remaining path-tracer work; this says how much that work is worth, and that a GPU build is
another way to buy the same thing.

**NOT settled: whether Metal traces faster.** The two trace numbers measure different work and
comparing them would be dishonest. Embree's 58.9 s is a full shaded sample — lights, textures, MIS,
the glTF BRDF — over geometry that includes 4,492 instances. The probe's 1.5 ms is primary
visibility with a dot product over geometry that skips those instances entirely. The ratio between
them is meaningless and is not quoted as a speedup anywhere in this document.

**NOT settled: instancing.** The probe skips it and says so per run. Glowmere's scatter is 6.8M
triangles stored as 321K plus 67,770 instances (ADR-351 Phase 8); an instance acceleration structure
is real work and is the first thing a backend would need.

## Recommendation

**Do not start the backend on these numbers.** The one comparable measurement favours Metal by 12x
on a stage that is currently 3% of a 70-second frame, and the stage that is the other 97% has not
been compared at all.

The next step is one more day, not one more month: extend the probe to do **matched work** — the
same shading model on both sides, over the same geometry including instances — and run it on a
**quiet machine**. That is a real answer. If Metal then wins by a large factor on tracing too, the
backend is justified and the seam is already identified (`pathtrace::EmbreeScene` is the only file
that includes Embree, behind a pimpl, and answers exactly two questions). If it does not, the right
work is BVH reuse across frames in the Embree backend, which the audit already lists and which the
136 ms number says is worth a lot.

One thing that is settled and is worth carrying forward either way: **a Metal backend needs no new
dependency and no new build machinery.** `enable_language(OBJC OBJCXX)` is already on, `-framework
Metal` is already linked, and `src/share/syphon_share.mm` already does Dawn↔Metal interop in
production through `SharedTextureMemoryIOSurface` + `SharedFenceMTLSharedEvent`. The probe took a
day because the question was hard, not because the plumbing was.
