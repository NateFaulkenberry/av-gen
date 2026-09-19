# The path tracer

A CPU path tracer over Embree, written to produce an image that is not constrained by the realtime
renderer's approximations. It is a **second renderer over one scene contract**, not a mode of the
first: it reads the same `scene::Scene` the WebGPU rasteriser reads and shares no pass, no resource
and no frame graph with it.

**It is not what this codebase calls "offline".** That word means `QualityTier::Offline` -- a
higher-quality configuration of the rasteriser -- and the ADR-020 batch render pipeline. Both keep
their meaning. This renderer is named for its technique: `avgen::pathtrace`, in `src/pathtrace/`.
See **ADR-344**, which is the thing to cite when the two vocabularies are confused.

## Where it sits

```
author -> realtime WebGPU -> compose / animate / scrub -> LOCK SHOT
                                                              |
                                                              v
                                          path trace -> AOVs -> linear HDR EXR
                                                                      |
                                                                      v
                                                          final colour pipeline
```

The tracer's job **ends at scene-linear radiance**. It never tone maps, never writes 8-bit, and
never reaches into the realtime post chain. Colour management is downstream of the EXR.

## Status

| Phase | What | State |
|---|---|---|
| 0 | Recon, Embree spike on Apple Silicon | done |
| 1 | Primary rays, Lambertian, one area light, shadow rays, accumulation, EXR | **done** |
| 2 | glTF metallic-roughness BRDF, textures, colour space | **done** |
| 3 | BSDF + light sampling, MIS, Russian roulette | **done** |
| 4 | OIDN denoising | **done** (opt-in) |
| 5 | Scene integration: CPU skinning, procedural scatter | **done** |
| 6 | AOVs: albedo, normal, multi-layer EXR | **partly** |
| 7 | Glowmere | not started |
| 8 | Production features | not started |

## Design

**`pathtrace::Snapshot`** (`snapshot.hpp`) is the transient render snapshot. `scene::Scene` is
mutated in place by its controller, so two timeline times cannot coexist and the tracer must never
hold a reference across an update. `buildSnapshot(scene)` takes a flat, world-space, immutable copy
of exactly what the tracer can see. It is **not a second scene graph**: no hierarchy, no parameters,
no update, and nothing reads it back.

Call `Engine::update(FrameTime{t, dt, i})` first. The tracer does no scene evaluation of its own,
by design.

**`pathtrace::EmbreeScene`** (`embree_scene.hpp`) is the only file that includes Embree, behind a
pimpl so `<embree4/rtcore.h>` stays out of every translation unit and `embree` stays a PRIVATE link
dependency. Embree owns intersection, the BVH and occlusion queries; **everything else is AV Gen's**
-- materials, BSDFs, sampling, the integrator, AOVs, accumulation and output.

**Threading.** Embree is given an explicit thread count and the BVH is committed with
`rtcJoinCommitScene` from threads this process already owns, so Embree starts no pool of its own.
`app::JobSystem` is not used and cannot be: it is a two-worker FIFO of whole jobs with no
parallel-for, and its header forbids waiting on it from a render thread. The render loop uses the
banded scanline split that `src/world/terrain.cpp` and `src/world/ecology.cpp` already established
as this repo's CPU-parallelism convention.

**Determinism** (spec section 27). The image is a pure function of the snapshot and the settings.
`pathtrace::Sampler` wraps `avgen::Rng` (the project's PCG32, mandated by
`docs/research/offline-rendering.md` section 12.3) and seeds from `(seed, pixelIndex, sampleIndex)`,
so a pixel can be regenerated in isolation and the image does not depend on the thread count. A test
asserts bit-identical output at 1 thread and at 6, with a different-seed control so the equality is
not vacuous.

## Capability report (spec sections 55, 87)

A scene feature the tracer cannot represent is **detected, counted and reported** -- never silently
dropped and never silently altered in the realtime scene. `Snapshot::capabilities` carries one row
per feature and `logCapabilities()` prints it at render startup, so the answer arrives before the
pixels rather than after somebody notices they are wrong.

