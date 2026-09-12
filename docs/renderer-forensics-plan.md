# Renderer Forensics Investigation Plan

**Status:** in progress  
**Created:** 2026-09-12  
**Scope:** forensic investigation of renderer instability, subsystem isolation, architectural ownership and regression proof  
**Primary references:** [Renderer Stabilization QA](renderer-qa-2026-09-11.md), [Rendering](rendering.md), [Testing](testing.md), [Renderer 2 architecture](renderer-2-architecture.md), [Renderer 2 backlog](renderer-2-backlog.md)

**Interim evidence report:** [renderer-forensics-report.md](renderer-forensics-report.md)

## Mission

Determine, with reproducible evidence, which subsystem introduces each remaining rendering failure. The investigation must distinguish transforms, camera mathematics, render-object state, GPU buffers, render-pass state, depth, culling, LOD, animation/skinning, terrain, water, transparency, shadows, particles, post-processing, sequencer state and asset-specific behavior.

This is an investigation and correctness effort, not a visual feature sprint. No new artistic rendering features are in scope. Every fix must identify the state transition, add a regression test where practical, and preserve the production renderer's performance when diagnostics are disabled.

## Status legend

- `[ ]` Not started.
- `[~]` Partially complete; remaining subtasks are listed below.
- `[x]` Complete and supported by code, tests or documented measurements.
- `[!]` Blocked or dependent on evidence from another task.

## Working rules

- Preserve existing user and agent work. Inspect `git status --short` before editing.
- Begin each implementation slice with one local hypothesis and one cheap discriminating check.
- Do not disable a system permanently to make a symptom disappear.
- Do not add scene-specific hacks such as `if (scene == ...)` or asset-name exceptions.
- Keep diagnostics developer-only, selective and low overhead when disabled.
- Use release for acceptance validation; use debug and sanitizers for diagnosis.
- A bug is not marked fixed until the reproducer, root cause, fix and regression evidence are recorded.
- Update this document as tasks complete. Add links to commits, tests, captures and forensic findings.

## Phase 0: Investigation charter and baseline

### 0.1 Establish the baseline

- `[x]` Read and preserve the current renderer QA baseline in [renderer-qa-2026-09-11.md](renderer-qa-2026-09-11.md).
- `[x]` Record the active reference scenes: The Living Constellation, Glowmere and `RendererQA`.
- `[x]` Record the current build machine, GPU, OS, renderer tier, resolution, frame rate and workload conditions for every benchmark.
  - `[x]` Apple M2 Max and release/WebGPU-on-Metal environment recorded.
  - `[x]` Add a repeatable benchmark command and result table for each canonical scene.
  - `[x]` Separate warm-up, steady-state and background-load conditions.

**Canonical baselines.** One command shape, three scenes, measured 12 September 2026 on Apple M2 Max,
release, Dawn/Metal, `--tier realtime --size 1280x800 --frames 120 --fps 30`:

```sh
./build/release/src/avgen --headless --project <scene> --frames 120 --fps 30 --size 1280x800 --tier realtime
```

| Scene | GPU median | Wall median | p10 / p90 | Dominant pass | Draws | Triangles | Bound by |
|---|---:|---:|---|---|---:|---:|---|
| `examples/world/glowmere-stylized.json` | 18.87 ms | 23.60 ms | 21.52 / 26.25 | `scene` 15.79 (84%) | 141 | 430,233 | scene geometry, fragment side |
| `examples/constellation/constellation.json` | 6.09 ms | 8.40 ms | 6.90 / 13.60 | `volume` 3.93 (64%) | 11 | 3,121 | volumetrics |
| `examples/qa/renderer-qa.json` | 1.70 ms | 2.75 ms | 2.46 / 3.37 | `scene` 0.72 (42%) | 8 | 7,961 | nothing; it is the control |

**Conditions, stated because they change the numbers.** The first 12 frames are discarded as warm-up
(the harness reports a median over 108 *steady* frames); pipelines are compiled and Metal replaces
their GPU binaries shortly after creation, so a cold frame is not comparable. Background load matters
more than it should: **treat the shares as durable and the absolutes as machine state.** The
11 September QA record measured 23.79 ms GPU for the Glowmere scene with near-identical geometry
counters; the same command measured 18.87 ms today, and the cause has not been established. Only a
controlled A/B *within one run* is evidence for a change.

- `[x]` Record known symptoms without assuming their causes.
  - `[x]` Static-object/UFO motion, alien flicker/culling, water boundary artifacts and timing instability are named regression areas.
  - `[x]` Give every symptom a stable identifier, exact scene, frame/time range and reproduction command.

**Symptom register.** Identifiers are stable; a symptom keeps its id after it is fixed, so evidence
stays quotable.

| Id | Symptom | Scene | Where | State |
|---|---|---|---|---|
| `SYM-STATIC-1` | A static object appears to move as the camera moves | Glowmere (UFO), RendererQA | any camera motion | **Not reproduced in the renderer.** 680 frames over five camera motions hold the authored TRS bit-for-bit, and a 240-frame excursion returns byte-identical. The composition-side path (node flattening, terrain grounding, sequencer writes) is untested and is the remaining half. |
| `SYM-ANIM-1` | A character's pose jumps when the playhead is scrubbed | `examples/characters/alien.scene.json` | any seek | **Reproduced and fixed.** Frame 500 reached from frame 100 differed from frame 500 reached directly by 98 joint matrices. Root cause: the authored animation state's phase origin was the engine's first update. See the report. |
| `SYM-ANIM-2` | The alien flickers or disappears near a frustum edge | Glowmere, alien | camera edge | **Not reproduced.** A 65-position sweep across the edge, each step rendered against a no-cull control, shows culling never removes a pixel the character would draw. Note the alien cannot discriminate bind-pose from posed bounds (a T-pose bind is wider); that property is tested separately against a rig that reaches past its bind pose. |
| `SYM-WATER-1` | Water leaks past or intersects terrain incorrectly at a shoreline | Glowmere | shoreline, grazing angles | **Open.** Basic view cases pass; the flat/steep/shallow/deep/angled matrix and the mask/depth visualisations are not built. |
| `SYM-TIME-1` | GPU timing tests fail intermittently | any | `ctest -j4` | **Understood, not fixed.** Contention-sensitive; passes alone and at `-j2`. Same root cause as the reproducibility limitation. |

