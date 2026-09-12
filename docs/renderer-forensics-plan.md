# Renderer Forensics Investigation Plan

**Status:** in progress  
**Created:** 2026-09-12  
**Scope:** forensic investigation of renderer instability, subsystem isolation, architectural ownership and regression proof  
**Primary references:** [Renderer Stabilization QA](renderer-qa-2026-09-11.md), [Rendering](rendering.md), [Testing](testing.md), [Renderer 2 architecture](renderer-2-architecture.md), [Renderer 2 backlog](renderer-2-backlog.md)

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
- `[~]` Record the current build machine, GPU, OS, renderer tier, resolution, frame rate and workload conditions for every benchmark.
  - `[x]` Apple M2 Max and release/WebGPU-on-Metal environment recorded.
  - `[ ]` Add a repeatable benchmark command and result table for each canonical scene.
  - `[ ]` Separate warm-up, steady-state and background-load conditions.
- `[~]` Record known symptoms without assuming their causes.
  - `[x]` Static-object/UFO motion, alien flicker/culling, water boundary artifacts and timing instability are named regression areas.
  - `[ ]` Give every symptom a stable identifier, exact scene, frame/time range and reproduction command.

### 0.2 Define evidence standards

- `[ ]` Define the minimum evidence package for every bug:
  - symptom and visible result;
  - exact reproduction steps and scene revision;
  - enabled/disabled subsystem matrix;
  - captured frame state;
  - root-cause classification;
  - fix and regression test;
  - remaining uncertainty.
- `[ ]` Define when a subsystem is `PASS`, `FAILED`, `FAILED -> FIXED`, `NOT ISOLATED` or `UNKNOWN`.
- `[ ]` Add a root-cause ledger to this document or a linked forensic report.
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
- `[~]` Confirm authoritative camera state and matrix generation.
  - `[x]` `Camera::view()` and `glm::perspectiveRH_ZO` are documented.
  - `[ ]` Find and compare every competing view/projection construction path.
- `[~]` Confirm authoritative animation time and pose ownership.
  - `[x]` Timeline/render-time ownership is documented; renderer does not pose rigs.
  - `[ ]` Trace seek, reverse, pause, loop and frame-rate paths for duplicate time writes.
- `[ ]` Confirm authoritative owners for bounds, visibility, material state, object IDs, render IDs, GPU indices and pass state.
- `[ ]` Record each derived copy, update timing, lifetime, thread, frame boundary and synchronization rule.

### 1.3 Audit duplicated state

- `[ ]` Search for duplicated position, rotation, scale, world matrix, camera position/orientation, bounds, visibility and animation time.
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
- `[ ]` Document and test camera-relative origin ownership across entities, terrain, water and particles.

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

- `[ ]` Add a known `STATIC_TEST_OBJECT` at a fixed position such as `(10, 2, -20)`.
- `[ ]` Capture world position, rotation, scale, world matrix, render transform, GPU transform and camera state over hundreds/thousands of frames.
- `[ ]` Test static object with camera translation.
- `[ ]` Test static object with camera rotation.
- `[ ]` Test camera dolly toward the object.
- `[ ]` Test camera passing through or near the object.
- `[ ]` Test camera orbit.
- `[ ]` Require authored world transform stability in every case.
- `[ ]` Stop downstream investigation if the minimal renderer fails these tests; repair transform/camera ownership first.

## Phase 3: Camera, GPU object data and frame synchronization

### 3.1 Camera matrix forensics

- `[ ]` Instrument camera world position and rotation.
- `[ ]` Instrument view, projection, view-projection, inverse-view and inverse-projection matrices.
- `[ ]` Instrument near plane, far plane, aspect ratio, viewport width and viewport height.
- `[ ]` Verify multiplication order, handedness, forward direction, up axis, clip-space range and depth convention.
- `[ ]` Verify that culling, shading, depth reconstruction, shadows, volumetrics, picking and overlays consume the same authoritative camera model.
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
- `[ ]` Verify CPU/WGSL structure size, alignment, offsets, padding, type widths and matrix layout.
- `[ ]` Add an alternating-transform two-object test to detect stale or swapped GPU data.
- `[ ]` Fix mesh upload identity/version collisions; specifically verify that two fresh scenes with same local mesh versions cannot reuse the wrong geometry.

### 3.4 Frame synchronization and resource lifetime

- `[ ]` Document when CPU state updates, GPU data is written, GPU consumes it, GPU finishes and memory is reused.
- `[ ]` Audit textures, buffers, bind groups, pipelines, materials, meshes, animation buffers, depth textures, water textures and post-process targets.
- `[ ]` Stress resource reuse through resize, scene reload, timeline seek/reverse, camera cuts and frame-index reuse.
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
  - `[ ]` Add per-object cull reason, bounds, camera and frustum-plane capture.
- `[ ]` Record world bounds, render bounds, culling bounds, camera position and visibility result for each object.
- `[ ]` Verify culling never mutates scene transforms or authored visibility.
- `[ ]` Verify animated bounds are current-pose, conservative or otherwise proven safe.
- `[ ]` Add frustum-edge image regression where a posed limb crosses the plane while bind pose does not.

### 5.2 LOD isolation

- `[ ]` Run all canonical bugs with LOD disabled and enabled.
- `[ ]` Instrument LOD selection, camera-relative distance, screen size, transition state and hysteresis.
- `[ ]` Test rapidly moving cameras for deterministic, non-oscillating LOD transitions.
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
- `[ ]` Verify every pass establishes the state it requires rather than relying on a previous pass.
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
- `[ ]` Add scripted camera translation, rotation, orbit, dolly, clipping and resize paths.
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

- `[ ]` Implement the frame-100 -> frame-500 -> frame-100 replay experiment.
- `[ ]` Compare static transforms, animation state, camera state and deterministic object ordering.
- `[ ]` Add CPU hashes for scene state, camera state, render objects and animation state.
- `[ ]` Log hash transitions with frame number and seek direction.
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
- `[ ]` Run the complete release suite and document skips, failures and platform-gated tests.
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
