# The path tracer

A CPU path tracer over Embree, written to produce an image that is not constrained by the realtime
renderer's approximations. It is a **second renderer over one scene contract**, not a mode of the
first: it reads the same `scene::Scene` the WebGPU rasteriser reads and shares no pass, no resource
and no frame graph with it.

**It is not what this codebase calls "offline".** That word means `QualityTier::Offline` -- a
higher-quality configuration of the rasteriser -- and the ADR-020 batch render pipeline. Both keep
their meaning. This renderer is named for its technique: `avgen::pathtrace`, in `src/pathtrace/`.
See **ADR-351**, which is the thing to cite when the two vocabularies are confused.

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
| 6 | AOVs: albedo, normal, emission, depth, id, motion | **done** |
| 7 | Glowmere | not started |
| 8 | Embree instancing | **partly** |

## From the Render panel

`Renderer: (o) Realtime  ( ) Path trace` at the top of the existing Render panel -- a choice
*within* it, not a second panel beside it, so resolution, the output path and the progress line are
the same affordances serving whichever renderer is selected. `docs/pathtrace/render-panel-path-trace.png`
and `-realtime.png` are captures of both states.

**The panel is a view of `pathtrace::TraceJob` and holds no trace state of its own.** Its pointers
(`pathTraceSettings`, `pathTraceSeconds`, ...) address settings the Application owns, exactly as
`renderSettings` does, and every number it shows comes from `pathTraceProgress()`. The only thing
the panel owns is `rendererChoice_`, which is which controls are on screen rather than anything
about a render.

**Progress honesty survives the progress bar.** A bar is drawn *only* for a stage that can measure
itself -- rendering, which counts finished samples. Scene build, BVH build, denoise and write show
the **stage name** instead ("Building acceleration..."), because a bar sitting at 40% while nothing
is known is a lie, and a UI is where that discipline usually dies: a still bar looks broken and a
creeping one looks fine.

`denoise` is greyed out with an explanatory tooltip when the build has no OIDN, rather than offered
and then failing.

## Running one

```
avgen --project examples/world/glowmere-valley-2-multicam.json \
      --pathtrace out.exr --pt-seconds 12 --pt-samples 32 --pt-depth 3 --size 640x360 --pt-aovs
```

Spelled like `--render` on purpose: `--pathtrace <out>` takes the output path the same way, sets
headless the same way, and takes its resolution from the existing `--size WxH` rather than inventing
a parallel vocabulary. The `--pt-*` flags are only the things a rasteriser has no equivalent for --
samples per pixel, path depth, seed, threads, AOVs, denoise, and the ADR-352 albedo probe.

**No GPU is involved.** The tracer is CPU-only and `EngineMode::Offline` evaluates a scene without a
device, so a trace runs while the GPU is busy with something else -- which, on a machine shared by
several agents, it usually is.

`pathtrace::TraceJob` is the entry point behind that flag, with the states spec section 36 names:
`Queued -> BuildingScene -> BuildingAcceleration -> Rendering -> Denoising -> Writing ->
Complete | Cancelled | Failed`. `start()` runs it on the job's own thread and returns immediately;
`progress()` is safe from any thread; `cancel()` sets a flag the stages poll -- between sample
batches, and before denoising and before writing -- and never terminates a thread.

**Progress is real, not interpolated.** Rendering reports a genuine count of finished samples, so
`fractionKnown` is true. Scene build, BVH build, denoise and write emit no intermediate signal, so
they report `fractionKnown == false` rather than a bar that creeps while nothing is known. A test
asserts both halves of that.