### 0.2 Define evidence standards

- `[x]` Define the minimum evidence package for every bug.

**Evidence package.** A bug is not written up without all seven, and "not established" is an
acceptable entry for any of them -- an honest gap is evidence and a guess is not:

1. **Symptom** as a person would describe it, and what is visible.
2. **Reproduction**: the exact command, scene file, resolution, tier and frame or second range.
3. **Subsystem matrix**: which of `--disable shadows,ao,volume,post,shadowmask` and which
   engine/composition paths change the symptom, and which do not.
4. **Captured state** at the divergent frame -- diagnostic frame, palettes, transforms, bounds,
   culling verdicts -- *not only an image hash*. An image hash says something differs; it never says
   what, and the Phase 9.2 defect was localised in one comparison by checking palettes and transforms
   separately.
5. **Root-cause classification**: the state transition, named, in one sentence that identifies the
   owner.
6. **Fix and regression**, where the regression *fails without the fix*. A regression that passes
   either way is not evidence, and Phase 5.1 shipped one such test before it was caught.
7. **Residual uncertainty**: what the fix does not cover, and what would still be believed if it were
   wrong.

- `[x]` Define when a subsystem is `PASS`, `FAILED`, `FAILED -> FIXED`, `NOT ISOLATED` or `UNKNOWN`.

**Status definitions.** These are claims about *evidence*, not about confidence:

| Status | Means |
|---|---|
| `PASS` | Its failure mode has a controlled reproducer that **can** fail, the reproducer passes, and the scope of the claim is stated. Never "we looked and saw nothing". |
| `FAILED` | Reproduced, with the reproducer recorded; root cause may be unknown. |
| `FAILED -> FIXED` | Reproduced, root-caused, repaired, and a regression that fails without the repair. |
| `NOT ISOLATED` | The symptom is real and reproducible but no subsystem boundary has been established. |
| `UNKNOWN` | Not investigated. Distinct from `PASS`: no test has been pointed at it. |
| `PARTIAL` | Some failure modes are `PASS` and others are `UNKNOWN`; the table entry must say which. |
- `[x]` Add a root-cause ledger and subsystem status table in the linked [interim forensic report](renderer-forensics-report.md).
- `[ ]` Decide which evidence is automated, manual visual review, GPU capture or performance measurement.

## Phase 1: Architecture map and state ownership

### 1.1 Trace the real frame pipeline

- `[~]` Document the actual application-to-GPU flow:
  `Application -> Engine -> Scene -> entities/components -> transforms -> animation -> visibility -> render extraction -> render queue -> GPU object data -> camera -> passes -> depth -> opaque -> terrain -> water -> transparent -> particles -> shadows -> post-processing -> output`.
  - `[x]` Existing high-level flow is documented in [architecture.md](architecture.md) and [rendering.md](rendering.md).
  - `[ ]` Replace the conceptual flow with a code-level map naming the actual functions and files.
  - `[ ]` Record ordering differences between live, headless, capture and test paths.
- `[ ]` Identify every render pass, its inputs, outputs, clears, readbacks and resource ownership.
- `[ ]` Record where culling, LOD, animation, water, shadows, particles and post effects execute relative to extraction and submission.

**Current code-level map:** `Application::runLive` / the headless render loop owns the command
encoder and calls `Engine::update` before `SceneRenderer::render` or `renderFrame`. `Engine::update`
evaluates signals, parameters, timeline and the active scene controller. `Composition::update` runs
composition-node updates, `cullEntityNodes`, terrain/water updates and character updates; the latter
calls `scene::updateRigs`, which poses rigs before renderer submission. `SceneRenderer::render`
constructs camera matrices, updates environment/lights, uploads skinning data, writes object
uniforms in `makeItem`, builds camera and shadow draw lists, and encodes the ordered GPU passes.
`SceneRenderer::renderFrame` and `renderToImage` wrap the same render path for headless/capture use.
The current map is sufficient to begin instrumentation; pass-by-pass resource ownership remains
open in Phase 7.

### 1.2 Identify authoritative owners

- `[~]` Confirm authoritative scene world transforms.
  - `[x]` `scene::Entity::transform` and `scene::Transform::matrix()` are documented as authoritative.
  - `[ ]` Audit every caller that writes transforms during update, composition flattening, animation, terrain grounding and sequencer evaluation.
- `[x]` Confirm authoritative camera state and matrix generation.
  - `[x]` `Camera::view()` and `glm::perspectiveRH_ZO` are documented.
  - `[x]` Find and compare every competing view/projection construction path.
    **There are none.** `Camera::view()` is the only `glm::lookAt*` in `src/` outside the shadow
    light-views, and `Camera::projection()` the only camera `glm::perspective*`. Every consumer calls
    those two: `SceneRenderer`, `ProceduralRenderer`, `Composition::cullEntityNodes`,
    `Composition::updateTerrainLod`, `Application` (inverse view-projection for picking),
    `entity::placement`, `ai::engine_tools` and `ui::world_probe`.
    - One deliberate asymmetry, recorded rather than fixed: **terrain culling widens the aspect to a
      floor of 2.5** (`kCullAspect` in `updateTerrainLod`) while entity culling uses the exact
      viewport aspect. It is documented in place and the error is taken on the safe side -- a hole in
      the ground is worse than an extra draw -- but it means an entity can be culled in a frame where
      the terrain under it is not.
    - The conventions those two functions choose are now pinned by test, because everything else
      assumes them: right-handed with the target at negative view-space z, the eye at the view-space
      origin, WebGPU's 0..1 depth (near -> 0, far -> 1, further is larger), and an aspect that widens
      horizontally rather than cropping vertically. `tests/unit/test_camera.cpp`,
      `[scene][camera][forensics]`. The degenerate forward/up case is pinned there too rather than
      left to the one GPU test that happened to catch it.
