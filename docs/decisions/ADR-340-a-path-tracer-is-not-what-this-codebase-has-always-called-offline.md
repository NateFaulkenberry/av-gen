# ADR-340: A path tracer is not what this codebase has always called "offline"

**Status:** Accepted (Phase 0 of the Embree path tracer; the later phases revise this file)
**Date:** 2026-09-18
**Branch:** `agent/raytrace`, branched from `57b41994`
**Relates to:** ADR-020 (render jobs), ADR-125 / ADR-146 / ADR-147 / ADR-186 / ADR-212 (the
Offline *tier*), ADR-008 (dependency policy), research `docs/research/offline-rendering.md`

## Problem

The brief asks for a second renderer: a CPU path tracer over Embree that consumes an evaluated
scene and writes linear HDR EXR with AOVs, denoised by OIDN. Its own spec calls the entry points
`OfflineRenderer`, `OfflineRenderScene`, `OfflineRenderSettings`, `OfflineRenderJob`.

**That word is taken.** In this codebase "offline" already denotes two established, shipped,
ADR-backed things, neither of which involves a ray:

* `QualityTier::Offline` — a higher-quality configuration of the *same* WebGPU rasteriser.
  ADR-125 (no hysteresis), ADR-146 (only half of that is true, measured), ADR-147 (the batch
  path never actually selects it, measured), ADR-186 (it lifts distance limits), ADR-212 (it may
  supersample) are five ADRs of accumulated meaning attached to that one word.
* The deterministic frame-sequence pipeline of ADR-020 — `app::RenderJob`, `RenderSettings`,
  the readback ring, the sequence hash.

An `OfflineRenderer` that is a path tracer, sitting beside a `QualityTier::Offline` that is a
rasteriser setting and an `app::RenderJob` that renders at `QualityTier::Realtime` (ADR-147),
would make both halves of the codebase harder to read. The spec's §9 layout is explicitly a
suggestion. The project's own coherence wins.

The owner's §88 makes the collision worse before it makes it better: it uses "offline" in the new
ray-tracing sense ("Realtime WebGPU → LOCK SHOT → Offline Embree"). Both meanings are now in
circulation, which is exactly why the code needs names that cannot be confused even when the prose
can.

## Decision: name it after the technique, not after when it runs

New namespace **`avgen::pathtrace`**, new directory **`src/pathtrace/`**, a sibling of
`src/rendering/` (the WebGPU rasteriser) rather than a child of it.

| Spec name | This codebase | Why |
|---|---|---|
| `OfflineRenderer` | `pathtrace::PathTracer` | says what it does, not when |
| `OfflineRenderScene` | `pathtrace::Snapshot` | it is the §8 transient render snapshot, not a scene graph; the name should not suggest it is authoritative |
| `OfflineRenderSettings` | `pathtrace::TraceSettings` | cannot be mistaken for `app::RenderSettings` |
| `OfflineRenderJob` | `pathtrace::TraceJob` | cannot be mistaken for `app::RenderJob` |

"Offline" keeps its existing meaning everywhere it already appears. Nothing in
`src/rendering/`, `QualitySettings::forTier`, or `app::RenderJob` is renamed: a rename would be
five ADRs of churn to free up a word that the technique-based name does not need.

The rule, stated once so it can be cited: **"offline" is a quality tier and a batch pipeline;
"path trace" is a renderer.** A future `--pathtrace` render is still an *offline render* in the
ADR-020 sense, and that sentence should read as sensible rather than as a contradiction.

`scene::CameraShot::locked` is a third trap in the same area and is **not** what §88's "LOCK SHOT"
means. That flag (`src/scene/camera_rig.hpp:244`) means "an event camera may not steal this shot".
§88's "LOCK SHOT" is a workflow verb — the author stops scrubbing and hands a shot to the tracer.
No identifier in `pathtrace` will be called `locked`.

## Decision: extend the existing tinyexr writer; do NOT add OpenEXR

The spec (§6, §33) asks for OpenEXR and arbitrary named AOV channels. This project already writes
EXR through **tinyexr** (`src/assets/exr.{hpp,cpp}`, `exr_impl.cpp`), pinned as a SHA256-pinned
v3.2.0 release archive.

Today's wrapper is narrow: `writeExr(path, w, h, rgba, half)` hard-requires exactly four channels
and calls the deprecated `SaveEXR` convenience function, so channel names are fixed at R/G/B/A and
the pixel type is per-image. That is genuinely not enough for §33, which requires `normal.X`,
`motion.X` named layers so downstream colour management does not treat them as colour.

