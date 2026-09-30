# Live anti-aliasing: investigation, research and recommendation (01-brief-live.md PARTS 2-7)

Engineering agent, 2026-09-30, branch `proto/sonic-garden`. Apple M2 Max, Metal through Dawn. The owner's
display is a 3456x2234 Retina panel; the editor opens maximised at 1728x1051 points, scale 2.0, and the canvas the
world is drawn into is 1640x1326 device pixels (2.17 Mpx). Every GPU number here was taken under
`tools/gpu-lock.sh`. Review media: `~/Desktop/av-gen-review/23-sonic-garden/aa/`. Decision: ADR-1024.

## 1. The answer in one paragraph

The live viewport looks worse than an offline render mainly because **it renders a quarter of the pixels**. The
editor's adaptive render scale (§15-§17, on by default, 16.67 ms GPU budget) takes the Sonic Garden from 1640x1326 to
820x662 within two seconds of pressing play. The tone map then stretches that 2x with a bilinear filter. Nothing
antialiases the frame at any stage: FXAA exists but is off unless a scene authors it, and there is no MSAA, TAA or
projection jitter anywhere in the renderer. The offline render is the same renderer at 1:1. For GV3 it is also 2x
supersampled plus FXAA 0.96, and the review stills are 2x supersampled too. That makes up to 16x more samples per
displayed pixel than the live picture. The quality tier contributes nothing measurable: at matched resolution,
Realtime and Offline are 0.6% apart on edge error.

So the fix is a configuration question first and an algorithm second, which is what the brief suspected.

## 2. The two paths, traced through the code

```
LIVE (Application::runInteractive, Realtime tier, application.cpp ~4400-4520)
  canvas = ImGui canvas points x backing scale (2.0) x canvasRenderScale (1.0)          1640x1326
    -> InteractiveResolution picks a rung {1, .85, .71, .58, .5}; GPU > 16.67 ms => rung 4
    -> SceneRenderer::resize: HDR target = canvas x renderScale                         820x662 RGBA16F
    -> scene pass (forward+, clustered) into HDR + 5 aux targets, Depth24Plus, 1 sample
    -> screen-space passes at scene res (AO 820x663 with history, shadow mask, fog march)
    -> post chain at scene res: exposure, DOF, motion blur, bloom, halation, composite,
       [FXAA only if post/output/antialias > 0 -- the Sonic Garden authors 0]
    -> tonemap.wgsl: textureSample(bilinear) of the 820x662 HDR into the 1640x1326 final
       (the tone map IS the upscaler), sRGB encode by hand
    -> `final` BGRA8Unorm 1640x1326 -> ImGui::Image, bilinear sampler, 1:1 in Workspace mode
    -> swapchain                                                                          DISPLAY

OFFLINE (RenderJob, Offline tier, render_job.cpp ~150-260, ~690-780)
  output = project render size (Sonic Garden 1280x720; review renders 1920x1080; GV3 1920x1080)
    -> renderScale = --supersample / render.supersample (1 or 2; GV3 = 2, review stills = 2)
    -> SceneRenderer::resize: HDR target = output x renderScale                         e.g. 3840x2160
    -> the SAME scene pass, screen-space passes and post chain, at scene res
       (Offline tier: shadows 4096, sky cube 1024, 16x aniso, full-res fog/history, no LOD hysteresis)
    -> tonemap.wgsl: bilinear sample at 2:1 = a 2x2 box of HDR, then tone map
    -> RGBA8Unorm output -> PNG, or h264 4:2:0 (chroma halved again by the encoder)       OUTPUT
```

Where they diverge:

| | live | offline |
|---|---|---|
| samples per output pixel | 0.25 at the floor (rung 0.5), 1 at rung 0 | 1, or 4 with supersample 2 |
| resolve / resample | bilinear 2x **up**-scale in the tone map | none, or a 2x2 box **down** in the tone map |
| MSAA sample count | 1 | 1 |
| colour format | RGBA16F HDR, BGRA8Unorm final | RGBA16F HDR, RGBA8Unorm output |
| depth | Depth24Plus | Depth24Plus |
| post chain | identical code; FXAA as authored | identical code; FXAA as authored |
| tone map | identical | identical |
| temporal | AO history, temporal ring (echo) at 0.5 res, no jitter, no TAA | same passes at full res, no jitter, no TAA |
| tier | Realtime | Offline |
| presentation | ImGui bilinear, 1:1 in Workspace; scaled in Output Preview (Draft is half, Native is scaled to fit) | the file, viewed in a player |