- `[~]` Confirm authoritative animation time and pose ownership.
  - `[x]` Timeline/render-time ownership is documented; renderer does not pose rigs.
  - `[ ]` Trace seek, reverse, pause, loop and frame-rate paths for duplicate time writes.
- `[ ]` Confirm authoritative owners for bounds, visibility, material state, object IDs, render IDs, GPU indices and pass state.
- `[ ]` Record each derived copy, update timing, lifetime, thread, frame boundary and synchronization rule.

### 1.3 Audit duplicated state

- `[~]` Search for duplicated position, rotation, scale, world matrix, camera position/orientation, bounds, visibility and animation time.
  - `[x]` **Culling bounds were computed twice.** `Composition::cullEntityNodes` contained the posed
    box, the conservative pad and the world-AABB corner transform written out in full, once for
    EntityWorld-driven characters and once for authored mesh nodes. Two copies of a rule is two
    places for it to drift, and neither copy was reachable by a test. Extracted to
    `scene::entityCullBounds` (`src/scene/scene.hpp`), both call sites replaced, and the property is
    now unit-tested directly. Full release suite unchanged at 1,681 passing.
  - `[ ]` Continue the search for the remaining duplicated values.
- `[ ]` Search for duplicated material, object ID, render ID, GPU index, buffer offset and generation values.
- `[ ]` Build a table for each duplicate:
  `value | authoritative source | derived copies | writer | reader | update timing | lifetime | thread | GPU sync risk`.
- `[ ]` Explicitly audit the chain `scene transform -> render transform -> GPU transform -> camera-relative transform -> shader transform`.
- `[ ]` Identify any path where camera-relative conversion can be written back into authoritative scene state.

### 1.4 Establish and enforce invariants

- `[x]` Document the invariant that scene world transforms remain authoritative and renderer-derived transforms are temporary.
- `[ ]` Add development assertions that culling and rendering never mutate authoritative transforms.
- `[ ]` Add finite-value validation for transforms, matrices, bounds, camera state, materials and GPU upload structures.
  - `[x]` Camera/entity matrix and skinning palette validation exists.
  - `[ ]` Complete joint, bounds, material and water validation coverage.
- `[~]` Document and test camera-relative origin ownership across entities, terrain, water and particles.
  The renderer audit found no camera-relative conversion or authoritative scene-transform write under
  `src/rendering`; the static-camera regression confirms authored entity TRS survives camera motion.
  Explicit origin ownership for terrain, water and particles is still open because no shared origin
  contract is currently documented.

## Phase 2: Immutable frame boundary and reference renderer

### 2.1 Design the render snapshot contract

- `[ ]` Define an explicit per-frame render snapshot or equivalent immutable contract.
- `[ ]` Ensure each renderable carries, directly or through stable references:
  - object/entity ID;
  - mesh and material IDs;
  - world position, rotation and scale;
  - world/model matrix;
  - world bounds;
  - visibility/cull reason;
  - animation/skin state when applicable;
  - GPU slot/index metadata.
- `[ ]` Define which values are copied at extraction and which are resolved by the renderer.
- `[ ]` Ensure the render frame consumes the snapshot without mutating scene state.
- `[ ]` Add unit coverage for snapshot stability and repeated extraction.

### 2.2 Build the minimal reference renderer

- `[ ]` Create a developer-only `ReferenceRenderer` or `MinimalRenderer` path.
- `[ ]` Support only basic opaque mesh, solid material, depth and explicit camera matrices initially.
- `[ ]` Keep the path free of culling, LOD, animation, water, transparency, particles, shadows, post FX, batching optimizations and temporal history.
- `[ ]` Implement the explicit path:
  `authoritative world transform -> render snapshot -> GPU upload -> model -> view -> projection -> MVP -> basic material -> depth -> draw`.
- `[ ]` Give the reference path deterministic object ordering and stable resource lifetime.
- `[ ]` Add a direct comparison harness for the same simple scene through reference and production renderers.
- `[ ]` Compare transforms, visibility, object IDs, depth, geometry, material inputs and image hashes.

### 2.3 Static-object invariant and camera experiments

- `[x]` Add a known `STATIC_TEST_OBJECT` at a fixed position such as `(10, 2, -20)`.
  Exactly `(10, 2, -20)`, with a non-identity rotation and non-uniform scale so a lost or re-derived
  TRS cannot look correct by accident. `tests/rendering/test_gpu.cpp`, `[gpu][renderer][forensics][static]`.
- `[x]` Capture world position, rotation, scale, world matrix, render transform, GPU transform and camera state over hundreds/thousands of frames.
  680 rendered frames per run; each asserts authored TRS, authored matrix, the renderer's diagnostic
  world matrix and world position, and the diagnostic frame's camera state.
- `[x]` Test static object with camera translation. *(120 frames, lateral sweep.)*
- `[x]` Test static object with camera rotation. *(120 frames, full turn from a fixed position.)*
- `[x]` Test camera dolly toward the object. *(120 frames, 70 m to 3 m along the sight line.)*
- `[x]` Test camera passing through or near the object. *(160 frames straight through and out the far
  side, which crosses the near plane against its geometry.)*
- `[x]` Test camera orbit. *(160 frames, full orbit at 28 m.)*
- `[x]` Require authored world transform stability in every case.
  Bit equality, not tolerance: `transform.position/rotation/scale` and `matrix()` compare with `==`.
