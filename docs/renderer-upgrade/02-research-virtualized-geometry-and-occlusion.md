# Deliverable 2a — Research: virtualized geometry, hierarchical occlusion, virtual shadow maps

Researched against primary sources. Every engine claim carries a citation. Where a source does not
state something it is marked as such rather than inferred. **Conclusions are then tested against
this engine's own measurements (Deliverable 4), which is where several of them invert.**

## A sourcing correction worth recording

Epic's [Nanite Technical Details](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-technical-details)
page — one of the two the brief nominates — **does not describe Nanite's architecture.** The terms
*cluster hierarchy*, *software rasterizer*, *visibility buffer*, *64-bit atomics*, *screen-space
error* and *mesh shaders* do not appear on it; it covers vertex precision, fallback meshes,
visualization modes and console variables. The authoritative Epic-authored primary source is the
SIGGRAPH 2021 Advances talk
[*Nanite — A Deep Dive* (Karis, Stubbe, Wihlidal)](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf),
and all architecture claims below are slide-cited to it. Its 2021 support matrix **contradicts** the
UE 5.8 docs on masked materials, skeletal meshes, foliage and tessellation; where they conflict the
docs are current and the talk is historical. VSM citations render only at `?application_version=5.4`.

## The four-way taxonomy, because conflating these causes bad decisions

| | Unit of decision | What it buys | What it does not |
|---|---|---|---|
| **Whole-mesh LOD** | the mesh | cheap, universal | pops; no intra-object adaptivity |
| **Meshlet / cluster geometry** | a ~128-triangle cluster | fine-grained culling and dispatch | **nothing about LOD** |
| **Virtualized geometry (Nanite)** | a cluster *group*, via a DAG | continuous sub-object LOD + paged residency | instance counts; aggregates |
| **HLOD** | a *group of objects* | draw-call and instance-count collapse | triangle density within an object |
| **Impostors** | the object, replaced by a billboard/atlas | the far field, where even a DAG bottoms out | anything near |

A meshlet renderer with one LOD is **not** virtualized geometry. And Nanite still needs impostors:
the hierarchy "ends at 1 root cluster of 128 triangles. At that point cost stops scaling with
resolution. It stops scaling all together" (slide 95), answered with 12×12 octahedral
depth+triangle-ID impostors at 40.5 KB per mesh (slide 97).

**The load-bearing consequence:** virtualized geometry solves *triangles*, not *instances*. Karis:
"Joke around the office is instances are the new triangles" (slide 95).

## Nanite — the parts that matter here

**Cluster size is exactly 128 triangles** (slide 45), built by METIS graph partitioning of the mesh
dual (slides 48–51). The DAG is built by *merge → simplify to 50% → re-split*, which is what keeps
boundary edges from locking permanently: "you can't draw a line from LOD0 all the way to the root
without crossing an edge" (slide 47).

**LOD selection is purely local**, which is the whole trick: "Draw a cluster when: Parent error is
too high && Our error is small enough… This is entirely local, does not depend on the entire path to
this node, and thus can be evaluated in parallel" (slide 65). This is only valid because parent error
*and* parent bounds are forced ≥ their children's at build time (slide 66) — skip that and you get
cracks and double-drawn geometry, silently.

**Seamlessness is contingent on temporal AA:** "if we only draw clusters that are less than 1 pixel
of error they are imperceptibly different and temporal antialiasing smoothes out any change… It does
our work for us" (slide 67). There is no geomorph and no cross-fade. **No TAA means popping.**

**The software rasterizer exists because hardware cannot do sub-pixel triangles:** "Modern GPUs setup
4 tris/clock max and outputting primitive ID needed for vis buffer makes this even worse. Primitive
shaders or mesh shaders can be faster but are still bottlenecked and not designed for this"
(slide 80). It is "3x faster than hardware on average" (slide 81), and the crossover is much larger
than folklore suggests: "We software rasterize any clusters whose triangles are **less than 32 pixels
long**" (slide 88). In the shipped demo it handled 96.5% of visible clusters (slide 109).

## The capability wall — and it is absolute

Nanite's software rasterizer depends on a 64-bit `InterlockedMax` into the visibility buffer, packing
30 bits of depth over a 34-bit payload. Karis states the dependency plainly: "The payload needs to be
small enough to pack in 34 bits or less. **Without that we wouldn't be able to do fast software
rasterization**" (slide 84). The *hardware* path writes through the same atomics so the two can
overlap (slide 87) — **there is no atomics-free variant.**

