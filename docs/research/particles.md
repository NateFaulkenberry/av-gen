# Research: GPU Particle Systems

**Status:** research only — milestone 0.1 does NOT implement any of this.
**Purpose:** establish the direction for a festival-scale particle engine (millions of particles, trails, fluids, flocking) and extract the constraints the milestone-0.1 renderer abstraction must respect so that none of these techniques are precluded later.
**Target platform:** macOS 26, Apple M2 Max, Metal 4 (unified memory, TBDR, no forward-progress guarantee across threadgroups).
**Date of research:** 2026-09-08. All URLs accessed 2026-09-08 unless noted.

Citation convention used throughout: each claim block lists *Source*, *URL*, *Accessed*, *What was learned*, *Relevance*, *Confidence / limitations*.

---

## 1. Reference architectures

### 1.1 Wicked Engine — "GPU-based particle simulation" (Turánszki, 2017)

- **Source:** Wicked Engine blog, "GPU-based particle simulation" (2017-11-07); engine source as mirrored by DeepWiki.
- **URL:** https://wickedengine.net/2017/11/07/gpu-based-particle-simulation/ (live page returned HTTP 404 on 2026-09-08; the article abstract was recovered through search and the engine architecture through https://deepwiki.com/turanszkij/WickedEngine/8.1-lua-integration and the public repo https://github.com/turanszkij/WickedEngine).
- **Accessed:** 2026-09-08.
- **What was learned:** The canonical, open-source, minimal GPU particle design:
  - Buffers: a particle buffer (position, velocity, life, etc.), a **dead-list index buffer** (initially every index is dead), **two alive-list index buffers** (ping-pong), a **counter buffer** (alive count, dead count, real emit count, alive-count-after-simulation), an **indirect-args buffer** (dispatch args for emit/simulate and draw args), and in newer versions a distance buffer for sorting and a "general buffer" of expanded vertex data for mesh-shader / ray-traced rendering.
  - Passes, in order, each frame: (1) **kick-off** compute shader — reads counters and writes the indirect dispatch arguments for the emit and simulate passes and resets per-frame counters; (2) **emit** — dispatched indirectly with `ceil(emitCount/threadgroup)` groups; each thread pops an index from the dead list with an atomic decrement, initialises the particle and pushes the index into alive-list-A with an atomic increment; (3) **simulate** — dispatched indirectly for `aliveCount`; integrates, applies forces/collision, and either pushes the index into alive-list-B (still alive) or back onto the dead list (expired); (4) **finish** — writes final `DrawIndirect` arguments (instance count = alive-after-simulation); optional **sort** pass; then the draw is issued with `DrawIndirect`/`DrawIndexedInstancedIndirect`, so the CPU never reads the count back.
  - Two alive lists are required because the emit pass appends to one list while the simulate pass compacts into the other; they swap each frame.
  - Optional SPH fluid stages use a spatial hash grid (512^3 buckets in the current engine) for neighbour search; newer versions can use mesh shaders and build a BLAS from the particle vertex data for ray tracing.
- **Relevance:** This is the baseline architecture for av-gen's future particle engine. Everything else in this document is an elaboration on it.
- **Confidence / limitations:** High on the overall structure (it matches Frostbite/Niagara/VFX Graph patterns and the engine source). Exact buffer names and the 512^3 figure come from the DeepWiki mirror rather than the original article; verify against the repository before relying on numbers.

### 1.2 Frostbite — GPU Emitter Graph System (Hillaire & Egleus, GDC 2018)

- **Source:** GDC 2018 talk "Frostbite GPU Emitter Graph System", Sébastien Hillaire (EA) and Anders Egleus (DICE); EA Frostbite news post; Real Time VFX forum thread.
- **URL:** https://www.gdcvault.com/play/1025132/Frostbite-GPU-Emitter-Graph ; https://realtimevfx.com/t/gdc-2018-frostbite-gpu-emitter-graph-system/4003 (the EA news post https://www.ea.com/frostbite/news/frostbite-gpu-emitter-graph-system returned 404 on 2026-09-08).
- **Accessed:** 2026-09-08.
- **What was learned:** Frostbite's production particle system is a **graph-based, GPU-simulated emitter system**; the four pillars named in the abstract are *shader generation* (artist graphs compile to compute shaders), *memory management* (a shared pool of particle memory across all emitters), *sorting*, and *rendering*. It shipped in Star Wars Battlefront II. The talk presents the system as data-driven so that one engine serves many games.
- **Relevance:** Confirms that a production system (a) generates compute shaders from a node graph rather than hand-writing per-effect kernels, and (b) manages memory as one shared pool. av-gen's future node/graph layer should emit compute kernels; the renderer must therefore support runtime pipeline creation from generated shader source (Metal supports this via `MTLLibrary` from source or via the Metal 4 compiler contexts).
- **Confidence / limitations:** Medium. The slide deck itself could not be fetched; details are from the abstract and forum descriptions. The original 2014 "The Technology Behind Frostbite's GPU particles" title referenced in the task brief could not be located as a distinct talk; the 2018 GDC talk appears to be the canonical public description.

### 1.3 Unreal Engine — Niagara

