# The AV Gen Engineering Lab Suite

Status: **Phase 0 (research) and Phase 1 (shared infrastructure) complete.** Ten of the fifteen labs
are unbuilt; this document is what they are built on and what each of them owns.

The fifteenth arrived after this document was written and is the only one whose subject is not a
pixel: the **Interaction Latency Lab** asks how long after a person acts the application answers.
Every other lab asks why the renderer produced the frame it produced. It is in the registry rather
than beside it because a suite that cannot hold "why does the editor feel slow" sends people off to
build a harness of their own, and this repository had already paid for one of those. See
[interaction-latency-lab.md](investigations/interaction-latency-lab.md).

The suite answers a different question from a production scene. Glowmere answers *does the complete
system produce a beautiful result*. A lab answers *why is the system behaving this way*.

---

## 0. The finding that shaped everything below

**Most of what the specification asked for already exists, under other names, and is better than a
new version of it would be.** The single largest risk in a fourteen-lab programme was ten agents
each inventing a registry, a launcher, a fixture system and a debug-drawing layer. The second
largest — found during the audit — was *this* work inventing them on top of four that were already
there:

| Asked for | Already exists | Where |
|---|---|---|
| shared debug drawing (§8) | `DebugDraw` + `buildDebugGeometry` + `DebugViewOptions`, 21 switches, ADR-031 | `src/rendering/debug_draw.hpp`, `debug_visualizer.cpp` |
| per-object "why isn't this rendered" (§9) | `RenderObjectDiagnostic` with `cullReason`, per-plane `frustumMargins`, `visible`/`cameraCulled`/`submitted`/`finite` | `src/rendering/renderer_diagnostics.hpp` |
| reproduction state, capture, diff (§32, §33) | `FrameSnapshot`, `writeSnapshot`, `compareSnapshots` — differences reported as sentences naming object and field | `src/rendering/renderer_snapshot.hpp` |
| performance reporting (§25) | `RenderStats`, `GeometryCounters`, `SubmittedGeometry`, `CpuFrameBreakdown`, `gpu::FrameTimeline` | `src/rendering/render_stats.hpp`, `src/gpu/frame_timeline.hpp` |
| isolation arms (§28's opposite) | `SceneRenderer::passArms()` (`--disable`) and `qualityArms()` (`--quality-arm`), one table read by CLI, panel and bisection | `src/rendering/scene_renderer.hpp` |
| minimal fixtures (§7) | `examples/qa/renderer-qa-*.scene.json`, a progressive isolation ladder with committed state baselines | `examples/qa/` |
| a launcher (§6) | `examples/index.json` + `File > Examples`, already grouped by category, already carrying a `Lab` category with 8 entries | `src/app/examples.cpp`, `src/ui/control_panel.cpp` |

So Phase 1 built the four things that genuinely did not exist — a registry, a case format, an
overlay selection, and the reason-code vocabulary — and wired them to what was there.

---

## 1. Where each system in §4 actually lives

Named by file and symbol, because a category is not an address.

### Scene, entities, transforms

* `scene::Scene` — `src/scene/scene.hpp`. The flat container the renderer is handed.
* `scene::Entity`, `scene::Transform`, `scene::Material`, `scene::MeshData`, `scene::Camera` —
  `src/scene/scene_types.hpp`.
* `scene::Composition` — `src/scene/composition.hpp`/`.cpp` (~7,000 lines). The *authored*
  hierarchy, and — this surprises people — **the scene file format lives here too**. There is no
  `scene_json.cpp`: `Composition::toJson`, `fromJson`, `loadFile`, `saveFile`, `loadChild`,
  `kMaxNestingDepth = 4`.
* Bounds: `scene::entityCullBounds` (`src/scene/scene.cpp`) — a *posed* box for skinned entities,
  because a bind-pose box is not always conservative.

### Asset loading and serialisation

* `assets::AssetRegistry` — `src/assets/asset_registry.hpp`. `asset://project/…` and
  `asset://builtin/…` resolution.
* Project format: `params::serialization.cpp` + `Engine::saveProject`/`loadProject`
  (`src/app/engine.cpp`). `docs/project-format.md`.
* `assets.scene.path.sha256` is verified in **exactly one place**: `findRelinkCandidate`
  (`src/app/engine.cpp`), and **only when the file is missing**. A load of a present-but-changed
  asset checks nothing. Worth knowing before building anything on the digest.

### Camera

* `scene::Camera` is pose + optics. `scene::CameraRig` / `resolveActiveCamera` —
  `src/scene/camera_rig.hpp` — is a **pure function** of (cameras, shots, events, time).
* **Three shot types, and they are different things**: `app::Shot` (`src/app/cinematic.hpp`) is how
  a camera *moves*; `seq::Shot` (`src/seq/sequence.hpp`) is a timeline span with a setup and
  automation; `scene::CameraShot` (`src/scene/camera_rig.hpp`) is a span that names a camera and
  holds nothing a camera holds.
* Evaluated per frame at the end of `Composition::applyParameters` (`src/scene/composition.cpp`).
  Viewport free-roam is an override **on the result**, never an input — ADR-091's purity rests on it.
* Terrain clearance: `world::clearPath` (`src/world/camera_clearance.cpp`), one caller,
  `src/app/camera_director.cpp`. **Line of sight to the subject** is in the same file —
  `world::heroSightline` and `world::clearSightlines`, same caller, run immediately after — and is
  the half ADR-080 never had: `clearPath` keeps the camera *out of* things, and nothing asked what
  was *between* it and the hero it was framing. See [camera-lab.md](camera-lab.md).

### Visibility and culling

* Frustum planes are built **twice, deliberately**: `world::frustumPlanes` /
  `world::aabbVisible` (`src/world/terrain.hpp`, GPU-free core) and `rendering::frustumPlanes`
  (`src/rendering/procedural_renderer.hpp`, for the GPU cull uniform).
* Entities: `Composition::cullEntityNodes` sets `Entity::cameraCulled`; `SceneRenderer::render`
  consumes it.
* Terrain chunks: the loop in `Composition::updateTerrainLod` (`src/scene/composition.cpp`).
* Scatter instances: `cs_cull_classify` in `shaders/cull.wgsl` — six planes, `maxDistance`,
  `minScreenRadius`, depth-band thinning. CPU reference `rendering::cullLodLevel`.
* Whole-object rejection: `rendering::objectFullyCulled`.
* **There is no occlusion culling.** No HiZ, no depth pyramid, no query, no two-phase cull. The only
  "occlusion" in the tree is ambient.

### LOD

* Scatter: the decision is in `cs_cull_classify`; the data is `scene::LodSettings`
  (`src/scene/procedural.hpp`). World scatter layers are authored at
  `lodDistances = {28, 11, 4}` px of projected radius, `lodByScreenSize = true`.
* **The ladder and the rejection tests measure with two different spheres.** The rejection tests use
  the conservative radius about the instance record (`rendering::sourceCullRadius`); the ladder uses
  the tight radius about the source's box (`CullParams::thresholds.w`). They were one number, and
  that made a threshold in pixels mean a size that depended on where the artist put the asset's
  origin — 28 px meant a drawn radius of 11.1 px on `grass` and 18.7 px on `ferns`. See
  `docs/lod-lab/README.md` §2.
* **Rungs 2 and 3 are impostors only for a generated primitive** (`scene::lodLevelIsImpostor`). A
  Mesh source's levels 1–3 have been simplified meshes since ADR-085, and the renderer drew them
  through the impostor vertex path until the LOD Lab; ask the predicate, never the level index.
* **Which rungs are recorded as draws is a proof, not a guess** (`rendering::objectLevelRange`).
* Hysteresis: two mechanisms, both ADR-082 — `lodSpread` (default 0.12, per-instance threshold
  offset, **on**) and `lodHysteresis` (default 0.0, dead zone, **opt-in**, forced to zero offline).
* Terrain: `world::chunkLod` (`src/world/terrain.hpp`). **No hysteresis.**
* **Entity LOD is not live.** `rendering::RepresentationSelector` and `ImportanceEvaluator` exist,
  are tested, and `scene_renderer.cpp` calls neither.
* **There is no LOD cross-fade, dither or alpha fade.** Spread and hysteresis are what was built
  instead.

### Draw generation

* Entities: a CPU draw list built inside `SceneRenderer::render` — local `DrawItem`, bucketed into
  `opaque` / `grid` / `blended` / `water` / `shadowCasters`; one `DrawIndexed` per entity, **no
  instancing**.
* Scatter: `DrawIndexedIndirect`, one per LOD level, args written entirely on the GPU by
  `writeLevel()` in `cull.wgsl` including the ADR-108 fanout.
* Particles: `DrawIndirect` off the compute counters.

### Shadows

* `rendering::ShadowRenderer` + GPU-free maths in `src/rendering/shadow_math.hpp`:
  `cascadeSplits(lambda = 0.85)`, `fitDirectionalCascade`, `directionalShadowRange` (ADR-112).
* Cascades: 2 / 3 / 4 / 4 by tier; `scene.environment.shadowCascades` overrides, clamped 1..4.
* **The caster list is a second cull**: the opaque list, plus a second pass over *camera-culled*
  entities kept by `anyCascadeSees`. A camera-culled object is still a shadow caster.
* Half-resolution mask: `rendering::ShadowMaskRenderer`, `shadowMaskScale` 0.5 preview/realtime,
  **1.0 at high and offline — so the mask is never built at the tiers an offline render uses**
  (ADR-255/258). `--quality-arm maskconsume` is the diagnostic that builds it anyway.

### Lighting and materials

* `scene::PunctualLight` → `rendering::GpuLight` (128 B) via `packLight`; clustered forward,
  froxels 16×8×24, 32 lights per cluster, 256 scene lights. `assignClusters` is the CPU reference
  for the grid's **build**, `shaders/clusters.wgsl` the pass; `ClusterGrid::clusterOf` is the
  reference for its **look-up** (`clusterIndexFor`), which is a different half and had none.
* **A scene file authors a light with a top-level `"lights"` array** (ADR-278). Five routes into
  `scene::Scene::lights`: a glTF asset's own KHR_lights_punctual lights (no asset here has any),
  the scene file's own array, the one default key — added only when there is no rig and no light
  from either of the first two — a light rig, and the procedural ecology. A rig is appended
  *alongside* the authored lights rather than instead of them. An entry's `"node"` makes the light
  ride that node's world transform, which is what a `NodeKind::Light` would have been for. Until
  ADR-278 there was no such key and the `"lights"` arrays in `lod-geometry-lab.scene.json` and
  `visibility-culling-lab.scene.json` were read by nothing. See `docs/lighting-lab/README.md` §1.0.
* **A key a file writes and the parser does not read is warned about by name**, not ignored
  (`core/json_keys.hpp`, ADR-278); `_`-prefixed keys are exempt, because `_note` is deliberate.
* A local light's reach (`lightInfluenceRadius`) is a **hard** edge, not a fade: past it the froxel
  pass does not assign the light at all. ADR-272.
* Area lights: LTC. `directLighting` in `shaders/lighting.wgsl` is roughly 75% of the scene pass.
* Materials bind at `SceneRenderer::materialBindGroup`, group 2, 9 entries.
* Shading tiers (`MaterialTier::Full / ReducedLights / Flat`) are assigned per draw by
  `rendering::MaterialTierSelector`.

### HDR, post, temporal

* `rendering::PostProcessor::run` — the order is `meter → exposure → dof → motionblur → lens →
  bloom → halation → anamorphic → composite → fxaa → sharpen`.
* Exposure: both manual (photographic triangle) and auto (GPU metering, readback next frame).
  `scene::ExposureState`, `updateExposure` — GPU-free and deterministic.
* Tonemap: AgX by default; `scene::TonemapOperator`.
* **There is no TAA.** Antialiasing is FXAA. The only history buffer in the renderer is GTAO's
  (`AoUniforms::temporal`, `AoRenderer::resetHistory`); the shadow mask deliberately has none.
* Motion vectors exist — scene target 2, `RG16Float`, from `FrameUniforms::prevViewProj` — and feed
  tile-based motion blur.

### Volumetrics and particles

* `rendering::VolumeRenderer`, passes `volume.march` and `volume.composite`, scaled by
  `volumeResolutionScale` / `volumeStepScale`.
* `rendering::ParticleRenderer` — compute emit / simulate / prefix-sum compaction / indirect draw.
* World effects and atmospherics are CPU-resolved in `src/world/effects.hpp` and
  `src/world/atmospherics.hpp` and uploaded as GPU blocks.

### AOVs and debug targets

* Five scene targets always written: HDR, normal+roughness (octahedral), velocity, emission,
  identifiers (`R32Uint`). Plus a linear-depth pass.
* `--aov` → `RenderSettings::aovNames()`: `normal, emission, depth, velocity, id, shadow`. Offline
  only; refused together with `--supersample`. **`shadow` is the odd one** — it is not a target the
  scene pass writes, it is a dedicated pass that *recomputes* the term (ADR-258), and it ships
  labelled an approximation because measurement said 0.43% of pixels disagree with the mask the lit
  pass consumed.
* `--debug-target` → `rendering::AuxDebugView`: `None, Normal, Roughness, Velocity, Emission, Ids,
  Occlusion, Depth, LinearDepth, DepthEdges, ObjectDepth, Overdraw, FragmentDensity`.

### There is no frame graph

Pass order is hand-coded in `SceneRenderer::render` (~1,450 lines) and documented in the header.
`docs/renderer-2-architecture.md` lists a frame graph as future work.

### Determinism

* `avgen::Rng` — PCG32, `src/core/rng.hpp`. **No global instance**; every seeded system owns its own
  and threads it through. `std::random_device` is banned and `std::mt19937` distributions rejected.
* **ADR-091, two-tier authority.** The sequencer is pure (`bake()` → `Track::evaluate(t)`); the
  entity layer is stateful. `Engine::seekSeconds` moves the clock, resets the modulator, the
  director, the music runtime and the event cursor, re-simulates `EntityWorld` at a fixed 1/60 step
  and reseeds every rig. **`Engine::update` re-derives the scene.** A test that seeks and then reads
  measures the frame it was already on; that has produced two false results here.

The correct order in any lab test is:

```cpp
engine.seekSeconds(t);
FixedStepClock clock(60.0);
clock.restartAt(t);
const FrameTime time = engine.tick(clock);
engine.setViewport(w, h);
engine.update(time);          // <- the scene is derived HERE
renderer.renderToImage(engine.scene(), time, w, h);
```

---

## 2. The existing inventory

### Labs, sandboxes and diagnostic environments that already existed

* **The Render Quality Lab** — `tools/quality-lab/` (a `avgen_quality_metrics` static library plus
  an `avgen_quality` CLI), `docs/quality-lab/` (9 documents), `tests/unit/test_quality_lab.cpp`,
  `tests/unit/test_quality_metrics.cpp`, `tests/rendering/test_quality_lab_gpu.cpp`.
  **It is the Rendering Lab of this suite, and it is not an in-engine lab at all** — see §3.
* **`examples/qa/`** — a progressive isolation ladder (`minimal` → full → `transparency` → `water` →
  `character`) with the repository's only committed baselines, `examples/qa/baselines/*.snapshot.json`,
  which are `FrameSnapshot` JSON and not images.
* **`examples/lab/`** (procedural geometry), **`examples/benchmark/`**, **`examples/stress/`**,
  **`examples/camera/behaviors.json`**, **`examples/quality/`** (the two Quality Lab benchmark
  scenes), **`examples/recipes/glowmere-{low,medium,dense,extreme}.recipe.json`** (the density
  ladder).
* **Forensic suites** — `tests/rendering/test_character_forensics_gpu.cpp`,
  `test_post_artifact_forensics_gpu.cpp`, `test_water_depth_forensics_gpu.cpp`,
  `test_phase_g_certification.cpp`.
* **Developer UI** — the Debug tab of `src/ui/control_panel.cpp` already holds the forensic arms,
  the selected object's renderer diagnostic (bounds, per-plane frustum margins, reason, palette
  version, model matrix) and the canvas scale. `src/ui/world_panel.cpp`'s `drawDebugOptions` holds
  the overlay switches and the navigation overlay.