WebGPU fails this on two independent axes, verified against the specification:

1. **No 64-bit atomics.** WGSL: "An atomic type is parameterized by a scalar type that is either
   **i32 or u32**", and there is no 64-bit integer type in the language at all
   ([WGSL](https://www.w3.org/TR/WGSL/)).
2. **No texture atomics of any width.** WGSL atomics "may only be instantiated in the **workgroup and
   storage address spaces**" (ibid.) — buffer-only.

Neither appears anywhere in the complete `GPUFeatureName` enumeration
([WebGPU spec](https://gpuweb.github.io/gpuweb/)). Mesh shaders, task shaders, primitive shaders,
bindless, multi-draw indirect and indirect count buffers are likewise **not optional features that
happen to be unsupported — they are not concepts the specification has.**

Apple8 (M2) hardware reportedly exposes 64-bit atomic min/max in Metal, corroborated indirectly by
Epic listing "Apple Silicon: M2 or newer" among VSM's requirements
([VSM docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine?application_version=5.4)).
**This does not help.** The hardware may have the instruction; WebGPU has no way to express it, and
reaching it means forking Dawn and leaving the standard.

**Verdict: Nanite's rasterizer cannot be ported. Nanite's DAG/LOD machinery can.** They are separable,
and that separation is the single most useful outcome of this research.

## The most relevant negative result for *this* engine

Epic's own 2021 assessment: Nanite is "**Not great with aggregates** / Many tiny things becoming a
porous volume / **Grass, leaves, hair**" (slide 146), with overdraw named as the cause — "Riddled
with holes sounds like a content problem but actually describes most aggregate geometry cases like
leaves and grass. Overdraw is one of many reasons Nanite doesn't perform as well with those"
(slide 93) — and the resolution-scaling guarantee explicitly breaking down: "The property of scaling
cost with screen resolution doesn't hold with aggregates such as grass and leaves" (slide 146).

UE 5.8's "Nanite Foliage" is a *different system* combining "instancing, skinned meshes,
voxelization, animation, and material characteristics"
([Nanite overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine)),
not an improvement to the clustering scheme.

**Glowmere is aggregate geometry.** Dense ecology, leaves, scattered plants. Adopting virtualized
geometry here would be adopting it exactly where its authors say it works worst.

## Hierarchical occlusion culling — the only technique with no capability blockers

Needs compute shaders, storage buffers, `u32` atomics and indirect dispatch/draw. All core WebGPU
([indirect draw parameters](https://gpuweb.github.io/gpuweb/#indirect-draw-parameters)).

**Epic orders culling by cost, deliberately putting occlusion last:** "Distance, View Frustum,
Precomputed Visibility, Dynamic Occlusion"
([UE occlusion](https://dev.epicgames.com/documentation/en-us/unreal-engine/visibility-and-occlusion-culling-in-unreal-engine)).

**Two-pass occlusion** avoids tracking a previous visible set — "Explicitly tracking previously
visible state gets complicated / LOD selection likely different / Visible clusters from previous
frame might not even be in memory anymore!" — and instead tests *this* frame's selection against the
previous HZB with previous transforms (slide 75). Non-occlusion culling runs only in pass 1
(slide 76).

**False positives (says visible, is occluded) cost time. False negatives (says occluded, is visible)
are a correctness bug.** Every design decision must round toward over-inclusion. Epic's HZB path is
"more conservative in the way that it culls objects, meaning fewer objects are culled as a result"
(UE occlusion docs). Unity's rule is the same: objects "unoccluded in **either** frame" stay visible
([URP](https://docs.unity.cn/6000.1/Documentation/Manual/urp/gpu-culling.html),
[HDRP](https://docs.unity.cn/Packages/com.unity.render-pipelines.high-definition%4017.1/manual/gpu-culling.html)).

**Unity's candour, quoted identically in both pipelines, is the adoption test:**

> "If occlusion culling doesn't have a big effect on your scene, rendering time might increase
> because of the extra work the GPU does to set up GPU occlusion culling."

> "GPU occlusion culling performs best when: Multiple objects share the same mesh, enabling single
> draw calls; The scene has substantial occlusion, particularly with high-vertex occluded objects."

All three conditions must hold. **Open terrain under a low horizon is close to the worst case.**

**Documented weak point, volunteered by Epic:** "The reliance on previous frame depth for occlusion
culling is one of Nanite's biggest deficiencies" (slide 94). At a hard camera cut the previous HZB is
meaningless: pass 1 culls nothing, everything falls through to pass 2, and the frame roughly doubles.
**For a cinematic engine with scripted shot cuts this is a per-cut frame spike at precisely the moment
a viewer is most likely to notice it.** Mitigation: detect the cut and skip pass 1.

**And it presupposes GPU-driven submission.** Unity requires the GPU Resident Drawer before GPU
occlusion can be enabled at all (URP docs) — the culling result needs a path to the draw call.

## Virtual shadow maps

16K×16K virtual resolution in 128×128 pages, "allocated and rendered only as needed to shade
on-screen pixels based on an analysis of the depth buffer" (VSM docs). Demand flows **from the
screen's depth buffer backwards into shadow space**: "Pick the mip level where 1 texel matches the
size of 1 screen pixel / Mark that page in that level as needed" (slide 116).

**Is VSM practical without virtualized geometry? Possible, but not practical.** Three structural
couplings: Nanite picks the LOD matching one pixel of error *in shadow space* so cost scales with
resolution not complexity (slide 120); page-granularity culling is fused into cluster culling
(slide 119); and writing into the non-contiguous physical page pool **depends on scattered atomic
writes** — "Because we are doing atomic UAV writes, even in the HW path, we are free to scatter them"
(slide 119). The docs list "Shader Model 6.6 atomics or Vulkan (VK_KHR_shader_atomic_int64)" as a VSM
platform requirement. **Same wall as Nanite, for the same reason: they were co-designed.**

**Two VSM ideas port cleanly with zero capability requirements, and they are the highest
value-to-effort items in this research:**

1. **Screen-space-driven shadow resolution demand.** A compute pass over the depth buffer deciding
   where shadow resolution is needed — applied even to a conventional cascade, to place cascades or
   skip ones nothing samples.
2. **Static/dynamic cache separation with an explicit invalidation vocabulary** — Auto / Always /
   Rigid / Static per primitive (VSM docs).

**A warning worth carrying into any lighting design:** light rotation "invalidates **all** cached
pages for that light" (VSM docs). A slowly panning key light is pathological for shadow caching —
it pays full re-render every frame while looking almost static.

## What this research says, tested against this engine's measurements

> The "18.6" total and the technique-vs-bottleneck calls below are from the pre-LOD0/pre-shadow-work
> baseline audited in [01 §1.1/§3.2](01-audit-and-baseline.md); current figures are in 01 §3.2.2
> (GPU 13.37 ms, scene pass 10.88 ms, shadow pass 0.33 ms). The feasibility/attacks-bottleneck
> verdicts are architectural and unaffected by the later numbers moving.

| Technique | WebGPU feasible? | Attacks *this engine's* measured bottleneck? |
|---|---|---|
| Nanite software rasterizer | **No** — 64-bit texture atomics | — |
| Nanite DAG / cluster LOD | **Yes** | **Yes** — fewer, larger triangles is the lever |
| Meshlet cluster culling | Yes | **No** — geometry work is 1.7% of the scene pass |
| HZB two-pass occlusion | **Yes** (the only unblocked one) | **Marginally** — see below |
| HLOD / merged proxies | Yes | **Yes** — collapses distant instances |
| Impostors | Yes | **Yes** — the far field, where a DAG bottoms out |
| Virtual shadow maps | No — same atomics wall | — |
| VSM's *ideas* on a conventional map | Yes | Shadows are 1.1 ms of 18.6 — low ceiling |

**The inversion worth stating plainly:** the one technique with no capability blockers — hierarchical
occlusion culling — attacks *geometry* work, and this engine's geometry work is **1.7% of its scene
pass** (Deliverable 4). Occlusion also removes the fragment work behind occluders, which is the real
prize, but a depth prepass already suppresses opaque overdraw here. Unity's own three-part
precondition must be measured before any of it is built, and Glowmere — an open valley under a low
horizon — is not obviously a scene where it holds.

Conversely, the technique family that *does* attack the measured bottleneck — fewer and larger
triangles at distance, via cluster LOD, HLOD and impostors — is only partly portable, and Epic warns
it works worst on exactly this engine's content.