- `[x]` Add a projection check that distinguishes correct parallax from transform corruption.
  Each frame also predicts the object's NDC from `Camera::view()` and `perspectiveRH_ZO`
  independently and compares it with the renderer's own view-projection: 2e-3 in all three axes,
  over the ~500 frames where the object is in front of the camera.
- `[x]` Prove a camera excursion is reversible. A 240-frame orbit/climb away and back reproduces the
  first frame **byte for byte** (0 of 49,152 channels differ) and reproduces its diagnostic state
  hash. This is the half that transform assertions cannot reach: temporal history, a stale object
  slot or an accumulated camera-relative origin all pass the TRS checks and fail this one.
- `[x]` Stop downstream investigation if the minimal renderer fails these tests; repair transform/camera ownership first.
  Not triggered: the renderer path passes every case.

**Result: `SceneRenderer` does not move a static object.** The renderer-side half of completion-gate
question 1 is answered with evidence. The *composition*-side path (node hierarchy flattening,
terrain grounding, sequencer writes) is a separate surface and is still covered only by the existing
static-camera regression; the Glowmere UFO matrix in Phase 10.1 remains open.

## Phase 3: Camera, GPU object data and frame synchronization

### 3.1 Camera matrix forensics

- `[ ]` Instrument camera world position and rotation.
- `[ ]` Instrument view, projection, view-projection, inverse-view and inverse-projection matrices.
- `[ ]` Instrument near plane, far plane, aspect ratio, viewport width and viewport height.
- `[x]` Verify multiplication order, handedness, forward direction, up axis, clip-space range and depth convention.
  Pinned in `tests/unit/test_camera.cpp`, `[scene][camera][forensics]`: handedness, view-space origin,
  0..1 depth in the conventional direction, aspect behaviour, finiteness, and the parallel
  forward/up fallback.
- `[x]` Verify that culling, shading, depth reconstruction, shadows, volumetrics, picking and overlays consume the same authoritative camera model.
  Established by the audit above: every one of them calls `Camera::view()` and `Camera::projection()`.
  Shadow views are the intended exception -- they are light views, built in `shadow_math.cpp`.
- `[ ]` Add camera basis and frustum visualizations to the diagnostics path.

### 3.2 Camera-relative rendering audit

- `[ ]` Identify every camera-relative conversion and its exact execution stage.
- `[ ]` Verify scene state is never mutated by camera-relative conversion.
- `[ ]` Verify camera-relative origin is shared consistently by entities, bounds, terrain, water and particles.
- `[ ]` Add a regression that detects double subtraction across consecutive frames.
- `[ ]` Add projected-position checks for world, camera-relative, clip and screen coordinates.

### 3.3 GPU object and buffer audit

- `[ ]` Track entity ID, render-object ID, GPU object index, buffer offset, frame index and buffer generation for every submitted object.
- `[ ]` Add debug object-ID coloring with stable IDs for the UFO, alien, tree, water and test geometry.
- `[ ]` Audit uniform/storage buffers, dynamic offsets, ring buffers, staging buffers, bind groups, views and frame allocators.
  - `[~]` Object uniform slot stride and capacity now have compile-time guards, and focused GPU
    diagnostics verify stable object-slot assignment. Ring-buffer/resource reuse and full pass
    bind-state auditing remain open.
- `[~]` Verify CPU/WGSL structure size, alignment, offsets, padding, type widths and matrix layout.
  Frame and object uniform sizes were already asserted; explicit C++ field-offset assertions now
  guard the WGSL field order. Alignment/padding and the remaining GPU-side structures still need
  the same treatment.
- `[ ]` Add an alternating-transform two-object test to detect stale or swapped GPU data.
- `[x]` Fix mesh, texture and environment/IBL upload identity/version collisions by keying renderer
  caches to the owning `Scene` as well as local IDs/versions. GPU regressions cover distinct
  same-version geometry and HDR data through one renderer, both matching fresh-renderer results.
  The broader buffer-layout audit remains open.

### 3.4 Frame synchronization and resource lifetime

- `[ ]` Document when CPU state updates, GPU data is written, GPU consumes it, GPU finishes and memory is reused.
- `[ ]` Audit textures, buffers, bind groups, pipelines, materials, meshes, animation buffers, depth textures, water textures and post-process targets.
- `[~]` `FrameTimeline` uses a four-slot non-stalling resolve/map ring and waits for in-flight maps
  during destruction. A 32-frame GPU stress regression now proves sustained slot reuse, readback
  completion and zero timeline overflow; the broader resource inventory remains open.
- `[~]` Repeated render-target replacement is covered by an eight-size alternating GPU regression;
  HDR/auxiliary targets, bind groups and tonemap views recreate without WebGPU errors. Live-path
  asynchronous replacement and the remaining texture/buffer resource inventory are still open.
- `[x]` The real post chain is stress-tested across twelve frames with alternating bloom, DoF, motion
  blur, antialiasing and target sizes. Every frame leaves the transient pool with zero textures in
  use, and the sequence completes without WebGPU errors.
- `[x]` Skinning palette upload cache is scene-aware. Distinct scenes with equal rig palette versions
  now force a palette upload; a GPU regression renders rest and posed same-version scenes through one
  renderer and verifies the pixels differ.
- `[x]` Particle simulation pools are scene-aware. Switching scenes resets alive/dead lists, emission
  carry and trail history; a reused-versus-fresh renderer regression covers visible same-frame output.
- `[x]` Renderer temporal history now resets at a scene boundary: previous model matrices, previous
  view-projection state and AO history cannot leak between distinct scenes. A reused-versus-fresh
  renderer regression covers a same-time scene swap.