* **Instruments** — `tools/*.py`: `spatial_stats`, `temporal_stats`, `sharpness`, `chroma_speckle`,
  `flicker_bench`, `image_diff`, `image_stats`, `render_bench`, `certify`, `post_artifact_stats`,
  `review_frames`, `experiment`. All stdlib-only.
* **Arms and locks** — `tools/bench_ab.sh` (interleaved A/B, ADR-051), `tools/bench_world.sh`,
  `tools/gpu-lock.sh` (cross-process GPU serialisation; ctest's `RESOURCE_LOCK` cannot see a
  sibling worktree).

### Test infrastructure

Two globbed binaries and no CMake edit needed for a new test file:

| Binary | Glob | Links |
|---|---|---|
| `avgen_tests` | `tests/unit`, `tests/integration`, `tests/support` | `avgen_core`, `avgen_quality_metrics` |
| `avgen_render_tests` | `tests/rendering`, `tests/support` | `avgen_gpu`, `avgen_platform`, `avgen_quality_metrics` |

Catch2 v3.16. Timing-magnitude assertions are tagged `[.perf]` (hidden) so they are absent from
ctest. `tests/support/` holds `temp_dir.hpp`, `dense_scene.hpp`, `gltf_fixture.hpp`, `synth.hpp`,
`stride_speed.hpp` — **and no golden-image comparison helper**; image comparison lives in
`tools/image_diff.py` and in `avgen_quality_metrics`.