### Known limitations

| Feature | Status | Why |
|---|---|---|
| Particles | **unsupported** | GPU-only by construction (`src/scene/particles.hpp`): the CPU holds settings, the simulation is a compute shader, and no positions exist to intersect at any time |
| Skinned characters | **supported** | four-influence linear blend skinning from `SkinnedRig::palette`. A rig with an EMPTY palette (frozen by `cullDistance`) is refused and counted, because its bind pose is not where the character is |
| Procedural / scatter | **degraded** | placed correctly from the baked `instances`, but baked to world-space triangles rather than Embree instances, so memory grows with instance count; deformers and effectors are not applied |
| SDF in `Raymarch` mode | **unsupported** | no triangles exist; `spatial::SdfTree` is CPU-evaluable but sphere tracing is a separate integrator path |
| Water surface | **degraded** | the mesh is a real CPU sheet, but ripples, swell, foam and refraction live in `shaders/water.wgsl` |
| Point / spot lights | **degraded** | treated as delta positions, so their shadows are hard; the realtime path softens them |
| Volumetrics | **unsupported** | a stated non-goal for now (spec section 72) |
| HDR environment map | **degraded** | the analytic sky (`scene::skyRadiance`, ADR-036) is used; image-based lighting needs section 46 importance sampling |
| DWAA / DWAB EXR compression | unavailable | tinyexr declares them "not yet supported"; ZIP, PIZ and PXR24 are available |

Three realtime per-frame decisions are deliberately **overridden rather than inherited**, because
inheriting them would be the ADR-146 mistake in a new place:

* terrain's camera-dependent LOD -- the tracer wants LOD 0;
* `Entity::cameraCulled`, which means "outside the realtime frustum", not "not in the scene";
  off-screen geometry still casts shadows and still bounces light into the frame;
* procedural LOD rungs 2 and 3, which are camera-facing billboards.

## Phase 6 in particular (partly)

`writeFramebufferAovExr` puts the beauty pass and every captured AOV into **one multi-layer EXR**.
Beauty is `R/G/B` because it is colour and every compositor expects it there; albedo is
`albedo.R/G/B` for the same reason under its own prefix; and the normal is **`normal.X/Y/Z`, never
`normal.R/G/B`** -- spec section 33, because a colour-managed pipeline downstream will transform
anything it believes is colour, and the result looks like a shading bug rather than a naming
mistake. Tests assert the forbidden names are absent, not merely that the right ones are present.

`assets::writeExrLayers` is the new engine-side primitive: arbitrary channel names and
**per-channel** half/float, which `writeExr` cannot express at all. No new dependency -- the pinned
tinyexr header always exposed `EXRHeader`/`EXRChannelInfo`/`SaveEXRImageToFile`. This discharges the
debt recorded at `src/app/render_settings.hpp`: *"the layered form is a better file and a bigger
change; it is recorded as not done rather than half-built."*

Still to do in Phase 6: depth, motion vectors, object and material IDs, and reconciling this
vocabulary with `app::RenderSettings::aovNames()`.

## Phase 5 in particular

**CPU skinning.** Four-influence linear blend skinning from `Scene::rigs[e.rig].palette`, the same
model `shaders/skinning.wgsl` runs. Normals take the skin matrix's inverse transpose, which only
differs under a non-uniform joint scale -- and when it differs, the shading shears in a way that
looks like a BSDF fault. A rig whose palette is empty is **refused and counted**: that happens when
`SkinnedRig::cullDistance` froze it, and the bind pose is not where the character is. Set
`cullDistance` and `updateHz` to 0 for an offline trace.

**Procedural scatter.** Resolved through `scene::makeSourceMesh`, the same function the realtime
renderer calls, at LOD 0 only.