**But the shortfall is in our wrapper, not in tinyexr.** The already-pinned header exposes the full
struct API — `EXRHeader`, `EXRChannelInfo` (256-char arbitrary names), per-channel
`pixel_types`/`requested_pixel_types`, `header.compression_type`, `SaveEXRImageToFile`,
`SaveEXRMultipartImageToFile`, `LoadEXRWithLayer`. Everything §33 asks for is reachable as **new
engine code in `exr.cpp` with no dependency change**, and it additionally buys per-channel half/float,
which the current API cannot express at all.

Adding OpenEXR would mean: a second EXR writer in tree; OpenEXR plus Imath as real CMake builds
rather than one header and miniz; two pinned dependencies with their own licences to audit; and a
reconciliation between two writers that would outlive this branch. §80 says add dependencies only
where not already present. EXR writing is present.

Accepted cost: tinyexr declares DWAA/DWAB compression "not yet supported", so the AOV files get
ZIP (or PIZ/PXR24). Deep EXR is a §72 non-goal anyway. If a delivery ever requires DWAA, that is
the moment to reopen this — not before.

This also discharges a debt the project already wrote down. `src/app/render_settings.hpp:117-119`
records one-file-per-AOV as a deliberate stopgap: *"The layered form is a better file and a bigger
change; it is recorded as not done rather than half-built."* This is that change.

## Decision: Embree owns no threads of its own

§36 forbids a second uncontrolled global thread pool, and the reconnaissance found the constraint
is sharper than it looks, because **`app::JobSystem` cannot serve as the path tracer's parallelism**:
it is a coarse FIFO of whole jobs with **two hard-coded workers**
(`application.cpp:959`, `:1101`), it has **no parallel-for**, and its own header forbids waiting on
it from a render thread. Nothing in the repository provides a data-parallel primitive; the de-facto
convention is a hand-rolled banded `std::thread` split (`src/world/terrain.cpp:452-471`,
`src/world/ecology.cpp:420-440`, both written to be byte-identical to serial).

Measured on the spike, all three of these work on this machine:

* `rtcNewDevice("threads=1")` — Embree starts no worker pool.
* `rtcNewDevice("threads=4,set_affinity=0")` — a bounded pool, affinity left to the OS.
* **`rtcJoinCommitScene` called from four AV Gen-owned `std::thread`s** — the BVH build runs on the
  caller's threads. `joined=4`, and the committed scene answers an intersection query afterwards.

So the path tracer builds its BVH with `rtcJoinCommitScene` on AV Gen-owned threads and configures
the device with an explicit thread count rather than accepting Embree's default. **TBB is not used**
(`EMBREE_TASKING_SYSTEM=INTERNAL`), so no oneTBB dependency and no second scheduler.

## The build trap, which cost the first spike build

Embree 4.4.0 **does not compile** under this machine's Apple clang 21 (`clang-2100.3.34.2`) when
built inside this project's CMake conventions. Twenty errors, all of the shape
*"type 'PrimInfoMB' (aka 'PrimInfoMBT<LBBox<Vec3fa>>') is not a direct or virtual base of
'embree::SetMB'"* in `kernels/builders/priminfo_mb.h`.

The cause is not Embree and not the compiler alone. Embree appends `-std=c++11` to
`CMAKE_CXX_FLAGS` (`common/cmake/clang.cmake:98`), but a globally-set `CMAKE_CXX_STANDARD`
emits its own `-std=` flag **later** on the command line, and the last one wins. The verbatim
compile line from the failing build:

```
-fsigned-char -fPIC -std=c++11 -fvisibility=hidden ... -DNDEBUG -O3 -std=gnu++2b -arch arm64
```

Embree was being compiled as C++23 against its will. **`CMakeLists.txt:14` sets
`CMAKE_CXX_STANDARD 23` globally**, so the integration will hit this exactly.

Mitigation for `cmake/Dependencies.cmake`: save, clear and restore `CMAKE_CXX_STANDARD` around
Embree's `CPMAddPackage`, and carry the engine's standard on engine targets via
`target_compile_features(... cxx_std_23)`. With the standard scoped, Embree 4.4.0 builds clean on
arm64. This is recorded because it is invisible at the CMake level and the error message points at
Embree's source.

## Licences (§81), verified from the fetched source rather than assumed

| Component | Licence | How verified |
|---|---|---|
| Embree 4.4.0 | Apache-2.0 | `LICENSE.txt` in the fetched tree |
| `sse2neon.h`, vendored inside Embree for the NEON path | MIT | header comment in `common/simd/arm/sse2neon.h` |
| OIDN | **not yet verified** — Phase 4 | not fetched at Phase 0; recorded as outstanding rather than assumed |