- `[~]` Stress resource reuse through resize, scene reload, timeline seek/reverse, camera cuts and frame-index reuse.
  Resize now clears previous view/model history in addition to AO history, with a motion-blur
  reused-versus-fresh renderer regression. An explicit reset API now covers in-place scene reloads
  and the application camera-cut action. Timeline seek/reverse stress and frame-index reuse remain
  open.
  - `[x]` Repeated frame indices are deterministic: the renderer drops AO history on a non-advancing
    index, and a same-index replay with motion blur matches a fresh renderer.
  - `[x]` Added a seek-only transport discontinuity revision and wired both live/headless render
    loops to reset temporal state when it changes, including forward seeks whose render time rises.
  - `[ ]` Run longer application-level seek/scrub/reload sequences and capture their frame hashes.
- `[ ]` Add a conservative synchronization option to the reference path if evidence points to reuse hazards.
- `[ ]` Add validation for resources destroyed, replaced, resized or rebound while still referenced.
- `[~]` Sanitizer coverage exists for selected animation/sequence paths; expand it to renderer resource lifetime and full relevant suites.

## Phase 4: Renderer Forensics developer mode

### 4.1 Mode and panel

- `[ ]` Add a developer-only `Renderer Forensics` mode that does not alter production scene behavior.
- `[ ]` Add a panel for isolation toggles, diagnostic views, selected-object inspection, frame capture and snapshot replay.
- `[x]` Selected-object transform diagnostics are available through `SceneRenderer`'s last-frame
  snapshot and the existing World-panel selection. The Performance panel shows world/camera state,
  model matrix, culling/submission state and GPU object slot; change-only logging is enabled.
- `[ ]` Make all controls truthful: every enabled control must isolate or visualize a real path.
- `[ ]` Record toggle state in captured frame metadata.

### 4.2 Core and feature isolation controls

- `[ ]` Minimal rendering path.
- `[ ]` Normal rendering path.
- `[ ]` Disable all culling.
- `[ ]` Disable LOD.
- `[ ]` Disable animation.
- `[ ]` Disable water.
- `[ ]` Disable terrain.
- `[ ]` Disable transparency.
- `[ ]` Disable shadows.
- `[ ]` Disable particles.
- `[ ]` Disable post FX.
- `[ ]` Disable VFX.
- `[ ]` Freeze camera.
- `[ ]` Freeze projection.
- `[ ]` Freeze view matrix.
- `[ ]` Freeze camera-relative origin.

### 4.3 Transform and geometry controls

- `[ ]` Freeze all transforms.
- `[ ]` Freeze static transforms.
- `[ ]` Show object origins.
- `[ ]` Show world axes.
- `[ ]` Show transform history.
- `[~]` Show bounds and bounding spheres.
  - `[x]` Existing debug drawing covers ordinary and procedural bounds.
  - `[ ]` Add selected-object history and culling-reason presentation.
- `[ ]` Show submitted geometry.
- `[ ]` Show object IDs.
- `[ ]` Show frustum and camera basis.

### 4.4 GPU and depth controls

- `[ ]` Show GPU object index.
- `[ ]` Show buffer generation.
- `[ ]` Show frame index.
- `[ ]` Validate GPU object data.
- `[ ]` Show raw depth.
- `[ ]` Show linear depth.
- `[ ]` Disable depth test.
- `[ ]` Disable depth write.
- `[ ]` Show depth discontinuities and object-specific depth.

### 4.5 Animation and water controls

- `[ ]` Freeze animation.
- `[ ]` Show skeleton and bones.
- `[ ]` Show animation time.
- `[ ]` Show skinning state.
- `[ ]` Show water mask.
- `[ ]` Show water depth.
- `[ ]` Show terrain depth.
- `[ ]` Show intersection mask.
- `[ ]` Disable water post effects.
- `[ ]` Ensure water diagnostics explain pixels only through water geometry, depth and masks.

## Phase 5: Culling, LOD, animation and character isolation

### 5.1 Culling forensics

- `[~]` Compare culling on/off in controlled scenes.
  - `[x]` Existing terrain, authored-node and animation culling regressions cover several cases.
- `[x]` Selected-object diagnostics now capture world bounds, camera position, visibility, cull reason,
  submission/object-slot state and six signed frustum margins. All-object change-only logging and
  richer explicit cull-reason codes remain open.
- `[~]` Record world bounds, render bounds, culling bounds, camera position and visibility result for
  each object. World bounds and selected-object state are captured; current-pose culling bounds and
  plane-level rejection evidence remain open.
- `[ ]` Verify culling never mutates scene transforms or authored visibility.
- `[x]` Animated entity culling derives bounds from the current joint palette and applies a conservative
  residual pad before frustum testing. The renderer QA record and scene code establish the ownership
  and implementation; a pixel-level limb-crossing regression is still required below.
- `[x]` Add frustum-edge regression where a posed limb crosses the plane while bind pose does not.
  `tests/unit/test_skeleton.cpp`, `[scene][skeleton][culling]`, 218 assertions.

  **Disproven hypothesis, recorded so it is not retried:** the same regression written against the
  alien composition *cannot discriminate*. A T-pose bind box is **wider** than every pose the clip
  animates into, so bind-pose bounds are conservative there and both implementations agree. A sweep
  of 65 positions across the frustum edge, each rendered twice (as culled, and with `cameraCulled`
  cleared as a no-cull control), passed identically with the posed-bounds path deliberately reverted
  to bind-pose bounds. Any future regression here needs a rig that reaches **past** its bind pose.

  The instrument that does discriminate is a two-joint bar whose tip rotates a right angle, swinging
  the top half ~1.5 m outside the bind box. The test asserts the pose genuinely leaves that box
  (`REQUIRE(bent.min.x < bindLo.x - 1.0f)`) before asserting the cull box contains every posed
  vertex, so it cannot pass for the wrong reason. Negative-controlled: forcing bind-pose bounds fails
  it.

### 5.2 LOD isolation

- `[~]` Run all canonical bugs with LOD disabled and enabled.
  Existing culling GPU coverage compares LOD-enabled behavior against direct/no-LOD rendering for
  representative procedural scenes; canonical Glowmere/alien image toggles remain open.
