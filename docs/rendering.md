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
- `gpu::FrameTimeline`: one timestamp per pass, written at the pass's end into a single query set
  in submission order, so a pass costs `end[i] - end[i-1]` and the passes partition the frame.
  Resolved once per frame through a 4-slot mapped-buffer ring; never stalls; reports -1 without
  the feature. It replaced per-pass begin/end pairs, which measured the wrong thing entirely --
  see docs/performance.md.
- `gpu::readTexture8`, `readTextureF16`, `hashImage`, `writePpm`: synchronous readback for tests
  and captures; `gpu::ReadbackRing`: asynchronous readback through three staging buffers for the
  offline render job (ADR-020 revision).
- `rendering::SceneRenderer`: the frame's pass list for a `scene::Scene`.

## Frame

```
encoder = device.CreateCommandEncoder()
  [environment passes: only when scene.environment.environmentMap changed; see ADR-013]
  pass "scene-pass"  : HDR RGBA16Float + Depth24Plus, clear to environment.backgroundColor
                       opaque PBR entities (pbr.wgsl; back-face cull, or none for doubleSided)
                       procedural geometry (procedural.wgsl; one DrawIndexed(indexCount, instanceCount)
                         per object, deformer stack in the vertex stage, same fragment shading; ADR-023)
                       skybox (skybox.wgsl, far plane, LessEqual) when an environment map is set,
                         or when the procedural sky asks to be the background (ADR-036)
                       grid entities (grid.wgsl, additive, depth test only)
                       particles (particles.wgsl, indirect draw, additive/alpha, depth test only)
                       alpha-blended PBR entities, sorted back to front
  pass "tonemap-pass": fullscreen triangle, textureLoad HDR, AgX (default; ADR-039), sRGB encode
  [pass "ui-pass"    : Dear ImGui, LoadOp::Load]           (added by the application)

  [simulation, particle, procedural effector/cull and SDF compute passes]
  pass "cluster-build-pass" : compute, 16x8x24 froxels of light indices                (ADR-033)
  pass "shadow-pass" x N    : depth only, one per cascade / spot map, into the atlas   (ADR-034)
                              opaque entities, meshed SDFs, procedural instances (same
                              instance buffer), raymarched SDFs at a quarter of the steps
  pass "background-pass"    : HDR colour cleared, background user-shader layers
  pass "depth-prepass"      : depth only, the same casters, into the scene depth       (ADR-035)
  pass "linear-depth-pass"  : Depth24Plus -> R32Float view distance
  pass "gtao-pass"          : half-resolution horizon occlusion + bent normal          (ADR-034)
  pass "gtao-temporal-pass" : reprojected, neighbourhood-clamped accumulation
  pass "shadow-mask-pass"   : half-resolution shadow-map term of up to three directional
                              lights, bilaterally upsampled in the scene pass         (ADR-087)
  pass "scene-pass"         : HDR RGBA16Float + 4 auxiliary targets + Depth24Plus      (ADR-035)
                              opaque PBR entities (pbr.wgsl; back-face cull, or none for doubleSided)
                              procedural geometry (procedural.wgsl; one DrawIndexed(indexCount,
                                instanceCount) per object, deformer stack in the vertex stage; ADR-023)
                              meshed SDFs, then "sdf-raymarch-pass" for raymarched ones
                              skybox (skybox.wgsl, far plane, LessEqual) when an environment map is set,
                         or when the procedural sky asks to be the background (ADR-036)
                              grid entities (grid.wgsl, additive, depth test only)
                              particles (particles.wgsl, indirect draw, additive/alpha, depth test only)
                              alpha-blended PBR entities, sorted back to front
  [volume march + composite, debug geometry, user post layers, the built-in post chain]
  [pass "aux-debug-pass"    : one auxiliary target full-screen, when a debug view is selected]
  pass "tonemap-pass"       : fullscreen triangle, textureLoad HDR, ACES fitted, sRGB encode -> target
  [pass "ui-pass"           : Dear ImGui, LoadOp::Load]        (added by the application)
timer.resolve(encoder); queue.Submit; timer.collect(); surface.Present()
```