---

## 3. The Rendering Lab, audited

**It is not what the specification assumed, and the difference is the most important structural fact
in this document.**

The Quality Lab does not render, does not link the renderer, has no runtime, no scene, no UI and no
launcher. It is an *offline image-forensics tool*: `avgen --project … --render … --aov …` writes
frames and EXRs, and `avgen_quality analyze | compare | control | validate --ladder` reads them.
It links `avgen_core` only because image I/O already lives there. ADR-250 made that separation on
purpose — "the instrument is not the engine" — and it is what makes a finding about the renderer a
finding about the renderer.

Three properties of it are worth copying and one is worth **not** copying.

Worth copying:

1. **Measured fact and inferred cause are different types.** `Finding` holds a measurement;
   `Hypothesis` carries evidence and a confidence and says in its own JSON that it is one. There is
   no conversion between them.
2. **A metric that could not be computed reports `available: false` with a reason.** Never omitted,
   never defaulted to zero, because a zero in a quality column is a claim.
3. **The validation ladder is a library, not test code**, so `validate --ladder` and the unit suite
   run the *same* arms — and the ladder takes its metrics as bindings so a deliberately broken
   metric can be substituted and the ladder shown to fail. ADR-182 applied to the validator itself.

Not worth copying: **its shape**. It has no runtime because its subject is a finished frame. Eleven
of the other thirteen labs have a subject that only exists *while the engine is running* — a cull
decision, a joint palette, a cascade fit — and cannot be recovered from a PNG. So the Quality Lab's
harness does not generalise into a lab framework, because there is no harness: there is a CLI over a
directory of images.