- `[x]` Instrumented LOD selection is covered by CPU/GPU count comparisons, camera-distance and
  screen-size threshold tests, per-instance spread migration and hysteresis checks.
- `[x]` Moving-camera threshold traversal proves deterministic, non-strobing transitions when spread
  and hysteresis are configured; matched image/performance calibration on `RendererQA` remains open.
- `[ ]` Verify mesh/material replacement cannot use stale GPU state.
- `[ ]` Calibrate LOD ratios against a moving camera and record image/performance tradeoffs.

### 5.3 Character isolation and skinning

- `[ ]` Create a scene with only camera, ground, one character and one light.
- `[ ]` Run the progression: animation off -> animation on -> culling -> shadows -> transparency -> post FX.
- `[ ]` Instrument character ID, skeleton ID, clip, animation time/delta, pose version, bone count, skinning buffer and GPU index.
- `[ ]` Reject or report NaN/Inf bone matrices, invalid quaternions/scales, invalid bone indices and invalid weights.
- `[ ]` Capture the exact animation/frame/state for invalid pose data.
- `[ ]` Test bind-pose, idle, walk, run, loop, pause, resume, seek, scrub, reverse, close/far camera and scene reload.

### 5.4 Character terrain ownership

- `[ ]` Document ownership of character X/Z, terrain height, root motion, visual offset and foot offset.
- `[ ]` Detect multiple systems writing Y in the same frame.
- `[ ]` Add a terrain-crossing regression that proves no feedback loop or one-frame disappearance.
- `[ ]` Record whether animation, terrain, physics and sequencer writes are authoritative or derived.

## Phase 6: Water, terrain, transparency and depth isolation

### 6.1 Water-only diagnostic scene

- `[ ]` Create a scene with camera, terrain plane, water plane and one light.
- `[ ]` Keep characters, vegetation, particles, post FX and shadows disabled initially.
- `[~]` Cover above-water, grazing, near-parallel and below-surface camera cases.
  - `[x]` Native water image tests cover these basic views.
  - `[ ]` Add larger flat/steep/shallow/deep/angled shoreline cases.
- `[ ]` Progressively enable water geometry, depth, transparency, terrain intersection and shoreline effects.

### 6.2 Water mask and depth forensics

- `[ ]` Visualize water geometry mask, water depth, terrain depth, linear depth, reconstructed thickness, shoreline fade, foam/intersection mask and water object ID.
- `[ ]` Capture shoreline pixel values and spaces: water depth, terrain depth, linear depth, surface height, terrain position and camera depth.
- `[ ]` Audit every depth comparison for compatible spaces and nonlinear-to-linear conversion.
- `[ ]` Document water/terrain/transparent pass order and every depth/color read/write/clear.
- `[ ]` Verify overlapping chunk sort order and chunk/world transforms.
- `[ ]` Add no-water-over-dry-terrain, stable-edge, no-z-fight, terrain-through-water and seek-determinism image tests.
- `[ ]` Do not solve seams with arbitrary depth offsets without a reproduced cause.

### 6.3 Transparency and post-processing isolation

- `[ ]` Create opaque cube, transparent cube, terrain and water test scene.
- `[ ]` Test depth test, depth write, sorting, camera movement and intersections.
- `[ ]` Disable all post-processing and run every known problem scene.
- `[ ]` Re-enable bloom, tone mapping, color grading, atmosphere/fog, volumetrics, water post FX and other screen-space effects one at a time.
- `[ ]` Record the first enabled subsystem that changes the failure.

## Phase 7: Render-pass state and pass contracts

- `[ ]` Enumerate every pass's pipeline, bind groups, vertex/index buffers, dynamic offsets, blend, depth, stencil, viewport, scissor and target ownership.
- `[~]` The main `SceneRenderer` pass contract is traced: shadow and depth passes clear/store depth;
  linear depth writes R32F; the scene pass loads background color and clears auxiliary targets;
  water/blended pipelines disable depth writes; debug/post/auxiliary/tonemap passes load or clear
  their declared color targets. Remaining work is to extend this inventory through procedural,
  particle, SDF, water, post-layer and external renderer helpers and add state assertions where
  descriptors do not make the contract visible.
- `[ ]` Verify every pass establishes the state it requires rather than relying on a previous pass.
- `[~]` Procedural draws reset local pipeline/material/mesh trackers at each helper entry and particle
  draws bind their render group and blend-specific pipeline per system. These contracts are visible in
  code; equivalent assertions/regressions for all helper pass boundaries remain open.
- `[x]` SDF state is rebuilt from the current scene each frame and water materials are uploaded each
  frame rather than retained as scene-local simulation state. Compile-time guards cover their
  dynamic uniform strides, and reused-versus-fresh SDF/water scene-swap image regressions pass.
- `[ ]` Add pass-boundary assertions or explicit state setup where the API does not make state implicit.
- `[ ]` Verify render target load/store/clear behavior and resource transitions.
- `[ ]` Verify depth prepass, terrain, water, transparent, particle, shadow, volume, debug and post pass interactions.
- `[ ]` Add raw/linear/object depth diagnostics to the pass-level test matrix.
- `[ ]` Test resize, target recreation and auxiliary debug target selection through all passes.

## Phase 8: RendererQA torture scene and progressive enablement

### 8.1 Complete `RendererQA`

- `[~]` Maintain `examples/qa/renderer-qa.json` as the permanent controlled test scene.
  - `[x]` Existing scene contains near/far/behind-camera geometry, skinned alien, transparent orb, particles and floor.
  - `[ ]` Add explicit UFO/static object, flat/slope/irregular terrain, water shoreline/depth cases, LOD distance ladder, shadow casters and labeled camera positions.
  - `[ ]` Add stable object IDs and labels that map to the forensic panel.
  - `[ ]` Add scene variants for minimal, water-only, character-only and transparency-only tests.
