# Architecture Options: Rendering Foundation for av-gen

Research date: 2026-09-08. Author: engineering lead (synthesised from the per-topic research
documents in this directory, each of which carries its own dated sources). Machine used for
verification: macOS 26.6.2, Apple M2 Max (Metal 4), Xcode 26.6, CMake 4.0.1, Apple clang 21.

This document compares candidate *architectures* for the rendering foundation, not merely
libraries. It ends with a decision, its consequences, and an exit strategy. The corresponding
Architecture Decision Record is `docs/decisions/ADR-001-rendering-backend.md`.

---

## 1. What is actually being decided

Three layers are in play. The decision is about the bottom two; the third is where the value
of this project lives and must be insulated from the first two.

```
┌──────────────────────────────────────────────────────────────┐
│ 3. Audiovisual layer: parameters, modulation, scene, render   │  <- the product
│    graph, post-processing, offline job runner                 │
├──────────────────────────────────────────────────────────────┤
│ 2. Engine GPU facade ("gpu" module): device, resources,       │  <- thin, ours
│    passes, pipelines, shader modules, readback, capabilities  │
├──────────────────────────────────────────────────────────────┤
│ 1. GPU API / hardware abstraction: Metal, Vulkan, D3D12,      │  <- chosen here
│    or a library that wraps them                               │
└──────────────────────────────────────────────────────────────┘
```

The question for layer 1 is: which API do we write the facade against? The question for layer 2
is: how thick must the facade be? The answer to the second depends on the first.

## 2. Requirements recap

From `rendering.md` §1 (R1-R12), `particles.md` §11, `rendering-techniques.md` Part I and
`offline-rendering.md` §12. Only the requirements that discriminate between options are listed.

| Id | Requirement | Discriminates because |
|---|---|---|
| R1 | Native Metal on macOS, not OpenGL | Eliminates GL; penalises layered Vulkan |
| R2 | Compute with storage buffers **and** storage textures, indirect dispatch | Eliminates Filament, Magnum, GL; sokol lacks indirect |
| R3 | Indirect draw from GPU-written args (MDI-count optional) | sokol has none; SDL_GPU/bgfx/wgpu loop per-draw on Metal |
| R4 | MRT >= 4 (8 preferred), HDR float targets | SDL_GPU caps at 4 |
| R5 | Runtime shader compilation from source for live iteration | bgfx's offline dialect fights this |
| R7 | GPU->CPU readback with explicit sync (offline frames) | sokol has no readback API |
| R10 | Windows/Linux without a rewrite | Raw Metal requires a second backend written by us |
| R11 | Maintenance by an organisation, not one person | sokol, bgfx, LLGL are single-maintainer |
| R12 | Does not own the frame; we control passes and resource lifetimes | Filament, OGRE-Next own the frame |
| P1 | Passes are peers: compute, render, blit in one ordered list (`particles.md` §11.1) | Needs command-encoder-level API |
| P2 | Persistent vs transient resources, ping-pong by handle (§11.3-4) | Any explicit API is fine; scene-graph engines are not |
| P3 | 3D textures writable from compute (§11.11) | WebGPU: yes (`texture_storage_3d`); SDL_GPU: yes |
| O1 | Headless offscreen rendering, no window required (`offline-rendering.md` §12) | Any explicit API; sokol needs sokol_app or manual swapchain |
| O2 | Timestamp queries for GPU frame time | WebGPU: `timestamp-query` feature; Metal/Vulkan: native |
| T1 | First pixels within days, not weeks, on one developer's time | Weighs against writing an RHI first |
| T2 | User's explicit steer: do not write a renderer from scratch against Metal unless necessary | Weighs against Option A |

## 3. Option A: In-house RHI over raw Metal (metal-cpp), Vulkan backend later

