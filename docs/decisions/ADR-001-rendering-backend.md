# ADR-001: Rendering backend

- Status: Accepted (2026-09-08)
- Deciders: engineering lead
- Research: `docs/research/architecture-options.md`, `docs/research/rendering.md`

## Problem

Choose the GPU API layer for a native C++ audiovisual engine that must run first on macOS
(Apple silicon, Metal 4), remain portable to Windows/Linux without a rewrite, support compute
shaders, indirect draw/dispatch, multiple HDR render targets, runtime shader compilation for live
iteration, and GPU->CPU readback for deterministic offline rendering. The layer must not own the
frame; the engine's own render graph and parameter system sit above it.

## Alternatives considered

1. In-house RHI over raw Metal (metal-cpp), Vulkan backend later.
2. WebGPU as the RHI: Dawn (Google) or wgpu-native (gfx-rs).
3. SDL3's SDL_GPU.
4. An existing library or engine: bgfx, sokol_gfx, Filament, OGRE-Next, Diligent, The Forge,
   LLGL, NVRHI, Magnum.
5. Vulkan everywhere via MoltenVK or KosmicKrisp on macOS.
6. OpenGL.

## Decision

**WebGPU, implemented by Dawn, wrapped in a thin in-house `gpu` module.**

- Link `dawn::webgpu_dawn` from the pinned prebuilt release archive (macOS arm64 Release,
  `v20260907.201642`) by default; from-source Dawn via CPM behind `AVGEN_DAWN_FROM_SOURCE=ON`.
- Only `src/gpu/` includes `webgpu/webgpu_cpp.h`. Exactly one file includes any `dawn/native/*`
  header (device/instance creation), so wgpu-native can substitute for Dawn.
- The `gpu` module exposes device, buffers, textures, samplers, shader modules, render/compute
  pipelines, bind groups, an ordered pass list per frame, readback, timestamp queries, and a
  capability struct. It adds no features beyond WebGPU; it exists to isolate API drift and to
  give the engine a seam for an alternative implementation.
- Engine shaders are WGSL loaded from files at runtime (ADR-006).

## Rationale

- Portability from day one with no second backend to write; Metal on macOS is the same backend
  Chrome ships, Vulkan and D3D12 exist already.
- Meets every renderer requirement extracted from the particle, technique, and offline-rendering
  research (compute with storage buffers and 3D storage textures, indirect draw and dispatch,
  8 colour attachments, float targets, map-async readback, timestamp queries) except
  multi-draw-indirect with GPU count on Metal, which no candidate other than raw Metal/Vulkan
  offers and which is not required by the roadmap.
- Runtime shader compilation with readable validation errors, which matters for a product whose
  users will drop in arbitrary shaders.
- Maintained by two browser vendors; two independent implementations of the same C API.
- Verified on 2026-09-08 that Dawn's prebuilt macOS archive contains a static library, headers,
  Tint, and CMake package config, and that Dear ImGui and SDL3 integrate with it directly.
- The brief explicitly steers away from writing a renderer from scratch against Metal; scoring
  in `architecture-options.md` §8 ranks WebGPU first under any weighting that values portability
  and time-to-first-pixel.

## Consequences

- WGSL becomes the engine shader language; the SPIR-V-pivot pipeline from the shader research is
  re-routed through Tint's SPIR-V reader, naga, or Slang's WGSL target for user GLSL shaders
  (milestone 0.4). WGSL user shaders work with zero translation.
- Metal-only features (indirect command buffers, MetalFX, residency sets, EDR output control) are
  unreachable except through the forbidden `dawn/native/MetalBackend.h`. None is on the roadmap
  before 1.x; revisit if one becomes required.
- The prebuilt archive is arm64-only with a macOS 26.0 deployment floor. Distribution to Intel
  Macs or older macOS requires the from-source build, which is configured but must be exercised
  before any release.
- Dawn releases are date-tagged; upgrades are deliberate: bump the pinned URL and SHA, rebuild,
  run the rendering tests.
- Default WebGPU limits (`maxColorAttachmentBytesPerSample` 32, 8 storage buffers per stage,
  256 invocations per workgroup) must be raised at device creation when needed; the `gpu` module
  requests the adapter's maximum limits.
- Xcode GPU capture works but shows Tint-generated MSL.

## Rejected alternatives

- Raw Metal RHI: 10-20k lines of platform code before Windows/Linux exist; contradicts the
  brief's steer; delays the audiovisual layer.
- SDL_GPU: hard cap of 4 colour targets; no escape hatch; explicit "not for cutting-edge
  features" scope.
- bgfx: offline shader dialect blocks live shader iteration; single maintainer.
- sokol_gfx: no indirect draw/dispatch, no readback API, frequent breaking changes.
- Filament, OGRE-Next: own the frame.
- Diligent: Metal backend is commercial-only.
- The Forge: Windows-only current release, no CMake.
- LLGL: single-maintainer beta, no shader toolchain.
- NVRHI, Magnum: no Metal.
- Vulkan via MoltenVK/KosmicKrisp: worst time to first pixel; layered on macOS; KosmicKrisp is
  under a year old. Re-evaluate in 2027.
- OpenGL: deprecated on macOS, 4.1 maximum, no compute.
- wgpu-native (as first implementation): does not implement the stable `webgpu.h` yet, needs
  Cargo, Metal backend lacks argument buffers. Remains the designated alternative implementation.

## Revisit triggers

- A required technique that WebGPU cannot express, or a measured >20% performance gap against a
  native prototype of the same pass.
- KosmicKrisp reaching Vulkan 1.4 conformance with a stable release cadence.
- Dawn ceasing to publish prebuilt archives (fall back to from-source; already configured).
