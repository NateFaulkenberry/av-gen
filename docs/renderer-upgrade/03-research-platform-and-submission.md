# Deliverable 2b — Research: GPU-driven rendering, the WebGPU envelope, frame graphs, Apple TBDR

## The WebGPU capability envelope — what can actually be built

Verified against the specification. This is the constraint every later decision lives inside.

**Present in core:** `drawIndirect` (16-byte record), `drawIndexedIndirect` (20-byte record),
`dispatchWorkgroupsIndirect` (12-byte record), storage buffers in vertex/fragment/compute, render
bundles, up to 8 colour attachments, `u32`/`i32` atomics
([spec](https://www.w3.org/TR/webgpu/#dom-gpurendercommandsmixin-drawindirect)).
**Optional features:** `timestamp-query`, `indirect-first-instance`, `subgroups`, `primitive-index`,
`dual-source-blending`, `shader-f16` ([spec §25](https://www.w3.org/TR/webgpu/#gpufeaturename)).

**Absent from the specification entirely** — not optional features, not concepts it has:
multi-draw indirect, indirect draw-count buffers, 64-bit atomics, texture atomics, mesh/task shaders,
bindless/descriptor indexing, conditional rendering, and **any early-fragment-test attribute in WGSL**.

**And the implementation fact that settles GPU-driven rendering for this target:** Dawn's Metal
backend has multi-draw-indirect **commented out** — `// TODO(crbug.com/393183837) - Re-enable this
feature when we use argument buffers`
([PhysicalDeviceMTL.mm](https://raw.githubusercontent.com/google/dawn/main/src/dawn/native/metal/PhysicalDeviceMTL.mm)).
So on Dawn-on-Metal today there is no multi-draw at all, standard or extension. **Every draw remains
one CPU-side call.** That is the hard ceiling on any GPU-driven design here.

**Two traps worth knowing.** `firstInstance` must be 0 unless `indirect-first-instance` is enabled,
and if it is not, the draw "**will be treated as a no-op**" — silent, not a validation error
([spec](https://www.w3.org/TR/webgpu/#dom-gpurendercommandsmixin-drawindirect)). And render bundles
*clear* pass state rather than restoring it, "even if zero `GPURenderBundle`s are executed"
([spec](https://www.w3.org/TR/webgpu/#dom-gpurenderpassencoder-executebundles)).

**Two Dawn-on-Metal extensions that are enabled and directly useful:**
- **`framebuffer-fetch`** — Metal programmable blending through WebGPU: a fragment shader reads its
  own colour attachments from tile memory ([Dawn docs](https://github.com/google/dawn/blob/main/docs/dawn/features/framebuffer_fetch.md)).
- **`transient-attachments`** — Metal memoryless through WebGPU: "render pass operations … stay in
  tile memory, avoiding VRAM traffic and potentially avoiding VRAM allocation"
  ([Dawn docs](https://github.com/google/dawn/blob/main/docs/dawn/features/transient_attachments.md)).

## Apple TBDR — the findings that change the plan

**Hidden surface removal makes a performance-only depth prepass redundant.** Apple, explicitly: "if
you only perform a depth pre-pass for performance, then hidden surface removal serves the same
purpose on Apple GPUs. When HSR is maximized, it can reject hidden fragments as well as depth
pre-passes can, **but without any additional costs**"
([WWDC20-10632](https://developer.apple.com/videos/play/wwdc2020/10632/)). But: "if you need
visibility information before your main scene renders for a particular technique, then you don't have
to change your app." **This engine's prepass feeds the linear-depth target consumed by AO, water and
fog — so it stays.** Measured at 0.26 ms, it is not a cost worth arguing about anyway.

**Three documented mechanisms defeat HSR**, and each silently turns a TBDR GPU into an immediate-mode
one:

1. **Fragment shaders writing to buffers or textures.** "By default Metal is required to execute all
   such fragments even if they're occluded by later fragments." Metal's remedy is the
   `[[early_fragment_tests]]` attribute — **and WGSL has no equivalent**, nor does Dawn emit one. A
   WebGPU fragment shader that writes a storage buffer therefore disables HSR with no way to opt back
   in. ✅ **Audited: no scene-pass fragment shader in this engine writes storage. Clean.**
2. **Colour write masks, including unintentional ones.** "fragment functions that don't write any
   channels of an attachment are also write masking… unless you really need to preserve the previous
   values of an attachment, you should write to all render pass attachments to improve HSR
   efficiency." ✅ **Audited: every scene-pass shader returns `SceneOut`, which writes all five
   attachments. Clean.** The blended/water/particle pipelines do set an explicit `writeMask = None`
   on the four auxiliary targets — which is the *deliberate* case Apple describes — but they are
   drawn after the opaque block, and Apple's specific warning is against *interleaving* opaque meshes
   with differing masks. The renderer's submission order (opaque → grid → water → blended →
   particles) already avoids that.
3. **Interleaved draw order.** Apple: draw opaque first, then alpha-test, then translucent, and
   "avoid interleaving opaque meshes with different color attachment write masks"
   ([WWDC20-10603](https://developer.apple.com/videos/play/wwdc2020/10603/)). ✅ Already satisfied.

**Load and store actions are the bandwidth lever.** "Apple GPUs process render pass attachments in
tile storage and those attachments are moved in and out of tile memory using load and store actions.
**These actions consume the majority of your app's system bandwidth**" (WWDC20-10632).

**The tile-memory budget — and the correction it forces.** Apple's Feature Set Tables give Apple8
(M2) an implicit imageblock budget of **128 bytes per pixel** with a 32×32 tile
([Metal Feature Set Tables](https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf)).

This engine's five targets cost, under the WebGPU accounting rules
([spec §26.1.1](https://www.w3.org/TR/webgpu/#plain-color-formats)):

| Target | Format | Byte cost | Align | Running |
|---|---|---|---|---|
| 0 HDR radiance | `rgba16float` | 8 | 2 | 8 |
| 1 normal+roughness | `rgba16float` | 8 | 2 | 16 |
| 2 velocity | `rg16float` | 4 | 2 | 20 |
| 3 emission | `rgba16float` | 8 | 2 | 28 |
| 4 identifiers | `r32uint` | 4 | 4 | **32** |

**Exactly 32 bytes per sample — exactly WebGPU's default `maxColorAttachmentBytesPerSample`,** and
25% of the M2's 128-byte tile budget.

Two consequences, and the first corrects a hypothesis of mine. **Tile memory is not the constraint:
at 25% of budget the five-attachment layout is not what is limiting occupancy, and any plan premised
on "reduce attachment count to fit the tile" is unfounded.** But **there is zero portability
headroom**: a sixth attachment, or widening any format, exceeds the default limit and requires an
explicitly raised device limit — unavailable in compatibility mode, which caps at 32.

**The counters that would settle the open question.** Apple: "Overdraw, in this context, is the ratio
between **Fragment Shader invocations** and **Pixels Stored**" (WWDC20-10603). That single ratio
directly tests this investigation's central hypothesis, and it is available only through Xcode.

## GPU-driven rendering — what it does and does not buy

Unity scopes the benefit precisely: "games that are CPU bound due to a high number of draw calls can
improve in performance" ([Unity](https://unity.com/how-to/gpu-optimization)). Fragment cost is a
separate axis with separate remedies, and **nothing in Unity's documentation claims the GPU Resident
Drawer reduces fragment shading cost.**

Unity also warns about the culling half: "If occlusion culling doesn't have a big effect on your
scene, rendering time might increase because of the extra work the GPU does to set up GPU occlusion
culling" ([gpu-culling](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.high-definition/Documentation~/gpu-culling.md)).

**Applied here:** the scene pass is 84% of the frame and CPU scene time is 0.66 ms over 136 draws.
Perfect elimination of CPU submission saves under 1 ms of a 22 ms frame and **nothing** of the
15.7 ms scene pass. Combined with the absence of multi-draw on Dawn-on-Metal, GPU-driven submission
is the wrong project for this engine at this time.

## Frame graphs — what Filament's document actually claims

Filament lists exactly three benefits: manage resource lifetime, calculate texture usage bits, and
"**Calculate the load/store bits of the rendertargets within a renderpass**"
([Filament FrameGraph](https://google.github.io/filament/notes/framegraph.html)).

**Not stated anywhere in that document:** transient memory *aliasing*, automatic barrier insertion,
or any performance number. Frame-graph advocacy elsewhere leans on aliasing; this primary source does
not support that claim. And Filament flags the cost of the two features you would most want —
imported history for temporal techniques, and subresource relationships — as having "added great
complexity to the implementation."

**Assessment.** Only the third bullet has real value here, and it has a lot: deriving load/store
actions from declared reads and writes is exactly the optimisation Apple says dominates system
bandwidth. But that is a **table**, not an architecture. For a renderer whose pass order is fixed,
documented and already asserted by test, a full frame graph buys pass culling (an `if` statement
does this) and lifetime pooling (a fixed set of targets allocated once does this).

**Recommendation: reject the frame graph as an architecture; adopt its load/store derivation as a
small declarative table.** Revisit only if pass composition becomes genuinely dynamic across quality
tiers. The measurement that would justify revisiting: count attachment load/store actions in the
current frame that are wrong — loading something immediately overwritten, or storing something no
later pass reads. If that count is zero or one, a frame graph has nothing to sell.