**Threading.** The job owns exactly one thread: a coordinator. The per-batch workers belong to
`PathTracer::render`, which creates and *joins* them inside each sample batch, so they exist only
while a batch is running. There is no second long-lived pool. `app::JobSystem` is deliberately not
used and cannot be -- two hard-coded workers, no parallel-for, and a header that forbids waiting on
it from a render thread; a trace occupying one of its two workers for minutes would starve world
generation and the AI control plane, which are what it exists for.

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
(Until ADR-582 that was true of the top level only: procedural children went through
`rtcCommitScene`, which does use Embree's pool. Every commit is joined now.)
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
| Procedural / scatter | **degraded** | drawn as Embree instances, so geometry is stored once. Deformers and effectors are still not applied and LOD rung 0 is always used |
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

## BVH reuse across a sequence (ADR-582)

A `PathTracer` keeps its `EmbreeScene` between renders, and `app::TraceSequence` keeps one tracer for
the whole range, so each frame rebuilds only what its snapshot changed.

* **Built over object space.** `TriangleMesh::objectPositions` + `objectToWorld` are what the BVH
  holds; `positions` stays world space for shading. The Tree of Life moves every vertex every frame
  in world space and none in object space -- the island drifts as a rigid body -- so this is what
  makes reuse possible at all there.
* **Always two levels.** Meshes sharing a bit-identical transform share a child scene (a rigid
  assembly keeps a single-level BVH); a rigged mesh is always alone; each procedural source is a
  child. The structure is a function of the snapshot only, never of history.
* **Exact change detection.** A child is kept only if its members, counts, vertices and indices are
  bit-identical to the buffers Embree holds; the top level only if every child and every transform
  is. Everything else is rebuilt from scratch through the first frame's code.
* **The control arm is live:** `TraceSettings::reuseAcceleration = false`, `--pt-rebuild-bvh`. A
  reused range and a rebuilt one write byte-identical EXRs; `test_pathtrace_bvh_reuse.cpp` holds the
  unit arms, each with a control that fails with detection switched off.
* The sequence logs each frame's update: objects built/kept, triangles built/kept, whether the top
  level was rebuilt, how many transforms moved, and the compare/build split.

## Phase 8 in particular (partly)

**Embree instancing for procedural scatter.** Each `ProceduralGeometry` becomes one child `RTCScene`
holding its triangles once, plus one `RTC_GEOMETRY_TYPE_INSTANCE` per copy. Measured on Glowmere
Valley: **6,832,682 stored triangles became 321,771** -- 21x less -- while still drawing 67,770
instances.

Two things this changed that are easy to get wrong:

* **A hit must be resolved through `instID[0]`, not `geomID`.** Inside an instance, `geomID` is the
  geometry within the *child* scene, so reading it as a top-level id makes every instanced hit
  resolve to object zero. `EmbreeScene` keeps its own table from top-level id to
  `(list, object, instance)`.
* **Normals are computed from our own data and transformed by the instance's normal matrix**, not
  read from `rh.hit.Ng`, which for an instanced hit is in the child's space. Doing it once, our way,
  avoids a convention that is easy to get right in one branch and wrong in the other.
* `RTC_FORMAT_FLOAT4X4_COLUMN_MAJOR`, **never** `FLOAT3X4`. `glm::mat4` is four columns of *four*
  floats; a 3x4 layout reads each column's w as the next column's x and every instance lands
  somewhere arbitrary. Caught because rays missed all three test cubes.

Motion vectors are deliberately **not** tracked for instanced geometry: a scattered instance's
previous transform is not carried, and pairing instances between frames by index would be a guess.

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

**Motion** (`motion.X/Y`) needs the previous frame, so `buildSnapshot(scene, &previousScene)` takes
a second evaluation and the snapshot remembers **both the geometry and the camera** as they were --
a motion pass built from object transforms alone is wrong whenever the camera moves, and one built
from the camera alone is wrong whenever anything else does. Tests cover both halves separately.

The previous positions are interpolated with the **current** hit's barycentrics, which is what makes
the vector track a point on the surface rather than a screen position. A mesh whose vertex count
changed between the two frames is refused and counted rather than paired arbitrarily: re-scattered
foliage and re-tessellated terrain both do that, and a confident wrong vector is worse than none.

The vocabulary **extends** `app::RenderSettings::aovNames()` rather than rivalling it (spec section
31). Shared: `normal`, `emission`, `depth`, `id`. Added: `albedo`, which the realtime renderer has no
equivalent of and the denoiser requires. **Not implemented:** `shadow`, which is a
realtime pass rather than a quantity a path tracer naturally produces. A test asserts the shared
names really are in `aovNames()`, so the two vocabularies cannot drift apart unnoticed.

Two details that are decisions, not incidentals:

* **Depth is view-space metres**, `dot(hit - eye, forward)`, not the ray's `t`. Using `t` makes a
  flat wall's depth bow outwards toward the corners, because those rays travelled further. A test
  pins the wall's corner and centre to the same value.
* **Depth and id are not averaged across samples.** A mean of two depths at a silhouette is a
  distance to nothing and a mean of two ids is a third object, so both take the first sample. A miss
  writes -1, never 0 or object 0, because those are plausible values a compositor would act on.

### `ProceduralGeometry::instanceMatrix` is not where instance *i* is

It is where the **distribution** would have put it: `distributionTransform * placement(i) *
variation * sourceTransform`, which is what its own documentation says and what
`procedural_renderer.cpp:1725` uses it for as the CPU reference the GPU is verified against. It does
**not** read `instances[i]`, and the baked array is what the renderer actually draws. Read
`instances`; leave that function alone.

The reusable lesson is the shape of the check that caught it: twelve cubes placed across 22 m came
out spanning 7 m **with a correct triangle count**. Counting what arrived would have passed. Asking
where it landed did not.

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
third-party **compiler toolchain** its CMake will not proceed without. See **ADR-353**, which also
amends ADR-351's claim that this work would bring no oneTBB into the tree.

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
faithful per section 19 and pinned as a band. **See ADR-352**, and check it first if an interior
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
