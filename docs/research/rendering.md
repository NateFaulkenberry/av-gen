# Rendering Backend Research for av-gen

**Status:** research phase, no decision taken. Prepared 2026-09-08.
**Target:** native C++ real-time audiovisual engine (festival/VJ-grade 3D, GPU particles, custom shaders, post-processing, offline deterministic frame rendering).
**Dev machine:** macOS 26, Apple M2 Max (Metal 4), CMake 4.0, Apple clang 21. macOS first; Windows/Linux must stay architecturally possible.

All facts below were checked against primary sources on **2026-09-08** unless explicitly marked otherwise. Release/commit dates were pulled from the GitHub REST API rather than from rendered web pages (two page summaries returned wrong years and were discarded). Where a claim could not be verified it is marked **UNVERIFIED**.

Citation format used throughout: **[Sn]** = entry in the Sources list at the end. Each candidate section carries an "Evidence" block that states, per source: what was learned, why it matters for av-gen, and confidence/limitations.

---

## 0. How to read this document

1. Section 1 turns the project brief into concrete evaluation criteria.
2. Section 2 covers every candidate in the same template so they can be compared line by line.
3. Section 3 covers windowing (GLFW, SDL3) and treats SDL_GPU as a rendering candidate in its own right.
4. Section 4 is the comparison matrix.
5. Section 5 is the shortlist with tradeoffs. It deliberately stops short of a recommendation.
6. Section 6 lists open questions and things we could not verify.
7. Sources are at the end.

---

## 1. What av-gen actually needs from a backend

Derived from the brief. These are the criteria used in the matrix.

| # | Requirement | Why it matters for a VJ/festival engine |
|---|---|---|
| R1 | First-class **Metal** path on macOS (not GL, not emulated) | Apple deprecated OpenGL in 10.14; GL on macOS tops out at 4.1 with no compute. Only Metal (or layers on Metal) is a serious primary path. |
| R2 | **Compute shaders** with storage buffers *and* storage textures, plus indirect dispatch | GPU particle systems, fluid/field sims, histogram/audio-reactive analysis, culling. |
| R3 | **Indirect draw** (ideally multi-draw-indirect with GPU count) | GPU-driven particle/instanced geometry without CPU readback. |
| R4 | **MRT** (>= 4 color targets, ideally 8) and **HDR float targets** (RGBA16F, RGBA32F, RG11B10F) | Deferred/g-buffer style effects, bloom chains, feedback buffers, HDR pipelines. |
| R5 | **Custom shader workflow**: an artist/dev can drop in a new fragment or compute shader with a short iteration loop; runtime (re)compilation strongly preferred | Live-coding style iteration is the norm in VJ tooling. |
| R6 | **Instancing** | Cheap. Every candidate has it; listed for completeness. |
| R7 | **Readback / offline rendering**: render N frames deterministically to disk | Required by the brief. Needs GPU->CPU copy with explicit sync. |
| R8 | **Debugging**: Xcode GPU capture on macOS; RenderDoc on Win/Linux | RenderDoc does **not** support macOS/Metal at all [S40], so Xcode capture is the only macOS option. |
| R9 | **CMake integration** with a clean `add_subdirectory` / `FetchContent` / `find_package` story | CMake 4.0 toolchain; we do not want a second build system. |
| R10 | **Portability path** to Windows/Linux (Vulkan/D3D12) without a rewrite | Architectural requirement. |
| R11 | **Maintenance and ecosystem** (activity in 2026, docs, community) | We will be living with this for years. |
| R12 | **Doesn't fight us**: we need control over passes, resource lifetimes, sync, and the frame graph | A scene-graph/PBR engine that owns the frame is a liability, not an asset. |

---

## 2. Candidates

### 2.1 Raw Metal via metal-cpp (Apple)

**What it is.** Apple's official header-only C++ binding for Metal, Foundation and QuartzCore. It maps 1:1 to the Objective-C API with zero overhead (inlined `objc_msgSend`), so it is "raw Metal" rather than an abstraction [S1][S2].