**The correction to the brief**: "where its harness generalises cleanly, the origin of the shared
infrastructure" describes something that is not there. What generalises is its *report discipline*
(the three properties above) and its *reader* side. The in-engine labs' shared substrate is the
renderer's own diagnostic layer — `RenderObjectDiagnostic`, `FrameSnapshot`, `RenderStats`,
`DebugViewOptions` — which is a different pile of code that already existed.

**Nothing in it was changed.** No file under `tools/quality-lab/` or `docs/quality-lab/` was
touched by Phase 1.

---

## 4. Ownership map

Each lab owns one decision. The second column is the half that prevents two labs from fixing the
same bug in different places. This table is generated from `src/labs/lab.cpp`, which is also what
`avgen --labs` prints — a document and a registry that disagree is a document, so the registry is
the copy that a test checks.

| Lab | Owns | Does **not** own | Decision site |
|---|---|---|---|
| Animation | clip sampling, blending, the joint palette | where the character stands → Character | `src/scene/animation.cpp` |
| Character Intelligence | what a body knows, what it chooses, where it walks, where its sockets are | what the joints do → Animation | `src/entity/behaviors.cpp:Explore` — [its own document](character-intelligence-lab.md) |
| Visibility | whether an object reaches a draw call | which representation → LOD | `Composition::cullEntityNodes` |
| LOD / Geometry | which level, and whether it holds still | whether it was culled → Visibility | `cull.wgsl:cs_cull_classify` |
| Camera / Framing **(built)** | camera pose, the frustum handed to the cull, clearance, line of sight, which camera the viewport shows | which objects survive that frustum → Visibility | `src/world/camera_clearance.cpp:heroSightline` |
| Shadow **(built)** | cascade fitting, the caster list, the atlas, the mask, the contact march | the light's position and intensity → Lighting; which LOD rung an instance draws at → LOD; how the camera frustum is built → Camera | `shadow_math.cpp:casterState` |
| Lighting **(built)** | light packing, the reach a light is given, cluster assignment and the froxel a fragment reads, LTC, IBL | whether a light is occluded → Shadow; what post does with the radiance → HDR | `light_data.cpp:assignClusters` |
| HDR / Exposure / Bloom **(built)** | metering, the exposure state, the bright pass, the bloom and halation pyramids, the wide tier, the composite and the tonemap operator | the radiance that entered → Lighting | `post_processor.cpp:PostProcessor::run` |
| Volumetric / Atmosphere | the march, its scaling, the composite, atmospheric effects | the bloom the in-scatter feeds → HDR | `src/rendering/volume_renderer.cpp` |
| Particle / VFX | emission, simulation, compaction, indirect draw, velocity writes | the fields that push them — authored data | `src/rendering/particle_renderer.cpp` |
| Temporal Stability | every piece of state crossing a frame: GTAO history, LOD hysteresis and spread, the 1–3 frame cull readback lag, the exposure meter | how loudly it reads to a person → Rendering | `src/rendering/ao_renderer.cpp` |
| AOV / Diagnostics | the five targets, linear depth, `--aov`, `--debug-target` | what the numbers mean about quality → Rendering | `src/app/render_job.cpp` |
| Rendering | measuring a finished frame: the quality vector, the ladder, the detectors, the report | *why* the renderer produced it — every other lab owns a piece | `tools/quality-lab/report/vector.cpp` |
| Integration / Stress | the scenes where everything runs at once, and their certified counters | any single subsystem — isolate it there first | `test_phase_g_certification.cpp` |
| Interaction Latency **(built)** | the seven stamps between an input event and the frame showing its evaluated result; which interactions require a synchronous whole-scene evaluation | how long the GPU took once it was asked → Rendering; why a frame costs what it costs → `core::PhaseProfiler`, a different question | `src/core/interaction_latency.cpp:summarise` |

