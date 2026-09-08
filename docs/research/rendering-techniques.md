# Research: Rendering Techniques for Festival-Quality Visuals

**Status:** research only — milestone 0.1 does NOT implement any of this.
**Purpose:** catalogue the rendering techniques a festival-grade AV engine will eventually need, with references, cost class, and — most importantly — what each demands from the renderer abstraction so that milestone 0.1 does not paint us into a corner.
**Target platform:** macOS 26, Apple M2 Max, Metal 4 (tile-based deferred rendering, unified memory, tile shaders / imageblocks, mesh shaders, hardware ray tracing).
**Date of research:** 2026-09-08. All URLs accessed 2026-09-08 unless noted.

Format per technique: *Description* · *Reference (source, URL, accessed)* · *What was learned* · *Cost class* · *Renderer requirements* · *Confidence / limitations*.

Cost classes used: **C0** trivial (< 0.2 ms at 4K), **C1** cheap (0.2-1 ms), **C2** moderate (1-3 ms), **C3** heavy (3-8 ms), **C4** offline-only or budget-defining (> 8 ms).

---

## Part A — The organising principle: render graphs

### A.1 FrameGraph (O'Donnell, Frostbite, GDC 2017)

- **Description:** Declare every render/compute pass together with the resources it reads and writes; the graph compiles the frame, culls unused passes, derives barriers/transitions, allocates **transient resources** with aliasing, and schedules async compute.
- **Reference:** Yuriy O'Donnell, "FrameGraph: Extensible Rendering Architecture in Frostbite", GDC 2017. https://www.gdcvault.com/play/1024612/FrameGraph-Extensible-Rendering-Architecture-in ; slides https://www.slideshare.net/slideshow/framegraph-extensible-rendering-architecture-in-frostbite/72795495 ; overview by R. Loggini https://logins.github.io/graphics/2021/05/31/RenderGraphs.html — accessed 2026-09-08.
- **What was learned:** Frostbite's frame is a DAG of passes and resources; each pass declares `create/read/write` in a *setup* lambda and encodes in an *execute* lambda. Benefits cited: engine extensibility (features are decoupled passes), simplified async compute, automated memory aliasing (ESRAM on Xbox One), reduced GPU memory, automatic transitions and fences. Widely regarded as the first shipped triple-A render graph.
- **Cost class:** C0 at runtime (graph compile is per frame but cheap); large engineering payoff.
- **Renderer requirements:** This *is* the abstraction. Milestone 0.1 should at minimum: (1) represent a frame as an ordered list of passes with declared inputs/outputs; (2) distinguish transient (per-frame) from persistent (cross-frame) resources; (3) allocate transient textures from a pool keyed by descriptor; (4) drive Metal encoder creation from pass declarations. Even a linear (non-DAG) v0 with explicit read/write sets gives the API shape needed later. On Metal specifically, declared read/write sets map to `MTLFence`/`memoryBarrier` in Metal 3 and to explicit barriers in Metal 4, and to `storageModeMemoryless` for TBDR-resident transients.
- **Confidence / limitations:** High. Applies equally to the offline path, where the same graph is executed with a different "present" sink (readback instead of drawable).

---

## Part B — Shading and colour pipeline

### B.1 Physically based shading (Cook-Torrance / GGX)

- **Description:** Specular microfacet BRDF `f = D·G·F / (4 N·V N·L)` with GGX/Trowbridge-Reitz `D`, height-correlated Smith-GGX visibility, Schlick Fresnel; Lambert or Burley diffuse; energy-conserving; image-based lighting via the split-sum approximation.
- **Reference:** Google, "Physically Based Rendering in Filament". https://google.github.io/filament/Filament.md.html — accessed 2026-09-08. Karis, B. "Real Shading in Unreal Engine 4", SIGGRAPH 2013 course notes. https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf — accessed 2026-09-08.
- **What was learned:** Filament documents the exact BRDF (GGX NDF, Smith-GGX height-correlated visibility, Schlick Fresnel; Kelemen visibility for clear coat) with reference GLSL, plus a metallic/roughness/reflectance parameterisation. Karis 2013 introduces the **split-sum** IBL approximation: prefiltered environment mips for the radiance term and a 2D DFG lookup for the BRDF term, both precomputable.
- **Cost class:** C1 per-pixel; IBL prefilter is a one-off compute/bake.
- **Renderer requirements:** cube-map textures with mip chains writable per mip by compute (prefilter), a 2D LUT texture, HDR (`RGBA16Float`) render targets, and a material/uniform binding model that can carry per-material parameters (argument buffers).
- **Confidence / limitations:** High (both are canonical references).

### B.2 HDR pipeline and tone mapping (ACES, AgX, Khronos PBR Neutral)