| Criterion | Finding |
|---|---|
| Latest release / activity | Tags `release/metal-cpp_macOS27_iOS27` (commit dated 2026-06-08), `macOS26.4_iOS26.4` (2026-04-03), `macOS26_iOS26` (2025-06-06). Changelog: Metal 4 support arrived with the macOS 26 drop; the macOS 27 drop "adds all the Metal APIs in macOS 27, iOS 27" [S1][S3]. |
| License | Apache-2.0 [S1]. |
| Platforms | macOS, iOS, iPadOS, tvOS, visionOS. **Apple only.** |
| macOS backend | Metal, natively. Metal 4 is supported on all Apple silicon (M1+/A14+) and is "part of the same Metal framework", adopted incrementally alongside Metal 3 objects [S4]. |
| Compute | Full: compute encoders, storage buffers/textures, indirect dispatch (`dispatchThreadgroupsWithIndirectBuffer`), threadgroup memory, SIMD-group ops, tensors in Metal 4 [S4]. |
| Indirect draw | Full, including Indirect Command Buffers (ICBs) that let a compute kernel encode draws on the GPU. |
| RTT / MRT | Up to 8 color attachments on all Mac GPU families (per Apple's feature set tables; bgfx's Metal backend hardcodes `maxFBAttachments = 8` citing the same tables [S27]). |
| HDR float targets | RGBA16F/RGBA32F/RG11B10F renderable and blendable; EDR output via `CAMetalLayer.wantsExtendedDynamicRangeContent`. (EDR claim from general Apple docs, not re-fetched today: **medium confidence**.) |
| Shader language / toolchain | Metal Shading Language only (MSL spec 4.1 dated 2026-06-04 per Apple's PDF listing [S5]). Offline: `xcrun metal` -> `.metallib`. Runtime: `MTLDevice::newLibrary(source, options, &error)` compiles MSL source at runtime, sync or async [S6]. |
| Custom-shader workflow | Best-in-class on macOS: hand a string of MSL to `newLibrary`, build a pipeline state, done. Metal 4 adds `MTL4Compiler` for explicit control over when/where compilation happens and QoS-aware priority [S4]. Cross-platform shaders would be our problem entirely. |
| Instancing | Yes (`instanceCount`, `baseInstance`). |
| Debugging | Xcode Metal debugger: frame capture, shader debugger, shader profiler, dependency viewer, programmatic capture via `MTLCaptureManager`; Xcode 26 ships a Metal 4 project template and "advanced debugging tools with Metal 4 support" [S4][S7]. RenderDoc: not available on macOS [S40]. |
| CMake | Trivial: `target_include_directories` on the metal-cpp folder, define `NS_PRIVATE_IMPLEMENTATION`/`MTL_PRIVATE_IMPLEMENTATION`/`CA_PRIVATE_IMPLEMENTATION` in one TU, link Foundation/QuartzCore/Metal frameworks [S2]. Requires C++17. Window/layer glue still needs a little Objective-C++ (or the AppKit/MetalKit `metal-cpp-extensions` headers, which Apple only distributes inside the LearnMetalCPP sample download, not in the main repo [S8]). |
| Docs / ecosystem | Apple's Metal docs are excellent but Objective-C/Swift-centric; metal-cpp itself is thin on docs. Notably, **bgfx's Metal backend was ported to metal-cpp** (`src/renderer_mtl.cpp` includes `<metal-cpp/metal.hpp>`) [S27], which is a good real-world reference for driving Metal from C++. |

**Frank assessment.** Zero friction on macOS: every feature on the M2 Max is reachable, the debugging story is the best of any candidate, and runtime MSL compilation makes live shader iteration trivial. The cost is R10: there is no Windows/Linux path without writing a second backend behind our own RHI, and no cross-compilation story for shaders (we would need to adopt one, e.g. author in Slang/HLSL/GLSL and target MSL via SPIRV-Cross or Slang's Metal backend). If the team is willing to write a thin RHI of its own, "Metal now, Vulkan/D3D12 later" is a well-trodden path (The Forge, bgfx, SDL_GPU all do exactly this internally).

**Evidence.**
- [S1] apple/metal-cpp README — learned: header-only, Apache-2.0, C++17, changelog through macOS 27 with Metal 4 added at macOS 26. Relevance: confirms currency and license. Confidence: high (official repo, commit dates via GitHub API).
- [S2] developer.apple.com/metal/cpp — learned: contents (Foundation/Metal/QuartzCore), single-header generator, private-implementation macros, frameworks to link. Confidence: high.
- [S4] WWDC25 "Discover Metal 4" — learned: MTL4CommandQueue/CommandBuffer/CommandAllocator, argument tables, residency sets, MTL4Compiler, M1+/A14+ support, incremental adoption. Relevance: the API we would target on the M2 Max. Confidence: high (official session).
- [S6] Apple docs, `newLibraryWithSource:options:error:` — learned: synchronous runtime MSL compilation; async variant exists. Relevance: R5. Confidence: high.
- [S7] Apple docs, Metal debugger / capturing a Metal workload — learned: capture tooling and MTLCaptureManager. Confidence: medium (page body did not render through the fetch tool; content matched from Apple's documented feature list and the WWDC session).
- [S40] RenderDoc features page — learned: "Currently RenderDoc supports Vulkan, D3D11, D3D12, OpenGL, and OpenGL ES on Windows, Linux, Android, and Nintendo Switch." No macOS, no Metal. Confidence: high.

---

### 2.2 Vulkan (+ MoltenVK / KosmicKrisp on macOS)

**What it is.** Khronos' explicit API. On macOS there is no native Vulkan driver from Apple; Vulkan runs through a layered ICD on Metal. In 2026 there are **two** such ICDs shipped in the LunarG SDK: **MoltenVK** (Khronos, mature, "nearly conformant") and **KosmicKrisp** (Mesa-based, Khronos-conformant, macOS 26 + Apple silicon + Metal 4 only) [S41][S42][S43].

| Criterion | Finding |
|---|---|
| Latest release / activity | MoltenVK **v1.4.2** (2026-07-24; Whats_New says released 2026-07-20), v1.4.1 (2025-11-30), v1.4.0 (2025-08-20, "Add support for Vulkan 1.4"); repo pushed 2026-09-05 with 1.4.3 work (`VK_EXT_multi_draw`, `VK_EXT_nested_command_buffer`) [S41]. Vulkan SDK for macOS **1.4.357.1** (notes dated 2026-08-17) bundling MoltenVK 1.4.2 and KosmicKrisp [S44]. |
| License | MoltenVK Apache-2.0; KosmicKrisp MIT (Mesa). |
| Platforms | Everywhere except Apple natively; Apple via the ICDs above. |
| macOS backend | Metal underneath in both cases. KosmicKrisp: "Metal4 is now mandated ... requires macOS 26 or later ... Moved to Metal4 command buffer encoding ... up to ~2.35 times faster than previous SDK release" [S44]. |
| Compute | Full Vulkan compute incl. indirect dispatch. |
| Indirect draw | `vkCmdDrawIndirect(Count)` supported (Vulkan 1.2 core `drawIndirectCount`; MoltenVK implements via Metal). `VK_EXT_multi_draw` in progress for MoltenVK 1.4.3 [S41]. |
| RTT / MRT / HDR | 8 color attachments guaranteed by Vulkan 1.4 baseline (8K x 8 render targets); float formats standard [S45]. Dynamic rendering (no render-pass objects) is core in 1.3/1.4 and supported by both ICDs. |
| Shader language / toolchain | SPIR-V. Author in GLSL (glslang), HLSL (DXC), or Slang (all ship in the SDK). MoltenVK converts SPIR-V -> MSL via SPIRV-Cross **at pipeline creation**, so pipeline caches matter for hot-reload latency [S42]. |
| Custom-shader workflow | Runtime: compile GLSL/HLSL/Slang -> SPIR-V in-process (glslang/DXC/Slang libraries) then `vkCreateShaderModule`; add the SPIR-V->MSL step's latency on macOS. Fully portable to Win/Linux. |
| Instancing | Yes. |
| Debugging | Xcode GPU capture works on MoltenVK apps (`METAL_CAPTURE_ENABLED=1`, `MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE`, `.gputrace` output; "You do not need to launch your app from Xcode") and, by construction, on KosmicKrisp [S42]. Validation layers, GPU-assisted validation, sync validation and GFXReconstruct from the SDK. **RenderDoc: Windows/Linux/Android only** [S40]. |
| CMake | `find_package(Vulkan)` (CMake's FindVulkan supports MoltenVK on macOS); volk/Vulkan-Headers as submodules; VMA for allocation. Standard, well-documented. |
| Docs / ecosystem | Best documented explicit API in existence; enormous ecosystem. |

**MoltenVK limitations that matter here** (from the official user guide [S42]): exposes `VK_KHR_portability_subset` (requires `VK_KHR_portability_enumeration` + the enumerate-portability instance flag or no device appears); no geometry shaders; tessellation emulated through compute; no triangle fans; `VK_QUERY_TYPE_PIPELINE_STATISTICS` unsupported; no `VK_EXT_shader_object` (only via SDK emulation layer), no `VK_EXT_mesh_shader`, no ray tracing/ray query, no `VK_EXT_descriptor_buffer`. LunarG's own page states MoltenVK "is NOT a fully-conforming Vulkan driver for macOS" [S44]. **`VK_EXT_metal_objects`** is supported and exposes the underlying `MTLDevice`/`MTLTexture`/`MTLCommandQueue`, which is what a Syphon/NDI/AVFoundation bridge would need [S42]. MoltenVK does not use Metal 4 (issue #2560 open, no activity) [S46].

**KosmicKrisp.** Mesa docs: "a Vulkan conformant implementation for macOS on Apple Silicon hardware", requires "macOS 26 and up" and "Metal 4"; no iOS [S43]. Khronos conformant-products entry #958 (2025-10-29): Apple M3 Pro (KosmicKrisp), Vulkan **1.3**, CTS 1.4.3.2 [S47]. LunarG's SDK notes and "State of Vulkan on Apple, Jan 2026" claim Vulkan 1.4 and call KosmicKrisp "the future of Vulkan on Apple", recommending apps ship both ICDs and pick at runtime [S48]. **UNVERIFIED:** a separate Vulkan 1.4 conformance entry on the Khronos registry was not found on 2026-09-08.

**Frank assessment.** Vulkan 1.4-core (dynamic rendering, sync2, timeline semaphores, BDA, push descriptors) is now a *credible* primary path on the M2 Max, and it is the only candidate where the exact same code is the Windows/Linux backend. Costs: Metal is still what you see in Xcode; a translation layer's bug tail (MoltenVK 1.4.2's notes are full of Apple-silicon-specific fixes); SPIR-V->MSL latency at pipeline creation; no mesh/RT; KosmicKrisp is under a year old and macOS 26-only. Apple-specific goodies (Metal 4 residency sets, ICBs, MetalFX, ML encoders, EDR surface control beyond what the swapchain extensions offer) are out of reach. The report's own suggestion is worth recording: an RHI with a Metal backend for macOS and a Vulkan backend for Win/Linux, using MoltenVK/KosmicKrisp mainly to *validate* the Vulkan backend on the dev Mac.

**Evidence.**
- [S41] KhronosGroup/MoltenVK README, releases (GitHub API), `Docs/Whats_New.md` — learned: Vulkan 1.4, release dates, 1.4.2 contents, 1.4.3 in progress. Confidence: high.
- [S42] MoltenVK Runtime User Guide — learned: supported/unsupported extensions, known limitations, Xcode capture env vars, `VK_EXT_metal_objects`, pipeline-cache advice. Confidence: high.
- [S43] Mesa docs, KosmicKrisp driver page — learned: conformant, macOS 26+, Apple silicon, Metal 4, no iOS. Confidence: high.
- [S44] LunarG Vulkan SDK macOS release notes 1.4.357.x — learned: SDK version/date, bundled ICDs, Metal 4 mandated for KosmicKrisp, ~2.35x speedup claim, "NOT a fully-conforming" statement for MoltenVK. Confidence: high.
- [S45] Khronos Vulkan 1.4 press release — learned: 1.4 baseline features/limits. Confidence: high.
- [S46] MoltenVK issue #2560 — learned: Metal 4 adoption not started. Confidence: medium (issue tracker).
- [S47] Khronos conformant products list, entry #958 — learned: KosmicKrisp Vulkan 1.3 conformance. Confidence: high.
- [S48] LunarG announcements (2025-10-30 conformance; Jan-2026 "State of Vulkan on Apple") — learned: positioning and 1.4 claim. Confidence: medium (vendor blog for the 1.4 claim).

---

### 2.3 Direct3D 12 (Windows-only future backend)

Included for the R10 portability discussion only; **no macOS relevance**.

- **Agility SDK** retail **1.619.5** (D3D12SDKVersion 619, 2026-07-30): Shader Model 6.9, DXR 1.2 (Shader Execution Reordering + Opacity Micromaps), revised resource-view APIs. Preview 1.721.3 (2026-07-30): SM 6.10 previews (`linalg::Matrix`), partial programs, UAVs of depth [S49].
- **SM 6.9** went retail 2026-02-26 with DXC 1.9.2602.x: long vectors, mandatory native 16-bit/wave/64-bit ops; Cooperative Vector deprecated in favor of a unified SM 6.10 design [S50].
- **Work graphs** and GPU upload heaps have been retail since Agility 1.613.3 (2024-03-11) [S51].
- **DXC** v1.9.2607 (2026-07-29) is latest retail; DXC also emits SPIR-V, which is the usual way to keep one HLSL codebase across D3D12 and Vulkan [S52].
- Implication: if a Windows backend is ever built, Vulkan is the cheaper first Windows path (single backend for Win+Linux); D3D12 only pays for itself if DXR 1.2 / work graphs are needed. Several abstraction candidates (Diligent, The Forge, NVRHI, SDL_GPU, wgpu, Dawn, bgfx) give D3D12 for free.

**Evidence.** [S49] DirectX Agility SDK download page; [S50] DirectX blog "Shader Model 6.9 retail and more"; [S51] DirectX blog "Agility SDK 1.613.0"; [S52] microsoft/DirectXShaderCompiler releases (GitHub API). All accessed 2026-09-08; confidence high (official Microsoft sources).

---

### 2.4 OpenGL on macOS

- **Deprecated since macOS 10.14 (2018).** Apple: "The APIs in the OpenGL and OpenCL frameworks are deprecated and remain present for compatibility purposes. Transition to Metal" [S53]. Apple-silicon porting guide: "OpenGL is deprecated, but is available on Apple silicon ... Use Metal instead" [S54].
- **Maximum version 4.1 core profile** (Apple support table lists 4.1 as the highest; no source claims newer) [S55]. Therefore: **no compute shaders (4.3), no SSBOs (4.3), no image load/store (4.2), no indirect compute, no bindless**. GLSL 4.10 only.
- **Still present on macOS 26**: Tahoe removed AGL but "OpenGL still remains in the SDK"; works on real Apple silicon (VM-only crashes reported) [S56]. GLFW's compat notes: on modern macOS "OpenGL is implemented on top of Metal and is not fully thread-safe" and only core-profile forward-compatible contexts are available [S57].
- **Debugging**: Xcode's Metal capture does not see GL calls; RenderDoc has no macOS build.
- **Verdict**: not viable as the engine's renderer (fails R1, R2, R3, R8). Only plausible use is a legacy compatibility path (ISF/Shadertoy-style fragment shaders), and even that is better served by translating GLSL to MSL/SPIR-V.

**Evidence.** [S53] macOS Mojave 10.14 release notes; [S54] "Porting your macOS apps to Apple silicon"; [S55] Apple Support HT101525 (archived table, Intel Macs only, max 4.1); [S56] JUCE forum thread quoting Tahoe beta 7 notes (**medium** confidence, second-hand); [S57] GLFW `docs/compat.md` 3.5.1. Accessed 2026-09-08.

---

### 2.5 WebGPU via wgpu-native (gfx-rs)

**What it is.** C bindings (`webgpu.h` + `wgpu.h` extensions) around `wgpu-core`, the Rust WebGPU implementation that powers WebGPU in Firefox, Servo and Deno [S9][S10].

| Criterion | Finding |
|---|---|
| Latest release / activity | wgpu-native **v29.0.1.1** (2026-06-23), v29.0.0.0 (2026-04-10), v27.0.4.1 (2026-04-08), v27.0.4.0 (2025-12-23) [S11]. Upstream wgpu: **v30.0.1** (2026-08-22), v30.0.0 (2026-07-01), v29.x through 2026-07-02; trunk commits on 2026-09-07 [S12]. wgpu does a breaking release every three months; wgpu-native tracks it with a lag of roughly one major version. |
| License | wgpu and wgpu-native: MIT OR Apache-2.0 [S9][S10]. |
| Platforms | Windows, Linux, Android, macOS, iOS, Web. Prebuilt binaries per release for macOS (10.13+), Windows (MSVC/GNU), Linux (manylinux_2_28), Android, iOS [S11]. |
| macOS backend | **Metal, first-class** per the wgpu support table; Vulkan only via MoltenVK; GL only via ANGLE [S9]. |
| Compute | Yes: storage buffers, storage textures, indirect dispatch (WebGPU core). Default WebGPU limits (8 storage buffers / 8 storage textures per stage, 256 invocations per workgroup) can be raised on native by requesting higher limits [S13]. |
| Indirect draw | Core `drawIndirect`/`drawIndexedIndirect`; **native extension** `MultiDrawIndirect(Count)` via `wgpuRenderPassEncoderMultiDrawIndirect*` in `wgpu.h`. `MULTI_DRAW_INDIRECT_COUNT` is documented as DX12 + Vulkan 1.2 only, i.e. **not Metal** [S14][S15]. `INDIRECT_FIRST_INSTANCE` is supported on Metal Apple3+/Mac1+ [S14]. |
| RTT / MRT | WebGPU default `maxColorAttachments = 8` but `maxColorAttachmentBytesPerSample = 32` (i.e. 2x RGBA32F or 4x RGBA16F by default). Higher limits are requestable on native [S13]. |
| HDR float targets | RGBA16F renderable; RGBA32F renderable, with filtering/blending behind `FLOAT32_FILTERABLE` / float32-blendable features (Metal on macOS supports FLOAT32_FILTERABLE) [S13][S14]. wgpu v30.0 added **surface color-space selection** for HDR/wide-gamut swapchains [S12]. |
| Shader language / toolchain | **WGSL** is primary and is compiled by `naga` to MSL/SPIR-V/HLSL at runtime. wgpu-native additionally exposes `WGPUShaderSourceGLSL` (with `#define`s) and `wgpuDeviceCreateShaderModuleSpirV` for GLSL and SPIR-V input [S15]. Native-only passthrough exists (`SPIRV_SHADER_PASSTHROUGH`, plus MSL passthrough in newer versions; mesh shaders require passthrough on non-Vulkan) [S14]. |
| Custom-shader workflow | Good: hand WGSL (or GLSL/SPIR-V via the extension) to `wgpuDeviceCreateShaderModule` at runtime; naga validates and translates. No offline step required. Downside: WGSL is a new language for most shader authors; naga's MSL output is generated code, which affects shader-profiler readability. |
| Instancing | Yes. |
| Debugging | Xcode GPU capture works (it captures any Metal process), but you are debugging naga-generated MSL. `TIMESTAMP_QUERY` and pipeline-statistics queries available natively [S14][S15]. Rich validation errors from wgpu-core. RenderDoc on Win/Linux via Vulkan. |
| CMake | wgpu-native is built with **Cargo** (Rust toolchain required to build from source; Meson file also present) [S10]. Practical options: (a) consume the prebuilt static/shared library per platform via FetchContent, or (b) build from source with Corrosion. Neither is `add_subdirectory`-simple. Rust 1.87+ MSRV [S9]. |
| Docs / ecosystem | wgpu docs are Rust-centric; the C side is documented by examples and the wiki. Large ecosystem (Bevy, Firefox). **Caveat:** webgpu-headers states "wgpu-native does not yet implement the stable version of this header" [S16], so C code written against Dawn's `webgpu.h` may need shims. |

**Known Metal-backend limitations found.** wgpu's Metal backend does not yet use argument buffers, which caps read-write texture bindings at 8 direct bindings and blocks some `binding_array` cases on Metal [S17]. Naga's MSL backend imposes loop iteration limits to avoid UB [S17]. These are edge cases, not blockers, but they matter for bindless-style designs.

**Frank assessment.** Very capable and actively maintained, with a first-class Metal backend and a genuine cross-platform story (Vulkan/D3D12 for R10). The WebGPU model (bind groups, explicit pipelines, validation) is a good match for an engine RHI. Friction points: Rust toolchain in the build, WGSL as the shader language (mitigable via naga's GLSL/SPIR-V front-ends or Slang's WGSL target), C header drift versus Dawn, default limits that need raising, and no multi-draw-indirect-count on Metal. It would not "fight" an AV engine; it would occasionally under-expose Metal.

**Evidence.**
- [S9] gfx-rs/wgpu README — learned: backend table (Metal first-class on macOS), MSRV 1.87, quarterly breaking releases, used by Firefox/Servo/Deno. Confidence: high.
- [S10] gfx-rs/wgpu-native README — learned: Cargo build, `ffi/webgpu-headers/webgpu.h` + `ffi/wgpu.h`, MIT/Apache, prebuilt binaries. Confidence: high.
- [S11] wgpu-native releases (GitHub API) — learned: v29.0.1.1 on 2026-06-23 with macOS/Windows/Linux/Android/iOS assets. Confidence: high.
- [S12] wgpu releases (GitHub API + release notes) — learned: v30.0.1 2026-08-22; v30 added surface color-space selection; v28 added mesh shaders. Confidence: high.
- [S13] W3C WebGPU spec limits — learned: default limits listed above. Confidence: high.
- [S14] docs.rs wgpu `Features` — learned: platform support for indirect count, binding arrays, float32 filterable, timestamp queries, mesh shaders. Confidence: high (official docs).
- [S15] wgpu-native `ffi/wgpu.h` (trunk) — learned: GLSL/SPIR-V shader source entry points, `WGPUNativeFeature_*` flags, multi-draw-indirect functions. Confidence: high.
- [S16] webgpu-native/webgpu-headers README — learned: wgpu-native "does not yet implement the stable version" of `webgpu.h`. Confidence: high.
- [S17] wgpu issues #4491 (argument buffers on Metal), #6744 (binding_array of storage textures on Metal), #6546 (naga MSL loop limits) — learned: open Metal-backend limitations. Confidence: medium (issue tracker; status could change).

---

### 2.6 WebGPU via Dawn (Google)

**What it is.** Google's C++ WebGPU implementation (Chromium's) with backends for D3D11/D3D12, Metal, Vulkan, OpenGL/GLES and Null, plus **Tint**, the WGSL compiler that reads WGSL/SPIR-V and writes MSL/HLSL/SPIR-V/GLSL [S18].

| Criterion | Finding |
|---|---|
| Latest release / activity | Commits daily (2026-09-08: vulkan-deps and ANGLE rolls, indirect-dispatch validation work) [S19]. **Dawn now publishes date-tagged GitHub releases every few days** (e.g. `v20260907.201642` on 2026-09-08) with prebuilt archives for macOS (arm64 and Intel, Debug/Release), an **Apple xcframework**, Ubuntu, Windows, Android, a headers tarball and an `emdawnwebgpu` package [S20]. This is new relative to the "no releases, clone with depot_tools" reputation. |
| License | BSD-3-Clause [S18]. |
| Platforms | Windows, macOS, Linux, Android, ChromeOS, iOS, Web (via Emscripten). |
| macOS backend | **Metal**, the same backend that ships in Chrome on macOS. |
| Compute | Full WebGPU compute (same core as wgpu). Dawn-specific extras include `subgroup_matrix`, `timestamp_query_inside_passes`, `pixel_local_storage`, `framebuffer_fetch` [S21]. |
| Indirect draw | Core indirect; **`multi_draw_indirect` feature** with `MultiDrawIndirect`/`MultiDrawIndexedIndirect` and a GPU count buffer; docs say "most desktop GPUs support this feature" but do **not** state Metal explicitly [S22]. **UNVERIFIED** whether the Metal backend implements the count variant. |
| RTT / MRT / HDR | Same WebGPU limits as wgpu (8 color attachments, 32 bytes/sample default) [S13]. Dawn adds `dawn_load_resolve_texture`, `transient_attachments`, `msaa_render_to_single_samples`, `norm16_texture_formats`, `format_capabilities` [S21]. |
| Shader language / toolchain | WGSL. SPIR-V input via Tint's SPIR-V reader only if built with `TINT_BUILD_SPV_READER` [S23]. No GLSL front-end (Tint reads WGSL and SPIR-V). |
| Custom-shader workflow | Same as wgpu: WGSL string in at runtime. Tint's diagnostics are good. `shader_module_compilation_options` feature exists [S21]. |
| Instancing | Yes. |
| Debugging | Xcode GPU capture (Tint-generated MSL). Dawn has a "toggles" system and very thorough validation. RenderDoc via Vulkan on Win/Linux. |
| CMake | Supported: `cmake -DDAWN_FETCH_DEPENDENCIES=ON -DDAWN_ENABLE_INSTALL=ON`, then `find_package(Dawn)` and link `dawn::webgpu_dawn` (monolithic library). depot_tools is **not required**; a Python script fetches deps. Requires CMake >= 3.16, **C++20**, Python; macOS needs Xcode 12.2+ [S24][S25]. First build is heavy (Tint, SPIRV-Tools, Abseil, etc.), but the new prebuilt release archives sidestep that entirely. |
| Docs / ecosystem | The Chrome/WebGPU ecosystem is huge, but native-Dawn docs are thin beyond the quickstart; expect to read `dawn.json` and the feature docs. Google states it is "not an officially supported Google product" [S18]. |

**Frank assessment.** Dawn is arguably the most conformant native WebGPU and its Metal backend is battle-tested by Chrome on every Mac. The build-complexity objection has largely evaporated in 2026 thanks to the prebuilt release archives and the depot_tools-free CMake flow; the C++20 requirement is fine with Apple clang 21. Remaining friction: WGSL-only front door (SPIR-V reader optional, no GLSL), API surface still moving (the `webgpu.h` header is "stable" but Dawn adds extensions constantly), no real versioning beyond date tags, and the same WebGPU-imposed ceiling as wgpu (no direct ICBs, no Metal 4 residency sets, etc.). It would not fight an AV engine; it would make you live inside WebGPU's feature envelope.

**Evidence.**
- [S18] google/dawn README — learned: backends, BSD-3, Tint description, mirror of googlesource. Confidence: high.
- [S19] google/dawn commits (GitHub API) — learned: daily activity through 2026-09-08. Confidence: high.
- [S20] google/dawn releases (GitHub API) — learned: date-tagged releases with 13 assets each, including macOS and Apple xcframework builds. Confidence: high.
- [S21] dawn `docs/dawn/features/` listing — learned: native feature docs incl. multi_draw_indirect, subgroup_matrix, pixel_local_storage, transient_attachments. Confidence: high.
- [S22] dawn `multi_draw_indirect.md` — learned: API and semantics; backend list not explicit. Confidence: medium for Metal support.
- [S23] Dawn mailing list "Loading Spirv shaders" + Tint SPIR-V reader docs — learned: `TINT_BUILD_SPV_READER` required for SPIR-V input. Confidence: medium.
- [S24] dawn `docs/quickstart-cmake.md` — learned: DAWN_FETCH_DEPENDENCIES, DAWN_ENABLE_INSTALL, find_package flow, C++20. Confidence: high.
- [S25] dawn `docs/building.md` — learned: GN+depot_tools is primary, CMake supported, macOS Xcode 12.2+. Confidence: high.

---

### 2.7 bgfx (Branimir Karadzic)

**What it is.** A long-lived "bring your own engine" rendering library with a submit-sorted "views" model and eight backends [S26].

| Criterion | Finding |
|---|---|
| Latest release / activity | No tagged releases; `BGFX_API_VERSION 159`; commits on 2026-09-07 (GL/WebGL fix), 2026-09-06 (Metal external device fix, D3D11/12 swapchain fixes) [S26][S27]. bgfx.cmake bumped to latest bgfx on 2026-09-08 [S28]. |
| License | BSD-2-Clause [S26]. |
| Platforms | Android, iOS/tvOS 16+, Linux, macOS 13+, PS4 (NDA), RPi, UWP/Xbox, WebAssembly, Windows 7+ [S26]. |
| macOS backend | **Metal** (now implemented with metal-cpp in `renderer_mtl.cpp`); OpenGL also listed. Vulkan via MoltenVK is plausible but not documented as a macOS path (**UNVERIFIED**). |
| Compute | `BGFX_CAPS_COMPUTE` is set unconditionally in the Metal backend; `BGFX_CAPS_IMAGE_RW` when the device reports a read-write texture tier [S27]. `bgfx::dispatch` with compute buffers/images; indirect dispatch supported per docs [S29]. |
| Indirect draw | `BGFX_CAPS_DRAW_INDIRECT` on Metal for Apple3+/Mac2 families. **No `BGFX_CAPS_DRAW_INDIRECT_COUNT` on Metal**; the Metal backend issues one `drawIndexedPrimitives(... indirectBuffer ...)` per command in a CPU loop [S27]. |
| RTT / MRT | Up to `BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS = 8`; Metal backend sets `maxFBAttachments = 8` [S27][S30]. |
| HDR float targets | RGBA16F, RGBA32F, RG11B10F in `TextureFormat`; framebuffer capability flags per format at runtime [S29]. |
| Shader language / toolchain | **bgfx's own GLSL-like dialect** with `varying.def.sc`, `$input/$output`, `SAMPLER2D`, `mul()`, `vec4_splat()` macros; compiled **offline by `shaderc`** to GLSL/ESSL/HLSL/SPIR-V/**Metal**/PSSL [S31]. shaderc bundles fcpp, glslang, SPIRV-Cross. |
| Custom-shader workflow | Weakest of the finalists for live iteration: shaders must pass through shaderc (it can be invoked at runtime as a library, but this is not the supported path) and be written in the bgfx dialect. bgfx.cmake's `bgfx_compile_shaders()` automates build-time compilation for all targets [S28]. |
| Instancing | `InstanceDataBuffer`, `setInstanceCount`, `BGFX_CAPS_INSTANCING` [S29]. |
| Debugging | Xcode GPU capture works; `BGFX_CAPS_GRAPHICS_DEBUGGER` detection and RenderDoc integration on Win/Linux; built-in debug text/stats/profiler [S29]. |
| CMake | Official build is **GENie**; the community-maintained **bgfx.cmake** (CC0) wraps bgfx/bimg/bx with `add_subdirectory`, builds shaderc/texturec and supplies shader-compile helpers, tested on macOS/Xcode [S28]. |
| Docs / ecosystem | Sphinx docs, 50+ examples (including compute, indirect, MRT, tessellation), used in shipping games. Sole maintainer, but continuous. |

**Frank assessment.** Mature and stable, and its Metal backend is being actively modernized (metal-cpp port, external-device support). The submit/views model is extremely convenient for a compositing-style engine and offline rendering (render to view targets, `readTexture`). However bgfx imposes: an offline shader dialect and compiler (poor fit for R5), no multi-draw-indirect-count on Metal, no explicit control over barriers/synchronization (bgfx handles it), and a legacy-flavored binding model. It would not "fight" us on rendering features, but it would fight the live-shader workflow.

**Evidence.**
- [S26] bkaradzic/bgfx README + overview docs — learned: backends, platforms, license, GENie. Confidence: high.
- [S27] bgfx `src/renderer_mtl.cpp` / `renderer_mtl.h` (raw source, master) — learned: metal-cpp usage, caps set on Metal (COMPUTE, DRAW_INDIRECT on Apple3/Mac2, IMAGE_RW tiered, `maxFBAttachments = 8`), per-draw indirect loop. Confidence: high (read the code). Commit dates via GitHub API.
- [S28] bkaradzic/bgfx.cmake README — learned: add_subdirectory flow, `bgfx_compile_shaders`, CC0, macOS tested; bumped 2026-09-08. Confidence: high.
- [S29] bgfx API reference — learned: dispatch/indirect/MRT/format/instancing/readback/debug flags. Confidence: high.
- [S30] bgfx `src/config.h` — learned: `BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS 8`, `BGFX_CONFIG_MAX_VIEWS 256`. Confidence: high.
- [S31] bgfx tools docs (shaderc) — learned: dialect, profiles, macros. Confidence: high.

---

### 2.8 sokol_gfx (Andre Weissflog)

**What it is.** A single-header C graphics API abstraction with backends for GL 4.1/GLES3, D3D11, Metal, WebGPU and (since Dec 2025) experimental Vulkan [S32][S33].

| Criterion | Finding |
|---|---|
| Latest release / activity | No releases; rolling `master`. Changelog is dense: 2026-09-07 (bindings), **2026-09-06 new `sokol_cmdbuf.h`** (record/replay of in-pass calls), 2026-08-30 write-transient resource update API (breaking), 2026-08-09 "unsealed" immutable resources, 2026-07-02 HDR/sRGB/transparent swapchain options in sokol_app (HDR = RGBA16F, Metal + WebGPU only) [S34]. |
| License | zlib [S32]. |
| Platforms | macOS, iOS, Windows, Linux, Android, Web (WASM). |
| macOS backend | **Metal** (Objective-C++ implementation file required) or GL 4.1 [S32]. Vulkan backend is Linux-only tested and uses `EXT_descriptor_buffer`; MoltenVK not mentioned [S33]. |
| Compute | **Added 2025-03-08** ("compute milestone 1"): compute passes, storage buffers; **2025-05 milestone 2** added storage images (compute writes to `sg_image`, max 4 storage attachments per pass) and multi-purpose buffer usage flags. Feature flag `sg_features.compute`. Supported on Metal, D3D11, GL 4.3, WebGPU; not on macOS GL [S35][S36]. Readback to CPU was explicitly listed as missing in March 2025 and no readback API appears in today's header (**grep for readback/copy-to-CPU in `sokol_gfx.h` found nothing**) [S37]. |
| Indirect draw | **None.** The word "indirect" does not appear anywhere in today's `sokol_gfx.h` (28,564 lines); the draw API is `sg_draw` / `sg_draw_ex` (base vertex/instance) and `sg_dispatch(x,y,z)` [S37]. The only "indirect" hit online is a third-party GL extension header. |
| RTT / MRT | `SG_MAX_COLOR_ATTACHMENTS = 8` with per-target blend/write-mask feature flags [S37]. |
| HDR float targets | RGBA16F, RGBA32F, RG11B10F pixel formats; HDR swapchain via sokol_app on Metal/WebGPU [S37][S34]. |
| Shader language / toolchain | Backend-native shader source/bytecode passed to `sg_make_shader` (Metal accepts MSL source or metallib; docs say MSL "metal-1.1" is the tested dialect) with explicit per-backend bind-slot mapping. The official cross-compiler **sokol-shdc** takes Vulkan-GLSL 450 with `@` annotations and emits MSL/HLSL/GLSL/WGSL/SPIR-V plus C headers with reflection; it is **offline only** [S38]. |
| Custom-shader workflow | Two-tier: (a) offline sokol-shdc for portable shaders; (b) on macOS you *can* hand raw MSL source to `sg_make_shader` at runtime, since Metal compiles source, but you then own the bind-slot mapping and portability. |
| Instancing | Yes (`num_instances`, `base_instance`). |
| Debugging | Xcode GPU capture works; `sokol_gfx_imgui.h` gives a live resource/call inspector; Vulkan backend has no RenderDoc support yet [S33]. |
| CMake | Header-only; add the directory, compile one `.mm` on macOS with `SOKOL_METAL`. Effectively zero build cost. Author uses fips, not CMake, but that is irrelevant to consumers. |
| Docs / ecosystem | Header docs are excellent; samples for every feature; language bindings for Zig/Odin/Nim/Rust/D/Jai/C3. Single maintainer with very high velocity and frequent **breaking** API changes (three in the last two months) [S34]. |

**Frank assessment.** Delightful to build with and now has real compute, but it is explicitly a *simple* abstraction: no indirect draw/dispatch, no GPU->CPU readback API (R7 would need backend-native escape hatches), a hard cap of 4 storage-image attachments per compute pass, and rapid breaking changes. For a GPU-driven particle engine this means fighting the framework at exactly the points that matter (R3, R7). Excellent for prototypes and tooling; risky as the sole foundation.

**Evidence.**
- [S32] floooh/sokol README — learned: headers, backends, zlib, Metal .mm requirement. Confidence: high.
- [S33] floooh blog, "The experimental Sokol Vulkan backend" (2025-12-01) — learned: Vulkan backend scope/limits, no RenderDoc, no MoltenVK mention. Confidence: high.
- [S34] sokol CHANGELOG.md (raw, master) — learned: 2026 changes listed above. Confidence: high.
- [S35] floooh blog, "sokol-gfx compute shader update" (2025-03-03) — learned: compute merged 2025-03-08, initial limitations (no storage textures, no readback). Confidence: high.
- [S36] floooh blog, "compute milestone 2" (2025-05-19) — learned: storage images, 4 attachment cap, buffer usage flags. Confidence: high.
- [S37] `sokol_gfx.h` raw header (master, 2026-09-08) — learned: zero "indirect" matches, limits, formats, feature struct, backend defines incl. SOKOL_VULKAN. Confidence: high (grepped the file).
- [S38] sokol-tools `docs/sokol-shdc.md` — learned: GLSL 450 input, output targets, glslang/SPIRV-Cross/Tint pipeline, offline-only. Confidence: high.

---
### 2.9 Filament (Google)

**What it is.** A physically based real-time renderer (scene, lights, materials, post stack) with Metal, Vulkan, OpenGL, WebGPU and WebGL backends [S73].

| Criterion | Finding |
|---|---|
| Latest release / activity | **v1.76.0 (2026-08-28)**, v1.75.1 (2026-08-24), v1.75.0 (2026-08-04); a release every 1-3 weeks; commits 2026-09-04 [S73]. |
| License | Apache-2.0. |
| Platforms | Android, iOS, Linux, macOS, Windows, WASM. |
| macOS backend | **Metal** (default), Vulkan via MoltenVK (`VulkanPlatformApple.mm` uses `vkCreateMetalSurfaceEXT`), OpenGL 4.1, and WebGPU via Dawn [S74]. |
| Compute | **Not exposed to users.** The backend has `ShaderStage::COMPUTE`, `MaterialDomain::COMPUTE`, SSBO bindings (`MAX_SSBO_COUNT = 4`), but maintainer statement on issue #7995 (2024-07-26, still open, "low priority"): "We don't support compute shaders yet (even if some work in that direction exists)" [S75]. |
| Indirect draw | No public API found [S76]. |
| RTT / MRT | `RenderTarget::Builder` with up to 8 color attachments, MSAA, `View::setRenderTarget`; caveats around clear/post-processing when using custom targets [S77]. |
| HDR float targets | HDR/linear pipeline, bloom, many tone mappers; float formats via `backend::TextureFormat` (**not re-verified from the enum this session**). |
| Shader language / toolchain | `.mat` material files in a GLSL-flavored material language, compiled by `matc` (glslang, SPIRV-Cross, SPIRV-Tools, smol-v vendored) into a multi-backend package; runtime compilation via `libfilamat` [S78]. |
| Custom-shader workflow | Surface materials: fine. **Custom post-processing passes: "not directly possible"** (maintainer, discussion #7676, 2024-03-17); workaround is a second View rendering a quad from an offscreen target; no frame-graph hook as of Nov 2025 [S79]. |
| Instancing | `RenderableManager::Builder::instances()` up to 32,767 with `getInstanceIndex()`; automatic per-instance transforms capped at 64 [S80]. |
| Debugging | Metal driver has `startCapture/stopCapture` via `MTLCaptureManager` writing `filament.gputrace`, group markers, os_signpost [S81]. |
| CMake | CMake >= 3.22.1, **Clang >= 17 mandatory**, Ninja; 36 vendored third-party dirs including Dawn; self-contained but very large [S74]. |
| Offline rendering | Headless swapchains, `SwapChain::CONFIG_READABLE`, `Renderer::readPixels`, `setMaterialTimeEpoch`, `flushAndWait`, `renderStandaloneView`; golden-image CI [S82]. |

**Frank assessment.** Superb PBR and tooling, but the renderer is closed: no compute, no indirect, no post-process hook, no SSBO access. A GPU-particle/feedback-buffer engine would fork it or work around it constantly. Better used *inside* our frame for lit content than as the foundation.

**Evidence.** [S73] google/filament releases/commits; [S74] `BUILDING.md`, README backends list, `VulkanPlatformApple.mm`; [S75] issue #7995 and `MaterialEnums.h`/`DriverEnums.h`; [S76] `RenderableManager.h` and header search (absence); [S77] `RenderTarget.h`, `View.h`; [S78] Materials guide, `third_party/` listing; [S79] discussion #7676; [S80] `RenderableManager.h`; [S81] `MetalDriver.mm`; [S82] `Engine.h`, `SwapChain.h`, `Renderer.h`. Accessed 2026-09-08; confidence high except the float-format enum (medium).

---

### 2.10 OGRE-Next (OGRECave)

**What it is.** The modern branch of OGRE: full scene-graph engine with the Hlms shader system and a compositor that scripts every pass [S83].

| Criterion | Finding |
|---|---|
| Latest release / activity | **v3.0.0 "Eris" (2024-10-15)**; no 3.0.x patch tags; master is labeled **"4.0.0unstable"** with breaking Hlms changes; commits 2026-09-08 [S83][S84]. |
| License | MIT. |
| Platforms | Windows, Linux, macOS, iOS, Android. |
| macOS backend | **Metal** (recommended, built by default); GL3+ optional; **Vulkan has no Apple branch** in its CMake and MoltenVK is not mentioned [S85]. |
| Compute | **Yes, first-class**: `HlmsComputeJob` with UAV textures/buffers, thread-group control, `pass compute` in the compositor which also handles UAV barriers; tutorials `TutorialCompute01/02`, `TutorialUav01/02` [S86]. |
| Indirect draw | Yes: `VaoManager::createIndirectBuffer`, render queue draws through indirect buffers, `MetalRenderSystem::_render` implemented over them [S87]. |
| RTT / MRT / HDR | Compositor-declared textures (e.g. `PFG_RGBA16_FLOAT`), MRT with per-attachment load/store, `render_quad`, HDR/SMAA/SSAO/SSR showcases [S88]. |
| Shader language / toolchain | **Hand-written Hlms templates per API: `.glsl`, `.hlsl`, `.metal`** glued by the Hlms preprocessor; Vulkan compiles GLSL to SPIR-V at runtime via shaderc. **No single-source cross-compiler**; a custom effect needs MSL + GLSL (+ HLSL) [S89]. |
| Custom-shader workflow | Four escalating paths (piece overrides, `HlmsListener`, subclass `HlmsPbs`, own Hlms); low-level `.material` scripts with raw MSL/GLSL recommended for post effects and quick iteration [S89]. |
| Instancing | Automatic batching via Hlms/RenderQueue (medium confidence). |
| Debugging | `startGpuDebuggerFrameCapture`/`endGpuDebuggerFrameCapture` for RenderDoc or Metal debugger; `loadRenderDocApi`; debug annotations [S90]. |
| CMake | Separate `ogre-next-deps` repo built Debug+Release with the Xcode generator and copied into the tree; then `cmake -G Xcode`; known static/Metal issue #129; clunkiest setup of the engines [S91]. |
| Offline rendering | `Tutorial03_DeterministicLoop`, `Tutorial_EglHeadless`, `AsyncTextureTicket` readback [S92]. |

**Frank assessment.** The only high-level engine that has every capability we need (native Metal, compute with UAVs, indirect, scripted MRT/HDR, capture hooks, deterministic-loop tutorials). The price: a full scene-graph engine with strong conventions, three shader dialects to maintain per effect, no MoltenVK path (Win/Linux = D3D11/Vulkan with separate shader text), a "4.0 unstable" master, and a small community. We would not fight it on features; we would fight its conventions and shader duplication.

**Evidence.** [S83] ogre-next releases/tags/commits (GitHub API); [S84] "What's new in 4.0" doc page; [S85] `RenderSystems/Vulkan/CMakeLists.txt`, macOS setup page; [S86] `HlmsComputeJob` docs, compositor docs, Samples/2.0/Tutorials; [S87] `VaoManager` docs, `OgreRenderQueue.cpp`; [S88] compositor docs, Samples/2.0/Showcase; [S89] Hlms docs, `VulkanProgram` docs, ogre-next-deps; [S90] `RenderSystem` docs, `CMake/Dependencies.cmake`; [S91] macOS setup page; [S92] `AsyncTextureTicket` docs. Accessed 2026-09-08. Confidence high except Metal-compute statement (implied by `.metal` templates, not read verbatim) and instancing rules (medium).

---

### 2.11 Diligent Engine

**What it is.** A modern cross-platform low-level graphics library (D3D11/D3D12/Vulkan/GL/WebGPU/Metal) with HLSL as the universal shader language [S93].

| Criterion | Finding |
|---|---|
| Latest release / activity | **v2.5.6 (2024-09-02)** is the last tag; master is very active (512 DiligentCore commits in 2026; pushed 2026-09-08; API version tags like `API256019`) [S93]. |
| License | Apache-2.0 for the open-source parts. |
| Platforms | Windows, UWP, Linux, Android, macOS, iOS, tvOS, visionOS, Web. |
| macOS backend | **The Metal backend is closed-source / commercial.** `Graphics/GraphicsEngineMetal/` holds only interface headers and a readme: "Implementation of Metal backend is available for commercial clients." Open-source macOS options: OpenGL 4.1, **Vulkan via MoltenVK**, WebGPU via Dawn [S94]. |
| Compute / indirect | `DispatchCompute(Indirect)`, `DrawIndirect`, `DrawIndexedIndirect`, `MultiDraw(Indexed)` (v2.5.5), mesh shaders + indirect, `DispatchTile` [S95]. |
| RTT / MRT / HDR | `SetRenderTargets` with RTV arrays, render passes; RGBA32F/RGBA16F/R11G11B10F formats [S95]. MRT cap not captured (assume 8; **UNVERIFIED**). |
| Shader language / toolchain | Same HLSL on all backends; `SHADER_SOURCE_LANGUAGE_{HLSL,GLSL,MSL,WGSL,...}`; DXC, glslang, SPIRV-Cross, SPIRV-Tools vendored; HLSL->SPIR-V->MoltenVK on open-source macOS [S96]. |
| Custom-shader workflow | Excellent: `ShaderCreateInfo` from source string/file/bytecode, async compile, `RenderStateCache` with `EnableHotReload` + `Reload`, JSON render-state notation [S97]. |
| Debugging | Performance guide lists RenderDoc, Nsight, RGP, PIX and "XCode - GPU profiler & debugger for Metal API (including Vulkan on top of Metal)"; debug groups; PIX runtime option [S98]. |
| CMake | `add_subdirectory(DiligentCore)` documented; CMake 3.20+, Python 3; heavy in-tree deps (DXC, glslang, SPIRV-Tools, Dawn, abseil; `DILIGENT_NO_WEBGPU` to trim) [S93]. |
| Docs / ecosystem | Best documentation of the RHI-style libraries: 30 tutorials (instancing, RT, compute, bindless, mesh shaders, ray tracing, state cache, post-processing), DiligentFX, DiligentTools [S93]. |

**Frank assessment.** Feature-complete and superbly documented, with the best runtime-HLSL + hot-reload story of any abstraction. But on our primary target the open-source build gives MoltenVK, not native Metal, unless we license the Metal backend. It is a strong foundation *if* "Vulkan-on-Metal on macOS" (or paying) is acceptable; otherwise it fails R1.

**Evidence.** [S93] DiligentEngine/DiligentCore READMEs, releases and commits (GitHub API); [S94] `Graphics/GraphicsEngineMetal/readme.md` and directory listing; [S95] `DeviceContext.h`, `GraphicsTypes.h`; [S96] `Shader.h`, `ThirdParty/` listing, `Graphics/ShaderTools/src`; [S97] `RenderStateCache.h`; [S98] `doc/PerformanceGuide.md`. Accessed 2026-09-08; confidence high.

---

### 2.12 The Forge (The Forge Interactive)

| Criterion | Finding |
|---|---|
| Latest release / activity | GitHub: **v1.63 (2025-03-21)**; only two 2026 commits, both README edits (2026-08-27) pointing to Codeberg. Codeberg: **Release 1.64 (2026-08-12)**, repo created 2026-08-14, 12 commits, no tags. Policy: development happens on an internal server; releases are periodic code drops [S99][S100]. |
| License | Apache-2.0; consoles need a commercial license; no CLA/CONTRIBUTING found. |
| macOS backend | README claims macOS 11/14 Metal (Intel + Apple silicon), but **the current 1.64 tree is "PC / DirectX 12 runtime" only**: `Common_3/Graphics/` = `Direct3D12, FSL, ...`, `Common_3/OS/` = `Windows`; no Metal or Vulkan directories. Metal exists only in the frozen GitHub v1.63 (`MetalRenderer.mm`, Xcode projects, targets Xcode 14.3/15.0, macOS 11/14) [S100]. |
| Compute / indirect / MRT | `cmdDispatch`, `cmdExecuteIndirect` with count buffer, `cmdDispatchWorkgraph`, `MAX_RENDER_TARGET_ATTACHMENTS = 8`; 4M+ GPU particle and visibility-buffer showcases [S101]. |
| Shader language / toolchain | **FSL** (HLSL superset with SRT macros `BEGIN_SRT`, `DECL_TEXTURE`, ...) translated by a **Python** tool that invokes DXC / glslangValidator / `xcrun metal`; VS custom build step or Xcode pre-build; "Shader Server" hot reload through the same pipeline [S102]. |
| CMake | **None.** Visual Studio, Xcode and CodeLite projects only [S100]. |
| Debugging | GPU breadcrumbs, Microprofiler, remote UI; Xcode capture works on any Metal app. |

**Frank assessment.** A production renderer with excellent GPU-driven features, but for a macOS-first, CMake-based, live-shader engine in 2026 it is a poor fit: the only public macOS code is an 18-month-old frozen snapshot, the current drop is Windows-only, there is no CMake, and FSL is a mandatory dialect with its own resource-table macros and Python/xcrun pipeline. Closed development means no upstream patching. Ruled out for our constraints.

**Evidence.** [S99] ConfettiFX/The-Forge releases/commits (GitHub API); [S100] Codeberg The-Forge/The-Forge README, contents and commits API; [S101] `Common_3/Graphics/Interfaces/IGraphics.h` @v1.63; [S102] FSL Programming Guide wiki, `Common_3/Tools/ForgeShadingLanguage/compilers.py`. Accessed 2026-09-08; confidence high.

---

### 2.13 LLGL (Lukas Hermanns)

| Criterion | Finding |
|---|---|
| Latest release / activity | **Release-v0.04b (2025-07-08)**; README says 0.05 Beta in progress; **217 commits in 2026**, pushed 2026-09-07 (Metal backend and example-shader work) [S103]. Appears to be a single maintainer (**not verified via contributor stats**). |
| License | BSD-3-Clause. |
| Platforms | Windows (D3D12/D3D11/Vulkan/GL), Linux (Vulkan/GL), **macOS (Metal/GL/Vulkan-experimental)**, iOS, Android, UWP, WASM [S103]. |
| macOS backend | **Open-source native Metal** (`sources/Renderer/Metal/*.mm`, on by default on Apple); Vulkan via MoltenVK behind `LLGL_BUILD_RENDERER_VULKAN` ("experimental"), sets the portability-enumeration bit; macOS CI builds GL+Metal+Vulkan [S104]. |
| Compute / indirect | `Dispatch`, `DispatchIndirect`, `DrawIndirect`, `DrawIndexedIndirect`, `DrawInstanced`; all implemented in the Metal backend (`dispatchThreadgroupsWithIndirectBuffer:`); Metal gaps: tessellation with indirect args and stream-output trap [S105]. |
| RTT / MRT / HDR | `RenderTarget`/`RenderPass` with multiple color attachments; `RGBA16Float`, `RGBA32Float`, `RG11B10Float` [S105]. MRT cap not captured (**UNVERIFIED**). |
| Shader language / toolchain | **No cross-compiler in the library**: supply native source/bytecode per backend (HLSL, GLSL, SPIR-V, MSL). Metal accepts MSL source at runtime (`newLibraryWithSource:`) or metallib. New Aug-2026 offline script `TranslateShaders.py` (HLSL -> DXC -> SPIR-V -> SPIRV-Cross -> MSL/GLSL) for the examples [S106]. |
| Custom-shader workflow | Runtime MSL/HLSL/GLSL strings work; Vulkan needs SPIR-V so you add glslang/DXC yourself. Binding via `PipelineLayoutDescriptor` (string-parseable). |
| Debugging | `RenderingDebugger` validation/profiling layer; no RenderDoc/Xcode mentions in the repo (Xcode capture works regardless) [S103]. |
| CMake | CMake >= 3.12, per-backend options, tiny in-tree deps (GaussianLib, GL headers, SPIRV-Headers, stb); vcpkg port; static-lib cyclic-dependency caveat [S103]. |
| Docs / ecosystem | `docu/` + `refman.pdf`, ~30 examples, 2.6k stars; API churn between 0.04 and 0.05. |

**Frank assessment.** The only RHI-style library here that gives open-source native Metal *and* Vulkan/D3D12 behind one small BSD CMake package, with compute, indirect, float targets and runtime MSL. It is thin (no automatic barriers/state tracking to speak of, no bindless, no mesh/RT), ships no shader toolchain, and rests on one maintainer with a beta API. Workable as a base if we bring our own shader pipeline and accept the maintenance risk.

**Evidence.** [S103] LukasBanana/LLGL README, releases and commits (GitHub API), `CMakeLists.txt`; [S104] `sources/Renderer/Metal/`, `VKRenderSystem.cpp`, `BuildMacOS.command`, CI; [S105] `include/LLGL/CommandBuffer.h`, `Format.h`, `MTDirectCommandBuffer.mm`; [S106] `MTShader.mm`, `scripts/TranslateShaders.py`. Accessed 2026-09-08; confidence high.

---

### 2.14 NVRHI (NVIDIA)

| Criterion | Finding |
|---|---|
| Latest release / activity | **No releases or tags**; 104 commits in 2026, pushed 2026-08-25 (Vulkan cluster, SRV swizzle, RT position fetch) [S107]. |
| License | MIT + NVIDIA CLA for contributions. |
| Platforms / backends | D3D11, D3D12, Vulkan 1.3; "Windows (x64 only) and Linux (x64 and ARM64)". **No Metal backend, no macOS support**: zero Apple paths/`__APPLE__` in the tree, no `APPLE` CMake branch, CI on Windows/Ubuntu only, issue #68 "Metal support?" (2025-04-11) open with no maintainer response [S107][S108]. |
| Features | `drawIndirect`, `drawIndexedIndirectCount`, `dispatchIndirect`, mesh dispatch (+indirect/count), `c_MaxRenderTargets = 8`, RGBA16F/RGBA32F, bindless tables, push constants, automatic barriers, ray tracing, validation layer [S109]. |
| Shader language / toolchain | **Bytecode only** (DXBC/DXIL/SPIR-V); ShaderMake offline (FXC/DXC/Slang); no MSL output [S110]. |
| CMake | Cleanest of the RHI group: submodule + `add_subdirectory(nvrhi)`, FetchContent for headers, C++17 [S107]. |

**Frank assessment.** Would be a top pick for a Windows/Linux-first engine (clean API, auto barriers, bindless, indirect count, validation, trivial CMake, MIT). It fails the primary target outright: no Metal, no macOS, no runtime shader compilation. An unsupported MoltenVK build is conceivable but unverified. Ruled out.

**Evidence.** [S107] NVIDIA-RTX/NVRHI README, releases/tags/commits (GitHub API), `CMakeLists.txt`, CI workflow; [S108] issue #68; [S109] `include/nvrhi/nvrhi.h`, `doc/ProgrammingGuide.md`; [S110] NVIDIA-RTX/ShaderMake README. Accessed 2026-09-08; confidence high.

---

### 2.15 Magnum (Vladimir Vondrus)

| Criterion | Finding |
|---|---|
| Latest release / activity | **Last tag 2020.06 (2020-07-02)**; issue #453 is now "2026.0a release", open since 2020 and still slipping; master is the product, commits 2026-08-23 [S111]. |
| License | MIT/Expat. |
| Backends | OpenGL 2.1-4.6 / ES / WebGL (`Magnum::GL`), plus `Magnum::Vk` (off by default). **No Metal, and none planned**: issue #254 closed; maintainer (2021-01-05) says single-platform APIs are out of scope [S112]. |
| macOS reality | Frozen at GL 4.1 core with documented driver bugs; suggests ANGLE/Zink or MoltenVK [S113]. |
| Vulkan readiness | **Not production-ready for on-screen use**: no swapchain/surface classes, no `KHR_swapchain` on the support page, both Vk examples render offscreen to PNG; no Vk equivalents of the GL shader/mesh/scene-graph helpers [S114]. |
| Compute / indirect | GL compute needs 4.3 (unavailable on macOS); no indirect draw methods on `GL::Mesh` (raw `glDrawArraysIndirect` possible) [S115]. |
| MRT / HDR | `GL::Framebuffer` MRT, RGBA16F/RGBA32F/R11FG11FB10F [S116]. |
| Shaders | Plain GLSL; `magnum-shaderconverter` has glslang and SPIRV-Tools plugins but **no SPIRV-Cross plugin found**, so no MSL/HLSL emission [S117]. |
| Debugging | `GL::DebugOutput` needs 4.3/KHR_debug (not on macOS); no GPU debugger for Magnum-GL on macOS [S118]. |
| CMake | Lightest of the engines: C++11, CMake >= 3.5, only Corrade required; Homebrew tap, vcpkg [S111]. |

**Frank assessment.** Pleasant and unopinionated, but cannot give us a modern macOS GPU path: no Metal ever, GL 4.1 without compute/indirect/debug output, and a Vulkan layer without presentation. Its non-rendering pieces (Math, MeshTools, Trade importers, Corrade containers) remain attractive utilities next to a custom renderer. Ruled out as a rendering foundation.

**Evidence.** [S111] mosra/magnum releases/commits, discussion #615, issue #453; [S112] issue #254 comments; [S113] `platforms-macos` doc page; [S114] `src/Magnum/Vk` tree, vulkan-support page, example index, PR #234; [S115] `GL::AbstractShaderProgram`, `GL::Buffer`, `GL::Mesh` docs; [S116] `GL::Framebuffer` docs; [S117] `magnum-shaderconverter` and `ShaderTools` docs; [S118] `GL::DebugOutput` docs. Accessed 2026-09-08; confidence high.

---
## 3. Windowing and platform glue

### 3.1 GLFW

| Criterion | Finding |
|---|---|
| Latest release | **3.5.1** (2026-07-31). 3.5 was skipped "because of an accidentally published incorrect Git tag"; previous stable 3.4 (2024-02-23); master has started 3.6 (2026-08-03). No 4.0 [S58]. |
| Activity | 40 commits in 2025, 52 in 2026 through September; single maintainer, batchy but steady [S58]. |
| License | zlib/libpng. |
| macOS | 10.11+; 3.5.1 dropped 10.10, added QuartzCore as a link-time dependency. Requires Cocoa, IOKit, QuartzCore [S58]. |
| Metal | No dedicated Metal API; the supported pattern is `GLFW_CLIENT_API = GLFW_NO_API`, then `glfwGetCocoaWindow()`/`glfwGetCocoaView()` and attach a `CAMetalLayer` yourself. GLFW's own `cocoa_window.m` does exactly this for Vulkan surfaces [S59]. You manage `drawableSize`, EDR, `preferredFrameRateRange`, etc. yourself. |
| Vulkan | `glfwCreateWindowSurface` uses `VK_EXT_metal_surface` (fallback `VK_MVK_macos_surface`); on macOS you must enable `VK_KHR_portability_enumeration` [S60]. |
| HiDPI | `glfwGetFramebufferSize`, `glfwGetWindowContentScale`, `GLFW_SCALE_FRAMEBUFFER` (Retina backing, default on) [S61]. |
| CMake | `add_subdirectory(glfw)` or `find_package(glfw3 3.5)`; target `glfw`; min CMake 3.16 [S62]. |

**Assessment.** Minimal, predictable, zero-surprise. Gaps for a VJ engine: no HDR/EDR or ProMotion controls, no multi-display polish, no `CAMetalLayer` hooks; all of that is a few dozen lines of Objective-C++ we would own anyway. Good fit if we go raw Metal or Vulkan; redundant if we go SDL3.

**Evidence.** [S58] glfw/glfw releases + commits (GitHub API), glfw.org news; [S59] `src/cocoa_window.m` @3.5.1 and native API docs; [S60] GLFW Vulkan guide + `docs/compat.md`; [S61] GLFW window guide; [S62] GLFW build guide. Accessed 2026-09-08; confidence high.

### 3.2 SDL3 and the SDL_GPU API (a rendering candidate in its own right)

**What it is.** SDL 3's windowing/input/audio layer plus **SDL_GPU**, a modern explicit-but-simplified graphics API "in the style of Metal, Vulkan, and Direct3D 12" with **both 3D graphics and compute** [S63].

| Criterion | Finding |
|---|---|
| Latest release / activity | **SDL 3.4.16** (2026-09-02); 3.4.x point releases monthly; 3.4.0 (2026-01-01) [S64]. 3.4.16 notes mention GPU API depth-format and fence-query fixes [S64]. |
| License | zlib. |
| Platforms | Windows, macOS, Linux, iOS/tvOS, Android, consoles (NDA), Web. |
| GPU backends | **Vulkan** (Windows, Linux, Switch, Android), **Metal** (macOS 10.14+, iOS/tvOS 13+), **Direct3D 12** (Windows 10+, Xbox). Backend is chosen from the shader formats the app declares [S63]. |
| Compute | Yes: `SDL_BeginGPUComputePass`, storage buffers, storage textures with `COMPUTE_STORAGE_READ / WRITE / SIMULTANEOUS_READ_WRITE` usage, `SDL_DispatchGPUCompute` and `SDL_DispatchGPUComputeIndirect` (Metal: `dispatchThreadgroupsWithIndirectBuffer`) [S63][S65][S66]. |
| Indirect draw | `SDL_DrawGPUPrimitivesIndirect`, `SDL_DrawGPUIndexedPrimitivesIndirect` (with draw count). On Metal the backend loops and issues one `draw...Indirect` per command; **no GPU-side count buffer** in the API [S66]. `SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN` added in 3.4 [S67]. |
| RTT / MRT | **Max 4 color targets** per render pass plus depth/stencil [S63]. MSAA up to 8x [S65]. |
| HDR float targets | R16G16B16A16_FLOAT, R32G32B32A32_FLOAT, R11G11B10_UFLOAT, R16/R32 variants; 16F/32F formats universally supported for sampling, color target and storage [S68]. Swapchain compositions: SDR, SDR_LINEAR, **HDR_EXTENDED_LINEAR (RGBA16F)**, HDR10_ST2084; query `SDL_WindowSupportsGPUSwapchainComposition` [S69]. |
| Shader language / toolchain | The API takes **backend-native** shader blobs: SPIR-V (Vulkan), DXBC SM5.1 / DXIL SM6.0 (D3D12), **MSL source or metallib** (Metal) [S70]. Metal backend calls `newLibraryWithSource:` for MSL [S66]. Cross-compilation is via the separate **SDL_shadercross** library/CLI: HLSL or SPIR-V in, SPIR-V/DXBC/DXIL/MSL/HLSL out, usable **at runtime or offline**; depends on SPIRV-Cross and DXC; zlib; CMake. **No tagged releases** as of 2026-09-08 (commits 2026-09-03) [S71][S72]. |
| Custom-shader workflow | Good in practice: ship HLSL, run SDL_shadercross at runtime to the backend's format (or hand raw MSL on macOS). Requires bundling DXC + SPIRV-Cross. Resource binding follows a fixed slot convention (documented in `SDL_CreateGPUShader`). |
| Instancing | Yes. |
| Debugging | Docs recommend RenderDoc (Win/Linux) and Xcode's Metal debugger (macOS); Vulkan/D3D12 validation via debug-mode device [S63]. |
| CMake | First-class: `find_package(SDL3)` / `add_subdirectory` / FetchContent; targets `SDL3::SDL3`. SDL_shadercross is a separate CMake project with vendored SPIRV-Cross and DXC options [S71]. |
| Docs / ecosystem | SDL wiki is thorough; SDL_GPU has a growing tutorial base; used by FNA/MoonWorks and Godot-adjacent projects. Explicit scope statement: "If you need cutting-edge features with limited hardware support, this API is probably not for you"; ray tracing and mesh shaders are "not 'near future' items" [S63]. Readback via transfer buffers + `SDL_DownloadFromGPUTexture` with fences [S65]. |

**Frank assessment.** SDL_GPU is the most "batteries included" path that still gives an explicit command-buffer/pass model with compute, indirect dispatch, storage textures, HDR swapchains and clean CMake, in a library that also solves windowing, input, audio devices and HiDPI on all three OSes. Its ceiling is real: 4 MRTs, no multi-draw-indirect-count, no bindless, no push constants beyond uniform slots, no mesh/RT, no direct Metal escape hatch beyond the fixed binding model. For a festival engine that mostly composites full-screen passes and GPU particles, those limits are livable; for a deferred renderer with fat g-buffers, 4 MRTs is a constraint. It would not fight us on the basics; it would say "no" to some advanced things.

**Evidence.**
- [S63] SDL wiki CategoryGPU — learned: backends/platforms, compute pass, 4 color targets, indirect functions, scope statement, debugging recommendations. Confidence: high.
- [S64] libsdl-org/SDL releases (GitHub API + 3.4.16 notes) — learned: 3.4.16 on 2026-09-02; 3.4.0 on 2026-01-01. Confidence: high.
- [S65] `include/SDL3/SDL_gpu.h` (raw, main) — learned: storage usage flags, sample counts, download API. Confidence: high.
- [S66] `src/gpu/metal/SDL_gpu_metal.m` (raw, main) — learned: per-draw indirect loop, native indirect dispatch, `newLibraryWithSource` for MSL, metallib path. Confidence: high.
- [S67] SDL 3.4.0 release notes — learned: new GPU device feature properties, GPU 2D renderer interop. Confidence: high (date taken from API, page summary misreported year).
- [S68] SDL wiki SDL_GPUTextureFormat — learned: float formats and universal support notes. Confidence: high.
- [S69] SDL wiki SDL_GPUSwapchainComposition — learned: HDR_EXTENDED_LINEAR = RGBA16F etc. Confidence: high.
- [S70] SDL wiki SDL_GPUShaderFormat — learned: PRIVATE/SPIRV/DXBC/DXIL/MSL/METALLIB. Confidence: high.
- [S71] libsdl-org/SDL_shadercross README — learned: formats, deps, runtime+offline, zlib, CMake. Confidence: high.
- [S72] SDL_shadercross releases/commits (GitHub API) — learned: no releases, active commits. Confidence: high.

---
## 4. Comparison matrix

Legend: Y = yes, N = no, P = partial/with caveats, ? = unverified. "MDI-count on Metal" = multi-draw-indirect with a GPU-side count buffer available through the candidate's Metal path. "Runtime shader" = can a user hand the library shader *source* at runtime on macOS without an offline step.

| Candidate | Native Metal on macOS (open source) | Compute (SSBO + storage tex) | Indirect draw / MDI-count on Metal | Max MRT | Float HDR RT | Shader input on macOS | Runtime shader | Xcode capture | RenderDoc (Win/Linux) | CMake story | Win/Linux path | 2026 activity | License | Fits R12 ("won't fight us")? |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **Metal / metal-cpp** | Y (Metal 4) | Y | Y / Y (ICBs) | 8 | Y + EDR | MSL | Y | Y (best) | n/a | trivial | N (own RHI needed) | Apple drops per OS release (last 2026-06-08) | Apache-2.0 | Y |
| **Vulkan + MoltenVK** | P (layered) | Y | Y / Y (drawIndirectCount) | 8 | Y | SPIR-V (GLSL/HLSL/Slang) | Y (+SPIR-V->MSL latency) | Y | Y | standard | Y (same code) | MoltenVK 1.4.2 2026-07-24; SDK 1.4.357.1 2026-08-17 | Apache-2.0 | Y, minus Apple extras |
| **Vulkan + KosmicKrisp** | P (layered, conformant, macOS 26 only) | Y | Y / Y | 8 | Y | SPIR-V | Y | Y | Y | standard | Y | New (conformant 2025-10-29) | MIT (Mesa) | Y, young |
| **Direct3D 12** | N | Y | Y / Y | 8 | Y | DXIL | Y (DXC lib) | n/a | Y (PIX too) | standard | Windows only | Agility 1.619.5 2026-07-30 | MS | n/a for macOS |
| **OpenGL (macOS 4.1)** | N (deprecated, on Metal) | **N** | P (4.0 indirect, no compute) | 8 | Y | GLSL 4.10 | Y | **N** | Y | trivial | Y | frozen | n/a | **N** |
| **wgpu-native** | Y (first-class Metal) | Y | Y / **N** (count = DX12/Vulkan only) | 8 (32 B/sample default) | Y (+HDR surface v30) | WGSL, GLSL, SPIR-V | Y | Y (naga MSL) | Y | Cargo/prebuilt | Y | v29.0.1.1 2026-06-23; wgpu v30.0.1 2026-08-22 | MIT/Apache | Y, WebGPU ceiling |
| **Dawn** | Y (Chrome's Metal backend) | Y | Y / ? (feature exists; Metal support not stated) | 8 (32 B/sample default) | Y | WGSL (+SPIR-V if built with reader) | Y | Y (Tint MSL) | Y | CMake + prebuilt releases | Y | date-tagged releases every few days (2026-09-08) | BSD-3 | Y, WebGPU ceiling |
| **bgfx** | Y (metal-cpp based) | Y | Y / **N** (per-draw loop) | 8 | Y | bgfx dialect via **offline shaderc** | **N** (supported path) | Y | Y | bgfx.cmake (community) | Y | commits 2026-09-07 | BSD-2 | P (shader workflow) |
| **sokol_gfx** | Y | Y (2025; 4 storage images/pass) | **N** / N | 8 | Y (+HDR swapchain) | MSL source/metallib; sokol-shdc offline | Y (raw MSL) | Y | Y | header-only | Y | commits 2026-09-08; frequent breaking changes | zlib | **N** (no indirect, no readback) |
| **SDL3 SDL_GPU** | Y | Y | Y / **N** (per-draw loop) | **4** | Y (+HDR swapchain) | MSL/metallib; HLSL/SPIR-V via SDL_shadercross | Y (via shadercross or raw MSL) | Y | Y | first-class | Y (Vulkan, D3D12) | 3.4.16 2026-09-02 | zlib | P (feature ceiling) |
| **Filament** | Y | **N** (not public) | **N** | 8 | Y | .mat via matc | P (libfilamat) | Y (built-in) | Y | heavy (Clang 17+, Dawn vendored) | Y | v1.76.0 2026-08-28 | Apache-2.0 | **N** |
| **OGRE-Next** | Y | Y (HlmsComputeJob) | Y / ? | Y | Y | MSL + GLSL + HLSL templates | P (low-level materials) | Y (+hooks) | Y (+hooks) | clunky (deps repo, Xcode gen) | Y (D3D11/Vulkan; no MoltenVK) | 4.0 unstable master 2026-09-08 | MIT | P (heavy engine) |
| **Diligent** | **N** (commercial) | Y | Y / Y (MultiDraw) | ? (8 assumed) | Y | HLSL (any) | Y (+hot reload) | Y (via MoltenVK) | Y | heavy but documented | Y | master very active; last tag 2024-09-02 | Apache-2.0 | Y, if MoltenVK/paid Metal OK |
| **The Forge** | P (frozen v1.63 only) | Y | Y / Y (ExecuteIndirect count) | 8 | Y | FSL (Python + xcrun) | P (shader server) | Y | ? | **none** | Windows only in 1.64 | 1.64 on Codeberg 2026-08-12 | Apache-2.0 | **N** |
| **LLGL** | Y | Y | Y / N (no count) | ? | Y | MSL/HLSL/GLSL/SPIR-V native | Y (MSL) | Y | ? | minimal | Y | 0.04b 2025-07-08; 217 commits 2026 | BSD-3 | Y, thin + one maintainer |
| **NVRHI** | **N** | Y | Y / Y | 8 | Y | DXIL/SPIR-V bytecode | **N** | n/a | Y | clean | Y | commits 2026-08-25; no releases | MIT + CLA | n/a (no macOS) |
| **Magnum** | **N** | GL 4.3+ only (not macOS) | N | Y (GL) | Y | GLSL | Y | **N** | Y | light | Y (GL) | master 2026-08-23; last tag 2020 | MIT | **N** |
| **GLFW** (windowing) | n/a (CAMetalLayer DIY) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | trivial | Y | 3.5.1 2026-07-31 | zlib | Y |

---

## 5. Shortlist and tradeoffs (no decision taken)

### Eliminated, with the single decisive reason each

- **OpenGL**: deprecated on macOS, capped at 4.1, no compute, no GPU debugger on macOS.
- **Filament**: no user compute, no indirect, no post-process hook.
- **Magnum**: no Metal, no viable Vulkan presentation layer.
- **NVRHI**: no Metal/macOS.
- **The Forge**: current public code is Windows/D3D12 only; no CMake; closed development.
- **Diligent Engine**: native Metal is commercial-only. (Would re-enter if the lead accepts MoltenVK on macOS or a license.)
- **sokol_gfx**: no indirect draw/dispatch and no readback API; frequent breaking changes. Excellent for tooling and prototypes.
- **bgfx**: offline shader dialect blocks the live-shader workflow; no MDI-count on Metal. Otherwise mature and portable.
- **OGRE-Next**: has everything, but at the cost of adopting a full scene-graph engine and maintaining three shader dialects per effect.
- **LLGL**: credible thin option but single-maintainer beta with no shader toolchain; kept as a reference implementation rather than a finalist.

### Finalist A: Raw Metal (metal-cpp) behind an in-house thin RHI, Vulkan as the second backend

- **Why**: maximum macOS fidelity (Metal 4, ICBs, residency sets, MetalFX, EDR, ML encoders), the best debugger on the platform, runtime MSL compilation for live shader work, trivial CMake, Apple-maintained headers. The R10 story is "write a Vulkan 1.4 backend later", which is exactly what bgfx/SDL_GPU/The Forge/LLGL do internally, and it can be *validated on the dev Mac today* with MoltenVK or KosmicKrisp (Section 2.2).
- **Costs**: we own an RHI (command lists, resource state, sync, descriptors) and a shader cross-compilation choice (Slang or HLSL/GLSL -> SPIR-V -> SPIRV-Cross -> MSL). Nothing ships on Windows/Linux until the second backend exists. Bus factor is us.
- **Best when**: the team wants to be limited by the GPU, not by a framework, and accepts writing ~10-20k lines of platform code over the project's life.

### Finalist B: WebGPU-native (Dawn or wgpu-native)

- **Why**: one API that is first-class on Metal, Vulkan and D3D12 today, with compute, storage textures, indirect draw/dispatch, timestamp queries, HDR surfaces and runtime shader compilation. Both are maintained by browser vendors with daily activity. Dawn's 2026 prebuilt release archives (including an Apple xcframework) and depot_tools-free CMake flow remove the historical build objection; wgpu-native ships prebuilt binaries and adds GLSL/SPIR-V input, push constants ("immediates") and MDI-count on Vulkan/DX12.
- **Costs**: WebGPU's envelope (8 storage buffers/textures per stage and 32 B/sample MRT by default unless raised; no ICBs; MDI-count absent on Metal in wgpu and unverified in Dawn; no bindless without native extensions), WGSL as the front door (naga/Tint/Slang can translate), generated MSL in the shader profiler, and an API surface that still moves (date-tagged Dawn, quarterly wgpu breaks, header drift between the two).
- **Dawn vs wgpu-native**: Dawn = the reference implementation, stable `webgpu.h`, richer native extensions (pixel local storage, subgroup matrix), C++20 required, Chrome-tested Metal backend. wgpu-native = GLSL/SPIR-V input, MDI-count and immediates, but does not yet implement the stable header and needs Cargo to build from source. Either is viable; the choice is mostly "reference conformance vs. native-feature convenience".
- **Best when**: portability from day one matters more than the last 10% of Metal, and the team prefers a validated API to writing sync code.

### Finalist C: SDL3 + SDL_GPU (+ SDL_shadercross)

- **Why**: the fastest path to a running, portable engine. One zlib dependency covers windowing, input, audio devices, HiDPI, HDR swapchains, and a Metal/Vulkan/D3D12 renderer with compute passes, storage textures, indirect draw/dispatch and fenced readback. First-class CMake. Runtime HLSL through SDL_shadercross, or raw MSL on macOS.
- **Costs**: hard ceiling of **4 color targets**, no MDI-count, no bindless, no mesh/RT, fixed binding convention; SDL_shadercross has no tagged release yet; the Metal backend is thinner than Dawn's. Escaping to native Metal from inside SDL_GPU is not a supported pattern.
- **Best when**: the engine's look is mostly full-screen passes, particles and instanced geometry, and shipping on three OSes quickly beats deferred-style fat g-buffers.

### Cross-cutting notes for the lead

1. **Shader language is the real fork in the road.** A/C can use HLSL or Slang with SPIRV-Cross; B pushes toward WGSL (or Slang -> WGSL). Slang ships in the Vulkan SDK and targets MSL, SPIR-V, HLSL and WGSL, which would keep all three finalists open. (Slang's current release state was **not researched** in this pass.)
2. **Every finalist debugs through Xcode on macOS.** RenderDoc is Windows/Linux only. The difference is whether you see your own MSL (A, C with raw MSL) or generated MSL (B, MoltenVK).
3. **Offline deterministic rendering** is straightforward on all three (explicit fences + readback). Only sokol lacked a readback API.
4. **Windowing**: GLFW 3.5.1 is fine for A and B; C brings its own. A hand-written Cocoa layer (~200 lines Objective-C++) is the alternative for A if EDR/ProMotion control matters.
5. **KosmicKrisp** is the wildcard: if it matures over the next year, "Vulkan 1.4 everywhere, including macOS" becomes a fourth credible option with zero abstraction layer of our own. It is macOS 26-only and under a year old, so today it is a validation tool, not a foundation.

---

## 6. Open questions and things not verified

- Dawn: whether the Metal backend implements `multi_draw_indirect` with count (feature doc does not enumerate backends).
- wgpu: current status of Metal argument-buffer work (#4491) and MSL passthrough on the v30 line.
- bgfx: whether the Vulkan backend is supported on macOS via MoltenVK (not documented).
- SDL_shadercross: release timing; whether MSL output quality is acceptable for profiling.
- Diligent: MRT attachment cap; commercial Metal license terms/price.
- LLGL: contributor count; MRT cap; whether `DrawIndirect` on Metal loops per draw like SDL/bgfx.
- OGRE-Next: explicit statement that `HlmsComputeJob` is supported on the Metal render system (implied by `.metal` templates only).
- Filament: float render-target format list (enum fetch truncated).
- KosmicKrisp: Vulkan 1.4 conformance entry on the Khronos registry (only 1.3 entry #958 seen).
- Apple: Tahoe release-notes text on AGL removal read second-hand (JUCE forum); Metal Feature Set Tables PDF not opened this session (8 color attachments taken from bgfx's Metal backend and general knowledge).
- Slang as the single shader source: not researched here; recommended as a follow-up.
- Build times were not measured for any candidate.

---

## Sources

All accessed 2026-09-08.

**Apple / Metal**
- [S1] apple/metal-cpp README and changelog — https://github.com/apple/metal-cpp
- [S2] Getting started with Metal-cpp — https://developer.apple.com/metal/cpp/
- [S3] apple/metal-cpp tags and commit dates (GitHub API) — https://api.github.com/repos/apple/metal-cpp/commits
- [S4] WWDC25 session 205 "Discover Metal 4" — https://developer.apple.com/videos/play/wwdc2025/205/
- [S5] Metal Shading Language Specification (PDF, listed as v4.1, dated 2026-06-04 in search metadata; not opened) — https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf
- [S6] MTLDevice `newLibraryWithSource:options:error:` — https://developer.apple.com/documentation/metal/mtldevice/makelibrary(source:options:)?language=objc
- [S7] Capturing a Metal workload in Xcode / Metal debugger — https://developer.apple.com/documentation/xcode/capturing-a-metal-workload-in-xcode and https://developer.apple.com/documentation/xcode/metal-debugger
- [S8] Apple Developer Forums, metal-cpp-extensions (AppKit/MetalKit headers shipped in LearnMetalCPP) — https://developer.apple.com/forums/thread/722886

**WebGPU (wgpu / Dawn / headers / spec)**
- [S9] gfx-rs/wgpu README — https://github.com/gfx-rs/wgpu
- [S10] gfx-rs/wgpu-native README — https://github.com/gfx-rs/wgpu-native
- [S11] wgpu-native releases — https://github.com/gfx-rs/wgpu-native/releases
- [S12] wgpu releases — https://github.com/gfx-rs/wgpu/releases
- [S13] W3C WebGPU specification, limits — https://www.w3.org/TR/webgpu/#limits
- [S14] wgpu `Features` docs — https://docs.rs/wgpu/latest/wgpu/struct.Features.html
- [S15] wgpu-native `ffi/wgpu.h` — https://raw.githubusercontent.com/gfx-rs/wgpu-native/trunk/ffi/wgpu.h
- [S16] webgpu-native/webgpu-headers README — https://github.com/webgpu-native/webgpu-headers
- [S17] wgpu issues #4491, #6744, #6546 — https://github.com/gfx-rs/wgpu/issues/4491 , https://github.com/gfx-rs/wgpu/issues/6744 , https://github.com/gfx-rs/wgpu/issues/6546
- [S18] google/dawn README — https://github.com/google/dawn
- [S19] google/dawn commits (GitHub API) — https://api.github.com/repos/google/dawn/commits
- [S20] google/dawn releases (GitHub API) — https://github.com/google/dawn/releases
- [S21] Dawn feature docs directory — https://github.com/google/dawn/tree/main/docs/dawn/features
- [S22] Dawn `multi_draw_indirect.md` — https://github.com/google/dawn/blob/main/docs/dawn/features/multi_draw_indirect.md
- [S23] Dawn SPIR-V reader overview and dawn-graphics thread "Loading Spirv shaders" — https://dawn.googlesource.com/dawn/+/HEAD/docs/tint/spirv-reader-overview.md , https://groups.google.com/g/dawn-graphics/c/_1dGRs6aKlY
- [S24] Dawn `docs/quickstart-cmake.md` — https://github.com/google/dawn/blob/main/docs/quickstart-cmake.md
- [S25] Dawn `docs/building.md` — https://dawn.googlesource.com/dawn/+/HEAD/docs/building.md

**bgfx / sokol**
- [S26] bgfx README and overview — https://github.com/bkaradzic/bgfx , https://bkaradzic.github.io/bgfx/overview.html
- [S27] bgfx `src/renderer_mtl.cpp`, `src/renderer_mtl.h` — https://raw.githubusercontent.com/bkaradzic/bgfx/master/src/renderer_mtl.cpp , https://raw.githubusercontent.com/bkaradzic/bgfx/master/src/renderer_mtl.h
- [S28] bkaradzic/bgfx.cmake — https://github.com/bkaradzic/bgfx.cmake
- [S29] bgfx API reference — https://bkaradzic.github.io/bgfx/bgfx.html
- [S30] bgfx `src/config.h` and `include/bgfx/defines.h` — https://raw.githubusercontent.com/bkaradzic/bgfx/master/src/config.h , https://raw.githubusercontent.com/bkaradzic/bgfx/master/include/bgfx/defines.h
- [S31] bgfx tools (shaderc) — https://bkaradzic.github.io/bgfx/tools.html
- [S32] floooh/sokol README — https://github.com/floooh/sokol
- [S33] "The experimental Sokol Vulkan backend" (2025-12-01) — https://floooh.github.io/2025/12/01/sokol-vulkan-backend-1.html
- [S34] sokol CHANGELOG — https://raw.githubusercontent.com/floooh/sokol/master/CHANGELOG.md
- [S35] "The sokol-gfx compute shader update" (2025-03-03) — https://floooh.github.io/2025/03/03/sokol-gfx-compute-update.html
- [S36] "The sokol-gfx 'compute milestone 2' update" (2025-05-19) — https://floooh.github.io/2025/05/19/sokol-gfx-compute-ms2.html
- [S37] `sokol_gfx.h` (master) — https://raw.githubusercontent.com/floooh/sokol/master/sokol_gfx.h
- [S38] sokol-shdc docs — https://github.com/floooh/sokol-tools/blob/master/docs/sokol-shdc.md

**Debug tooling**
- [S40] RenderDoc features/platform support — https://renderdoc.org/docs/getting_started/features.html

**Vulkan on macOS**
- [S41] KhronosGroup/MoltenVK README, releases, Whats_New — https://github.com/KhronosGroup/MoltenVK , https://github.com/KhronosGroup/MoltenVK/releases , https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/Whats_New.md
- [S42] MoltenVK Runtime User Guide — https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md
- [S43] Mesa docs, KosmicKrisp — https://docs.mesa3d.org/drivers/kosmickrisp.html
- [S44] LunarG Vulkan SDK for macOS release notes 1.4.357.x and Getting Started — https://vulkan.lunarg.com/doc/view/1.4.357.1/mac/release_notes.html , https://vulkan.lunarg.com/sdk/latest/mac.txt
- [S45] Khronos Vulkan 1.4 press release — https://www.khronos.org/news/press/khronos-streamlines-development-and-deployment-of-gpu-accelerated-applications-with-vulkan-1.4
- [S46] MoltenVK issue #2560 (Metal 4) — https://github.com/KhronosGroup/MoltenVK/issues/2560
- [S47] Khronos conformant products (KosmicKrisp entry #958) — https://www.khronos.org/conformance/adopters/conformant-products/vulkan
- [S48] LunarG: KosmicKrisp conformance announcement; "State of Vulkan on Apple, Jan 2026" — https://www.lunarg.com/lunarg-achieves-vulkan-1-3-conformance-with-kosmickrisp-on-apple-silicon/ , https://www.lunarg.com/the-state-of-vulkan-on-apple-jan-2026/

**Direct3D 12**
- [S49] DirectX 12 Agility SDK downloads — https://devblogs.microsoft.com/directx/directx12agility/
- [S50] "Shader Model 6.9 retail and more" — https://devblogs.microsoft.com/directx/shader-model-6-9-retail-and-more/
- [S51] "Agility SDK 1.613.0" (work graphs retail) — https://devblogs.microsoft.com/directx/agility-sdk-1-613-0/
- [S52] microsoft/DirectXShaderCompiler releases — https://github.com/microsoft/DirectXShaderCompiler/releases

**OpenGL on macOS**
- [S53] macOS Mojave 10.14 release notes — https://developer.apple.com/documentation/macos-release-notes/macos-mojave-10_14-release-notes
- [S54] Porting your macOS apps to Apple silicon — https://developer.apple.com/documentation/apple-silicon/porting-your-macos-apps-to-apple-silicon
- [S55] Apple Support, Mac computers that use OpenCL and OpenGL graphics — https://support.apple.com/en-us/101525
- [S56] JUCE forum, "macOS Tahoe and OpenGL" — https://forum.juce.com/t/macos-tahoe-and-opengl/66921
- [S57] GLFW `docs/compat.md` @3.5.1 — https://github.com/glfw/glfw/blob/3.5.1/docs/compat.md

**GLFW**
- [S58] glfw/glfw releases and commits (GitHub API); glfw.org news — https://github.com/glfw/glfw/releases , https://www.glfw.org/
- [S59] GLFW `src/cocoa_window.m` @3.5.1; native access docs — https://github.com/glfw/glfw/blob/3.5.1/src/cocoa_window.m , https://www.glfw.org/docs/latest/group__native.html
- [S60] GLFW Vulkan guide — https://www.glfw.org/docs/latest/vulkan_guide.html
- [S61] GLFW window guide — https://www.glfw.org/docs/latest/window_guide.html
- [S62] GLFW build guide — https://www.glfw.org/docs/latest/build_guide.html

**SDL3 / SDL_GPU**
- [S63] SDL3 CategoryGPU — https://wiki.libsdl.org/SDL3/CategoryGPU
- [S64] libsdl-org/SDL releases — https://github.com/libsdl-org/SDL/releases
- [S65] `include/SDL3/SDL_gpu.h` — https://raw.githubusercontent.com/libsdl-org/SDL/main/include/SDL3/SDL_gpu.h
- [S66] `src/gpu/metal/SDL_gpu_metal.m` — https://raw.githubusercontent.com/libsdl-org/SDL/main/src/gpu/metal/SDL_gpu_metal.m
- [S67] SDL 3.4.0 release notes — https://github.com/libsdl-org/SDL/releases/tag/release-3.4.0
- [S68] SDL_GPUTextureFormat — https://wiki.libsdl.org/SDL3/SDL_GPUTextureFormat
- [S69] SDL_GPUSwapchainComposition — https://wiki.libsdl.org/SDL3/SDL_GPUSwapchainComposition
- [S70] SDL_GPUShaderFormat — https://wiki.libsdl.org/SDL3/SDL_GPUShaderFormat
- [S71] libsdl-org/SDL_shadercross README — https://github.com/libsdl-org/SDL_shadercross
- [S72] SDL_shadercross releases (none) and commits — https://github.com/libsdl-org/SDL_shadercross/releases

**Filament**
- [S73] google/filament releases and commits — https://github.com/google/filament/releases , https://github.com/google/filament/commits/main
- [S74] Filament BUILDING.md, README, `VulkanPlatformApple.mm` — https://github.com/google/filament/blob/main/BUILDING.md , https://github.com/google/filament/blob/main/filament/backend/src/vulkan/platform/VulkanPlatformApple.mm
- [S75] Filament issue #7995 (compute); `MaterialEnums.h`, `DriverEnums.h` — https://github.com/google/filament/issues/7995 , https://github.com/google/filament/blob/main/filament/backend/include/backend/DriverEnums.h
- [S76] Filament `RenderableManager.h` — https://github.com/google/filament/blob/main/filament/include/filament/RenderableManager.h
- [S77] Filament `RenderTarget.h`, `View.h` — https://github.com/google/filament/blob/main/filament/include/filament/RenderTarget.h
- [S78] Filament Materials guide; `third_party/` — https://google.github.io/filament/main/materials.html , https://github.com/google/filament/tree/main/third_party
- [S79] Filament discussion #7676 (custom post-processing) — https://github.com/google/filament/discussions/7676
- [S80] Filament `RenderableManager.h` (instancing) — as [S76]
- [S81] Filament `MetalDriver.mm` — https://raw.githubusercontent.com/google/filament/main/filament/backend/src/metal/MetalDriver.mm
- [S82] Filament `Engine.h`, `SwapChain.h`, `Renderer.h` — https://github.com/google/filament/tree/main/filament/include/filament

**OGRE-Next**
- [S83] OGRECave/ogre-next releases, tags, commits — https://github.com/OGRECave/ogre-next/releases , https://github.com/OGRECave/ogre-next/commits/master
- [S84] OGRE-Next "What's new in 4.0" — https://ogrecave.github.io/ogre-next/api/latest/_ogre40_changes.html
- [S85] `RenderSystems/Vulkan/CMakeLists.txt`; Setting up OGRE on macOS — https://github.com/OGRECave/ogre-next/blob/master/RenderSystems/Vulkan/CMakeLists.txt , https://ogrecave.github.io/ogre-next/api/latest/_setting_up_ogre_mac_o_s.html
- [S86] `HlmsComputeJob` docs; compositor docs; tutorials — https://ogrecave.github.io/ogre-next/api/latest/class_ogre_1_1_hlms_compute_job.html , https://ogrecave.github.io/ogre-next/api/latest/compositor.html , https://github.com/OGRECave/ogre-next/tree/master/Samples/2.0/Tutorials
- [S87] `VaoManager` docs; `OgreRenderQueue.cpp` — https://ogrecave.github.io/ogre-next/api/latest/class_ogre_1_1_vao_manager.html
- [S88] Samples/2.0/Showcase — https://github.com/OGRECave/ogre-next/tree/master/Samples/2.0/Showcase
- [S89] Hlms docs; `VulkanProgram` docs; ogre-next-deps — https://ogrecave.github.io/ogre-next/api/latest/hlms.html , https://ogrecave.github.io/ogre-next/api/latest/class_ogre_1_1_vulkan_program.html , https://github.com/OGRECave/ogre-next-deps
- [S90] `RenderSystem` docs; `CMake/Dependencies.cmake` — https://ogrecave.github.io/ogre-next/api/latest/class_ogre_1_1_render_system.html
- [S91] macOS setup page — as [S85]
- [S92] `AsyncTextureTicket` docs — https://ogrecave.github.io/ogre-next/api/latest/class_ogre_1_1_async_texture_ticket.html

**Diligent Engine**
- [S93] DiligentEngine / DiligentCore READMEs, releases, commits — https://github.com/DiligentGraphics/DiligentEngine , https://github.com/DiligentGraphics/DiligentCore
- [S94] `Graphics/GraphicsEngineMetal/readme.md` — https://github.com/DiligentGraphics/DiligentCore/tree/master/Graphics/GraphicsEngineMetal
- [S95] `DeviceContext.h`, `GraphicsTypes.h` — https://github.com/DiligentGraphics/DiligentCore/tree/master/Graphics/GraphicsEngine/interface
- [S96] `Shader.h`; `ThirdParty/`; `Graphics/ShaderTools/src` — https://github.com/DiligentGraphics/DiligentCore/tree/master/ThirdParty
- [S97] `RenderStateCache.h` — https://github.com/DiligentGraphics/DiligentCore/blob/master/Graphics/GraphicsTools/interface/RenderStateCache.h
- [S98] `doc/PerformanceGuide.md` — https://github.com/DiligentGraphics/DiligentCore/blob/master/doc/PerformanceGuide.md

**The Forge**
- [S99] ConfettiFX/The-Forge releases and commits — https://github.com/ConfettiFX/The-Forge
- [S100] The-Forge on Codeberg (README, contents, commits) — https://codeberg.org/The-Forge/The-Forge
- [S101] `Common_3/Graphics/Interfaces/IGraphics.h` @v1.63 — https://github.com/ConfettiFX/The-Forge/blob/v1.63/Common_3/Graphics/Interfaces/IGraphics.h
- [S102] FSL Programming Guide; `compilers.py` — https://github.com/ConfettiFX/The-Forge/wiki/FSL-Programming-Guide

**LLGL**
- [S103] LukasBanana/LLGL README, releases, commits, CMakeLists — https://github.com/LukasBanana/LLGL
- [S104] `sources/Renderer/Metal/`, `VKRenderSystem.cpp`, `BuildMacOS.command` — https://github.com/LukasBanana/LLGL/tree/master/sources/Renderer
- [S105] `include/LLGL/CommandBuffer.h`, `Format.h`, `MTDirectCommandBuffer.mm` — https://github.com/LukasBanana/LLGL/blob/master/include/LLGL/CommandBuffer.h
- [S106] `MTShader.mm`, `scripts/TranslateShaders.py` — https://github.com/LukasBanana/LLGL/blob/master/scripts/TranslateShaders.py

**NVRHI**
- [S107] NVIDIA-RTX/NVRHI README, releases/tags, commits, CMake, CI — https://github.com/NVIDIA-RTX/NVRHI
- [S108] NVRHI issue #68 "Metal support?" — https://github.com/NVIDIA-RTX/NVRHI/issues/68
- [S109] `include/nvrhi/nvrhi.h`, `doc/ProgrammingGuide.md` — https://github.com/NVIDIA-RTX/NVRHI/blob/main/doc/ProgrammingGuide.md
- [S110] NVIDIA-RTX/ShaderMake — https://github.com/NVIDIA-RTX/ShaderMake

**Magnum**
- [S111] mosra/magnum releases, commits, discussion #615, issue #453 — https://github.com/mosra/magnum/releases , https://github.com/mosra/magnum/discussions/615 , https://github.com/mosra/magnum/issues/453
- [S112] Magnum issue #254 (Metal backend) — https://github.com/mosra/magnum/issues/254
- [S113] Magnum macOS platform notes — https://doc.magnum.graphics/magnum/platforms-macos.html
- [S114] `src/Magnum/Vk`; Vulkan support page; example index; PR #234 — https://github.com/mosra/magnum/tree/master/src/Magnum/Vk , https://doc.magnum.graphics/magnum/vulkan-support.html , https://github.com/mosra/magnum/issues/234
- [S115] `GL::AbstractShaderProgram`, `GL::Buffer`, `GL::Mesh` docs — https://doc.magnum.graphics/magnum/classMagnum_1_1GL_1_1AbstractShaderProgram.html
- [S116] `GL::Framebuffer` docs — https://doc.magnum.graphics/magnum/classMagnum_1_1GL_1_1Framebuffer.html
- [S117] magnum-shaderconverter; ShaderTools namespace — https://doc.magnum.graphics/magnum/magnum-shaderconverter.html
- [S118] `GL::DebugOutput` docs — https://doc.magnum.graphics/magnum/classMagnum_1_1GL_1_1DebugOutput.html