- **Source:** Epic Games documentation, "Key Concepts in Niagara Effects", "Overview of Niagara Effects", plus community write-ups on Simulation Stages.
- **URL:** https://dev.epicgames.com/documentation/en-us/unreal-engine/key-concepts-in-niagara-effects-for-unreal-engine ; https://dev.epicgames.com/documentation/unreal-engine/overview-of-niagara-effects-for-unreal-engine ; https://dev.to/dinesh_04/how-simulation-stages-work-in-unreal-engine-gpu-emitters-149c ; https://www.strayspark.studio/blog/niagara-vfx-advanced-simulation-stages
- **Accessed:** 2026-09-08.
- **What was learned:**
  - Hierarchy is Module → Emitter → System. A particle's payload is a "parameter map" of arbitrary attributes (structs, matrices, bools), addressed through namespaces (System, Emitter, Particle, Engine, User) that gate read/write access.
  - Execution is a **stack** of groups: Emitter Update, Particle Spawn, Particle Update, Particle Event, Particle Render. Modules execute top-to-bottom.
  - **GPU Compute emitters** translate the module graph to HLSL compute code; the docs and community sources state GPU emitters comfortably run hundreds of thousands to millions of particles, whereas CPU emitters degrade past ~10k.
  - **Simulation Stages** (GPU-only) allow multiple extra Spawn/Update passes per frame, and can iterate over Data Interfaces (e.g. Grid2D/3D) rather than particles — this is how Niagara builds grid fluids and neighbour grids inside the particle system.
  - Collision options for GPU particles: scene depth buffer (screen-space, only what is visible), global distance field (whole scene, low resolution — thin objects can be missed), and experimental hardware ray-tracing collisions. See §7.
- **Relevance:** The "stack of stages that can also iterate over grids" model is the most general one. av-gen should model a particle system as an ordered list of compute passes over either the particle set or an auxiliary grid, not as a single fixed simulate kernel.
- **Confidence / limitations:** High for structure (official docs). Particle-count figures are community claims, not benchmarks.

### 1.4 Unity — VFX Graph

- **Source:** Unity Visual Effect Graph manual ("Graph Logic and Philosophy"), Unity blog, forum threads on capacity and strips.
- **URL:** https://docs.unity3d.com/Packages/com.unity.visualeffectgraph@17.0/manual/GraphLogicAndPhilosophy.html ; https://unity.com/blog/engine-platform/new-possibilities-with-vfx-graph-in-2020-lts-and-beyond ; https://forum.unity.com/threads/vfx-capacity-and-performance.1013098/ ; https://uhiyama-lab.com/en/notes/unity/unity-vfx-graph-guide/
- **Accessed:** 2026-09-08.
- **What was learned:**
  - A **System** is a chain of **Contexts**: Spawn (per frame, computes how many to spawn), Initialize (per particle at birth), Update (per particle per frame: forces, collisions), Output (per particle per frame: shape/renderer). Blocks stack inside contexts.
  - Simulation runs entirely on the GPU in compute shaders; capacity is a fixed **pre-allocated pool** set on the Initialize context. Forum guidance: capacity directly costs performance even if few particles are alive (the update dispatch covers the pool), so keep it as small as possible.
  - **Particle Strips** (ribbons/trails) are a distinct system type with their own strip capacity; mismatched strip vs particle capacity causes silent spawn failure — evidence that trails need their own explicit allocation.
  - GPU Events allow one system to spawn particles in another on the GPU.
- **Relevance:** Confirms the fixed-capacity pool model and the need for a separate strip/trail allocation. Also confirms Spawn/Init/Update/Output as the minimal set of stages.
- **Confidence / limitations:** High for the model; the "capacity costs even when empty" claim is forum-sourced but consistent with a pool-wide dispatch design.

### 1.5 Notch

- **Source:** Notch website feature pages and manual (2026.1).
- **URL:** https://www.notch.one/features/particles-simulations-volumetrics ; https://manual.notch.one/2026.1/en/docs/learning/simulations/particles/overview-of-particles/ ; https://manual.notch.one/2026.1/en/docs/reference/nodes/particles/
- **Accessed:** 2026-09-08.
- **What was learned:** Notch (the de-facto festival/concert tool) advertises "tens of millions of particles in real time" and "hundreds of millions of points" in its unified simulation systems, all GPU-resident. Every particle system has a root node with a **Max Particle Count** that sizes the pool. Node-based, with timeline + curve editing; the same project can be rendered live or exported to video.
- **Relevance:** Sets the ambition bar (10^7 particles) and confirms the fixed-pool-per-system model. Notch's "same scene, live or offline" is exactly av-gen's dual requirement; see `offline-rendering.md`.
- **Confidence / limitations:** Marketing numbers; treat as an upper bound reached with simple point rendering.

### 1.6 Sascha Willems — Vulkan `computeparticles` / `computenbody`

- **Source:** SaschaWillems/Vulkan repository examples.
- **URL:** https://github.com/SaschaWillems/Vulkan/blob/master/examples/computeparticles/computeparticles.cpp ; https://github.com/SaschaWillems/Vulkan/blob/master/examples/computenbody/computenbody.cpp
- **Accessed:** 2026-09-08.
- **What was learned:** Minimal pattern: particle data lives in a single shader-storage buffer that is only ever modified on the GPU; the compute queue updates it and the graphics queue reads it as a vertex buffer, with **memory/queue-ownership barriers** between compute write and vertex read. 256K particles (128K on Android). No dead/alive lists — every slot is always alive — which is the simplest possible design and enough for "attractor" style effects.
- **Relevance:** Shows the minimal barrier requirement: the renderer must be able to express "compute wrote buffer X, vertex/fragment stage now reads X". In Metal this is implicit within one command buffer between encoders (with `MTLFence`/`memoryBarrier` or, in Metal 4, explicit barriers between passes).
- **Confidence / limitations:** High (source code).