The depth prepass exists because ambient occlusion, the contact-shadow march and the shadow mask
need the depth of the whole opaque scene *while it is being shaded*, which a forward pass cannot
give them. It costs one fragment-free geometry pass and pays part of itself back as early-Z in the
scene pass; it is skipped entirely when occlusion, contact shadows and the shadow mask are all off.

Note what the prepass draws, because the shadow mask depends on it: opaque and alpha-masked
geometry, never blended. A blended surface therefore has no mask texel of its own -- the depth
under it belongs to whatever is behind -- and shades its directional shadows at full resolution
instead (ADR-087).

### Colour targets of the scene pass (ADR-035)

Every pipeline used inside `scene-pass` declares all five, in this order, or WebGPU rejects it
(`rendering/scene_targets.hpp` builds the array):

| # | Target | Format | Contents |
|---|---|---|---|
| 0 | HDR colour | RGBA16Float | scene-linear radiance |
| 1 | normal + roughness | RGBA16Float | rg = octahedral normal, b = roughness, a = flags (1 lit, 2 emissive, 4 transparent, 8 sky) |
| 2 | velocity | RG16Float | screen motion in UV units, from this and last frame's clip positions |
| 3 | emission | RGBA16Float | rgb = emitted radiance, a = bloom weight |
| 4 | identifiers | R32Uint | low 16 bits object id, high 16 bits material id |

Alpha-blended surfaces and particles leave targets 1, 3 and 4 to the opaque geometry behind them
(their write masks are off); particles do write velocity, from their simulated previous position.
Two more targets live outside the scene pass: `aux-linear-depth` (R32Float view distance) and the
half-resolution occlusion target (rg = octahedral bent normal, b = visibility, a = view depth).
`SceneRenderer::setAuxDebugView` displays any of them full-screen; `avgen --debug-target
normal|roughness|velocity|emission|ids|occlusion|depth` does the same from the command line.

### Quality tiers

`rendering::QualityTier` (preview, realtime, high, offline) scales sample counts, resolutions and
history lengths only - never the scene, its parameters or its determinism. `avgen --tier <name>`
selects one; `rendering/render_quality.hpp` lists what each scales.

Bind groups: 0 the frame group - 0 `FrameUniforms` (976 B: viewProj, invViewProj, prevViewProj,
camera basis, params, envParams, skyParams, fogParams, audio, cluster and shadow parameters, AO
parameters, target size, and 8 `LightUniform`s for the fallback tier), 1 the packed scene lights
(read-only storage), 2 the froxel light lists, 3 `ShadowUniforms`, 4 the shadow atlas
(`texture_depth_2d_array`), 5 its comparison sampler, 6 the occlusion target, 7 the linear scene
depth, 8/9 the LTC tables, 10 their sampler, 15 the simulated-grid table; 1 `ObjectUniforms`
(272 B: model, normalMatrix, prevModel, baseColor+opacity, emissive rgb+intensity, material
roughness/metallic/normalScale/occlusion, flags alphaMode/cutoff/unlit/textureMask, ids
object/material/bloom weight) in one buffer with 512-byte dynamic offsets (up to 256 objects);
2 material (one filtering sampler + baseColor, metallicRoughness, normal, emissive, occlusion
textures; 1x1 defaults fill absent slots; bind groups cached per texture combination);
3 image-based lighting (clamp sampler, irradiance cube, prefiltered cube, BRDF LUT). The C++
structs are `static_assert`ed against the WGSL layouts. Vertex layout: position, normal, uv
(32 bytes, `scene::Vertex`); tangents are derived per fragment.

The passes that write something the shading pass reads (the shadow maps, the linear depth, the
occlusion target) bind a placeholder in its slot, because WebGPU forbids sampling a resource that
the same pass is writing. `SceneRenderer::rebuildFrameBindGroups` builds the three variants: the
shading group, the auxiliary group, and one per shadow view whose `viewProj` is the light's - which
is what lets every depth-only pass reuse the ordinary vertex shaders unchanged.