- **Description:** Render in linear scene-referred HDR; apply exposure; map to display with a view transform. ACES (RRT+ODT, or Hill's fit) is the film standard; **AgX** (Sobotka; Blender 4.0 default) desaturates gracefully toward white without hue skews; **Khronos PBR Neutral** (2024) is designed for colour-accurate product rendering with minimal hue/saturation change.
- **Reference:** Khronos press release. https://www.khronos.org/news/press/khronos-pbr-neutral-tone-mapper-released-for-true-to-life-color-rendering-of-3d-products — accessed 2026-09-08. Blender PR #118936 "Add Khronos PBR Neutral tone mapper" (discussion contrasting with AgX/Filmic). https://projects.blender.org/blender/blender/pulls/118936 — accessed 2026-09-08. model-viewer tone mapping example. https://modelviewer.dev/examples/tone-mapping — accessed 2026-09-08. Unity AgX port. https://github.com/unitycoder/AgX-Tonemapping-Unity — accessed 2026-09-08.
- **What was learned:** Khronos explicitly positions PBR Neutral as "not a competitor to AgX or other filmic tone mappers" but as a replacement for *no* tone mapping in well-exposed scenes; filmic mappers (ACES, AgX) are recommended for strongly HDR scenes and wide gamuts. ACES and AgX both cannot reproduce fully saturated primaries (canary yellow, pure greens/blues) — a known complaint. For festival visuals (extreme HDR, lasers, bloom) a filmic transform is the right default; AgX avoids the ACES hue skew toward orange/red in bright saturated regions. Community "AgMax" blends AgX with Neutral's saturation retention.
- **Cost class:** C0 (a per-pixel function or a 3D LUT lookup).
- **Renderer requirements:** float/half HDR colour targets end-to-end; a final full-screen pass; sRGB/Display-P3/HDR10 output framebuffer formats (Metal `bgra10_xr`/EDR on Apple displays); the tone mapper should be a pluggable shader function so offline output can skip it and write scene-linear EXR.
- **Confidence / limitations:** High for the facts; choice of default transform is a taste decision.

### B.3 Colour grading and LUTs

- **Description:** Arbitrary colour transforms baked into a 3D LUT (typically 32^3 or 64^3) applied with a single trilinear sample; grading sits after tone mapping in display space or before it in a log space.
- **Reference:** Selan, J. "Using Lookup Tables to Accelerate Color Transformations", GPU Gems 2 ch. 24. https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-24-using-lookup-tables-accelerate-color — accessed 2026-09-08.
- **What was learned:** 3D LUTs express essentially any colour operator (they are what ICC profiles use); interactive film colour correction has used them since the mid-2000s. Precision concerns: apply LUTs in a perceptual/log encoding, not linear, to avoid banding in shadows.
- **Cost class:** C0.
- **Renderer requirements:** 3D textures with trilinear filtering readable in fragment shaders; `.cube` LUT loader; ability to chain full-screen passes.
- **Confidence / limitations:** High.

---

## Part C — Post-processing

### C.1 Bloom (Jimenez 2014, Kawase / dual filter)

- **Description:** Progressive downsample of the HDR image into a mip pyramid with a wide, stable filter, then progressive upsample with a tent filter, accumulating each level; result added to the frame.
- **Reference:** Jimenez, J. "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014 Advances course. https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/ ; https://advances.realtimerendering.com/s2014/ — accessed 2026-09-08. Bjørge, M. "Bandwidth-Efficient Rendering" (dual filter), SIGGRAPH 2015 mobile course. https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/siggraph2015_2D00_mmg_2D00_marius_2D00_notes.pdf — accessed 2026-09-08. LearnOpenGL "Phys. Based Bloom" (implementation walkthrough). https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom — accessed 2026-09-08.
- **What was learned:** Jimenez's bloom uses a pyramid with a custom 13-tap downsample (partial Karis average by luminance weight on the first mip to kill fireflies) and a 9-tap tent upsample with per-mip radius, which is what gives temporal stability and avoids the "pulsating" of naive downsampling. Dual-Kawase (Bjørge 2015) is a cheaper alternative optimised for bandwidth-limited/tile GPUs — directly relevant to Apple's TBDR hardware.
- **Cost class:** C1-C2 (bandwidth-bound; ~10-14 passes at decreasing resolution).
- **Renderer requirements:** texture mip chains with per-level render-target views; half-resolution intermediate targets from the transient pool; linear-filtered sampling of HDR formats; additive blend on the composite.
- **Confidence / limitations:** High.

### C.2 Depth of field

- **Description:** Circle-of-confusion from depth and camera params; near/far field separation; bokeh gathered with "scatter-as-gather" or separable disk kernels.
- **Reference:** Jimenez 2014 (above; scatter-as-gather with transparency-aware masks); Wronski, B. "Separable disk-like depth of field" (2017). https://bartwronski.com/2017/08/06/separable-bokeh/ — accessed 2026-09-08. AMD FidelityFX DoF manual. https://gpuopen.com/manuals/fidelityfx_sdk/techniques/depth-of-field/ — accessed 2026-09-08. Sousa, T. "Graphics Gems from CryENGINE 3", SIGGRAPH 2013. https://www.slideshare.net/TiagoAlexSousa/graphics-gems-from-cryengine-3-siggraph-2013 — accessed 2026-09-08. MJP, "How to fake bokeh". https://therealmjp.github.io/posts/bokeh/ — accessed 2026-09-08.
- **What was learned:** Production DoF runs at half resolution with separate near/far layers; near-field silhouettes need dilation/alpha masks; Sousa uses bilateral upsampling to recombine. Complex-kernel separable approaches (Wronski, and the earlier "circular DoF" from EA) give circular bokeh in two passes.
- **Cost class:** C2.
- **Renderer requirements:** depth texture readable as a sampled resource; multiple half-res transient targets; MRT (near/far in one pass).
- **Confidence / limitations:** High.

### C.3 Motion blur

- **Description:** Per-pixel velocity buffer, tile-max/neighbour-max dilation, then a reconstruction filter along dominant velocities.
- **Reference:** McGuire, M. et al. "A Reconstruction Filter for Plausible Motion Blur", I3D 2012. https://www.researchgate.net/publication/254007575_A_Reconstruction_Filter_for_Plausible_Motion_Blur — accessed 2026-09-08. Guertin, J.-P., McGuire, M., Nowrouzezahrai, D. "A Fast and Stable Feature-Aware Motion Blur Filter", HPG 2014. https://casual-effects.com/research/Guertin2014MotionBlur/Guertin2014MotionBlur-small.pdf — accessed 2026-09-08.
- **What was learned:** Guertin 2014 fixes artifacts of the 2012 filter with a two-direction sampling scheme and runs under 2 ms at 1280x720 (2014 hardware). Both require a per-pixel velocity buffer, i.e. previous-frame transforms for every object and the camera.
- **Cost class:** C1-C2.
- **Renderer requirements:** **velocity target** (RG16F) in the geometry pass — the scene/camera system must retain previous-frame matrices; tile-max compute passes. For offline output, post-process motion blur is usually replaced by temporal sub-frame accumulation (see `offline-rendering.md`), so the velocity buffer is optional there but still useful for TAA.
- **Confidence / limitations:** High.

### C.4 Temporal anti-aliasing (TAA)

- **Description:** Sub-pixel jitter per frame; reproject previous frame's history with the velocity buffer; clamp/clip history to the neighbourhood colour AABB (YCoCg); blend.
- **Reference:** Karis, B. "High Quality Temporal Supersampling", SIGGRAPH 2014. https://de45xmedrsdbp.cloudfront.net/Resources/files/TemporalAA_small-59732822.pdf — accessed 2026-09-08. Tardif, A. "Temporal Antialiasing Starter Pack". https://alextardif.com/TAA.html — accessed 2026-09-08.
- **What was learned:** Karis 2014 is the origin of neighbourhood clamping and the practical UE4 pipeline; Tardif's summary catalogues jitter sequences (Halton 2,3), reprojection, clipping, and sharpening. TAA is inherently history-dependent — the first N frames after a cut are wrong, which is why offline renderers use warm-up frames (`offline-rendering.md`).
- **Cost class:** C1.
- **Renderer requirements:** projection-matrix jitter hook; **persistent history texture** (ping-pong across frames); velocity buffer; the frame index must drive the jitter sequence deterministically (frameIndex, not wall time).
- **Confidence / limitations:** High.

### C.5 Screen-space ambient occlusion (GTAO)

- **Description:** Horizon-based AO with a radiometrically correct formulation; spatio-temporal sampling (interleaved gradient noise + TAA).
- **Reference:** Jimenez, J., Wu, X.-C., Pesce, A., Jarabo, A. "Practical Realtime Strategies for Accurate Indirect Occlusion", SIGGRAPH 2016 course. https://www.iryoku.com/downloads/Practical-Realtime-Strategies-for-Accurate-Indirect-Occlusion.pdf — accessed 2026-09-08. Intel XeGTAO (MIT reference implementation). https://github.com/GameTechDev/XeGTAO — accessed 2026-09-08.
- **What was learned:** GTAO matches ground truth in ~0.5 ms on 2016 console hardware by distributing work over space and time; XeGTAO is a clean HLSL implementation with denoise passes.
- **Cost class:** C1.
- **Renderer requirements:** depth (and optionally normal) buffer sampled by compute; half-res transient; relies on TAA for temporal convergence.
- **Confidence / limitations:** High.

### C.6 Screen-space reflections (SSR)

- **Description:** Ray-march the depth buffer from each pixel along the reflected direction; stochastic importance sampling of the GGX lobe with spatial/temporal denoising for rough surfaces.
- **Reference:** Stachowiak, T. "Stochastic Screen-Space Reflections", SIGGRAPH 2015. https://www.ea.com/frostbite/news/stochastic-screen-space-reflections ; https://advances.realtimerendering.com/s2015/ — accessed 2026-09-08.
- **What was learned:** SSSR handles spatially-varying roughness via Monte Carlo sampling with hierarchical-Z tracing and reuses neighbours' rays; needs previous-frame colour + TAA. Screen-space limits (nothing off-screen) are the standard caveat — cube-map/probe fallback needed.
- **Cost class:** C2-C3.
- **Renderer requirements:** Hi-Z depth pyramid (compute mip generation of min/max depth); previous-frame HDR colour; roughness/normal G-buffer; blue-noise texture.
- **Confidence / limitations:** High.

### C.7 Chromatic aberration, film grain, vignette, lens distortion

- **Description:** Per-channel UV offsets scaled by distance from centre (CA); hash/blue-noise grain modulated by luminance; radial darkening; barrel distortion.
- **Reference:** Jimenez 2014 (above) for interleaved gradient noise `frac(52.9829189 * frac(0.06711056 x + 0.00583715 y))` used for per-pixel variation; general practice.
- **Cost class:** C0 (folded into the final composite pass).
- **Renderer requirements:** noise must be seeded from frameIndex for determinism; the composite pass must be able to run at output resolution after tone mapping.
- **Confidence / limitations:** High; no single canonical paper.

---

## Part D — Volumetrics and atmosphere

### D.1 Froxel volumetric fog / lighting (Wronski 2014; Hillaire 2015)

- **Description:** A low-resolution 3D texture aligned to the view frustum ("froxels", e.g. 160x90x64 with exponential depth slicing). Pass 1 voxelises participating-media density and albedo; pass 2 injects in-scattered light from every light (with shadow maps) per froxel; pass 3 integrates front-to-back along each ray to produce accumulated scattering + transmittance; then the scene is composited by a single 3D-texture lookup per pixel. Temporal reprojection with per-frame jitter reduces aliasing.
- **Reference:** Wronski, B. "Volumetric Fog: Unified Compute Shader Based Solution to Atmospheric Scattering", SIGGRAPH 2014. https://advances.realtimerendering.com/s2014/ ; https://bartwronski.com/publications/ ; https://bartwronski.com/wp-content/uploads/2014/03/ac4_gdc.pdf — accessed 2026-09-08. Hillaire, S. "Physically Based and Unified Volumetric Rendering in Frostbite", SIGGRAPH 2015. https://www.slideshare.net/slideshow/physically-based-and-unified-volumetric-rendering-in-frostbite/51840934 ; https://advances.realtimerendering.com/s2015/ — accessed 2026-09-08 (EA news page 404 on that date).
- **What was learned:** Wronski introduced the froxel approach in Assassin's Creed 4; Hillaire generalised it in Frostbite with physically based parameters (extinction, albedo, phase), a **cascaded extinction volume**, **voxelisation of particles into the volume**, a volumetric shadow map, and temporal integration. Sébastien Hillaire's Shadertoy "VolumetricIntegration" demonstrates the analytic per-froxel integration used.
- **Cost class:** C2 (three compute passes over ~1M froxels; highly parallel).
- **Renderer requirements:** 3D textures written by compute (`RGBA16Float`) and sampled in fragment/compute; two 3D history volumes for temporal reprojection; light list and shadow maps accessible from compute; particle systems must be able to write into the volume (particles ↔ volumetrics coupling is exactly the festival "smoke + lasers" look).
- **Confidence / limitations:** High for the technique; Hillaire's detailed numbers (froxel resolution, ms) were not retrievable via fetch and should be checked in the slides.

### D.2 Atmospheric scattering (Hillaire 2020)

- **Description:** Physically based sky from ground to space with small LUTs (transmittance, multiple scattering, sky-view, aerial perspective) computed each frame, so atmosphere parameters can animate.
- **Reference:** Hillaire, S. "A Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020 / CGF 39(4). https://onlinelibrary.wiley.com/doi/abs/10.1111/cgf.14050 ; https://diglib.eg.org/items/8a3e5350-18b3-46bd-9274-3add5af88c75 ; overview https://trist.am/blog/2024/atmosphere-rendering/ — accessed 2026-09-08.
- **What was learned:** Avoids high-dimensional LUTs (and their artifacts); new real-time multiple-scattering approximation; scales from mobile to high-end; this is the UE sky atmosphere; reference code is public.
- **Cost class:** C1 (a few small compute LUT passes + one full-screen sky pass).
- **Renderer requirements:** small 2D/3D LUT textures regenerated per frame by compute; camera-relative planet coordinates; couples with froxel fog for aerial perspective.
- **Confidence / limitations:** High.

---

## Part E — Procedural content: noise, SDFs, ray marching

### E.1 Noise: Perlin (improved), simplex, Worley, fBM, domain warping

- **Reference:** Perlin, K. "Improving Noise", SIGGRAPH 2002. https://mrl.cs.nyu.edu/~perlin/noise/ (reference implementation) — accessed 2026-09-08. Gustavson, S. "Simplex noise demystified" (2005). https://cgvr.cs.uni-bremen.de/teaching/cg_literatur/simplexnoise.pdf — accessed 2026-09-08. McEwan, Sheets, Gustavson, Richardson, "Efficient computational noise in GLSL" (2012, the `webgl-noise` textureless implementations). https://ar5iv.labs.arxiv.org/html/1204.1461 — accessed 2026-09-08. Worley, S. "A Cellular Texture Basis Function", SIGGRAPH 1996. https://dl.acm.org/doi/10.1145/237170.237267 — accessed 2026-09-08. Quilez, I. "fBM". https://iquilezles.org/articles/fbm/ ; "Domain Warping". https://iquilezles.org/articles/warp/ — accessed 2026-09-08. GPU Gems 2 ch. 26 "Implementing Improved Perlin Noise". https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-26-implementing-improved-perlin-noise — accessed 2026-09-08.
- **What was learned:** Improved Perlin fixes the second-order interpolation discontinuity (quintic fade) and gradient selection; simplex noise has lower complexity in higher dimensions and no axis-aligned artifacts (Gustavson gives the definitive derivation, and the 2012 GLSL paper provides textureless permutation-free implementations suitable for compute); Worley/cellular noise complements Perlin for cells, crusts and craters; Quilez's fBM article analyses the spectral meaning of the Hurst exponent `H` (gain `G = 2^-H`), and his domain-warping recipe `f(p + fBM(p + fBM(p)))` is the canonical festival "organic flow" texture.
- **Cost class:** C0-C2 depending on octave count and dimension; 3D/4D noise in fragment shaders at 4K quickly becomes C3 — bake to 3D textures when static.
- **Renderer requirements:** the **shared shader library** (see particles.md §11.10) must house these functions with identical results in compute and fragment stages; a bake path writing 2D/3D noise textures via compute; per-frame `time` uniform for animated noise.
- **Confidence / limitations:** High.

### E.2 Signed distance fields and sphere tracing

- **Reference:** Hart, J. "Sphere tracing: a geometric method for the antialiased ray tracing of implicit surfaces", The Visual Computer 12 (1996). https://link.springer.com/article/10.1007/s003710050084 — accessed 2026-09-08. Quilez, I. distance-function articles (index at https://iquilezles.org/articles/) ; Scratchapixel, "Rendering Distance Fields: Sphere Tracing". https://www.scratchapixel.com/lessons/advanced-rendering/rendering-distance-fields — accessed 2026-09-08.
- **What was learned:** Sphere tracing steps along a ray by the SDF value, guaranteed not to overshoot when the field is Lipschitz-1; requires only a bound on the derivative. Quilez's catalogue of primitives, smooth-min blends, domain repetition and analytic normals is the standard toolkit; SDF noise (his "sphere-grid" noise) can be raymarched exactly.
- **Cost class:** C2-C4 (full-screen ray marching at 4K with 100+ steps is expensive; render at half resolution, reproject, or restrict to bounding volumes).
- **Renderer requirements:** full-screen (or bounding-box) passes that **write depth** so raymarched geometry composes with rasterised particles and volumetrics; access to the same SDF functions from particle kernels (collision) and froxel voxelisation; optionally a baked 3D SDF texture.
- **Confidence / limitations:** High.

---

## Part F — GPU feedback and simulation effects

### F.1 Ping-pong render targets / feedback loops

- **Description:** Read texture A, write texture B, swap; the basis of reaction-diffusion, fluid grids, trails, and "video feedback" looks.
- **Reference:** Harris, M. GPU Gems ch. 38 (states rendering to a bound texture is undefined; two textures are required). https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu — accessed 2026-09-08.
- **Cost class:** C0 per pass.
- **Renderer requirements:** **persistent texture pairs** with a swap operation exposed at the graph level; identical formats; ability to run N iterations per frame (the graph must allow a pass to be repeated with swapped bindings).
- **Confidence / limitations:** High.

### F.2 Reaction-diffusion (Gray-Scott)

- **Reference:** Sims, K. "Reaction-Diffusion Tutorial". https://karlsims.com/rd.html — accessed 2026-09-08. Webb, J. "Reaction-Diffusion Playground". https://jasonwebb.github.io/reaction-diffusion-playground/ — accessed 2026-09-08. Codrops, "Reaction-Diffusion Compute Shader in WebGPU" (2024). https://tympanus.net/codrops/2024/05/01/reaction-diffusion-compute-shader-in-webgpu/ — accessed 2026-09-08.
- **What was learned:** Two chemicals A/B with feed `f` and kill `k`; Laplacian via 3x3 kernel; multiple iterations per frame (Webb's playground runs many per frame) using ping-pong; a final pass maps concentration to colour. Cheap and extremely "festival" when fed by audio-modulated `f`/`k`.
- **Cost class:** C0-C1 per iteration; typically 8-32 iterations per frame.
- **Renderer requirements:** F.1 plus a way to inject seeds (mouse/audio) into the state texture (a small draw or compute write).
- **Confidence / limitations:** High.

### F.3 Grid fluid simulation (Stam 1999; GPU Gems 38)

- **Description:** Semi-Lagrangian advection, implicit diffusion, force application, pressure projection via Jacobi iterations; unconditionally stable.
- **Reference:** Stam, J. "Stable Fluids", SIGGRAPH 1999. Harris, M. "Fast Fluid Dynamics Simulation on the GPU", GPU Gems ch. 38. https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu — accessed 2026-09-08 (full chapter read).
- **What was learned:** Update is `u(n+1) = P(F(D(A(u(n)))))`; advection traces back along velocity and samples; diffusion and pressure are Poisson solves with **40-80 Jacobi iterations for pressure and 20-50 for viscosity** ("fewer than 20 produces noticeable artifacts"); boundaries via separate passes on edge cells; each step is a ping-pong.
- **Cost class:** C1-C2 in 2D at 1024^2; C3-C4 in 3D. 3D variants are what Notch/TouchDesigner market as "GPU fluids".
- **Renderer requirements:** 2D and 3D texture ping-pong pairs (RG/RGBA16F), many small dispatches per frame (60-130), so per-dispatch overhead must be low (Metal 4 unified encoder helps); dye/velocity injection from particles or audio.
- **Confidence / limitations:** High.

---

## Part G — Reflections / refraction and other looks

- **Cube-map / probe reflections** and **refraction** via screen-space UV offset by normal (cheap, C0) or true ray tracing (Metal 4 ray-tracing intersection, C3). Requirements: environment cube maps; optional acceleration structures. Confidence: high (standard practice).
- **Hardware ray tracing on M2 Max:** Metal 4 adds ray-tracing features for Apple silicon (WWDC25 "Discover Metal 4" https://developer.apple.com/videos/play/wwdc2025/205/ — accessed 2026-09-08). M2 lacks the dedicated RT hardware of M3+, so RT should be treated as an optional capability, not a dependency.

---

## Part H — Apple-specific rendering constraints that affect all of the above

- **TBDR and tile memory.** Apple GPUs render in tiles; multi-pass effects that keep data in tile memory (`storageModeMemoryless` attachments, tile shaders, imageblocks) avoid bandwidth. Deferred G-buffers and some post effects can stay on-tile. The abstraction should let a pass mark attachments as memoryless and should allow "tile" sub-passes later. (Apple developer documentation and WWDC sessions; general knowledge — verify per feature in the Feature Set Tables https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf.)
- **Unified memory.** Readback is cheap (no PCIe copy) but still needs a blit to a linear buffer and a completed command buffer; see `offline-rendering.md`.
- **Metal 4 changes** (WWDC25): unified compute/blit/AS encoder, `MTL4ArgumentTable` binding, compiler contexts for background pipeline compilation, MetalFX upscaling/frame interpolation/denoising (https://dev.to/arshtechpro/wwdc-2025-discover-metal-4-23f2 — accessed 2026-09-08). MetalFX upscaling is interesting for live, but must be disabled for deterministic offline frames.

---

## Part I — Requirements matrix (technique → renderer abstraction)

| Technique | Compute passes | 3D textures | Persistent history | Velocity buffer | Depth as texture | Mip-chain RT views | MRT | Indirect | Shared shader lib |
|---|---|---|---|---|---|---|---|---|---|
| PBR + IBL | bake | cube | – | – | – | yes | opt | – | yes |
| Tone map / grade | – | LUT | – | – | – | – | – | – | yes |
| Bloom | opt | – | – | – | – | **yes** | – | – | – |
| DoF | opt | – | – | – | **yes** | – | yes | – | – |
| Motion blur | yes | – | – | **yes** | yes | – | – | – | – |
| TAA | yes | – | **yes** | **yes** | yes | – | – | – | – |
| GTAO | yes | – | via TAA | – | **yes** | – | – | – | – |
| SSR | yes | – | yes | yes | Hi-Z | yes | – | – | – |
| Froxel fog | **yes** | **yes** | yes | – | yes | – | – | – | yes |
| Sky | yes | small | – | – | – | – | – | – | – |
| Noise / SDF | bake | yes | – | – | writes depth | – | – | – | **yes** |
| Feedback / RD / fluid | **yes** | 2D/3D pairs | **yes** | – | – | – | – | – | – |
| Particles (see particles.md) | **yes** | yes | yes | opt | yes | – | – | **yes** | yes |

**Milestone 0.1 must therefore provide (or leave room for):**

1. A pass-list/graph abstraction with declared resource reads/writes (Part A).
2. Transient vs persistent resource lifetimes; a texture pool; ping-pong swap by handle.
3. Compute, render and blit passes as peers; barriers derived from declarations.
4. HDR (`RGBA16Float`) colour targets by default; depth as a sampleable texture (not `framebufferOnly`).
5. 2D and 3D textures, cube maps, and per-mip render-target views; MRT.
6. A G-buffer-ready geometry pass contract: normal/roughness and **velocity** outputs even if unused in 0.1 (previous-frame matrices retained by the scene system).
7. Projection jitter and frameIndex-driven sequences (TAA, noise, grain) — the frame clock is an explicit object.
8. A shared MSL library compiled into both compute and graphics pipelines, with a source-hash pipeline cache.
9. Capability flags: mesh shaders, ray tracing, float atomics, tile shaders, MetalFX.
10. A "final sink" abstraction so the same graph ends in either a drawable or a readback buffer.

---

## Sources

1. O'Donnell, Y. "FrameGraph: Extensible Rendering Architecture in Frostbite", GDC 2017. https://www.gdcvault.com/play/1024612/FrameGraph-Extensible-Rendering-Architecture-in ; https://www.slideshare.net/slideshow/framegraph-extensible-rendering-architecture-in-frostbite/72795495 — accessed 2026-09-08.
2. Loggini, R. "Render Graphs" (2021). https://logins.github.io/graphics/2021/05/31/RenderGraphs.html — accessed 2026-09-08.
3. Google Filament, "Physically Based Rendering in Filament". https://google.github.io/filament/Filament.md.html — accessed 2026-09-08.
4. Karis, B. "Real Shading in Unreal Engine 4", SIGGRAPH 2013. https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf — accessed 2026-09-08.
5. Khronos Group, "Khronos PBR Neutral Tone Mapper Released" (2024). https://www.khronos.org/news/press/khronos-pbr-neutral-tone-mapper-released-for-true-to-life-color-rendering-of-3d-products — accessed 2026-09-08.
6. Blender PR #118936, "Add Khronos PBR Neutral tone mapper". https://projects.blender.org/blender/blender/pulls/118936 — accessed 2026-09-08.
7. model-viewer, "PBR Neutral Tone Mapping" example. https://modelviewer.dev/examples/tone-mapping — accessed 2026-09-08.
8. AgX Tonemapping Unity port. https://github.com/unitycoder/AgX-Tonemapping-Unity — accessed 2026-09-08.
9. Selan, J. "Using Lookup Tables to Accelerate Color Transformations", GPU Gems 2 ch. 24. https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-24-using-lookup-tables-accelerate-color — accessed 2026-09-08.
10. Jimenez, J. "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014. https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/ ; https://advances.realtimerendering.com/s2014/ — accessed 2026-09-08.
11. Bjørge, M. "Bandwidth-Efficient Rendering", SIGGRAPH 2015 (dual filter). https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/siggraph2015_2D00_mmg_2D00_marius_2D00_notes.pdf — accessed 2026-09-08.
12. LearnOpenGL, "Phys. Based Bloom" (2022). https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom — accessed 2026-09-08.
13. Wronski, B. "Separable disk-like depth of field" (2017). https://bartwronski.com/2017/08/06/separable-bokeh/ — accessed 2026-09-08.
14. AMD, "FidelityFX Depth of Field". https://gpuopen.com/manuals/fidelityfx_sdk/techniques/depth-of-field/ — accessed 2026-09-08.
15. Sousa, T. "Graphics Gems from CryENGINE 3", SIGGRAPH 2013. https://www.slideshare.net/TiagoAlexSousa/graphics-gems-from-cryengine-3-siggraph-2013 — accessed 2026-09-08.
16. Pettineo, M. "How To Fake Bokeh". https://therealmjp.github.io/posts/bokeh/ — accessed 2026-09-08.
17. McGuire, M. et al. "A Reconstruction Filter for Plausible Motion Blur", I3D 2012. https://www.researchgate.net/publication/254007575_A_Reconstruction_Filter_for_Plausible_Motion_Blur — accessed 2026-09-08.
18. Guertin, J.-P., McGuire, M., Nowrouzezahrai, D. "A Fast and Stable Feature-Aware Motion Blur Filter", HPG 2014. https://casual-effects.com/research/Guertin2014MotionBlur/Guertin2014MotionBlur-small.pdf — accessed 2026-09-08.
19. Karis, B. "High Quality Temporal Supersampling", SIGGRAPH 2014. https://de45xmedrsdbp.cloudfront.net/Resources/files/TemporalAA_small-59732822.pdf — accessed 2026-09-08.
20. Tardif, A. "Temporal Antialiasing Starter Pack". https://alextardif.com/TAA.html — accessed 2026-09-08.
21. Jimenez, J. et al. "Practical Realtime Strategies for Accurate Indirect Occlusion", SIGGRAPH 2016. https://www.iryoku.com/downloads/Practical-Realtime-Strategies-for-Accurate-Indirect-Occlusion.pdf — accessed 2026-09-08.
22. Intel, XeGTAO. https://github.com/GameTechDev/XeGTAO — accessed 2026-09-08.
23. Stachowiak, T. "Stochastic Screen-Space Reflections", SIGGRAPH 2015. https://www.ea.com/frostbite/news/stochastic-screen-space-reflections ; https://advances.realtimerendering.com/s2015/ — accessed 2026-09-08.
24. Wronski, B. "Volumetric Fog", SIGGRAPH 2014; AC4 GDC slides. https://advances.realtimerendering.com/s2014/ ; https://bartwronski.com/wp-content/uploads/2014/03/ac4_gdc.pdf ; https://bartwronski.com/publications/ — accessed 2026-09-08.
25. Hillaire, S. "Physically Based and Unified Volumetric Rendering in Frostbite", SIGGRAPH 2015. https://www.slideshare.net/slideshow/physically-based-and-unified-volumetric-rendering-in-frostbite/51840934 ; https://www.shadertoy.com/view/XlBSRz — accessed 2026-09-08.
26. Hillaire, S. "A Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020. https://onlinelibrary.wiley.com/doi/abs/10.1111/cgf.14050 ; https://diglib.eg.org/items/8a3e5350-18b3-46bd-9274-3add5af88c75 ; https://trist.am/blog/2024/atmosphere-rendering/ — accessed 2026-09-08.
27. Perlin, K. "Improving Noise" (2002), reference implementation. https://mrl.cs.nyu.edu/~perlin/noise/ — accessed 2026-09-08.
28. Gustavson, S. "Simplex noise demystified" (2005). https://cgvr.cs.uni-bremen.de/teaching/cg_literatur/simplexnoise.pdf — accessed 2026-09-08.
29. McEwan, I. et al. "Efficient computational noise in GLSL" (2012). https://ar5iv.labs.arxiv.org/html/1204.1461 — accessed 2026-09-08.
30. Worley, S. "A Cellular Texture Basis Function", SIGGRAPH 1996. https://dl.acm.org/doi/10.1145/237170.237267 — accessed 2026-09-08.
31. Quilez, I. "fBM". https://iquilezles.org/articles/fbm/ ; "Domain Warping". https://iquilezles.org/articles/warp/ — accessed 2026-09-08.
32. NVIDIA GPU Gems 2 ch. 26, "Implementing Improved Perlin Noise". https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-26-implementing-improved-perlin-noise — accessed 2026-09-08.
33. Hart, J. "Sphere tracing", The Visual Computer 1996. https://link.springer.com/article/10.1007/s003710050084 — accessed 2026-09-08.
34. Scratchapixel, "Rendering Distance Fields: Sphere Tracing". https://www.scratchapixel.com/lessons/advanced-rendering/rendering-distance-fields — accessed 2026-09-08.
35. Harris, M. "Fast Fluid Dynamics Simulation on the GPU", GPU Gems ch. 38. https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu — accessed 2026-09-08.
36. Sims, K. "Reaction-Diffusion Tutorial". https://karlsims.com/rd.html — accessed 2026-09-08.
37. Webb, J. "Reaction-Diffusion Playground". https://jasonwebb.github.io/reaction-diffusion-playground/ — accessed 2026-09-08.
38. Codrops, "Reaction-Diffusion Compute Shader in WebGPU" (2024). https://tympanus.net/codrops/2024/05/01/reaction-diffusion-compute-shader-in-webgpu/ — accessed 2026-09-08.
39. Apple, WWDC25 "Discover Metal 4". https://developer.apple.com/videos/play/wwdc2025/205/ ; summary https://dev.to/arshtechpro/wwdc-2025-discover-metal-4-23f2 — accessed 2026-09-08.
40. Apple, Metal Feature Set Tables. https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf — referenced 2026-09-08.
