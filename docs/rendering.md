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

## Frame (milestone 0.2)

```
encoder = device.CreateCommandEncoder()
  [environment passes: only when scene.environment.environmentMap changed; see ADR-013]
  pass "scene-pass"  : HDR RGBA16Float + Depth24Plus, clear to environment.backgroundColor
                       opaque PBR entities (pbr.wgsl; back-face cull, or none for doubleSided)
                       skybox (skybox.wgsl, far plane, LessEqual) when an environment is set
                       grid entities (grid.wgsl, additive, depth test only)
                       particles (particles.wgsl, indirect draw, additive/alpha, depth test only)
                       alpha-blended PBR entities, sorted back to front
  pass "tonemap-pass": fullscreen triangle, textureLoad HDR, ACES fitted, sRGB encode -> target
  [pass "ui-pass"    : Dear ImGui, LoadOp::Load]           (added by the application)
timer.resolve(encoder); queue.Submit; timer.collect(); surface.Present()
```

Bind groups: 0 `FrameUniforms` (704 B: viewProj, invViewProj, cameraPos, params, envParams,
skyParams, 8 `LightUniform`s); 1 `ObjectUniforms` (192 B: model, normalMatrix, baseColor+opacity,
emissive rgb+intensity, material roughness/metallic/normalScale/occlusion, flags alphaMode/
cutoff/unlit/textureMask) in one buffer with 256-byte dynamic offsets (up to 256 objects);
2 material (one filtering sampler + baseColor, metallicRoughness, normal, emissive, occlusion
textures; 1x1 defaults fill absent slots; bind groups cached per texture combination);
3 image-based lighting (clamp sampler, irradiance cube, prefiltered cube, BRDF LUT). The C++
structs are `static_assert`ed against the WGSL layouts. Vertex layout: position, normal, uv
(32 bytes, `scene::Vertex`); tangents are derived per fragment.

Materials follow glTF metallic-roughness: textures multiply factors; normal maps are applied
through a derivative-based cotangent frame; alpha mask discards below the cutoff; blend
materials draw last without depth write. Lights: up to 8 enabled `PunctualLight`s per frame
(directional, point with inverse-square and range window, spot with smooth cone). Ambient comes
from the environment (split sum) or a hemispheric fallback when no map is set.

Textures are uploaded with CPU-generated mip chains (sRGB filtered in linear space); HDR maps as
RGBA16Float. Uploads happen when `Scene::textureVersion` changes.

Conventions: right-handed, +Y up, CCW front faces, clip depth 0..1 (`GLM_FORCE_DEPTH_ZERO_TO_ONE`,
`glm::perspectiveRH_ZO`). Scene-linear HDR until the tone map; `environment.brightness` is the
exposure.

Meshes are uploaded when `Scene::meshVersion` changes (all meshes re-uploaded; fine for 0.1).
Invalid meshes and entities referencing missing meshes are skipped with a warning and no GPU
error.

## Particles (milestone 0.5, ADR-015; deterministic compaction 2026-09-08)

`ParticleRenderer` runs one compute pass per enabled `scene::ParticleSystem` before the scene
pass and one `DrawIndirect` of camera-facing quads inside the scene pass after the grid
(additive premultiplied or alpha, depth test only). The compute pass is five ordered dispatches
with no atomics, so slot assignment, per-slot random seeds and draw order are a pure function of
(slot, frame index, parameters): `cs_emit` (spawn `i` takes `deadList[i]`, clamped to last
frame's `deadCount`), `cs_simulate` (gravity, drag, curl-noise turbulence, attractor/orbit, kill;
writes an alive flag per slot), then a stable stream compaction: `cs_scan_reduce` (alive count
per 1024-slot block), `cs_scan_top` (one workgroup scans the block sums and writes
`aliveCount`, `deadCount` and the indirect args) and `cs_scan_scatter` (alive and dead lists in
slot order). Pools: particle AoS buffer, dead list, alive list, flags, block sums, counters,
indirect args; created per (system, capacity), reset on creation and on `resetAll()`. Emission
uses a fractional carry so low rates emit evenly; bursts add particles for one frame; requests
beyond the free slots are dropped. All settings are per-frame uniforms.
`ParticleRenderer::readCounts(i)` reads a pool's alive/dead counts back (blocking; tests only).

## Post-processing (milestone 0.6, ADR-016)

`PostProcessor` runs the built-in chain on `gpu::TransientPool` textures: depth of field
(view distance reconstructed from depth, CoC gather), camera motion blur (reprojection with the
previous view-projection, neighbourhood-max velocity), bloom (soft-knee prefilter, 13-tap
downsample chain, tent upsample chain), and a composite pass (distortion, chromatic aberration,
white balance, hue, contrast, saturation, lift/gamma/gain). The output pass tone-maps with the
selected operator and applies vignette and seeded grain. All settings come from `Scene::post`
(`post/*` parameters). Disabled effects add no passes; a full chain is 13 passes at 1280x720.

## Lifecycle

`Context::create` → `SceneRenderer::init` (layouts, buffers, pipelines) → `resize(w, h)` (HDR
target; tonemap bind group is rebuilt lazily) → per frame `render(encoder, scene, time, target)`.
Tone-map pipelines are cached per target format (BGRA8 swapchain, RGBA8 capture). Surface loss
or outdated swapchains are reconfigured once inside `acquireSurfaceView`.

## Threading

Everything GPU-related runs on the main thread. Dawn callbacks are delivered from
`ProcessEvents`/`WaitAny` on that thread.

## Performance (see docs/performance.md)

Orb scene at 1280x720: ~0.1 ms GPU. DamagedHelmet with IBL and skybox at 2880x1800: ~1.0 ms
GPU, 0.3 ms CPU work per frame (Release). Environment preprocessing: 18 ms Release for a 1k HDRI.

## Debugging

Xcode GPU capture works on the process (Tint-generated MSL is shown). Dawn validation messages
are logged with the `[wgpu]` prefix and counted; a headless run exits non-zero if any occurred.

## Seams for later milestones

- Pass list → frame graph with transient resources (0.6 post-processing).
- Compute passes in the same encoder; storage buffers/textures, indirect draw/dispatch, 3D
  storage textures are plain WebGPU features (particles, 0.5).
- MSAA, shadows, specular occlusion and multi-scatter compensation are not implemented.
- HDR/EDR swapchains are Dawn features not yet requested.
- `dawn/native/*` headers are forbidden outside `src/gpu/` so wgpu-native remains a drop-in.