### 1.7 Metal-specific: indirect draws/dispatches, indirect command buffers, argument buffers

- **Source:** Apple Metal documentation and best-practices guide; WWDC22 "Go bindless with Metal 3"; Metal 4 overview.
- **URL:** https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/IndirectBuffers.html ; https://developer.apple.com/documentation/metal/indirect-command-encoding ; https://developer.apple.com/documentation/metal/mtldispatchthreadgroupsindirectarguments ; https://developer.apple.com/videos/play/wwdc2022/10101/ ; https://developer.apple.com/videos/play/wwdc2025/205/ ; https://tellusim.com/metal-mdi/
- **Accessed:** 2026-09-08.
- **What was learned:**
  - Metal supports **indirect draw** (`drawPrimitives`/`drawIndexedPrimitives` with `indirectBuffer` + offset) and **indirect dispatch** (`dispatchThreadgroups(indirectBuffer:indirectBufferOffset:threadsPerThreadgroup:)`). The dispatch argument layout is `MTLDispatchThreadgroupsIndirectArguments { uint32_t threadgroupsPerGrid[3]; }` with **4-byte alignment**. Apple's guidance: use indirect buffers whenever draw/dispatch arguments are generated on the GPU.
  - **Indirect Command Buffers (ICBs, Metal 2+)** let a compute kernel encode whole draw or dispatch commands into a `MTLIndirectCommandBuffer`, enabling fully GPU-driven pipelines (multi-draw-indirect equivalent). Tellusim notes Metal has no native `MultiDrawIndirect`; ICBs are the substitute.
  - **Argument buffers Tier 2** (Apple6+/Mac2 families — all Apple silicon) give bindless access: one buffer holds references to many textures/buffers, shaders can index and even write into it. Metal 4 replaces per-encoder binding with `MTL4ArgumentTable`.
  - Metal 4 (WWDC25) consolidates encoders into a unified compute encoder (compute + blit + acceleration-structure), adds a compiler context with QoS for runtime shader compilation, and adds tensor types in MSL.
- **Relevance:** All prerequisites for GPU-driven particles exist on the target machine. The abstraction must expose indirect dispatch and indirect draw with a GPU-writable args buffer, and should not assume the CPU knows instance counts.
- **Confidence / limitations:** High for API facts. The Apple "Indirect command encoding" page body did not render through the fetch tool; details are from the linked archive docs and WWDC sessions.

---

## 2. Data layout: SoA vs AoS, ping-pong, dead/alive lists, atomic emission

- **What was learned (synthesis of §1):**
  - **Pool + index lists.** Particle state lives in a fixed-capacity pool. Liveness is tracked by index lists (dead list + two alive lists), not by scanning the pool. Emission pops from the dead list, simulation compacts into the "next" alive list. Counts live in a small **counter buffer** updated with atomics; the CPU never reads them.
  - **SoA vs AoS.** Wicked Engine and Sascha Willems use AoS structs; Niagara and VFX Graph generate SoA-style attribute buffers per particle attribute (Niagara's parameter map; VFX Graph's attribute layout). SoA is preferable when kernels touch a subset of attributes (sorting keys, position-only render paths) and for adding attributes without recompiling every kernel. Recommended: SoA of typed attribute buffers, with an attribute registry so generated kernels bind only what they use.
  - **Ping-pong.** Any pass that both reads and writes the same logical set (simulate, sort, grid fluids) must use two buffers/textures swapped per pass. This is a hard constraint in GPU programming (reading a resource while writing it is undefined — GPU Gems ch. 38 states this explicitly for textures; the same holds for unordered buffer access without ordering guarantees).
  - **Atomic emission and ordering.** Emission via `atomic_fetch_add` on a device counter is *order-nondeterministic*: which pool slot a given emitted particle receives depends on thread scheduling. This does not change the *set* of particles but does change their indices, and therefore anything that depends on index (per-index RNG seeds, ribbon connectivity, sort stability). See `offline-rendering.md` §GPU determinism. Alternative: wave-level compaction (§2.1) reduces atomics to one per wave and gives intra-wave ordering, but still no cross-wave order.
- **Renderer requirements:** storage buffers with GPU-side atomics (32-bit `atomic_uint` minimum; 64-bit atomics are optional on Apple silicon and should not be assumed); buffer aliasing/swapping by handle; ability to keep buffers resident across frames (persistent, not transient-per-frame).

### 2.1 Stream compaction with wave intrinsics

- **Source:** Kostas Anagnostou, "Stream compaction using wave intrinsics" (Interplay of Light, 2022-12-25); Wicked Engine "Thoughts on light culling: stream compaction vs flat bit arrays".
- **URL:** https://interplayoflight.wordpress.com/2022/12/25/stream-compaction-using-wave-intrinsics/ ; https://wickedengine.net/2019/02/thoughts-on-light-culling-stream-compaction-vs-flat-bit-arrays/
- **Accessed:** 2026-09-08.
- **What was learned:** Use `WaveActiveCountBits` (ballot count) + `WavePrefixCountBits` (per-lane prefix) so that one lane per wave performs a single global atomic to reserve a contiguous block; lanes then write at `base + prefix`. Ordering is preserved within a wave but arbitrary across waves. Simpler and lower-overhead than a full parallel prefix-scan.
- **Relevance:** Cuts atomic contention by ~32-64x for the alive-list append in simulate/emit. Metal exposes the equivalent as SIMD-group functions (`simd_prefix_exclusive_sum`, `simd_ballot`, `simd_is_first`); SIMD width on Apple GPUs is 32.
- **Confidence / limitations:** High. Cross-wave order remains nondeterministic; a fully deterministic alternative is a two-pass prefix-sum compaction (scan of alive flags, then scatter), which costs an extra pass but yields a stable order — important for offline determinism.