Embree's Example Renderer was **not** read or copied; nothing external is in this branch.

## What Phase 0 proved, and what it did not

Proved, on this Apple M2 Max (8P+4E) under Apple clang 21, by a standalone program built outside
AV Gen (§71): Embree 4.4.0 builds for arm64/NEON from a pinned tag; `rtcNewDevice` reports
version 40400 with native ray4 packets; a two-triangle BVH commits; `rtcIntersect1` returns the
nearer primitive, a correct `t`, a correct geometric normal and correct barycentrics; `rtcOccluded1`
reports occlusion; and a 320x200 image comes out. Sixteen checks, every one with a control arm
(ADR-182): a backward ray that must miss, an offset ray that must reach the *far* primitive, an
out-of-frame ray that must miss, a shadow ray whose `tfar` stops short and must not be occluded,
a coverage band rather than a floor, a not-black assertion, and a two-colour pixel count.

**Not proved, and worth stating because the first version of the spike hid it.** The coverage band
and the far-triangle unit check both passed while the rendered image contained **one** triangle:
the two triangles had been given identical angular size, so the far one was exactly occluded and
never appeared. Every number was green and the picture was wrong. The sixteenth check ("BOTH
triangles visible in the frame", counting orange and blue pixels separately) exists because of
that, and is the local form of the owner's standing rule that numerical agreement is not proof of
visual alignment.

Nothing about AV Gen scene translation, materials, coordinate-system agreement or performance is
proved by Phase 0, and no timings are recorded here: the machine is shared with two other agents
and a contended timing is worse than none (ADR-170).

## Consequences

- A second renderer's worth of new code under `src/pathtrace/`, touching nothing in
  `src/rendering/`. The realtime renderer cannot regress because it is not modified.
- `src/assets/exr.cpp` grows a second, wider entry point. The existing `writeExr` keeps its
  signature and its callers, so `app::RenderJob` is unaffected.
- Two new pinned dependencies eventually (Embree now, OIDN at Phase 4), both Apache-2.0,
  both needing `docs/dependencies.md` rows.
- `docs/decisions/` now has two neighbouring vocabularies for rendering quality. This ADR is the
  thing to cite when they are confused.

## The scene contract, as measured rather than assumed

The single question that could have killed this design is whether triangles exist on the CPU at
all. **They do.** `scene::Scene` owns `std::vector<MeshData> meshes` and
`std::vector<TextureData> textures` (`src/scene/scene.hpp:74-75`), and
`SceneRenderer::uploadMeshes` (`src/rendering/scene_renderer.cpp:1857-1902`) only *copies* from
them into GPU buffers — nothing is moved out or freed. `MeshData` is
`{vector<Vertex{pos,normal,uv}>, vector<uint32_t> indices, vector<SkinInfluence> skin}`, triangle
lists only. Texture pixels are retained in `TextureData::data` after upload. So the path tracer
reads the same memory the rasteriser reads, and §7's "one scene contract, two renderers" is
literally true rather than aspirational.

**Coordinate system, proved from code and not from the comment that also says so:**
`glm::lookAtRH` (`src/scene/scene.cpp:53`) and `glm::perspectiveRH_ZO` (`:57`), with
`GLM_FORCE_DEPTH_ZERO_TO_ONE` set project-wide (`src/CMakeLists.txt:56`) and no `proj[1][1] *= -1`
anywhere. Right-handed, +Y up, metres, camera looks down −Z, depth 0..1, CCW front faces. That is
glTF's convention, which is why §18/§19 map cleanly.

**A correction to the brief's framing of materials.** The brief warned that "the realtime path's
material model is thinner than glTF's, which matters for §17 and §19", on the evidence that a
`kind: "gltf"` node exposes only `emissiveBoost` and `roughnessScale`. Those two scalars are indeed
the only *node-authorable* controls (`composition.hpp:203-204`, applied at
`composition.cpp:5374-5379`) — but they are not the material. `scene::Entity::material` is a full
`scene::Material` (`scene_types.hpp:196-219`) copied verbatim from the glTF asset at flatten time
(`composition.cpp:3902-3910`): base colour, metallic, roughness, emissive colour and intensity,
alpha mode and cutoff, double-sidedness, and all five texture slots (base colour,
metallic-roughness, normal, emissive, occlusion) with wrap modes. **The thinness is in the
authoring surface, not in the data the tracer consumes.** §19's glTF metallic-roughness baseline is
therefore a direct mapping, not a reconstruction. This is a material improvement on the
reconnaissance and it makes Phase 2 much smaller than budgeted.

Two things that are genuinely missing and must be derived: **there are no tangents** in `Vertex`,
so normal mapping needs tangents generated from UVs; and `Material::program` may name a procedural
material program, which has a CPU evaluator (`scene::evaluateMaterialProgram`,
`src/scene/material_program.hpp:222`) that the tracer must call rather than reimplement.

**There is no snapshot type.** `Scene` is mutated in place by `SceneController::update(FrameTime)`;
two timeline times cannot coexist. `Engine::update(FrameTime{t,dt,i})` followed by `Engine::scene()`
is "the scene at time t", and the object is copyable. This is direct support for §8's transient
render snapshot and for §88's "LOCK SHOT": the tracer takes a deep copy at a single time and the
timeline is then free to move. It also means the tracer must never hold a bare reference across an
`update`.

### What the tracer will not be able to see, found now rather than at Phase 7

§87 requires that an unrepresentable feature be detected, reported, given a documented fallback and
a test — never silently altered. The capability report of §55 therefore has real content on day one:

| Feature | Status | Why |
|---|---|---|
| Particles (`Scene::particles`) | **cannot be represented** | GPU-only by construction: `src/scene/particles.hpp:3-5` says the CPU holds settings and the simulation lives in compute shaders. No CPU positions exist at any time. |
| SDF objects in `Raymarch` mode (the default) | needs work | no triangles; `spatial::SdfTree` is CPU-evaluable, so the tracer can sphere-trace it, but that is its own integrator path |
| Water surface appearance | degraded fallback | the mesh is a real CPU sheet but ripples, swell, normals, foam and refraction live in `shaders/water.wgsl`; a flat sheet is what a naive consumer gets |
| Volumetrics / atmospherics | parameters only | raymarched on the GPU; §72 already makes volumes a non-goal for now |
| Procedural deformers and effectors | reachable, must be invoked | GPU-applied per frame, but CPU references exist (`scene::deformPoint`, `spatial::applyEffectorsToRecords`); skipping them silently would be exactly the §87 failure |

Three per-frame realtime decisions must be *overridden* rather than inherited, which is the same
category of mistake ADR-146 recorded against the Offline tier:

* terrain `Entity::mesh` is rewritten every frame to a camera-dependent LOD
  (`composition.cpp:~6208`) — the tracer takes `TerrainChunk::meshes[0]`;
* `Entity::cameraCulled` is set from the realtime camera's frustum and means "off screen", not
  "not in the scene" — the tracer must still consider it for indirect light and shadows;
* procedural LOD levels 2 and 3 are camera-facing billboards (`procedural.hpp:202-209`) — level 0
  only. Skinned rigs need `cullDistance = 0` and `updateHz = 0` or a distant character renders in a
  stale pose.

Useful prior art rather than a thing to duplicate: `rendering::ReferenceRenderer`
(`src/rendering/reference_renderer.hpp`) already carries an explicit skip taxonomy
(`skippedSkinned/Blended/Water/Invisible/NoMesh`) and is the closest existing statement of what a
naive consumer of `Scene` does and does not see. The capability report should be recognisably its
descendant.

## The branch baseline, which is not green

`./build/release/tests/avgen_tests "~[gpu]"` at `57b41994` plus this ADR, verbatim:

```
test cases:    2295 |    2289 passed | 1 failed | 4 skipped | 1 failed as expected
assertions: 2225021 | 2225019 passed | 1 failed | 1 failed as expected
```

**2295 cases matches main. The assertion count does not**: 2,225,021 here against the 2,225,003
main was reported at. An 18-assertion drift in a suite this size is small, but it is unexplained
and is recorded rather than rounded away.

The one real failure is **pre-existing and not the path tracer's**:
`tests/integration/test_project_node_set.cpp:98`, *"an object deleted from the owner's own film
stays deleted"* — `CHECK_FALSE(doc.contains("sceneNodes"))` expanding to `!true`, on
`glowmere-valley-2-multicam.json`. It reproduces in isolation from a clean worktree at main's tip
with no engine source modified, so it belongs to whoever owns that scene, not here. Any future
claim that this branch is green means *this* baseline, not zero.

`tests/unit/test_character_lab_slopes.cpp:187` also prints `FAILED:` and is **a pass** — it is
tagged `[!shouldfail]` and is the "1 failed as expected" line. It is named here because three
agents have now misread that output.

One failure on the first baseline run **was** this branch's: `test_repo_hygiene.cpp:112` caught
ADR-339 not being listed in `docs/decisions/README.md`. The suite reads that directory from disk,
so a new ADR fails a test in a binary compiled before the ADR existed. Fixed, and worth knowing
about for every later phase of this branch.