**Glowmere and the demo worlds own authored production composition.** They are where a problem is
*noticed*, never where it is diagnosed, and never where it is fixed.

---

## 5. What Phase 1 built

Four pieces, all in `src/labs/` (globbed into `avgen_core`, so a lab agent adds a file and does not
touch CMake), all GPU-free, all checked by the CPU suite.

### `labs/lab.hpp` — the registry

Fifteen `LabDescriptor`s: key, title, status, question, `owns`, `doesNotOwn`, `decides`
(`path:symbol`), `doc`, `fixture`, `cases`. `tests/unit/test_lab_registry.cpp` opens every `decides`
file, greps it for the symbol, and checks every fixture, doc and case file exists — so a rename
fails the suite instead of quietly making this document wrong.

`LabStatus` is `Built` / `InProgress` / `Planned`, and the launcher shows all fifteen with their
status. A launcher that hid the unbuilt labs could not be used to hand work out; one that opened
them silently would teach people the suite is decorative.

### `labs/case.hpp` — the reproduction format (§32)

`{"format": "avgen-lab-cases", "version": 1, "lab": …, "cases": [ … ]}` at
`examples/labs/<lab>/cases.json`. A case carries a *path* to a fixture and the numbers that make one
frame of it reproducible — time, size, fps, seed, camera, tier, `disable`, `qualityArms`, `aovs`,
`supersample` — and `reproduceCommand()` prints the `avgen` invocation. Every field maps to a flag
the binary already takes, so a case cannot drift from the command that reproduces it.