- `[x]` RendererQA is included in the deterministic fresh-engine/fresh-renderer showcase hash suite;
  its static diagnostic content is compared for equality without assuming it changes over time.
- `[x]` RendererQA deterministic output coverage includes both `128x72` and `96x96` targets; each
  size matches across fresh runs and the aspect-ratio hashes differ as expected.
- `[~]` Add scripted camera translation, rotation, orbit, dolly, clipping and resize paths.
  A RendererQA camera-cut regression now covers four distinct poses and compares reused versus fresh
  renderers after an explicit temporal reset. A paired-renderer sequence now covers eight orbit/dolly
  frames with alternating target sizes. The same regression now covers forward/backward/fractional
  timeline updates and a composition reload against a fresh renderer; clipping and interactive scrub
  scripting remain.
- `[ ]` Add scripted playback, pause, seek, scrub, reverse, scene reload and resolution changes.

### 8.2 Run the progressive matrix

For every level, run camera translation, rotation, orbit, dolly, playback, pause, seek, scrub, reload, resolution change and viewport resize.

- `[ ]` Level 0: basic opaque geometry.
- `[ ]` Level 1: camera movement.
- `[ ]` Level 2: terrain.
- `[ ]` Level 3: lighting.
- `[ ]` Level 4: shadows.
- `[ ]` Level 5: water geometry.
- `[ ]` Level 6: water effects.
- `[ ]` Level 7: transparent objects.
- `[ ]` Level 8: characters.
- `[ ]` Level 9: animation.
- `[ ]` Level 10: particles.
- `[ ]` Level 11: post-processing.
- `[ ]` Level 12: culling.
- `[ ]` Level 13: LOD.
- `[ ]` Level 14: sequencer.
- `[ ]` Level 15: timeline seeking and scrubbing.
- `[ ]` Record the first level where each instability appears.

### 8.3 Automatic subsystem bisection

- `[ ]` Add a machine-readable feature-group configuration for QA runs.
- `[ ]` Implement binary isolation over feature groups where practical.
- `[ ]` Record the smallest reproducing subsystem combination.
- `[ ]` Ensure bisection results include scene revision, frame, toggles and capture artifact.

## Phase 9: Frame snapshots, determinism and guards

### 9.1 Frame snapshot and replay

- `[ ]` Capture frame number, camera, view/projection, render objects, transforms, IDs, GPU indices, visibility, bounds, animation, water and pass state.
- `[ ]` Add freeze-after-capture inspection.
- `[ ]` Add replay of a captured frame without allowing unrelated application state to change it.
- `[ ]` Compare captured reference and production frame state before image comparison.

### 9.2 Determinism and hashing

- `[x]` Implement the frame-100 -> frame-500 -> frame-100 replay experiment.
  `tests/rendering/test_composition_gpu.cpp`, `[gpu][composition][forensics][determinism]`.
  **It failed, and found a real defect** -- see "Animation phase origin" in the report. Frame 500
  reached by seeking from frame 100 differed from frame 500 reached directly: 98 joint matrices, with
  every entity transform identical. Root cause, fix and regression are recorded; the experiment now
  passes including three extra laps.

  Two things this established that are worth not re-deriving:
  - **`Composition::update` is not a seek.** Jumping its clock forward integrates stateful
    simulation across the gap. The experiment has to be driven through `Engine::seekSeconds`, which
    is the operation that makes a time jump reproducible; a test that skips it is testing an API
    contract nobody uses.
  - **The diagnostic state hash cannot be a replay identity.** It folds in `paletteVersion`, a
    monotonic counter, so two arrivals at the same second legitimately hash differently. It is a
    change detector, not a state identity, and Phase 9.2 comparisons must use the state itself.
- `[ ]` Compare static transforms, animation state, camera state and deterministic object ordering.
- `[~]` Diagnostic frames now carry a deterministic CPU state hash over camera matrices, object
  transforms/bounds, frustum margins, visibility, GPU slot/submission state and selected rig palette
  metadata. A focused regression verifies the hash is stable for the same state and changes with
  camera state; full frame-100/500 replay and bone-matrix/scene-state hashes remain open.
- `[~]` Log hash transitions with frame number and seek direction.
  The replay regression reports differing joint matrices and entity transforms by count at the
  divergent second, which is what localised the animation defect. Application-level logging of hash
  transitions during an interactive scrub is still open.
- `[ ]` Add tests for timeline seeking, reverse playback, scene reload and renderer reuse.
- `[ ]` Distinguish same-GPU bit equality from cross-GPU perceptual comparison.

### 9.3 NaN/Inf guards and transform history

- `[~]` Keep existing finite camera/entity/palette guards.
- `[ ]` Extend guards to bounds, materials, water state, packed GPU structures and all diagnostic snapshot values.
- `[ ]` Report entity, frame, system, property and value when invalid data is found.
- `[ ]` Add selected-object transform history containing frame, world TRS/matrix, GPU TRS/matrix and camera state.
- `[ ]` Add screen-space projection history to distinguish correct camera parallax from transform corruption.

## Phase 10: Canonical regressions

### 10.1 Glowmere/UFO regression

- `[ ]` Test static UFO close-up with camera dolly, orbit, rotation, cut, timeline seek, scrub, reload and resolution change.
- `[ ]` Capture world transform, GPU transform, camera, bounds, visibility, LOD and object ID.
- `[ ]` Classify apparent motion as transform, camera, GPU, culling, LOD, shader or post-processing behavior.
- `[ ]` Add a permanent regression test for the proven root cause.

### 10.2 Alien regression

- `[ ]` Test idle, walk, run, loop, pause, resume, seek, scrub, close/far camera, orbit, terrain crossing, water proximity and reload.
- `[ ]` Record animation time, pose version, bone matrices, bounds, visibility and GPU skinning state.
- `[ ]` Run culling-off, animation-off, post-FX-off and depth-off comparisons.
- `[ ]` Identify the first subsystem that changes flicker behavior and fix its ownership/state flow.
- `[ ]` Add the regression test and evidence capture.