---

## 3. Rendering: instancing vs indirect draw, billboards, mesh shaders

- **What was learned:**
  - Baseline: one **instanced indirect draw** of a quad (or a procedural vertex shader that expands `vertex_id` into a billboard, avoiding an index buffer) with instance count taken from the indirect args written by the finish pass. Particle attributes are fetched from the storage buffers by `instance_id` (or from the sorted alive list: `particleIndex = aliveList[instance_id]`).
  - Wicked Engine's current implementation optionally renders through **mesh shaders** (Metal supports object/mesh shaders since Metal 3 on Apple7+), and expands particles into a vertex "general buffer" that can also feed a BLAS for ray-traced particles.
  - ICBs (§1.7) allow multiple emitters/LODs to be drawn from one GPU-generated command stream, eliminating per-emitter CPU draw calls.
- **Renderer requirements:** indirect instanced draw with args in a GPU-written buffer; vertex-stage access to storage buffers (Metal: any buffer bound to the vertex stage); optional mesh-shader pipeline type; the render pass must be able to consume buffers produced by compute in the same frame with correct barriers.
- **Confidence / limitations:** High.

---

## 4. Sorting for transparency

### 4.1 Onesweep (Adinets & Merrill, NVIDIA 2022)

- **Source:** "Onesweep: A Faster Least Significant Digit Radix Sort for GPUs".
- **URL:** https://arxiv.org/abs/2206.01784 ; https://research.nvidia.com/publication/2022-06_onesweep-faster-least-significant-digit-radix-sort-gpus
- **Accessed:** 2026-09-08.
- **What was learned:** LSD radix sort using single-pass chained-scan (decoupled look-back) so each 8-bit digit pass needs ~2n global memory operations instead of ~3n; 29.4 GKey/s for 256M 32-bit keys on A100, ~1.5x faster than CUB. Four passes for 32-bit keys.
- **Relevance:** State of the art for large key/value sorts (e.g. sorting 10^6-10^7 particle distances per frame).
- **Confidence / limitations:** High on the paper; **portability caveat below is decisive for Apple silicon.**

### 4.2 AMD FidelityFX Parallel Sort

- **Source:** AMD GPUOpen manuals; "Boosting GPU Radix Sort performance" blog.
- **URL:** https://gpuopen.com/manuals/fidelityfx_sdk/techniques/parallel-sort/ ; https://gpuopen.com/fidelityfx-parallel-sort/ ; https://gpuopen.com/learn/boosting_gpu_radix_sort/
- **Accessed:** 2026-09-08.
- **What was learned:** Radix sort processing 4 bits per pass (8 passes for 32-bit keys) with a count → reduce → scan → scatter structure; key or key/value; designed for subgroup sizes >= 16. Roughly half the throughput of Onesweep-derived sorts but uses only conventional multi-pass reductions.
- **Relevance:** The safer baseline algorithm for Metal because it needs no forward-progress guarantee.
- **Confidence / limitations:** High.

### 4.3 Portability: forward progress on Apple silicon

- **Source:** Linebender wiki, "GPU sorting" (survey by Raph Levien et al.); Kieber-Emmons, "Memory Bandwidth Optimized Parallel Radix Sort in Metal for Apple M1 and Beyond".
- **URL:** https://linebender.org/wiki/gpu/sorting/ ; https://betterprogramming.pub/memory-bandwidth-optimized-parallel-radix-sort-in-metal-for-apple-m1-and-beyond-4f4590cfd5d3
- **Accessed:** 2026-09-08.
- **What was learned:** The Linebender survey states that one-pass (decoupled look-back) sorts can deadlock or stall on GPUs **without forward-progress guarantees, "of which Apple Silicon is especially noticeable"**, and that implementations must not assume subgroup-local atomics complete in order. Their FidelityFX-derived hybrid reaches ~1 G elements/s on M1 Max via WebGPU and ~3 G el/s in native Metal with SIMD ballots. Bitonic sort is dismissed for large n (dozens of passes vs 4-8 for radix) but remains fine for small per-emitter sorts and is trivially deterministic.
- **Relevance:** **Architectural constraint:** the av-gen sort must be a multi-pass radix sort (FidelityFX-style) or a segmented bitonic sort; do not port Onesweep/decoupled look-back to Metal without a device-specific fallback. Sorting also needs a key/value (distance, particleIndex) buffer pair and 2-4 scratch buffers.
- **Confidence / limitations:** High; Linebender is an active, well-maintained survey (page accessed 2026-09-08).

### 4.4 Alternatives to sorting

Order-independent transparency (weighted blended OIT, per-pixel linked lists) or additive blending (most festival looks are additive/emissive) avoid sorting entirely. Sorting is only required for alpha-blended, non-commutative particles. Plan: additive by default, sorted path available.

---

## 5. Particle trails / ribbons