Materials follow glTF metallic-roughness: textures multiply factors; normal maps are applied
through a derivative-based cotangent frame; alpha mask discards below the cutoff; blend
materials draw last without depth write. Lighting is clustered forward with area lights, colour
temperature, cascaded shadows, contact shadows and ground-truth occlusion: see `docs/lighting.md`
for the whole of it. Ambient comes from the environment (split sum, looked up along the bent
normal) or a hemispheric fallback when no map is set.

Textures are uploaded with CPU-generated mip chains (sRGB filtered in linear space); HDR maps as
RGBA16Float. Uploads happen when `Scene::textureVersion` changes.

**Image-based lighting always exists** (ADR-036): with an HDR map, `EnvironmentProcessor::process`
builds the cube/irradiance/prefiltered chain from it; without one, `processSky` builds the same
chain from the analytic procedural sky (`scene/sky.hpp`, `docs/lighting.md`), hashed so it is built
once per parameter change rather than per frame. The skybox is only *drawn* for a map, or for a sky
that asks to stand behind the scene.

Distance fog (`Environment::fogColor`, `fogDensity`; 0 = off) is exponential-squared in view
distance, `mix(fogColor, color, exp(-(d * density)^2))`, applied in `pbr_shade.wgsl` after
lighting to lit and unlit surfaces (entities and procedural instances alike); the skybox and
particles are untouched.

Procedural geometry (`rendering::ProceduralRenderer`, ADR-023): per `scene::ProceduralGeometry`
a source mesh cached by `meshHash` (generated with `scene::makeSourceMesh`), an instance storage
buffer of 96-byte `InstanceRecord`s re-uploaded when `structureVersion` changes (re-created when
it grows), a 528-byte deformer/time uniform block written every frame, and one 256-byte
`ObjectUniforms` slot (object matrix + the material fields exactly as entities fill them). Group
1 of `procedural.wgsl` is {0 object uniforms (dynamic offset), 1 instances (read-only storage),
2 `ProceduralUniforms`}; groups 0/2/3 are the entity layouts. The vertex stage runs local-space
deformers, the instance transform (position + rotate(quaternion, p * scale)), the object matrix
and world-space deformers, and recomputes the normal by finite differences of the whole chain
(epsilon = 1e-3 x source bounds radius). Per-object GPU state is keyed by name and dropped after
120 unused frames. `RenderStats::procedural` reports objects, instances, logical triangles, uploads
and CPU update time; draw calls and triangles are folded into the totals.

Conventions: right-handed, +Y up, CCW front faces, clip depth 0..1 (`GLM_FORCE_DEPTH_ZERO_TO_ONE`,
`glm::perspectiveRH_ZO`). Scene-linear HDR until the tone map. `environment.brightness` is a plain
global multiplier in the tone-map pass; the *camera's* exposure (ADR-037) is a separate, earlier
stage in the post chain — see `docs/image-formation.md`.

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

### Motion quality (ADR-040)

Five additions, all off by default so a pre-ADR-040 scene is bit-identical.

**Velocity-aligned stretching.** `velocityStretch` extends the billboard along the *screen
projection* of the simulated velocity by

```
added = clamp(projectedSpeed * shutterSeconds * velocityStretch, 0, stretchMax)   // 0 below stretchMin
shutterSeconds = frameDuration * camera/lens/shutterAngle / 360                   (ADR-037)
```

The quad's basis is rotated so +x runs along that velocity and only its length grows; the width
stays the particle's size, so the radial falloff turns the disc into an ellipse and a slow
particle is exactly the round quad it always was. `stretchMin` is a dead zone that keeps slow
particles perfectly round rather than slightly oval. `scene::particleStretchLength()` is the CPU
mirror of the shader's `stretchLength()`, and the unit tests check them against each other.
Because the stretched quad *is* a shutter smear, the fraction of the shutter it already covers is
subtracted from what the particle writes into the velocity target — otherwise the motion-blur pass
would smear it a second time.