**Shape.** We define `gpu::Device`, `gpu::Buffer`, `gpu::Texture`, `gpu::Pipeline`,
`gpu::CommandList` etc. as abstract interfaces and implement them first on Metal via metal-cpp
(Apache-2.0, header-only, Metal 4 in the macOS 26 drop). Windows/Linux arrive when we write a
Vulkan implementation of the same interfaces. Shaders: author in Slang or GLSL, compile to SPIR-V,
cross-compile to MSL with SPIRV-Cross at runtime; hand MSL to `newLibraryWithSource`.

**Strengths.** Every Metal feature is reachable (indirect command buffers, residency sets,
MetalFX, EDR output, Metal 4 argument tables). Best debugger (Xcode GPU capture on our own MSL).
Trivial CMake. Apple maintains the headers. This is the path bgfx, SDL_GPU, The Forge and LLGL
took internally, so it is well-trodden (`rendering.md` §2.1, §5).

**Costs.** We own a full RHI: resource state tracking, synchronisation, descriptor/argument
binding, pipeline caching, swapchain, readback, and two implementations of all of it before R10 is
satisfied. Realistic size: 10-20k lines over the project's life (`rendering.md` §5 Finalist A).
Nothing runs off macOS until the second backend exists. The RHI design must be validated against
Vulkan from day one or the Metal-shaped abstraction will leak. This is exactly the "renderer from
scratch" the brief warns against (T2), and it delays the audiovisual layer (T1).

**When it would be right.** A team of several graphics engineers, or a hard requirement for a
Metal-only feature that no abstraction exposes (e.g. ICB-driven GPU-generated draw streams, EDR
output to Pro Display XDR). None of these is required by milestone 0.1-0.6.

## 4. Option B: WebGPU as the RHI (Dawn or wgpu-native)

**Shape.** The engine's GPU facade wraps the standard `webgpu.h` C API (C++ wrapper
`webgpu_cpp.h`). Dawn (Google, BSD-3) provides Metal on macOS, Vulkan on Linux/Android, D3D12 on
Windows. wgpu-native (Mozilla/gfx-rs, MIT/Apache) provides the same API surface with its own
backends. The facade stays thin because WebGPU *is* an RHI: explicit pipelines, bind groups,
command encoders, render/compute passes, storage buffers and textures, indirect draw and dispatch,
timestamp queries, map-async readback (`rendering.md` §2.5-2.6).

**Strengths.**
- Portability from day one with no second backend to write (R10).
- Maintained by two browser vendors with daily activity; Dawn's Metal backend is the one Chrome
  ships to every Mac (R11).
- Runtime shader compilation from WGSL source with excellent diagnostics (R5).
- Validation layer built in: invalid usage is a readable error, not a GPU hang. This matters for a
  project whose custom-shader feature will let users feed the engine arbitrary code.
- Verified on 2026-09-08: Dawn now publishes prebuilt release archives (`v20260907.201642`) for
  macOS arm64 (Release: 13.6 MB) containing `libwebgpu_dawn.a`, headers, Tint libraries, a `tint`
  CLI, and CMake package config (`lib/cmake/Dawn/DawnConfig.cmake` exporting `dawn::webgpu_dawn`
  with Metal/QuartzCore/IOSurface frameworks in its link interface). Consumption is a `find_package`
  or a CPM URL download. From-source builds via `DAWN_FETCH_DEPENDENCIES=ON` need only CMake and
  Python (no depot_tools). Source: https://github.com/google/dawn/releases and
  https://raw.githubusercontent.com/google/dawn/main/docs/quickstart-cmake.md (accessed
  2026-09-08); archive contents inspected locally.
- Dear ImGui ships `imgui_impl_wgpu` with a Dawn mode (`IMGUI_IMPL_WEBGPU_BACKEND_DAWN`), verified
  in the header on 2026-09-08 (https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_wgpu.h).
- Rich native extensions in Dawn: `multi_draw_indirect`, `timestamp_query_inside_passes`,
  `pixel_local_storage`, `subgroup_matrix`, `transient_attachments` (`rendering.md` §2.6).