- **Source:** Real Time VFX forum (Niagara ribbon tutorial); Svante Jakobsson, "GPU Particle System"; Unity strip capacity threads.
- **URL:** https://realtimevfx.com/t/niagara-4-25-ribbon-trail-mini-tutorial/13043 ; https://svantejakobsson.com/gpu-particle-system/ ; https://discussions.unity.com/t/vfx-graph-stops-rendering-particles-if-strip-capacity-and-capacity-are-mismatched/1526919
- **Accessed:** 2026-09-08.
- **What was learned:** Two viable GPU approaches: (a) **per-particle history ring buffer** — each particle owns `N` historical positions (`N x capacity` extra storage) written every frame at `head = frame % N`; ribbons are drawn as `capacity x (N-1)` quads from one instanced indirect draw, with the vertex shader deriving `ribbonIndex = segment / segmentsPerParticle` and rejecting segments that cross particle boundaries; (b) **strip systems** as in VFX Graph where trail particles are separately allocated and linked by a strip index. Depth sorting of ribbon segments uses packed (depth, index) keys with bitonic sort in the cited implementation. Niagara's ribbon renderer sorts ribbon particles by a "ribbon link order" attribute.
- **Relevance:** Trails multiply memory by history length: 1M particles x 16 history x 16 bytes = 256 MB, feasible on M2 Max unified memory but must be budgeted. History buffers make the system stateful across many frames — critical for offline warm-up (see `offline-rendering.md`).
- **Confidence / limitations:** Medium; these are practitioner sources, not papers, but the approaches are standard.

---

## 6. Forces: attractors, force fields, noise fields, curl noise

### 6.1 Curl noise (Bridson, Hourihan, Nordenstam, SIGGRAPH 2007)

- **Source:** "Curl-Noise for Procedural Fluid Flow", ACM TOG 26(3), article 46. Full text read from the author PDF.
- **URL:** https://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph2007-curlnoise.pdf ; https://dl.acm.org/doi/10.1145/1275808.1276435
- **Accessed:** 2026-09-08.
- **What was learned (from the paper text):**
  - Velocity is the **curl of a potential**: in 3D, `v = (dψ3/dy - dψ2/dz, dψ1/dz - dψ3/dx, dψ2/dx - dψ1/dy)`; in 2D, `v = (dψ/dy, -dψ/dx)`. Because `div(curl ψ) ≡ 0`, the field is exactly incompressible — no "gutters" where particles accumulate, which raw Perlin-noise velocity fields suffer from.
  - The potential is Perlin noise: scalar `ψ = N(x)` in 2D, a 3-vector of decorrelated noises (the same noise at large offsets) in 3D. Partial derivatives are taken by **finite differences with a tiny displacement** (1e-4 of the domain works in single precision).
  - Scaling: noise at length scale L gives vortices of diameter ~L and speeds ~O(1/L); summing octaves with a Kolmogorov-like power-law falloff gives physically plausible turbulence. Time-varying noise animates the field.
  - Modulating the *potential* (not the velocity) by any smooth amplitude `A(x)` keeps the field divergence-free.
  - **Boundaries:** ramp the potential toward zero with distance `d(x)` to the nearest solid (`ψ_c = ramp(d/d0) ψ`, with the smooth ramp `15/8 r - 10/8 r^3 + 3/8 r^5` on (-1,1)); in 3D ramp only the tangential component (`ψ_c = α ψ + (1-α) n (n·ψ)`) so flow slips along surfaces without penetrating. Rigid-body motion has an analytic potential (`V × (x-x0) + (R^2 - |x-x0|^2)/2 ω`) that can be blended in for wakes around moving objects.
- **Relevance:** The single most important "festival look" force. It needs only: a noise function in the kernel, a signed-distance query for obstacles, and per-particle evaluation — no grids. Six noise evaluations per axis for finite differences (18-36 noise calls per particle) is the cost class; analytic-gradient noise reduces this.
- **Confidence / limitations:** High (primary source read in full).

### 6.2 Attractors / force fields / noise textures

- Standard point/line/vortex attractors and sinks are algebraic (Sims 1990, Wejchert & Haumann 1991 as reviewed in Bridson 2007). Pre-baked 3D noise or vector-field textures (e.g. curl noise baked to a 3D texture) trade the per-particle noise cost for a single trilinear `sample()`; the renderer must therefore support **3D textures readable from compute** and writable by a bake pass.
- Confidence: high; standard practice.

---

## 7. Collisions: depth buffer, SDFs, ray tracing

- **Source:** Epic documentation (Niagara collision modes; distance-field collision; GPU raytracing collisions).
- **URL:** https://docs.unrealengine.com/4.27/en-US/Resources/ContentExamples/EffectsGallery/1_E ; https://docs.unrealengine.com/4.27/en-US/BuildingWorlds/LightingAndShadows/MeshDistanceFields/HowTo/DFHT_3/ ; https://dev.epicgames.com/documentation/en-us/unreal-engine/gpu-raytracing-collisions-in-niagara-for-unreal-engine
- **Accessed:** 2026-09-08.
- **What was learned:** Three tiers: (1) **scene depth** — project the particle, compare against the depth buffer (and reconstruct the normal from depth); cheap but only collides with visible surfaces and pops when the camera moves; (2) **global signed distance field** — whole-scene, view-independent, but low resolution rounds corners and lets particles through thin geometry; (3) **hardware ray tracing** (experimental in Niagara) — accurate and view-independent but expensive. av-gen's environments are largely procedural (SDF-defined), so (2) is a natural fit: the same SDF that raymarches the environment can collide particles and drive curl-noise boundaries (§6.1).
- **Renderer requirements:** compute access to the previous or current frame's depth texture (read-only, same-frame ordering); a 3D SDF texture (or an analytic SDF in a shared shader library) accessible from particle kernels; optional acceleration-structure intersection from compute (Metal ray-tracing intersection queries).
- **Confidence / limitations:** High for the trade-offs (official docs).