About "the first frame has no temporal history": this is the AO accumulation and the temporal ring (ADR-410). Neither
is anti-aliasing. There is no history of the colour image anywhere.

## 3. Measured: which difference matters

Same moment (bell, 9.3 s, `temporal/echo` off in every arm). 1920x1080. Error against the 2x-supersampled offline
frame, on its 5% edge pixels (mean absolute difference, 8-bit):

| arm | edge error | PSNR |
|---|---|---|
| offline tier, 1:1 | 12.17 | 33.2 dB |
| realtime tier, 1:1 (live at rung 0) | 12.24 | 33.0 dB |
| realtime, 1:1 + FXAA 0.75 | 11.05 | 33.9 dB |
| realtime at 0.71 | 19.33 | 30.3 dB |
| realtime at 0.5 (live at the floor) | **27.72** | 27.2 dB |
| realtime at 0.5 + FXAA 0.75 | 27.44 | 27.5 dB |

- The tier contributes 0.07.
- The resolution floor contributes 15.5, which doubles the edge error.

Moving camera (the scene's own push-in plus sound-driven deformation), 3 s = 90 frames, echo and grain off. The
reference is a 3840x2160 render averaged 2x2 in linear light after the tone map (a display-space 4-sample
reference). **Crawl** is the mean frame-to-frame change of the error on edge pixels, which is what shimmer is. Content
motion cancels out of it; aliasing that moves does not.

| arm | bell edge error | bell crawl | perc edge error | perc crawl |
|---|---|---|---|---|
| full res, no AA | 11.94 | 12.57 | 15.46 | 16.98 |
| **full res + FXAA** | **10.53** | **10.99** | **12.99** | **14.26** |
| 0.85 + FXAA | 15.80 | 14.48 | 18.05 | 19.32 |
| 0.71 + FXAA | 19.31 | 16.95 | 21.21 | 22.86 |
| 0.5, no AA (live before) | 27.16 | 22.45 | 29.71 | 32.22 |
| 0.5 + FXAA (live after, default) | 26.55 | 21.53 | 29.19 | 31.55 |

The live floor has about twice the error and twice the crawl of full resolution. FXAA takes 12-16% off both at full
resolution, and only 2-3% at the floor. At the floor the rings are 2 px wide and the 6 mm shock ring is sub-pixel,
so there is no edge left for a spatial filter to find.

## 4. Research (sources in §8)

- **MSAA on Apple / TBDR.**
  - What Apple offers: 4x is guaranteed. A multisample attachment can be memoryless (`MTLStorageModeMemoryless`,
    store action `multisampleResolve`), so the samples live only in tile memory and cost almost no bandwidth. Apple
    recommends a tone-map-weighted custom resolve for HDR.
  - Where that breaks: only inside one render pass. An attachment that must survive into another pass has to be
    stored as a real 4x allocation. Memoryless textures can be neither loaded nor stored.
- **WebGPU / Dawn.**
  - Sample counts: 1 or 4 only.
  - Formats: RGBA16F and RG16F are resolvable. R32Uint (our identifier target) is not, and R32Float (our linear
    depth) is multisample-capable but not resolvable.
  - Depth: there is no depth resolve in the spec or in Dawn.
  - Multisampled textures are `textureLoad`-only.
  - Metal features: Dawn does expose `TransientAttachments` (memoryless) and `DawnLoadResolveTexture` (lossy
    re-expansion) on Metal. `MSAARenderToSingleSampled` is Vulkan/GL only.
- **FXAA.**
  - Cost: under 1 ms on a GTX 480 at 1920x1200.
  - Lottes' whitepaper warns that FXAA on scene-linear HDR "turns AA off" on edges crossing the display range. That
    describes our emissive rims, and our FXAA does run in HDR.
    - I tested the standard remedy: the resample blended in Karis's `c/(1+luma)` domain, then inverted.
    - On the strike frames (perc and bass at 17 s, scored against the display-space 4K reference), it made no
      measurable difference: edge error 13.39 vs 13.60 and 4.80 vs 4.82.
    - Our FXAA already finds its edges on a Reinhard-compressed luma, and exposure is applied before it. The change
      was reverted, so FXAA's output is byte-for-byte what it was.
- **SMAA** (MIT).
  - What it costs: three passes plus two lookup textures.
  - What it gives: better edges than FXAA, about 1 ms at 1080p on 2012 hardware, no history. Its own paper says it
    does not reconstruct sub-pixel features without MSAA or temporal samples (S2x, T2x).
  - Here: it would sharpen the full-resolution edges FXAA already fixes, and would not fix the floor's broken rings,
    which are the actual complaint. Not worth a vendored port now.
- **TAA / TAAU / TSR / MetalFX temporal.**
  - What it would need: projection jitter, complete velocity, history with neighbourhood clamping, and a reactive
    mask. Karis and the Yang/Liu/Salvi survey name its failure modes precisely: procedural animation without
    velocity smears, sub-pixel features flicker as they are clamped in and out of history, shading that changes with
    no motion lags. Unreal needs "Previous Frame Switch" and "Has Pixel Animation" for exactly the vertex-animated
    and pixel-animated content AV Gen makes.
  - What we already have: `vs_proc` does write a previous-frame clip position.
  - Why it is still a poor fit:
    - Emissive rims going black to white in one frame are the worst case, clamped or lagged.
    - Every screen-space history pass (AO, the temporal ring, the fog) would have to be made jitter-aware.
    - It would make the offline path nondeterministic unless held off there.
  - MetalFX specifically: it needs a two-queue IOSurface / shared-event interop spike, because Dawn exposes no
    `MTLTexture` or command buffer.
- **Phone-wire AA** (Persson 2012). A tube thinner than a pixel is drawn one pixel wide and faded by the ratio. It is
  the one technique aimed squarely at the Sonic Garden's thin rings and tendrils, and it is stateless and
  deterministic. The fade needs blending or alpha-to-coverage (which needs MSAA), and the widening needs the tube's
  radius in `vs_proc`. The ring sources are `torus` and `tube` procedurals, whose radii are known.

## 5. Comparison for this renderer

| technique | fixes jaggies at full res | fixes the floor's broken thin geometry | temporal risk on audio-reactive content | cost here | fit |
|---|---|---|---|---|---|
| render at full res (raise the floor) | n/a | yes | none | +50% GPU on the garden (24 to 37 ms) | the real fix; a frame-rate trade |
| FXAA (exists) | yes, 12-16% | no (2-3%) | none; stateless, ramped (ADR-189) | below timer resolution (at most 0.07 ms); one RGBA16F target at scene res | yes: live default |
| SMAA 1x | yes, better than FXAA | no | none | about 3 passes, 2 LUTs, a vendored port | later, if FXAA's softness is judged too much |
| MSAA 4x | triangle edges only (not SDF, shader edges or contour lines) | partly (coverage) | none | renderer redesign: 26 pipelines, 6 MRTs (36 B/sample, over WebGPU's default 32), R32Uint/R32Float unresolvable, no depth resolve, every screen-space pass needs a custom resolve | no |
| supersample 2x live | yes | yes | none | 4x pixels: 70-150 ms on these scenes | no live headroom on any tested project; offline only |
| TAA / TAAU / TSR | yes | partly | high: flashes, deformation, thin geometry | jitter, history, reactive mask, jitter-aware AO, echo and fog | not now |
| MetalFX temporal | yes | partly | as TAA | plus an interop spike and a second queue | not now; would need a dependency decision |
| phone-wire widening | n/a | yes, for tori and tubes | none | a `vs_proc` change plus a radius uniform; the fade needs blending | the next step for thin geometry |

## 6. Recommendation by use

- **Live playback / high-quality preview:** full resolution when the frame allows, and FXAA always. The ladder's
  floor is the person's choice between edges and frame rate, so it is now a setting. The adaptive scale is right to
  exist: GV3 and the SDF presets need it. But it must not hide what it did.
- **Low-resolution preview (Draft, the adaptive floor):** FXAA, and accept it. No spatial method rebuilds a ring that
  is sub-pixel at that resolution. Only more samples do (resolution, temporal, or phone-wire widening).
- **Offline / final:** unchanged. Supersample 2 plus the scene's FXAA, as GV3 already does. The Sonic Garden's own
  `render` block has neither (1280x720, no supersample, no FXAA), so its deliverable is the no-AA picture. Adding
  `"supersample": 2` there is a one-line project change I recommend and have not made, because it doubles the render
  time and is the art owner's call. A `1/(1+luma)`-weighted downsample for supersampling is a possible refinement,
  but FXAA's version of it measured as nothing here, so it is not recommended without its own evidence.
- **Modes worth exposing:** Off and FXAA, which are what is implemented. SMAA only if FXAA's softness is judged too
  much. TAA/TAAU not until there is a reactive mask and full velocity. MSAA not in this architecture.

## 7. What was implemented (ADR-1024)

- `QualitySettings::antialiasFloor`: FXAA runs at `max(authored, floor)`.
  - It is 0 at every tier, and the Offline policy assertion requires 0, so a render is unchanged by construction.
  - The live editor sets it to `kLiveAntialiasFloor` (0.75, ADR-059's own recommendation) from Settings > Rendering
    > "Anti-aliasing": FXAA (default) or Off.
  - `--live-aa fxaa|off` sets it on a headless run too, and the quality arm `liveaa` sets it on any run, which is how
    the comparisons here were rendered.
- The adaptive scale's floor is a setting: Settings > Rendering > "Lowest scale", one of the rungs, default 0.50x
  (unchanged). It is also `--adaptive-floor`, and it follows the panel live. 1.00x means "never reduce".
- The status bar shows the scene's resolution when it is below the canvas, in the warning colour:
  `1640 x 1326 (scene 820 x 662)`. Before, it was visible only in the Performance panel.

Measured cost:
- **FXAA pass:** below the GPU timer's resolution. The median is at most one 0.066 ms quantum at 1640x1326, and an
  interleaved A/B on the garden came out VOID, under its 7.5% noise floor.
- **Frame:** the full-frame medians with and without it are within run-to-run noise on the garden, GV3 and the
  showcase.
- **Memory:** one extra RGBA16F transient target at scene resolution, from the post pool: 17.4 MB at 1640x1326 and
  4.3 MB at the floor.
- **Passes and CPU:** one extra full-screen pass, and no CPU cost.
- **The floor:** raising it is the real cost. Garden (bell), 1640x1326, GPU p50: 0.5 = 22.9 ms, 0.71 = 28.6,
  0.85 = 32.8, 1.0 = 37.4.

## 8. Sources

- Apple, MSAA: https://developer.apple.com/documentation/metal/improving-edge-rendering-quality-with-multisample-antialiasing-msaa
- Apple, TBDR: https://developer.apple.com/documentation/metal/tailor-your-apps-for-apple-gpus-and-tile-based-deferred-rendering ; load/store actions: https://developer.apple.com/documentation/metal/setting-load-and-store-actions
- WebGPU: https://www.w3.org/TR/webgpu/ (sampleCount 1 or 4, format table 26.1.1, no depth resolveTarget, maxColorAttachmentBytesPerSample) ; WGSL: https://www.w3.org/TR/WGSL/
- Dawn: `src/dawn/native/metal/PhysicalDeviceMTL.mm`, `TextureMTL.mm`, `docs/dawn/features/transient_attachments.md`, `dawn_load_resolve_texture.md`, `msaa_render_to_single_samples.md`, `shared_texture_memory.md`, `include/dawn/native/MetalBackend.h` (github.com/google/dawn, main)
- FXAA whitepaper (Lottes): http://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf
- Karis, High Quality Temporal Supersampling (SIGGRAPH 2014): https://advances.realtimerendering.com/s2014/index.html ; tone-mapped averaging: http://graphicrants.blogspot.com/2013/12/tone-mapping.html
- SMAA: https://github.com/iryoku/smaa ; paper: https://www.iryoku.com/smaa/downloads/SMAA-Enhanced-Subpixel-Morphological-Antialiasing.pdf ; Bevy's summary: https://docs.rs/bevy/latest/bevy/anti_alias/smaa/index.html
- Unreal: https://dev.epicgames.com/documentation/en-us/unreal-engine/anti-aliasing-and-upscaling-in-unreal-engine , https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine , https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-upscalers-in-unreal-engine
- Yang, Liu, Salvi, A Survey of Temporal Antialiasing Techniques (CGF 2020): http://behindthepixels.io/assets/files/TemporalAA.pdf
- MetalFX: https://developer.apple.com/documentation/metalfx , https://developer.apple.com/documentation/metalfx/applying-temporal-antialiasing-and-upscaling-using-metalfx
- Phone-wire AA (Persson): http://www.humus.name/index.php?page=3D&ID=89