**Costs and risks.**
- WebGPU's feature envelope: no indirect command buffers, no bindless, default limits that must
  be raised on native (`maxColorAttachmentBytesPerSample` 32 by default; 8 storage buffers per
  stage), no multi-draw-indirect with GPU count on Metal in wgpu and unverified in Dawn. For the
  visual techniques catalogued in `rendering-techniques.md` this is livable: compute particles with
  `drawIndirect` + GPU-written instance count, ping-pong feedback, froxel volumes via 3D storage
  textures, TAA, bloom chains, SSR all fit (`particles.md` §11 lists every requirement and WebGPU
  meets 1-12 and 14; 13 and 15 are design choices, not API features).
- WGSL as the front door. The shader research (`shaders.md` §1) recommended SPIR-V as the pivot
  IR; with Dawn the pivot becomes WGSL, and the prebuilt archive's Tint has **no SPIR-V reader**
  (verified: `tint --help` lists `--input-format <wgsl>` only). Consequences: engine shaders are
  authored in WGSL; GLSL/ISF user shaders will need glslang -> SPIR-V -> (Tint SPIR-V reader in a
  from-source Dawn build, or naga-cli offline, or Slang's WGSL target). This is a milestone 0.4
  problem and three viable paths exist. See §12.
- API drift. Dawn tags releases by date, not semver, and adds extensions constantly; wgpu breaks
  quarterly. Mitigation: pin the archive URL and SHA, and keep all `webgpu.h` usage inside the
  `gpu` module so an upgrade is a one-module change.
- Debugging: Xcode captures the process fine but shows Tint-generated MSL. Acceptable; Tint's
  output is readable and preserves identifiers.
- Prebuilt archive specifics (verified): arm64-only, `minos 26.0` deployment target, Release only
  built with the macOS 26.5 SDK. A from-source build is required for Intel Macs, older macOS, or
  a Debug Dawn. Documented in `docs/build.md`.
- Deterministic offline rendering: WebGPU exposes no `fast-math` toggle, but Tint does not emit
  `-ffast-math` for Metal by default (unverified; flagged in `offline-rendering.md`). Same-GPU
  determinism is achievable; cross-GPU determinism is not promised by any option.

**Dawn versus wgpu-native.** Dawn: reference implementation, implements the stable `webgpu.h`,
C++20, Chrome-tested Metal, prebuilt CMake packages, WGSL-only input. wgpu-native: GLSL and SPIR-V
input, push constants, MDI-count on Vulkan/D3D12, but "does not yet implement the stable version"
of `webgpu.h` (webgpu-headers README, accessed 2026-09-08), requires Cargo to build from source,
and its Metal backend lacks argument buffers (`rendering.md` §2.5). Dawn is the better first
implementation; the facade must not use Dawn-specific headers outside one file so wgpu-native
remains a drop-in.

## 5. Option C: SDL_GPU as the RHI

**Shape.** SDL3's `SDL_gpu.h`: Metal/Vulkan/D3D12 with compute passes, storage textures,
indirect draw/dispatch, fenced readback, HDR swapchains. Shaders as backend-native blobs (MSL on
macOS) or HLSL via SDL_shadercross at runtime (`rendering.md` §3.2).

**Strengths.** Fastest to a portable running engine; one zlib dependency solves windowing, input,
audio devices, HiDPI and rendering; first-class CMake; stable semver releases (3.4.16 on
2026-09-02).

**Costs.** Hard cap of **4 colour targets** per pass (R4 partially failed); no MDI-count, no
bindless, fixed binding-slot convention; explicit scope statement that cutting-edge features are
out of scope; SDL_shadercross has no tagged release; no supported escape hatch to native Metal.
The 4-MRT cap is the decisive one: a future deferred/g-buffer path or multi-output feedback pass
would hit it immediately, and there is no way around it inside the API.

## 6. Option D: Adopt an existing rendering library or engine

Each is eliminated by a single decisive reason (`rendering.md` §5):

| Library | Decisive reason |
|---|---|
| bgfx | Offline shader dialect and compiler (R5); per-draw indirect loop on Metal; single maintainer |
| sokol_gfx | No indirect draw/dispatch (R3), no readback API (R7); three breaking changes in two months |
| Filament | No user compute, no indirect, no post-process hook; owns the frame (R12) |
| OGRE-Next | Full scene-graph engine that owns the frame (R12); three shader dialects per effect |
| Diligent Engine | Native Metal backend is commercial-only; open-source macOS is MoltenVK |
| The Forge | Current public release is Windows/D3D12 only, no CMake, Metal frozen in v1.63 |
| LLGL | Credible thin option, but beta, single maintainer, no shader toolchain (R11) |
| NVRHI, Magnum | No Metal |

sokol_gfx deserves a note: it is the most pleasant API in the list and now has compute, but it
would fight us at precisely R3 and R7, the two requirements that GPU particles and offline
rendering depend on.

## 7. Option E: Vulkan everywhere (MoltenVK or KosmicKrisp on macOS)

**Shape.** Write against Vulkan 1.3/1.4 directly (or via a thin helper such as vk-bootstrap +
VMA) and run on macOS through MoltenVK (layered, Apache-2.0, 1.4.2 on 2026-07-24) or KosmicKrisp
(Mesa, MIT, Khronos-conformant Vulkan 1.3 on Metal 4, macOS 26 only, conformant 2025-10-29).

**Strengths.** One explicit API on every platform; RenderDoc on Windows/Linux; SPIR-V front door
matches the shader research exactly; MDI-count, bindless, everything.

**Costs.** Vulkan's verbosity is the RHI-writing cost of Option A without the Metal fidelity:
synchronisation, descriptor management, and memory allocation are ours. On macOS it is a layer on
a layer; KosmicKrisp is under a year old and macOS-26-only; MoltenVK carries known portability
subset gaps. Shader profiling shows MoltenVK/KosmicKrisp-generated MSL. Time to first pixel is
the worst of all options (T1). Re-evaluate in 2027 if KosmicKrisp matures; it would then be the
natural second implementation of the facade or a replacement for Dawn's Vulkan backend.

## 8. Scoring

Scores 0-3 per requirement (3 = fully met with no caveats). Weights reflect the brief: the
audiovisual layer is the product, macOS is first, portability is architectural, and time to a
working vertical slice matters.

| Requirement (weight) | A: Metal RHI | B: WebGPU/Dawn | C: SDL_GPU | E: Vulkan |
|---|---|---|---|---|
| R1 native Metal (3) | 3 | 3 | 3 | 1 |
| R2 compute (3) | 3 | 3 | 3 | 3 |
| R3 indirect (2) | 3 | 2 | 2 | 3 |
| R4 MRT/HDR (2) | 3 | 3 | 1 | 3 |
| R5 runtime shaders (3) | 3 | 3 | 2 | 2 |
| R7 readback (2) | 3 | 3 | 3 | 3 |
| R10 portability (3) | 0 | 3 | 3 | 3 |
| R11 maintenance (2) | 3 | 3 | 3 | 3 |
| R12 doesn't own frame (3) | 3 | 3 | 3 | 3 |
| T1 time to first pixel (3) | 1 | 2 | 3 | 0 |
| T2 not an RHI from scratch (2) | 0 | 3 | 3 | 1 |
| Debugging on macOS (1) | 3 | 2 | 2 | 1 |
| **Weighted total (max 87)** | **68** | **82** | **76** | **62** |

The weighting is a judgement, not a measurement; the ranking B > C > A > E is stable under any
weighting that values portability and T1/T2 at all. A only wins if R10 and T2 are weighted zero.

## 9. The layers above: what the facade and the audiovisual layer look like

These choices are independent of layer 1 but are made now so the facade is shaped correctly.

**Frame organisation: an ordered pass list now, a frame graph later.** `particles.md` §11.1 and
`rendering-techniques.md` (FrameGraph, O'Donnell 2017) both call for passes as peers with declared
resource reads/writes. Milestone 0.1 implements a linear `FrameBuilder`: a vector of passes
(render, compute, blit) executed in order into one command buffer. Resource lifetimes are explicit
(persistent handles; transient pool added when post-processing arrives in 0.6). This is the
minimum that does not preclude a real render graph.

**Scene representation: plain structs and vectors, no ECS in 0.1.** `tooling.md` §8 and
`audiovisual-systems.md` §21 both conclude an ECS earns its keep only once there are many entity
types with many components. 0.1 has one mesh, one camera, one light. The scene module exposes
`Scene { std::vector<Entity> }` with typed components as members; EnTT is a candidate for 0.2 when
glTF hierarchies arrive. The seam is that nothing outside `scene/` iterates entities directly.

**Parameters live above the scene, not inside the renderer.** Every scene property that the brief
calls "interesting" is a `Parameter<T>` with a path, default, range and current value
(`audiovisual-systems.md` lesson 4). Modulation routes (audio signal -> processor chain -> amount ->
parameter) are data owned by the scene document, evaluated once per frame before the renderer
reads parameter values (lesson 3). The renderer never sees an audio signal. See ADR-011.

**Time is injected.** A `FrameClock` produces `RenderTime`; live mode advances by measured delta,
offline mode by `frameIndex / fps`. Audio analysis is addressed by sample position, not wall time
(`offline-rendering.md` §12.9-11, `audio-analysis.md` §1.4). See ADR-012.

## 10. Decision

**Option B: WebGPU as the RHI, implemented by Dawn, wrapped by a thin in-house `gpu` module.**

Concretely:
1. The engine links `dawn::webgpu_dawn` from the pinned prebuilt macOS arm64 Release archive
   (fast path) or a from-source Dawn built with CPM (`AVGEN_DAWN_FROM_SOURCE=ON`; required for
   Intel, older macOS, Debug Dawn, or Tint's SPIR-V reader).
2. All `webgpu.h` / `webgpu_cpp.h` usage is confined to `src/gpu/`. Dawn-specific headers
   (`dawn/native/*`) appear in exactly one file (device creation), so wgpu-native can replace Dawn.
3. Shaders are authored in WGSL and loaded from files at runtime. Hot reload is a small addition
   (file watcher + pipeline swap) scheduled for 0.4 alongside the user-shader format.
4. Windowing is SDL3 (ADR-002), which hands the `CAMetalLayer` from `SDL_Metal_CreateView` to
   `WGPUSurfaceSourceMetalLayer` (struct name verified in Dawn's `webgpu.h`).
5. Dear ImGui uses `imgui_impl_sdl3` + `imgui_impl_wgpu` in Dawn mode (ADR-007).

## 11. Consequences

- **Positive.** Windows and Linux builds are a windowing/surface detail, not a rendering rewrite.
  The `gpu` module is small (hundreds of lines, not thousands) so the audiovisual layer starts
  immediately. Validation errors are readable. Two independent implementations of the API exist.
- **Negative.** WGSL is the engine shader language; the SPIR-V-centric user-shader pipeline from
  `shaders.md` must be re-routed (§12). Some Metal-only features are unreachable without dropping
  to `dawn/native/MetalBackend.h`, which we forbid outside `src/gpu/`. The prebuilt archive pins
  us to macOS 26+ on Apple silicon until a from-source build is exercised. Dawn's API drift means
  each upgrade is a deliberate, tested step.
- **Neutral.** Xcode GPU capture works but shows generated MSL. Timestamp queries need the
  `timestamp-query` feature requested at device creation; Dawn supports it on Metal.

## 12. Reconciling the shader research with the WebGPU decision

`shaders.md` was written with SPIR-V as the pivot IR. With Dawn the pivot is WGSL. The
recommendations survive with these adjustments:

| shaders.md recommendation | Adjusted for Dawn |
|---|---|
| Author engine shaders in Slang, emit SPIR-V, SPIRV-Cross to MSL | Author engine shaders in **WGSL** for 0.1-0.3. Evaluate **Slang -> WGSL** (Slang has a WGSL target) in 0.4 if WGSL's lack of modules/generics becomes a cost. |
| User shaders: ISF (GLSL) -> glslang -> SPIR-V -> SPIRV-Cross | User shaders: ISF (GLSL) -> glslang -> SPIR-V -> **Tint SPIR-V reader** (needs `TINT_BUILD_SPV_READER=ON`, from-source Dawn) **or naga** (`naga-cli` GLSL -> WGSL) **or Slang** (GLSL-compatible front end -> WGSL). Prototype all three in 0.4 and pick by output quality. WGSL-native user shaders are supported with zero translation from day one. |
| Reflection via SPIRV-Reflect | Reflection via **Tint's inspector** (`libtint_lang_wgsl_inspector.a` ships in the archive) or a minimal WGSL `struct Params` parser plus explicit JSON metadata (ISF style). The ISF JSON header is the source of truth for parameter metadata regardless. |
| Hot reload: efsw -> recompile -> swap | Unchanged. `wgpuDeviceCreateShaderModule` at runtime; Dawn's compilation-info callback provides diagnostics for the fallback error shader. |
| Threadgroup size from `LocalSize` reflection | Not needed: WGSL `@workgroup_size` is in the shader and Tint handles Metal's dispatch-time size. |

## 13. Exit strategy

If WebGPU's envelope becomes the limiting factor (evidence would be: a required technique that
cannot be expressed, or a measured >20% performance gap against a native prototype), the `gpu`
module's interface is the seam. Two exits exist and both are incremental:
1. **wgpu-native** for native-only extensions (push constants, MDI-count on Vulkan/D3D12), same
   API, one-file change in device creation.
2. **A native Metal implementation of the `gpu` interface** (Option A, deferred). Because the
   facade is WebGPU-shaped (bind groups, explicit pipelines, passes) it maps cleanly onto Metal
   and Vulkan; this is the same shape Dawn's own backends implement.

The audiovisual layer above the facade is unaffected by either exit. That property is the reason
the facade exists, and it is the only reason: the facade adds no features of its own.

## 14. Sources

Primary evidence lives in the per-topic documents. Sources consulted directly for this document
on 2026-09-08:

- Dawn releases (GitHub API): https://api.github.com/repos/google/dawn/releases/latest — release
  `v20260907.201642`, 13 assets including macOS arm64 Release/Debug archives, Apple xcframework,
  headers. Archive downloaded and inspected: `libwebgpu_dawn.a` (arm64, minos 26.0, SDK 26.5),
  `lib/cmake/Dawn/DawnConfig.cmake`, `bin/tint` (input format WGSL only). Confidence: high.
- Dawn CMake quickstart: https://raw.githubusercontent.com/google/dawn/main/docs/quickstart-cmake.md —
  `DAWN_FETCH_DEPENDENCIES=ON`, `DAWN_ENABLE_INSTALL=ON`, `find_package(Dawn)`,
  `dawn::webgpu_dawn`, C++20. Confidence: high.
- Dear ImGui WebGPU backend header: https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_wgpu.h —
  `IMGUI_IMPL_WEBGPU_BACKEND_DAWN` define, `ImGui_ImplWGPU_InitInfo`. Confidence: high.
- SDL3 `SDL_Metal_CreateView`: https://wiki.libsdl.org/SDL3/SDL_Metal_CreateView — creates a
  CAMetalLayer-backed view; `SDL_Metal_GetLayer` returns the layer. Confidence: high.
- W3C WebGPU specification, limits and features: https://www.w3.org/TR/webgpu/ — default limits
  cited in `rendering.md` §2.5. Confidence: high.
- All other claims: see `rendering.md` (118 sources), `particles.md`, `rendering-techniques.md`,
  `offline-rendering.md`, `shaders.md`, `tooling.md`, `audiovisual-systems.md`.