---

## 8. Boids / flocking on GPU

- **Source:** Paul Demeulenaere, "vfx-neighborhood-grid-3d"; BTH thesis "Performance Evaluation of Boids on the GPU and CPU"; "A Neighborhood Grid Data Structure for Massive 3D Crowd Simulation on GPU".
- **URL:** https://github.com/pauldemeulenaere/vfx-neighborhood-grid-3d/ ; https://www.diva-portal.org/smash/get/diva2:1191916/FULLTEXT01.pdf ; https://www.academia.edu/3394417/A_Neighborhood_Grid_Data_Structure_for_Massive_3D_Crowd_Simulation_on_GPU
- **Accessed:** 2026-09-08.
- **What was learned:** Brute-force O(n^2) neighbour search caps at ~50k agents even on GPU; a **uniform spatial hash grid** (cell index = hash(floor(p / cellSize)), particles sorted or bucketed by cell, neighbour loop over 27 cells) is required for >100k. Demonstrations reach 131k boids (Unity compute) and >1M boids at ~30 fps in research. The grid build is itself a count → prefix-sum → scatter (i.e. a counting sort by cell), which reuses the sort/scan primitives from §4.
- **Relevance:** The same neighbour grid serves boids, SPH (§9), and particle–particle collisions; it should be a shared reusable "spatial hash" module with its own ping-pong buffers.
- **Confidence / limitations:** Medium-high; numbers are implementation-specific.

---

## 9. Fluid-like particle systems

### 9.1 SPH and Position Based Fluids (Macklin & Müller, SIGGRAPH 2013)

- **Source:** "Position Based Fluids", ACM TOG 32(4); SIGGRAPH history page; Stanford CS348C assignment.
- **URL:** https://dl.acm.org/doi/10.1145/2461912.2461984 ; https://history.siggraph.org/learning/position-based-fluids-by-macklin-and-muller-fischer/ ; https://graphics.stanford.edu/courses/cs348c-17-fall/PA1_PBF2016/index.html
- **Accessed:** 2026-09-08.
- **What was learned:** PBF formulates incompressibility as a per-particle density constraint solved iteratively inside Position Based Dynamics; it inherits PBD's stability and allows large time steps, making it real-time friendly. Uses poly6 kernel for density and spiky for gradients; adds an artificial pressure term (surface tension, better particle distribution) and vorticity confinement as a velocity post-process. Per step: neighbour search → k iterations of (compute λ, compute Δp, apply) → velocity update → vorticity/viscosity. Each iteration is a separate compute dispatch over the neighbour grid.
- **Relevance:** Best candidate for "liquid-ish" festival visuals at 10^5-10^6 particles; needs the spatial hash (§8) and 3-5 solver iterations, so ~10-20 dispatches per frame with barriers between each.
- **Confidence / limitations:** High for the algorithm.

### 9.2 FLIP / PIC / MLS-MPM

- **Source:** Hu et al. 2018 "A Moving Least Squares Material Point Method" (taichi_mpm); Wu et al. 2018 "Fast Fluid Simulations with Sparse Volumes on the GPU"; Codrops WebGPU fluid write-up; Zibra AI overview.
- **URL:** https://github.com/yuanming-hu/taichi_mpm ; https://onlinelibrary.wiley.com/doi/abs/10.1111/cgf.13350 ; https://tympanus.net/codrops/2025/02/26/webgpu-fluid-simulations-high-performance-real-time-rendering/ ; https://www.zibra.ai/blog-posts/approaches-to-real-time-fluid-simulation-in-visual-effects
- **Accessed:** 2026-09-08.
- **What was learned:** Hybrid particle/grid methods transfer particle momentum to a grid (P2G, uses **atomic float adds** or a sort-and-reduce), solve pressure or apply grid forces, then transfer back (G2P). MLS-MPM unifies APIC/PolyPIC transfers and is the basis of most real-time GPU implementations (WebGPU demos run ~10^5-10^6 particles). GPU FLIP over sparse grids handles large scale offline.
- **Relevance:** P2G scatter is the one place particle systems *want* float atomics. Metal supports 32-bit float atomic add in MSL on Apple silicon (`atomic_fetch_add_explicit` on `atomic<float>`, MSL 3+) — verify against the Metal Feature Set Tables before relying on it; the deterministic alternative is sort-by-cell + segmented reduction (order-fixed, no atomics). **Float atomics are order-nondeterministic**, which matters for offline determinism.
- **Confidence / limitations:** Medium. Float-atomic availability on the target should be verified in code; the search did not surface an authoritative page for it (https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf is the authoritative table).

---

## 10. Cost classes (order of magnitude, M2 Max, 1M particles, per frame)

| Component | Passes | Notes |
|---|---|---|
| Kick-off + emit + simulate + finish | 4 dispatches | O(alive); bandwidth-bound; ~0.5-2 ms |
| Curl noise per particle | inside simulate | 18-36 noise evals/particle; ALU-bound; ~1-3 ms |
| Radix sort (FidelityFX-style, 32-bit keys) | 8 x (count, scan, scatter) ≈ 24 dispatches | ~1-3 ms at 1M (Linebender ~1-3 G el/s) |
| Spatial hash build | 3-4 dispatches | counting sort by cell |
| PBF solver | 3-5 iterations x 3 dispatches | ~5-15 ms at 1M; prefer 10^5 for real time |
| Ribbon history write | 1 dispatch + larger draw | memory x history length |
| Indirect instanced draw | 1 draw | fill-rate bound for large sprites; bloom after |