**Do not use `ProceduralGeometry::instanceMatrix()`.** It re-derives placement from the
*distribution* and ignores the baked `instances` array entirely. The realtime renderer uploads
`object.instances` (`procedural_renderer.cpp:1418`), so the tracer must read the same thing or the
two renderers draw different worlds. The instance transform is composed exactly as
`shaders/procedural.wgsl` composes it -- `objectModel * T(position) * R(rotation) * S(scale) *
sourceTransform` -- where the object model is `distributionTransform`, already baked to world at
flatten. Found by measurement: `instanceMatrix` put twelve cubes spanning 22 m into a 7 m span.

## Phase 4 in particular

Denoising through Open Image Denoise, **off by default**. Configure with
`-DAVGEN_PATHTRACE_DENOISE=ON` to fetch the SHA256-pinned prebuilt macOS arm64 archive. Without it,
`denoiseAvailable()` is false and `denoise()` fails with a message naming the option -- it never
returns the input unchanged, because a denoise that silently did nothing is worse to debug than a
refusal.

**Why prebuilt and not CPM-from-source:** OIDN's CPU device requires oneTBB *and* ISPC >= 1.21, a
third-party **compiler toolchain** its CMake will not proceed without. See **ADR-346**, which also
amends ADR-344's claim that this work would bring no oneTBB into the tree.

The albedo and normal **feature AOVs** arrived here rather than at Phase 6 because the denoiser
needs them. Captured from the first hit, opt-in via `TraceSettings::captureFeatures`, and capturing
them leaves the beauty pass bit-identical (tested).

`docs/pathtrace/phase4-denoise-before-8spp.png` and `-after-oidn.png`: a Cornell-style box at 8 spp,
before and after. RMSE against a 512-sample reference falls by more than 30% and the mean moves
0.1183 to 0.1175.

## Phase 3 in particular

Multiple importance sampling between light sampling and BSDF sampling, with the power heuristic
(beta = 2), plus Russian roulette from a configurable depth.

**Two kinds of emitter, deliberately treated differently.** `scene::PunctualLight` is analytic and
has no geometry in the BVH, so a BSDF ray can never hit one -- light sampling is the only strategy
that can find it and it takes **weight 1**. MIS-weighting it against the BSDF's density would
down-weight the only estimator that works and lose energy nothing else supplies. **Emissive
geometry** can be hit by both strategies, so it gets a real power-heuristic weight on both sides.
`Snapshot::emissiveTriangles` carries an area CDF for uniform-by-area selection.

`TraceSettings::Strategy` exposes `Mis`, `LightOnly` and `BsdfOnly`. That is not a feature; it is
test apparatus. All three are unbiased estimators of the same integral, so they must converge to the
same image, and a MIS weight error that leaves the combined estimator plausible is only visible when
the strategies are compared **separately** -- both overall and region by region.

`docs/pathtrace/phase3-fireflies-before-bsdf-only.png` and `-after-mis.png` are the same white room
at the same 64 spp: **1250 outlier pixels become 1**, and the mean luminance agrees to within 4%,
which is what makes it a variance reduction rather than a change of answer.

## Phase 2 in particular

The glTF 2.0 metallic-roughness BSDF (`bsdf.hpp`): Lambertian diffuse plus Cook-Torrance specular
with GGX, Smith height-correlated visibility and Schlick Fresnel, combined as the glTF spec's
Appendix B combines them. Metals take F0 from the base colour and have no diffuse lobe; dielectrics
get F0 = 0.04. `sampleBsdf` picks a lobe by Fresnel weight and returns the *combined* PDF, so the
estimator stays unbiased whichever lobe it picked -- and a test checks the returned weight against
`evaluateBsdf(...) * cos / bsdfPdf(...)` recomputed independently, because a disagreement there is a
bias no image inspection would reveal.

Textures (`texture.hpp`): all five glTF slots, wrap modes, bilinear or nearest.

**The colour-space rule (section 18) is taken from the loader, not guessed.**
`assets::gltf_loader` already tags base colour and emissive as sRGB and metallic-roughness, normal
and occlusion as linear, per the glTF spec, and the answer rides in `scene::TextureData::format`.
The tracer decodes if and only if the format says `Rgba8Srgb`. Two consequences worth stating:
alpha is coverage and is never decoded, and **filtering happens after the decode**, which is the
correct order -- blending two sRGB bytes and then decoding is a different number, and the difference
shows as darkened edges exactly where people look.