**Trails.** `trailEnabled` gives every particle a ring of `trailLength - 1` previous positions,
recorded by `cs_simulate` *before* integration (so the ribbon's live head is never duplicated by
its newest history entry) every `trailStride`-th step. `vs_ribbon` draws it as `trailLength - 1`
camera-facing quads per particle: point 0 is the live position, point *j* the *j*-th newest
history entry, the side direction is `cross(tangent, toEye)`, and the width and colour taper along
the length by `trailTaper`, `trailFade` and `trailTint`. Points past a particle's own write count
collapse onto the last valid one, so a ribbon *grows* out of a newborn particle instead of
springing from whatever the slot's previous occupant left behind. The history is part of the
simulation state, so two runs of the same frame sequence produce the same ribbon.

Memory is the whole cost: `capacity * (trailLength - 1) * 16` bytes.
`scene::validateParticleSystem()` enforces a **64 MiB per-system budget**, which makes trails
usable for hero emitters (32 k particles x 32 points is 15.5 MiB) and refuses them outright for
million-particle systems. A refusal from the scene loader is an error; a refusal at render time is
logged once and the system falls back to stretched billboards.

**Lifetime curves.** `sizeCurve`, `colorCurve` and `opacityCurve` are up to eight keyframes over
normalised age, packed into the particle uniforms and evaluated in the vertex shader as a clamped
piecewise-linear ramp. A curve with fewer than two keys is *not authored* and the linear
`sizeStart`/`sizeEnd`, `colorStart`/`colorEnd` ramp is used instead, so existing scenes are
unchanged. `ParticleCurve::evaluate()` is the CPU reference for the shader's rule.

**Atmosphere coupling.** Two one-directional links to `VolumeRenderer` (ADR-032):

- `fogCoupling` (default 1) fixes a particle being fogged by the depth of the *surface behind it*.
  The volume composite multiplies the whole HDR buffer by the transmittance it marched to the
  opaque surface, so `fs_particle` divides that out and puts back the transmittance to the
  particle's own depth. Both are estimated from the same exponential height-fog model with four
  midpoint samples; the noise and field terms cancel to first order in the ratio. It reads the
  ADR-035 linear-depth target, and a frame with no depth prepass disables the coupling rather than
  guessing (there is nothing to say what depth the composite marched to).
- `volumeGlow` reduces a system's alive emissive particles to **one aggregate sphere** — the
  emission-weighted centroid, the standard deviation of the positions about it, the mean colour
  and the total power — which `volume.wgsl` adds as an in-scattering source, so sparks light the
  dust around them. The reduction is a fixed-order workgroup tree per scan block plus a serial sum
  over blocks, so it has no atomics and is deterministic. Up to eight systems inject at once;
  slots past that are ignored, and the volume never touches the simulation.

**Motion vectors.** Particles write the velocity target from their simulated previous position,
which is what feeds the motion blur below.

## Image formation (milestone 0.6, ADR-016; reordered by ADR-037 and ADR-039)

The full treatment is `docs/image-formation.md`; this is the summary. `PostProcessor` runs the
chain on `gpu::TransientPool` textures **in this fixed order**, and encodes only the stages whose
settings are active:

```
scene HDR -> volumetrics -> user post layers
  1. metering        centre-weighted average luminance of the pre-exposure image, reduced to 1x1
                     and read back next frame (automatic exposure only)
  2. exposure        one multiply; skipped entirely when the scale is 1
  3. depth of field  view distance from depth, circle-of-confusion gather
  4. motion blur     tile-based reconstruction over the ADR-035 velocity target (ADR-040)
  5. lens            barrel/pincushion distortion, chromatic aberration
  6. bloom           soft-knee prefilter, 13-tap downsample chain, energy-conserving tent upsample
  7. halation        a wider, warm-weighted second pyramid; anamorphic streaks share its pass
  8. composite       bloom and the wide tier mixed in, then white balance, hue, contrast,
                     saturation, lift/gamma/gain
  9. sharpen         contrast-adaptive, optionally masked by object identifier
 -> tonemap pass     the selected operator (AgX by default), vignette, seeded grain, sRGB encode
```