Three refusals are the point of the format: a case with no `question` or no `expectation` is
rejected; two cases with the same number are rejected (a number with two answers is no answer); a
case filed under a lab that is not the file's lab is rejected.

**A case references a fixture; it never contains one.** This repository's scenarios are frequently
duplicated data rather than shared — `glowmere-valley-2`, `-multicam`, `-song` and
`glowmere-atmospherics` each carry their own copy of large authored blocks, and a nested scene's
`sha256` has to be hand-refreshed. A case format that embedded scene content would multiply that by
the number of cases.

### `labs/overlays.hpp` — which overlays, per lab (§8)

`overlaysFor(LabId)` returns a `rendering::DebugViewOptions`. §8's first sentence was already built;
this is its second one — *"do not display everything simultaneously; each lab should selectively
enable relevant overlays."*

To make that possible without linking Dawn into `avgen_core`, `DebugViewOptions` was split out of
`debug_draw.hpp` into `src/rendering/debug_view_options.hpp` (pure data, no GPU includes,
`debug_draw.hpp` includes it so every existing include still compiles). That is the same split, for
the same reason, that `renderer_diagnostics.hpp` already made.

### `labs/visibility_reason.hpp` — the vocabulary (§9), not the wiring

Thirteen reason codes, each naming where the decision is made and whether the engine *reports* it
today. Five are reported (the strings `scene_renderer.cpp` writes); eight are decisions the engine
makes and does not report, which is the Visibility Lab's work list.

`OCCLUSION_CULLED` is **not** in the list, because there is no occlusion culling. A reason that can
never be returned is a diagnostic that cannot fail.

The vocabulary is kept honest by a test that reads `scene_renderer.cpp`, extracts every string
literal on a line assigning `cullReason`, and requires the vocabulary to map all of them. A sixth
reason added to the renderer fails that test.

---

## 6. What Phase 1 deliberately did not build

* **A lab runtime, a lab `Application` mode, or a lab scene type.** Every fixture is a scene or
  project file the engine already loads. Nothing about a lab needs new engine code to *run*.
* **A shared golden-image store.** The repository has deliberately never had one, and
  `examples/qa/baselines/*.snapshot.json` says why: a renderer upgrade changes pixels by design, so
  an image baseline is discarded on the first day. What must not change is the *state* the renderer
  derives from a scene, and `FrameSnapshot` + `compareSnapshots` already records that and reports
  differences as sentences.