### 10.3 Water regression

- `[ ]` Test shoreline approach, parallel shoreline, above water, near water, crossing water and sloped terrain.
- `[ ]` Compare water effects off/on, post FX off/on and depth visualizations.
- `[ ]` Identify whether leakage is geometry, depth reconstruction, stencil/mask, render target or shader coordinates.
- `[ ]` Add image and state regression coverage for the proven cause.

## Phase 11: Performance safety and delivery

### 11.1 Diagnostic performance safety

- `[ ]` Confirm diagnostics have negligible cost when disabled.
- `[ ]` Avoid per-object per-frame logging by default; log changes, invalid values and selected objects.
- `[ ]` Avoid unnecessary CPU/GPU synchronization, expensive bounds work and full-frame readbacks.
- `[ ]` Measure diagnostic overhead with the same canonical scenes and resolutions.

### 11.2 Correctness and performance validation

- `[ ]` Re-measure CPU/GPU frame time, p90/tail, draw calls, visible/culled renderables, animated vertices, water/shadow/post costs and buffer uploads.
- `[ ]` Profile Constellation and Glowmere separately; do not generalize one workload to the other.
- `[ ]` Run release, debug, ASan/UBSan and TSan suites relevant to changed paths.
- `[~]` ASan/UBSan focused renderer-forensics coverage passes: 264 assertions across 16 cases with
  no sanitizer findings. The new transport discontinuity contract also passes under TSan (5
  assertions, no race diagnostics). The broader TSan transport filter is benchmark-inconclusive:
  an existing 100-us `refreshTransport` ceiling measured 111.97 us under sanitizer overhead, not a
  race. ASan then found and fixed a real composition lifecycle use-after-free: `detach()` now
  invalidates light, terrain and water node parameter pointers as well as the common node fields;
  the exact lifecycle test passes 466 assertions under ASan/UBSan. Full sanitizer suites and TSan
  resource-lifetime coverage remain open. A post-fix full ASan unit rerun reached test 412 without
  sanitizer findings but was terminated during the long world/example section; it is inconclusive,
  not a pass.
- `[x]` Complete release suite baseline: 1,677 tests passed, zero failures, with four expected
  platform/asset-gated skips (two Khronos sample imports, external ffmpeg/libx264 and NDI runtime).
  The run took 345.85 seconds on the current Apple M2 Max environment. Existing compiler warnings
  in `engine.cpp` remain unrelated to this forensic work.
- `[ ]` Investigate any order-dependent, contention-sensitive or intermittently failing test before sign-off.

### 11.3 Architectural repair standard

- `[ ]` For every defect, prefer repairing ownership/state flow over adding a conditional.
- `[ ]` Consider a subsystem rewrite only when its invariant cannot be maintained incrementally, incremental fixes worsen the design, the replacement is smaller/cleaner and it can be tested independently.
- `[ ]` Candidate rewrite areas, only if evidence supports them: transform extraction, render-object extraction, GPU object buffers, water pass, skinning or culling.
- `[ ]` Document why a rewrite was or was not justified.

## Phase 12: Final forensic report

- `[ ]` Produce the final renderer architecture map with actual state flow and authoritative owners.
- `[ ]` Produce a bug table for every meaningful issue:
  `bug | symptom | reproduction | root cause | evidence | affected subsystem | fix | regression test | residual risk`.
- `[ ]` Produce subsystem isolation results for transforms, camera, basic geometry, GPU object state, culling, animation, skinning, water, depth, transparency, shadows, post FX, sequencer, assets and performance.
- `[ ]` Explain the reference renderer contract, scope and comparison results.
- `[ ]` Document every RendererQA test and progressive matrix result.
- `[ ]` List all diagnostics, their intended use and overhead.
- `[ ]` List automated, manual, GPU-capture and sanitizer regression coverage.
- `[ ]` Document performance impact and measurement conditions.
- `[ ]` List remaining issues honestly; anything not proven fixed remains open.
- `[ ]` State whether the renderer architecture is sound enough for continued production work, with evidence.

## Completion gate

The investigation is complete only when the team can answer, with evidence:

- Why does a static object move?
- Why does the alien flicker?
- Why does water leak or intersect terrain incorrectly?
- Which subsystem causes each problem?
- Can each problem be reproduced in a controlled test?
- Can a subsystem be disabled to prove the failure boundary where applicable?
- Can the subsystem be re-enabled after repair without regression?
- Are scene state, frame snapshots and GPU submission ownership explicit?
- Are remaining unknowns documented instead of implied to be fixed?

## Useful validation commands

```sh
cmake --build --preset release -j 4
ctest --preset release --output-on-failure
ctest --preset debug --output-on-failure
cmake --preset asan && cmake --build --preset asan -j 4 && ctest --preset asan --output-on-failure
cmake --preset tsan && cmake --build --preset tsan -j 4 && ctest --preset tsan --output-on-failure

./build/release/src/avgen --headless --project examples/qa/renderer-qa.json \
  --frames 120 --fps 30 --size 1280x800 --tier realtime

./build/release/src/avgen --headless --project examples/constellation/constellation.json \
  --frames 120 --fps 30 --size 1280x800 --tier realtime

./build/release/src/avgen --headless --project examples/world/glowmere-stylized.json \
  --frames 120 --fps 30 --size 1280x800 --tier realtime
```

## Session handoff

At the end of every work session:

1. Update the nearest task status and add evidence links.
2. Record commands run and whether they passed, failed, skipped or were inconclusive.
3. Record any new hypothesis that was disproven, so it is not retried without new evidence.
4. Leave the next smallest falsifiable check in this document.
5. Do not mark a parent task `[x]` while any required child task remains `[ ]` or `[!]`.