**Known: the glTF BRDF gains energy at grazing angles.** A white, smooth, non-metallic surface
reaches a directional albedo of 1.68. This is the specification's model behaving as specified, not a
defect in this code -- metals conserve at 0.9988, so GGX/Smith/Fresnel are correct. It is kept
faithful per section 19 and pinned as a band. **See ADR-345**, and check it first if an interior
render is ever inexplicably bright.

`docs/pathtrace/phase2-metal-and-texture.png` shows the target scene with a real metal in the middle
-- reflecting the area light, the ground and the cyan sphere beside it -- and a checkered sRGB
texture on the floor.

## Phase 1 in particular

Primary rays, a Lambertian BSDF, direct lighting from the scene's lights with shadow rays, the
analytic sky as environment and miss colour, a cosine-weighted bounce per extra depth, progressive
accumulation and linear float EXR out.

Not yet at Phase 3: adaptive sampling, denoising, instancing, normal mapping (there are no tangents
in `scene::Vertex`, so they must be derived first). Emissive geometry is only found by rays that happen to hit
it, so its bounce light is noisy until MIS lands in Phase 3 -- visible in the target image below as
speckle on the floor near the cyan sphere.

`docs/pathtrace/phase1-target-scene.png` is the spec's section 69 first visual target rendered by
this code: three spheres on a diffuse ground plane under a 5x5 m area light, soft shadows, an
emissive sphere bouncing cyan onto the floor, and a dark analytic sky.

## Testing

`tests/unit/test_pathtrace_camera.cpp` and `tests/unit/test_pathtrace_render.cpp`.

Two rules the tests are built on, both learned the hard way on this branch:

**Camera rays are checked against `scene::Camera`'s own projection matrix**, not against a
hand-derived expectation. A ray generator and a projection that disagree by a sign or a half-pixel
both look plausible alone; only making them answer the same question catches it. There is a
wrong-pixel control so a generator that ignored its arguments could not pass.

**Every image arm asserts what is IN the picture**, not only that the arithmetic closed. The Phase 0
Embree spike passed every numeric check while rendering one triangle instead of two, because the
second was exactly occluded. So: the blue sphere must read blue, the emissive one must be the
brightest thing in the frame and must read cyan, and the ground under a sphere must be measurably
darker than ground beside it.

Two hidden cases (`[.pathtrace-preview]`, `[.pathtrace-diag]`) render the target scene to a PPM and
print per-region means. They are how the asserted pixel regions were chosen rather than guessed, and
they are how the first render's all-orange cast was found -- the test scene had set a light's
`temperature` to 0, which clamps to deep orange, and no numeric assertion in the suite would have
told anyone.

## Gotchas

* **Do not set `CMAKE_CXX_STANDARD` globally around Embree.** It overrides Embree's own
  `-std=c++11` and Embree 4.4.0 does not compile as C++23 under Apple clang 21. `Dependencies.cmake`
  saves, unsets and restores it; the comment there explains why.
* **`cp` of a Mach-O invalidates its ad-hoc code signature on Apple silicon** and the kernel
  SIGKILLs the copy with **no output at all** (exit 137). `tools/pathtrace-test.sh` re-signs after
  copying; it exists because a plain `cp` looked like a mysterious silent crash.
* **A shadow ray must clear the surface it leaves AND stop short of the one it aims at.** Offsetting
  the origin by `eps` along the normal already consumes roughly `eps * (n.dir)` of the distance
  budget, so subtracting `eps` from `tfar` as well cancels the gap to within 1e-5 and the ray reports
  itself occluded by its own target. Use a relative shortfall measured from the offset origin. This
  cost half of Phase 3: 200 of 200 shadow rays to an area emitter were blocked by that emitter.
* A new ADR fails `test_repo_hygiene` until it is listed in `docs/decisions/README.md`. The suite
  reads that directory from disk, so it fails in a binary compiled before the ADR existed.
