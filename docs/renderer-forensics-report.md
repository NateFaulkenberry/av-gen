# Renderer Forensics Report

**Status:** interim evidence report  
**Date:** 2026-09-12  
**Machine:** Apple M2 Max, macOS, Dawn WebGPU on Metal  
**Execution checklist:** [renderer-forensics-plan.md](renderer-forensics-plan.md)

This report records what has been proven so far. It is intentionally not a final sign-off: any item
without a reproducer, evidence and regression remains open.

## Architecture map

```text
Application::runLive / Application::runHeadless
    -> Engine::tick
    -> Engine::update
       -> signals, parameters, timeline, active scene controller
       -> Composition::update
          -> graph/structure rebuild
          -> parameter application
          -> terrain/water/floaters
          -> animation request + scene::updateRigs
          -> current-pose entity bounds and camera culling
    -> SceneRenderer::render / renderFrame / renderToImage
       -> camera view/projection
       -> environment and light extraction
       -> skinning palette upload
       -> object uniform extraction (makeItem)
       -> procedural/SDF/particle compute preparation
       -> shadow passes
       -> background
       -> depth prepass and linear depth
       -> AO and shadow mask
       -> scene pass: opaque, procedural, SDF, sky, water, particles, transparent
       -> volume and debug
       -> post layers and built-in post chain
       -> auxiliary debug and tonemap
    -> UI/composition overlay
    -> Queue::Submit / FrameTimeline collection / present
```

### Ownership currently established

| State | Authoritative owner | Renderer representation | Evidence |
|---|---|---|---|
| Entity world TRS | `scene::Entity::transform` | `ObjectUniforms::model` | Static-camera invariant; renderer write audit |
| Camera pose | `scene::Camera` | `view`, `projection`, `FrameUniforms` | Camera matrix path and QA camera sequence |
| Animation time/pose | timeline/scene update and `scene::updateRigs` | skinning palette upload | Skinning tests and scene culling audit |
| Visibility/culling | composition/procedural cull paths | draw lists and GPU cull buffers | Culling/LOD suite; selected-object diagnostics |
| Object GPU slot | `SceneRenderer::makeItem` | aligned dynamic object buffer slot | Forensic object-slot regression |
| Mesh/texture/IBL cache identity | owning `scene::Scene` + local version/id | renderer-owned GPU resources | Same-version scene regressions |
| Temporal history | renderer plus transport discontinuity signal | previous matrices, AO history, post history | Scene/resize/seek/replay regressions |

## Subsystem status

| Subsystem | Status | Evidence / remaining risk |
|---|---|---|
| Core transforms | `PASS` for audited renderer path | Renderer does not mutate authoritative entity TRS. Full scene-writer audit remains open. |
| Camera matrices | `PASS` for current path | RH/WebGPU 0..1 path, finite guards, camera-cut and motion sequences pass. Competing-path audit remains open. |
| Basic opaque geometry | `PASS` | Deterministic cube and full release suite pass. |
| GPU object state | `PASS` for audited slots/caches | Dynamic slot guards, stable object diagnostics and scene-owned cache fixes pass. Full buffer generation audit remains open. |
| Resource lifetime | `PARTIAL` | Timeline ring, target replacement, post transient release and scene swaps pass. Full asynchronous/live lifetime audit remains open. |
| Culling | `PARTIAL` | Terrain/authored/selected diagnostics and plane margins pass. Animated limb-crossing pixel regression remains open. |
| LOD | `PASS` for transition stability | CPU/GPU threshold, spread and hysteresis tests pass. RendererQA image/performance calibration remains open. |
| Animation/skinning | `PARTIAL` | Palette validation, scene-owned palette cache and culling-freeze fixes pass. Full idle/walk/run/terrain/water matrix remains open. |
| Terrain | `PARTIAL` | Visibility leave/return regression passes. Larger terrain/water boundary QA remains open. |
| Water | `PARTIAL` | Blend convention, view matrix cases, scene swaps and deterministic image tests pass. Mask/depth leakage isolation remains open. |
| Transparency/depth | `PARTIAL` | Main pass contract and water compositing tests pass. Dedicated generic transparency isolation remains open. |
| Shadows | `PARTIAL` | Existing shadow regressions and full release suite pass. Workload timing can be contention-sensitive. |
| Particles | `PARTIAL` | Deterministic compaction, scene-owned pools and post/helper stress pass. Full camera/depth isolation remains open. |
| Post-processing | `PARTIAL` | Existing effect tests and transient target stress pass. Full pass-state and temporal history inventory remains open. |
| Sequencer/transport | `PARTIAL` | Seek-only discontinuity reset and repeated-frame determinism pass. Full application scrub matrix remains open. |
| Assets | `PARTIAL` | Existing asset/import regressions pass; renderer asset-specific isolation is not complete. |
| Performance | `PARTIAL` | Release baseline is clean; per-scene forensic remeasurement and diagnostic overhead remain open. |