* **A performance-reporting schema.** `RenderStats` / `GeometryCounters` / `CpuFrameBreakdown` /
  `FrameTimeline` cover §25 and are better than a new one, including the parts that say which
  numbers are stale (`estimatedDraws`) and which the CPU cannot know (`unmeasuredDraws`).
* **A third test binary or a `labs/` CMake target.** The `src/app/*.cpp` source list is already
  duplicated three times (`avgen_tests`, `avgen_render_tests`, `avgen_overlay_shot`); a fourth
  target inherits that. Lab tests are files in the globbed tree.
* **A `labs/` top-level directory (§31).** The suite's code is `src/labs/` (registry, cases,
  vocabulary), its fixtures are `examples/`, its cases are `examples/labs/<lab>/cases.json`, its
  tests are `tests/unit` and `tests/rendering`, and its instruments stay in `tools/`. Moving the
  Quality Lab under a new tree would have renamed working code to satisfy a diagram.
* **Turning on `DebugViewOptions::culling` in any profile.** See §8 below. (`::lod` was on that
  list and has since been wired by the LOD Lab; its profile turns it on.)

---

## 7. How to use it

### Reproduce a bug

```
Glowmere reproduction  →  the lab that owns the decision  →  minimal fixture  →
  root cause  →  engine fix  →  lab regression test  →  Glowmere verification
```

`avgen --labs` prints the fifteen labs, what each owns, what it does not, and where its decision is
made. Start at the lab whose *decision* the symptom is about, not the lab whose *name* the symptom
sounds like. A tree that flickers is not necessarily the Temporal Lab: if it flickers because its
LOD level alternates, it is the LOD Lab, and the Temporal Lab's instruments will only tell you it
flickers.

### Create a test case

Add an entry to `examples/labs/<lab>/cases.json`. It must carry a `question` and an `expectation`
or it will be refused. Then:

```
avgen --lab-case <lab>:<n>          # open the fixture at the case's time with the lab's overlays
avgen --lab-case <lab>:<n> --print  # print the reproduce command without running anything
```

### Turn a reproduction into a regression

Write a new file in `tests/unit/` or `tests/rendering/` — no CMake edit, and a new file is the
lowest-conflict surface in this repository. Load the case with `labs::loadCases` +
`labs::findCase`, drive the engine with the ADR-091 sequence in §1, and assert the *invariant*, not
the debug flag. `[gpu]` tests take `tools/gpu-lock.sh`.

### Capture diagnostics

* Renderer state: `rendering::writeSnapshot(FrameSnapshot{...}, path)`; diff two with
  `compareSnapshots`, which names the object and the field.
* Pixels and AOVs: `avgen --render out --aov normal,emission,depth,velocity,id,shadow`.
* Post intermediates: `avgen --render out --post-stages <dir>` — one scene-linear EXR per stage the
  post chain rendered, at the resolution the chain chose, plus a manifest with each one's extent,
  peak and mean (ADR-277). Arming it does not change the picture; that is checked, not claimed.
* The interface: `avgen --capture-ui out.png --capture-ui-panel "World","Performance"`.
* Counters: `--bench-json`, `--cluster-stats`, `--profile-cpu`.
* Isolation: `--disable <passArms>` and `--quality-arm <qualityArms>` — one table, reachable
  identically from the CLI, the Debug panel and the automatic bisection.

---

## 8. Known defects this audit found, and who owns them

1. **`DebugViewOptions::culling` is inert.** It has a checkbox in `world_panel.cpp`;
   `debug_visualizer.cpp` reads the field nowhere. A control wired to nothing is ADR-225's defect,
   and it is worse in a diagnostic than anywhere else because somebody trusts it.
   **Owner: Visibility Lab.** Until it is wired, no lab overlay profile turns it on, and
   `test_lab_registry.cpp` enforces that.
   *(`::lod` was the other half of this item. **Done**: the LOD Lab wired it to
   `ProceduralRenderer::readLodLevels`, the rung the cull pass writes per record, and the registry
   test now asserts the LOD profile turns it on and that no other profile does.)*
2. **The GPU instance cull reports no reason at all.** `cs_cull_classify` collapses six planes, a
   distance test, a screen-radius test and a depth-band thinning into one bool and writes only
   `0xFFFFFFFF`. Four of the eight `Unreported` codes are this one shader. **Owner: Visibility Lab.**