Exposure moved ahead of bloom (ADR-039) so that `post/bloom/threshold` is a number in exposed
units; the depth-aware stages stay ahead of lens distortion because distortion resamples the
image and the depth buffer is not resampled with it. The upsample blends the levels
(`mix(fine, tent(coarse), radius/2)`) instead of adding them, so the pyramid's mean equals the
prefiltered image's mean whatever the level count — the old additive chain multiplied a
highlight's energy by the number of levels, which is why every surface glowed.

Settings come from `Scene::post` (`post/*` parameters) plus the camera's own exposure and lens
blocks (`camera/exposure/*`, `camera/lens/*`, `camera/focus/*`; ADR-037), which `app::Engine`
copies onto `Scene::camera` and into `Scene::post` each frame. `Camera::projection` uses
`effectiveFovY()`, which is the explicit `fovYRadians` unless `camera/lens/useExplicitFov` is off,
in which case it is derived from focal length and sensor height. All the defaults are no-ops, so
scenes authored before ADR-037 render unchanged.

Selective post (ADR-039) reads the ADR-035 emission and identifier targets through
`PostFrameInputs::emission` / `::identifier`. Those are optional: when they are null the chain
binds 1x1 placeholders, clears the `available` flags in the uniforms and every effect falls back to
its luminance-only behaviour, so nothing changes until the renderer starts writing them.

Cost at 1080p: one pass with everything off, 12 with bloom (0.3 ms of GPU on an M2 Max), 32 with
bloom, halation, anamorphic, depth of field and automatic exposure all on. Depth of field dominates.
See `docs/performance/image-formation.md`.

## Outputs (milestone 1.2)