Numbers are estimates for planning only; nothing here was benchmarked.

---

## 11. Architectural requirements extracted for milestone 0.1

These are the things the 0.1 renderer abstraction must have (or must not preclude) so that the above can be added later without redesign:

1. **Compute passes are first-class.** The frame is a list of passes, and a pass may be compute, render, or blit. Compute must be able to run before, between, and after render passes in the same frame with correct ordering (Metal: encoders in one command buffer; Metal 4: explicit barriers between passes).
2. **Storage (device) buffers with arbitrary layouts, readable and writable from compute and readable from vertex/fragment stages.** No assumption that buffers are "vertex buffers" or "uniforms" only. Size up to hundreds of MB.
3. **Persistent GPU resources across frames.** Particle pools, index lists, history buffers and grids live for the lifetime of a system, not one frame. The resource system must distinguish transient (per-frame, aliasable) from persistent resources.
4. **Explicit double/ping-pong buffering as a pattern.** Resource handles must be swappable by name/handle each frame (alive list A/B, grid textures) without re-binding at the pipeline level.
5. **Indirect dispatch and indirect draw with GPU-written argument buffers**, with the args layout documented (`MTLDispatchThreadgroupsIndirectArguments`, `MTLDrawPrimitivesIndirectArguments`, 4-byte aligned). Optional later: indirect command buffers.
6. **GPU atomics** on 32-bit integers in device memory (counters). Do not depend on 64-bit or float atomics being present; expose a capability flag.
7. **No CPU readback in the hot path.** Particle counts, sort results, and culling results never cross to the CPU. The only readback is the offline frame capture (see `offline-rendering.md`).
8. **Per-frame ring of CPU-written uniform/constant data (triple buffered)** so simulation parameters (time, dt, seeds, forces) can be updated without stalling — Apple's triple-buffering guidance applies to all dynamic data.
9. **Runtime shader/pipeline creation from generated source** (Frostbite/Niagara/VFX Graph all generate kernels). The abstraction should wrap `MTLLibrary`/pipeline creation behind a cache keyed by source hash, and must not assume all shaders are precompiled at build time.
10. **A shared shader library** (noise, SDF primitives, RNG, hashing, colour) includable from both compute and fragment shaders, so curl noise, SDF collision and procedural environments share code.
11. **3D textures** readable from compute (noise/velocity fields, SDF volumes, froxel grids) and writable by compute.
12. **Timing contract:** every simulation pass receives `dt` and `time` from the engine's clock abstraction, never from wall time inside the pass — required for fixed-step and offline rendering.
13. **Deterministic-mode hooks:** the API should let a system choose "fast (atomic append, float atomics)" versus "deterministic (prefix-sum compaction, sorted reduction)" paths. 0.1 only needs the enum to exist in the design, not both implementations.
14. **Capability queries** for SIMD-group functions, mesh shaders, ray-tracing intersection from compute, float atomics — so future code can pick paths without redesign.
15. **Sorting constraint:** any future sort must avoid single-pass decoupled look-back on Apple silicon (no forward-progress guarantee). Plan for multi-pass radix.

---

## Sources