3. ~~**The shadow cascades have no overlay.**~~ **Built, 2026-09-17.** `DebugViewOptions::shadowCascades`
   draws each view's orthographic volume from the matrix the renderer uploaded, `::shadowCascadeSlices`
   draws the part of the camera frustum whose pixels select it, and `::shadowCasters` colours every
   entity by `rendering::casterState`. See `docs/shadow-lab/README.md` §4.
4. **Nothing checks line of sight from a directed camera to the hero it is framing.** Terrain
   clearance (ADR-080, `world::clearPath`) does hold — 0 of 184 baked keys and 0 of 4,001
   interpolated samples below surface + 1.2 m on both Glowmere projects — but an object standing
   between the camera and the subject is unmeasured. **Owner: Camera Lab.**
5. **`assets/` is half tracked and half gitignored.** A fresh `git worktree add` gives the manifests
   but not the 1,782 large files they name, and tests then fail as "audio file not found", empty
   squads, missing environment maps — failures that read as code faults and are not. Run
   `tools/link-worktree-assets.sh`. **This is §37 with the sign flipped**: a test environment that
   silently lacks its data produces failures that are not about the code, and a lab suite whose
   fixtures use production assets (§29 asks for exactly that) inherits the trap.
6. ~~**A scene file cannot author an emissive above 50.**~~ **Closed by ADR-331.**
   `material/emissive` was registered with a hard maximum of 50 (`src/scene/procedural.cpp`) and the
   parameter clamped an authored 256 with no warning; `baseColor` and `emissiveColor` clamp to
   [0, 1], so `emissiveIntensity` is the only route above unit radiance from a scene file and 50 was
   its ceiling. ADR-225's defect in an authoring format, the same shape as the Lighting Lab's
   `coneDegrees` in a different parser. Found by the HDR Lab's fixture, which asked for 256 and got
   50. The ceiling is now a floor under the hard maximum — an object authoring more than 50 gets a
   range that holds it, every other object keeps exactly the range it had, and the soft range the
   panel draws is unchanged at 0..8. Separately and more usefully, every range in that table now
   **reports** the authored values it overrules, by name and with the number the engine runs; the
   whole CPU suite, which loads every scene in `examples/`, emits zero of them.
7. **The bloom pyramid's reach is a pixel count, not a fraction of the frame**, so `--supersample 2`
   halves a small highlight's glow across the delivered picture. Measured, with two controls, in
   ADR-279; deliberately not changed, because the fix is an art-direction decision. **Owner: the
   owner.**
8. **`indtune.cpp` at the repository root is a 0-byte file** referenced by no CMakeLists, and
   `avgen_bench_flatten` is marked temporary and slated for deletion with its investigation.

---

## 9. Recommended order for the remaining labs

Grouped by the files they touch, so the groups can run in parallel.

**Wave 1 — the decisions everything else is read against.**
*Visibility* (`debug_visualizer.cpp`, `cull.wgsl`, `scene_renderer.cpp`'s diagnostic block) and
*Camera* (`camera_clearance.cpp`, `camera_rig.cpp`, a new line-of-sight check) — **Camera is built**;
see [camera-lab.md](camera-lab.md). They share no file. Visibility must land before LOD, because
"which level" is only a question about objects that survived.

**Wave 2 — one shared file each, no overlap.**
*LOD* (`cull.wgsl`, `representation.cpp`) · *Shadow* (`shadow_math.cpp`, `shadow_renderer.cpp`) ·
*Particle* (`particle_renderer.cpp`). Three agents, three subsystems, disjoint.

**Wave 3 — the shading chain, which must be serialised against itself.**
*Lighting* → *HDR* → *Volumetric*, in that order and preferably not concurrently: all three write to
`post_processor.cpp` or the scene pass's uniforms, and an exposure change and a bloom change landing
together cannot be told apart. This is the one group where parallelism costs more than it buys.
**Lighting and HDR are built** ([lighting-lab](lighting-lab/README.md),
[hdr-lab](hdr-lab/README.md)); neither changed a shader, and HDR's only edit to
`post_processor.cpp` is one line in `resetExposure` (ADR-277 §2), so Volumetric still enters a post
chain it can measure against main.

**Wave 4 — the two that consume everything above.**
*AOV* (`render_job.cpp`, `render_settings.cpp`) and *Temporal* (`ao_renderer.cpp`, and the LOD
hysteresis the LOD Lab will have instrumented). Both are cheap once Wave 2 is in, and both are
mostly new test files.

**Wave 5 — Integration**, last by definition: it certifies the whole thing and has nothing to
certify until the rest exists.

Animation and Character are already staffed and sit outside this ordering; they touch
`src/scene/animation.cpp` and `src/entity/` and collide with nothing above.