## Confirmed root causes and repairs

### Scene-local GPU cache collisions

**Symptom:** A renderer could reuse resources from a different scene when local mesh, texture, HDR
texture or rig palette versions/IDs matched.

**Evidence:** Same-version mesh, HDR environment and skinning scene-swap regressions produce
scene-distinct images and match fresh renderers after the fix.

**Repair:** Mesh, texture, environment/IBL and skinning caches now include owning `Scene` identity
in their reuse boundary.

**Regression:** Renderer forensic scene-swap cases in `test_gpu.cpp`, `test_hdri_sky_gpu.cpp` and
`test_skinning_gpu.cpp`.

### Temporal history crossing boundaries

**Symptom:** Previous matrices, AO history or post state could represent a prior scene, target size,
camera cut or seek rather than the current frame sequence.

**Evidence:** Reused-versus-fresh renderer comparisons pass after scene swaps, resize, explicit reset,
camera cuts, forward seeks and repeated frame indices.

**Repair:** Centralized `SceneRenderer::resetTemporalHistory()`, scene/resize detection, application
camera-cut integration and a transport seek-only discontinuity revision.

### Particle pool state crossing scenes

**Symptom:** Alive/dead lists, fractional emission carry and trail history were retained when a
different scene entered the renderer.

**Evidence:** Reused-versus-fresh particle scene-swap regression passes with visible first-frame
emission.

**Repair:** `ParticleRenderer` tracks owning scene and resets pools on scene change.

### Detached composition parameter use-after-free

**Symptom:** `Composition::update()` dereferenced node light/terrain/water parameter pointers after
the old `ParameterSet` had been cleared.

**Evidence:** ASan stack traced the read to `Composition::applyParameters()` after
`ParameterSet::clear()` in the detach lifecycle test.

**Repair:** `Composition::detach()` now nulls every node-owned parameter pointer, not only common
transform/material fields.

**Regression:** The exact lifecycle case passes 466 assertions under ASan/UBSan.

## Diagnostics delivered

- Selected-object renderer snapshot with world TRS/matrix, bounds, camera state, cull reason,
  submission state and GPU object slot.
- Six signed frustum margins for the selected object's conservative world bounds.
- Deterministic CPU diagnostic-frame hash over camera/object/culling/submission state.
- Selected skinned-object rig index, joint count, palette version and palette time metadata.
- Change-only selected-object/camera logging.
- Performance-panel inspection of selected renderer state.
- Compile-time CPU/WGSL size, field-offset and dynamic-offset stride guards.
- FrameTimeline ring and transient-pool stress coverage.

## Validation inventory

- Release suite: **1,677 passed, 0 failed, 4 expected skips**.
- Focused renderer-forensics GPU cases: passing; current focused run reaches 265 assertions across
  16 cases after frustum diagnostics.
- RendererQA: deterministic fresh renderer baseline, two output sizes, camera cuts, continuous
  orbit/dolly with alternating sizes, seek states and scene reload.
- ASan/UBSan exact composition lifetime regression: **466 assertions passed**.
- TSan transport discontinuity contract: **5 assertions passed**, no race diagnostics.
- Broader TSan transport filter: benchmark-inconclusive under sanitizer overhead.
- Full post-fix ASan unit suite: inconclusive because the long world/example portion was terminated;
  no second sanitizer finding was established after the composition fix.

## Open evidence gaps

- Pixel-level animated limb crossing a frustum plane while bind-pose bounds differ.
- Full reference/minimal renderer path and immutable frame snapshot/replay.
- All-object cull reason history and complete GPU object generation/offset audit.
- Generic transparency/depth isolation and water mask/depth leakage proof.
- Progressive RendererQA enablement levels 0 through 15.
- Frame hash/state replay from frame 100 to 500 and back.
- Full Glowmere UFO, alien and water canonical regression matrix.
- Full sanitizer and resource-lifetime suites without environment timeout/benchmark interference.
- Final CPU/GPU performance remeasurement and diagnostic overhead measurement.

## Final classification rule

A subsystem may be promoted to `PASS` only when its relevant failure mode has a controlled reproducer,
a clear state transition/root cause, a repair where needed, and a regression test or documented
measurement. Everything else remains `PARTIAL`, `UNKNOWN` or `NOT ISOLATED`.