1. Wicked Engine, "GPU-based particle simulation" (2017). https://wickedengine.net/2017/11/07/gpu-based-particle-simulation/ — accessed 2026-09-08 (404 on live site; content via search abstract and https://deepwiki.com/turanszkij/WickedEngine/8.1-lua-integration and https://github.com/turanszkij/WickedEngine).
2. Hillaire, S., Egleus, A. "Frostbite GPU Emitter Graph System", GDC 2018. https://www.gdcvault.com/play/1025132/Frostbite-GPU-Emitter-Graph ; https://realtimevfx.com/t/gdc-2018-frostbite-gpu-emitter-graph-system/4003 — accessed 2026-09-08.
3. Epic Games, "Key Concepts in Niagara Effects". https://dev.epicgames.com/documentation/en-us/unreal-engine/key-concepts-in-niagara-effects-for-unreal-engine — accessed 2026-09-08.
4. Epic Games, "Overview of Niagara Effects". https://dev.epicgames.com/documentation/unreal-engine/overview-of-niagara-effects-for-unreal-engine — accessed 2026-09-08.
5. "How Simulation Stages Work in Unreal Engine GPU Emitters". https://dev.to/dinesh_04/how-simulation-stages-work-in-unreal-engine-gpu-emitters-149c — accessed 2026-09-08.
6. StraySpark, "Niagara VFX Beyond Particles". https://www.strayspark.studio/blog/niagara-vfx-advanced-simulation-stages — accessed 2026-09-08.
7. Epic Games, "GPU Particles with Scene Depth Collision" (UE 4.27 content example). https://docs.unrealengine.com/4.27/en-US/Resources/ContentExamples/EffectsGallery/1_E — accessed 2026-09-08.
8. Epic Games, "Using Particle Collision Mode for Distance Fields". https://docs.unrealengine.com/4.27/en-US/BuildingWorlds/LightingAndShadows/MeshDistanceFields/HowTo/DFHT_3/ — accessed 2026-09-08.
9. Epic Games, "GPU Raytracing Collisions in Niagara". https://dev.epicgames.com/documentation/en-us/unreal-engine/gpu-raytracing-collisions-in-niagara-for-unreal-engine — accessed 2026-09-08.
10. Unity, "Visual Effect Graph — Graph Logic and Philosophy". https://docs.unity3d.com/Packages/com.unity.visualeffectgraph@17.0/manual/GraphLogicAndPhilosophy.html — accessed 2026-09-08.
11. Unity blog, "New possibilities with VFX Graph in 2020 LTS and beyond". https://unity.com/blog/engine-platform/new-possibilities-with-vfx-graph-in-2020-lts-and-beyond — accessed 2026-09-08.
12. Unity forum, "VFX Capacity and performance". https://forum.unity.com/threads/vfx-capacity-and-performance.1013098/ — accessed 2026-09-08.
13. Unity Discussions, "VFX Graph stops rendering particles if strip capacity and capacity are mismatched". https://discussions.unity.com/t/vfx-graph-stops-rendering-particles-if-strip-capacity-and-capacity-are-mismatched/1526919 — accessed 2026-09-08.
14. Notch, "Particles, Simulations & Volumetrics". https://www.notch.one/features/particles-simulations-volumetrics — accessed 2026-09-08.
15. Notch Manual 2026.1, "Introducing Particles". https://manual.notch.one/2026.1/en/docs/learning/simulations/particles/overview-of-particles/ — accessed 2026-09-08.
16. Willems, S. Vulkan examples: computeparticles, computenbody. https://github.com/SaschaWillems/Vulkan/blob/master/examples/computeparticles/computeparticles.cpp — accessed 2026-09-08.
17. Apple, Metal Best Practices Guide: "Indirect Buffers". https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/IndirectBuffers.html — accessed 2026-09-08.
18. Apple, "Indirect command encoding". https://developer.apple.com/documentation/metal/indirect-command-encoding — accessed 2026-09-08 (page body did not render in fetch tool).
19. Apple, `MTLDispatchThreadgroupsIndirectArguments`. https://developer.apple.com/documentation/metal/mtldispatchthreadgroupsindirectarguments — accessed 2026-09-08.
20. Apple, WWDC22 "Go bindless with Metal 3". https://developer.apple.com/videos/play/wwdc2022/10101/ — accessed 2026-09-08.
21. Apple, WWDC25 "Discover Metal 4". https://developer.apple.com/videos/play/wwdc2025/205/ — accessed 2026-09-08.
22. Tellusim, "MultiDrawIndirect and Metal". https://tellusim.com/metal-mdi/ — accessed 2026-09-08.
23. Anagnostou, K. "Stream compaction using wave intrinsics" (2022). https://interplayoflight.wordpress.com/2022/12/25/stream-compaction-using-wave-intrinsics/ — accessed 2026-09-08.
24. Adinets, A., Merrill, D. "Onesweep: A Faster Least Significant Digit Radix Sort for GPUs" (2022). https://arxiv.org/abs/2206.01784 — accessed 2026-09-08.
25. AMD, "FidelityFX Parallel Sort". https://gpuopen.com/manuals/fidelityfx_sdk/techniques/parallel-sort/ — accessed 2026-09-08.
26. Linebender wiki, "GPU sorting". https://linebender.org/wiki/gpu/sorting/ — accessed 2026-09-08.
27. Kieber-Emmons, M. "Memory Bandwidth Optimized Parallel Radix Sort in Metal for Apple M1 and Beyond". https://betterprogramming.pub/memory-bandwidth-optimized-parallel-radix-sort-in-metal-for-apple-m1-and-beyond-4f4590cfd5d3 — accessed 2026-09-08.
28. Real Time VFX, "[Niagara 4.25] Ribbon Trail Mini Tutorial". https://realtimevfx.com/t/niagara-4-25-ribbon-trail-mini-tutorial/13043 — accessed 2026-09-08.
29. Jakobsson, S. "GPU Particle System". https://svantejakobsson.com/gpu-particle-system/ — accessed 2026-09-08.
30. Bridson, R., Hourihan, J., Nordenstam, M. "Curl-Noise for Procedural Fluid Flow", SIGGRAPH 2007. https://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph2007-curlnoise.pdf — accessed 2026-09-08 (full text read).
31. Demeulenaere, P. "vfx-neighborhood-grid-3d". https://github.com/pauldemeulenaere/vfx-neighborhood-grid-3d/ — accessed 2026-09-08.
32. "Performance Evaluation of Boids on the GPU and CPU" (BTH thesis). https://www.diva-portal.org/smash/get/diva2:1191916/FULLTEXT01.pdf — accessed 2026-09-08.
33. Macklin, M., Müller, M. "Position Based Fluids", SIGGRAPH 2013. https://dl.acm.org/doi/10.1145/2461912.2461984 — accessed 2026-09-08.
34. Hu, Y. et al. "A Moving Least Squares Material Point Method" (taichi_mpm), SIGGRAPH 2018. https://github.com/yuanming-hu/taichi_mpm — accessed 2026-09-08.
35. Wu, K. et al. "Fast Fluid Simulations with Sparse Volumes on the GPU", CGF 2018. https://onlinelibrary.wiley.com/doi/abs/10.1111/cgf.13350 — accessed 2026-09-08.
36. Codrops, "WebGPU Fluid Simulations" (2025). https://tympanus.net/codrops/2025/02/26/webgpu-fluid-simulations-high-performance-real-time-rendering/ — accessed 2026-09-08.
37. Apple, Metal Feature Set Tables (PDF). https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf — referenced, not fetched, 2026-09-08.
38. Harris, M. "Fast Fluid Dynamics Simulation on the GPU", GPU Gems ch. 38 (ping-pong constraint). https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu — accessed 2026-09-08.
