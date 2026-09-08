# Rendering

Decision: ADR-001 (WebGPU via Dawn). Research: `docs/research/rendering.md`,
`docs/research/architecture-options.md`, `docs/research/rendering-techniques.md`.

## Responsibilities

- `gpu::Context`: Dawn instance (with `TimedWaitAny`), adapter (Metal, high performance),
  device with the adapter's full limits and `timestamp-query` when available, queue, optional
  surface from a `CAMetalLayer` (BGRA8Unorm, FIFO). Counts uncaptured errors; exposes
  `waitFor(Future)`, `waitForQueue()`, `processEvents()`.
- `gpu::ShaderLibrary`: WGSL files from a search path with `#include "x.wgsl"`; compiles inside
  an error scope and reads `GetCompilationInfo` so failures carry `file:line:col` diagnostics.
- `gpu::RenderTarget`: colour (+depth) offscreen textures. `gpu::TargetView`: a view someone else
  owns (swapchain, capture texture).
- `gpu::GpuTimer`: two timestamps per frame resolved through a 4-slot mapped-buffer ring; never
  stalls; reports -1 without the feature.
- `gpu::readTexture8`, `hashImage`, `writePpm`: synchronous readback for tests and captures.
- `rendering::SceneRenderer`: the frame's pass list for a `scene::Scene`.

## Frame

```
encoder = device.CreateCommandEncoder()
  pass "scene-pass"  : HDR RGBA16Float + Depth24Plus, clear to environment.backgroundColor
                       lit entities (mesh.wgsl, back-face cull, depth write)
                       grid entities (grid.wgsl, additive blend, depth test only)
  pass "tonemap-pass": fullscreen triangle, textureLoad HDR, ACES fitted, sRGB encode -> target
  [pass "ui-pass"    : Dear ImGui, LoadOp::Load]           (added by the application)
timer.resolve(encoder); queue.Submit; timer.collect(); surface.Present()
```

Uniforms: `FrameUniforms` (128 B: viewProj, cameraPos, lightDir, lightColor, params{time,
gridIntensity, brightness}) in bind group 0; `ObjectUniforms` (176 B: model, normalMatrix,
baseColor, emissive rgb+intensity, material roughness/metallic) in one buffer with 256-byte
dynamic offsets in bind group 1 (up to 256 objects); `TonemapUniforms` (exposure). The C++
structs are `static_assert`ed against the WGSL layouts. Vertex layout: position, normal, uv
(32 bytes, `scene::Vertex`).

Conventions: right-handed, +Y up, CCW front faces, clip depth 0..1 (`GLM_FORCE_DEPTH_ZERO_TO_ONE`,
`glm::perspectiveRH_ZO`). Scene-linear HDR until the tone map; `environment.brightness` is the
exposure.

Meshes are uploaded when `Scene::meshVersion` changes (all meshes re-uploaded; fine for 0.1).
Invalid meshes and entities referencing missing meshes are skipped with a warning and no GPU
error.

## Lifecycle

`Context::create` → `SceneRenderer::init` (layouts, buffers, pipelines) → `resize(w, h)` (HDR
target; tonemap bind group is rebuilt lazily) → per frame `render(encoder, scene, time, target)`.
Tone-map pipelines are cached per target format (BGRA8 swapchain, RGBA8 capture). Surface loss
or outdated swapchains are reconfigured once inside `acquireSurfaceView`.

## Threading

Everything GPU-related runs on the main thread. Dawn callbacks are delivered from
`ProcessEvents`/`WaitAny` on that thread.

## Performance (see docs/performance.md)

Scene + tone map on the M2 Max at 1280x720: ~0.13 ms GPU. Two draw calls plus one fullscreen
triangle. CPU cost per frame is dominated by ImGui and the uniform writes.

## Debugging

Xcode GPU capture works on the process (Tint-generated MSL is shown). Dawn validation messages
are logged with the `[wgpu]` prefix and counted; a headless run exits non-zero if any occurred.

## Seams for later milestones

- Pass list → frame graph with transient resources (0.6 post-processing).
- Compute passes in the same encoder; storage buffers/textures, indirect draw/dispatch, 3D
  storage textures are plain WebGPU features (particles, 0.5).
- MSAA and HDR/EDR swapchains are Dawn features not yet requested.
- `dawn/native/*` headers are forbidden outside `src/gpu/` so wgpu-native remains a drop-in.