An *output* is a window with its own swapchain (`gpu::Surface`, one per `CAMetalLayer`; the
primary window's surface is the one `gpu::Context` owns and forwards to) that shows the final
frame through an `rendering::OutputMapping`. `app::OutputManager` owns the set: `open()` creates
the windows and surfaces for the enabled outputs (fullscreen on a chosen display, borderless,
always-on-top), `pumpEvents()` resizes swapchains and drops windows the user closed, and
`presentAll(context, finalTexture, w, h)` acquires every open surface, encodes one
`OutputMapper::draw` per output in its own command buffer, submits and presents. The main window
uses the same mapper with the identity mapping, so the final frame is rendered once into an
RGBA8/BGRA8 texture with `TextureBinding` usage and copied to every sink.

`OutputMapper::draw(encoder, source, target, targetWidth, targetHeight, mapping, targetFormat)`
is one fullscreen pass (`shaders/output_map.wgsl`) that clears the target to black and, per
target pixel `p` in normalised target space (0..1, top-left origin):

1. **Warp.** `mapping.corners` are the target-space positions of the source quad's TL, TR, BR,
   BL corners. The CPU computes the homography `H` from the unit square to that quad
   (`homographyFromCorners`, Heckbert's square-to-quad form; affine quads give a bottom row of
   `0 0 1`) and uploads its inverse, normalised so that `w > 0` at the quad centre. The shader
   evaluates `q = H^-1 · (p, 1)`, `uv = q.xy / q.z` (perspective-correct) and treats `q.z <= 0`
   or `uv` outside 0..1 as *outside the quad*: black.
2. **Blend.** Soft edges in quad space, per edge `weight = pow(clamp(d / width, 0, 1),
   blendGamma)` where `d` is the distance from that edge (`uv.x` for `left`, `1 - uv.x` for
   `right`, `uv.y` for `top`, `1 - uv.y` for `bottom`); a zero width is weight 1. The four
   weights multiply. Two projectors overlapping by 10 % of their width use `right = 0.1` on the
   left projector and `left = 0.1` on the right one with the same `blendGamma` (2.2 approximates
   linear-light addition of sRGB-encoded frames).
3. **Flip and crop.** `flipX` / `flipY` mirror `uv`, then `src = crop.xy + uv * crop.wh`
   selects the part of the source (`crop` in 0..1 of the source, top-left origin).
4. **Sample and grade.** Linear filtering, clamp to edge, no colour-space conversion: the
   source is already display-encoded. `colour = pow(max(colour * brightness, 0), 1 / gamma) *
   weight`; `gamma > 1` lifts midtones.

`OutputMapping::identity()` (full crop, unit-square corners, no blend, brightness and gamma 1,
no flips) takes a plain-blit fast path (`fs_blit`); `OutputMapper::setFastPathEnabled(false)`
forces the full path (tests). Pipelines are cached per target format; uniforms live in a
64-slot ring with dynamic offsets so several draws can share one submission. GPU-free parts
(`homographyFromCorners`, `inverseHomography`, `projectPoint`, `blendWeight`, JSON,
`validate()` — crop inside 0..1, convex non-degenerate corners, widths in 0..1, positive gammas)
are in `rendering/output_mapping.hpp` and unit-tested; `tests/rendering/test_output_mapper_gpu.cpp`
reads back identity, crop, flips, blend ramps, a projective warp, brightness and gamma.

## Lifecycle

`Context::create` → `SceneRenderer::init` (layouts, buffers, pipelines) → `resize(w, h)` (HDR
target; tonemap bind group is rebuilt lazily) → per frame `render(encoder, scene, time, target)`.
Tone-map pipelines are cached per target format (BGRA8 swapchain, RGBA8 capture). Surface loss
or outdated swapchains are reconfigured once inside `gpu::Surface::acquire` (which
`Context::acquireSurfaceView` forwards to for the primary window).

## Threading

Everything GPU-related runs on the main thread. Dawn callbacks are delivered from
`ProcessEvents`/`WaitAny` on that thread.

## Performance (see docs/performance.md)

Orb scene at 1280x720: ~0.1 ms GPU. DamagedHelmet with IBL and skybox at 2880x1800: ~1.0 ms
GPU, 0.3 ms CPU work per frame (Release). Environment preprocessing: 18 ms Release for a 1k HDRI.

## Debugging

Xcode GPU capture works on the process (Tint-generated MSL is shown). Dawn validation messages
are logged with the `[wgpu]` prefix and counted; a headless run exits non-zero if any occurred.

## Sharing (milestone 1.2)

`share::TextureShare` (`src/share/`) publishes a finished frame (RGBA8Unorm or BGRA8Unorm texture
with `CopySrc` usage) to other applications; one instance is one output. `available(kind)` and
`describe()` report what this machine can do; `open(kind, context, name)` starts a server, and
`publish(texture, w, h)` is called once per frame after the frame's command buffer was submitted.
`stats()` gives frames published, size, last error and whether clients are attached.

**Syphon** (macOS) is native, no CPU copy and no CPU wait:

1. An IOSurface in the source's pixel format is imported into Dawn once per size/format
   (`wgpu::SharedTextureMemory` with `SharedTextureMemoryIOSurfaceDescriptor`; the device is
   created with `SharedTextureMemoryIOSurface` and `SharedFenceMTLSharedEvent`, see
   `Capabilities::sharedTextureIOSurface`).
2. Each frame: `BeginAccess` → `CopyTextureToTexture` into the wrapped texture → `Submit` →
   `EndAccess`. `EndAccess` hands back Dawn's queue `MTLSharedEvent` and the copy's serial.
3. A `SyphonMetalServer` on a second `MTLDevice`/queue gets a command buffer that waits for that
   event value on the GPU, blits (BGRA) or re-draws (RGBA, through Syphon's sampling shader) the
   surface into Syphon's own IOSurface, signals our read-done `MTLSharedEvent`, and announces the
   frame to clients from its completion handler.
4. The next frame's `BeginAccess` passes the read-done event as a fence, so Dawn's copy never
   overwrites a surface Syphon is still reading. One IOSurface suffices; there is no added frame
   of latency. Size changes wait for the last frame to be announced first, because Syphon swaps
   its surface at encode time but announces from completion handlers (a stale handler would
   otherwise push a blank new-size surface to clients).

Measured with the hidden `[.perf]` probe (Debug, 1920x1080 BGRA8, 300 frames, in-process
client attached): `publish()` blocks the caller well under a millisecond on average. Clients
verified: `SyphonMetalClient` in the test process, discovered by name through
`SyphonServerDirectory` like any other application would.

**NDI** is runtime-loaded (`share/ndi_runtime.*`, never linked) and CPU-bound by nature: a ring
of three `MapRead` staging buffers. `publish()` pumps Dawn's events, hands every newly mapped
buffer to `NDIlib_send_send_video_async_v2` (BGRA or RGBA FourCC, progressive, the frame rate set
with `setFrameRate`), unmaps the buffer NDI has just finished with, then copies the new frame into
a free slot and maps it asynchronously. Nothing blocks; when all three slots are busy the frame is
dropped. Latency is one to two frames. `stats().clients` comes from
`NDIlib_send_get_no_connections`. Without the runtime `available(Ndi)` is false and `describe()`
says where it looked.

Limitations: no alpha premultiplication or colour-space tagging (frames go out as stored, which
is what Syphon and NDI clients expect for sRGB 8-bit); one output per `TextureShare`; NDI has
only been exercised against the loader's error paths on machines without the runtime.

## Seams for later milestones

- Pass list → frame graph with transient resources (0.6 post-processing).
- Compute passes in the same encoder; storage buffers/textures, indirect draw/dispatch, 3D
  storage textures are plain WebGPU features (particles, 0.5).
- MSAA, shadows, specular occlusion and multi-scatter compensation are not implemented.
- HDR/EDR swapchains are Dawn features not yet requested.
- `dawn/native/*` headers are forbidden outside `src/gpu/` so wgpu-native remains a drop-in.

## Ambient occlusion at landscape scale

GTAO takes a horizon from any sample whose direction has a large cosine against the view vector,
and on ground running away from the camera every *coplanar* sample has one. Without a test that an
occluder actually stands above the shaded point's tangent plane, a smooth surface occludes itself:
the term collapses into a flat grey wash over anything seen at a grazing angle. This was invisible
for the whole life of the engine because every scene was objects viewed from a normal angle, and it
dominated the first terrain frame ever rendered, taking roughly half the ambient light off open
hillsides. `fs_gtao` now requires `dot(delta, n) > bias`, where the bias is a pixel footprint or two
so it scales with distance rather than being tuned for one scene's size.

Removing that false occlusion exposed a second, older flaw underneath it. Each GTAO slice
contributes the part of the normal lying in its own plane, and every one of those planes contains
the view vector, so the summed bent normal systematically loses the component perpendicular to the
view and leans toward the camera. On an unoccluded convex surface that is plainly wrong -- there is
nothing to bend around -- and it showed as the underside of a sphere gathering *more* sky than its
top, which AO can never legitimately do. The bend is now brought in proportionally to the visibility
actually lost: no occlusion, no bend.

Both are pinned by tests that assert what the previous ones had no reason to. `[gpu][ao]` renders a
plane at a grazing angle and asserts it keeps its ambient (it was losing 51%, now 23%, the
remainder being the legitimate bent-normal tilt). The sky irradiance test asserts its top-vs-bottom
margin rather than a bare inequality -- it had been passing by 0.003 out of an unoccluded gap of
0.08, which made it a test of ambient occlusion rather than of which way is up.

The AO world radius is still `clamp(distance to the camera target * 0.05, 0.15, 4)`, and the
horizon march still clamps its screen radius to a 4 px floor. Together those mean AO fades out
beyond roughly `radius * 214` pixels of depth, which for a landscape is most of the frame. That is
the right behaviour -- there is nothing at that distance AO could resolve -- but it is a
consequence of two clamps rather than a decision, and it should become one if AO ever needs to
reach further.

At the realtime tier AO is 3 slices of 6 steps and relies on temporal accumulation to resolve; a
static offline frame at that tier still shows fine hatching in the AO buffer. Stills should be
rendered at `--tier offline` (6 slices, 12 steps), where it is clean.
