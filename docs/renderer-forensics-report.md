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
| Entity world TRS | `scene::Entity::transform` | `ObjectUniforms::model` | Static-object invariant over 680 renderer frames and 4,488 composition comparisons across five camera motions |
| Authored node TRS | **`nodes/<name>/position\|rotation\|scale`**, not `CompositionNode::transform` | flattened into `Entity::transform` | `applyParameters` re-derives the node field from the parameter every frame; a direct write to it does not survive one update (negative control) |
| Camera pose | `scene::Camera` | `view`, `projection`, `FrameUniforms` | **No competing construction exists**: `Camera::view()` is the only `glm::lookAt*` outside shadow light-views and `Camera::projection()` the only camera `glm::perspective*`; conventions pinned by test |
| Animation time/pose | timeline/scene update and `scene::updateRigs` | skinning palette upload | Skinning tests and scene culling audit |
| Visibility/culling | composition/procedural cull paths | draw lists and GPU cull buffers | Culling/LOD suite; selected-object diagnostics |
| Object GPU slot | `SceneRenderer::makeItem` | aligned dynamic object buffer slot | Forensic object-slot regression |
| Mesh/texture/IBL cache identity | owning `scene::Scene` + local version/id | renderer-owned GPU resources | Same-version scene regressions |
| Temporal history | renderer plus transport discontinuity signal | previous matrices, AO history, post history | Scene/resize/seek/replay regressions |

## Subsystem status

| Subsystem | Status | Evidence / remaining risk |
|---|---|---|
| Core transforms | `PASS` for renderer and composition paths | Renderer does not mutate authoritative entity TRS, proven over 680 frames of five camera motions with bit equality, and a 240-frame excursion returns byte-identical. Full scene-writer audit remains open. |
| Camera matrices | `PASS` | RH/WebGPU 0..1 path, finite guards, camera-cut and motion sequences pass. Competing-path audit **closed**: there are none. One deliberate asymmetry recorded -- terrain culls at an aspect floor of 2.5 while entities cull at the exact viewport aspect. |
| Basic opaque geometry | `PASS` | Deterministic cube and full release suite pass. |
| GPU object state | `PASS` for audited slots/caches | Dynamic slot guards, stable object diagnostics and scene-owned cache fixes pass. Full buffer generation audit remains open. |
| Resource lifetime | `PARTIAL` | Timeline ring, target replacement, post transient release and scene swaps pass. Full asynchronous/live lifetime audit remains open. |
| Culling | `PASS` for the audited path | Terrain/authored/selected diagnostics and plane margins pass. The cull box is now one function (`scene::entityCullBounds`) rather than two copies inside `Composition`, and a rig that reaches past its bind pose proves the box contains the pose; bind-pose bounds fail it. |
| LOD | `PASS` for transition stability | CPU/GPU threshold, spread and hysteresis tests pass. RendererQA image/performance calibration remains open. |
| Animation/skinning | `PARTIAL` | Palette validation, scene-owned palette cache, culling-freeze and phase-origin fixes pass; the pose is now a pure function of the timeline across seeks. Full idle/walk/run/terrain/water matrix remains open. |
| Terrain | `PARTIAL` | Visibility leave/return regression passes. Larger terrain/water boundary QA remains open. |
| Water | `PARTIAL` | Blend convention, view matrix cases, scene swaps and deterministic image tests pass. Mask/depth leakage isolation remains open. |
| Transparency/depth | `PARTIAL` | Main pass contract and water compositing tests pass. Dedicated generic transparency isolation remains open. |
| Shadows | `PARTIAL` | Existing shadow regressions and full release suite pass. Workload timing can be contention-sensitive. |
| Particles | `PARTIAL` | Deterministic compaction, scene-owned pools and post/helper stress pass. Full camera/depth isolation remains open. |
| Post-processing | `PARTIAL` | Existing effect tests and transient target stress pass. Full pass-state and temporal history inventory remains open. |
| Sequencer/transport | `PARTIAL` | Seek-only discontinuity reset, repeated-frame determinism and the frame-100/500/100 replay pass. Full application scrub matrix remains open. |
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

### Animation phase origin depended on when the engine first updated

**Symptom:** the same second of the same piece produced a different character pose depending on where
the playhead came from. Seeking straight to 16.67 s and playing to 16.67 s from 3.33 s disagreed. In
an editor this reads as a character flicking to a different point in its walk cycle when you scrub.

**Reproduction:** two `app::Engine`s over `examples/characters/alien.scene.json`; one seeked to
16.67 s, the other seeked to 3.33 s, updated, then seeked to 16.67 s. Compare `scene().rigs[*].palette`.

**Observed state:** 98 joint matrices differed. **Every entity transform was identical**, which ruled
out transforms, culling and the renderer in one comparison and pointed at the pose.

**Root cause:** `Composition` set `node.animationAppliedAt = time.renderTime` the first time it
applied a node's authored animation state, and `AnimationPlayer::localTime` measures a clip's phase
from that second. So the phase origin of a state a *scene file* authored was "whenever the engine
happened to run its first update" -- not a property of the piece, and different for every playback
history.

**Repair:** the first application of an authored state anchors at 0.0, the timeline's origin, because
that state has been in effect since the piece began. A state requested *during* playback -- by a
behaviour, a cue or the sequencer -- still starts when it was requested, which is what those mean.
One condition, at the one place that could tell the two apart.

**Regression:** the Phase 9.2 replay case, which asserts the palettes and transforms agree at the
divergent second as well as comparing image hashes, and runs three further laps. Full release suite
unchanged at 1,682 passing.

**Residual risk:** this changes the pose an authored state shows at any given second in a scene whose
first update was not at t=0. No existing test moved, and an offline render from 0 is unaffected by
construction, but a scene authored by eye against the old behaviour would now be a fraction of a
cycle further on.

### Detached composition parameter use-after-free

**Symptom:** `Composition::update()` dereferenced node light/terrain/water parameter pointers after
the old `ParameterSet` had been cleared.

**Evidence:** ASan stack traced the read to `Composition::applyParameters()` after
`ParameterSet::clear()` in the detach lifecycle test.

**Repair:** `Composition::detach()` now nulls every node-owned parameter pointer, not only common
transform/material fields.

**Regression:** The exact lifecycle case passes 466 assertions under ASan/UBSan.

## Established as contract, not defect

### Live-tier entities do not replay across a seek

**Symptom:** Glowmere's `wanderer` lands about 25 m apart at the same second depending on whether the
playhead arrived there directly or was seeked from earlier.

**Evidence:** the Phase 9.2 replay over Glowmere -- 278 entities -- reported exactly one differing
transform, with every joint matrix, visibility flag and culling flag identical.

**Not a defect.** ADR-091 divides simulation into a baked tier (`Track::evaluate(t)`, scrub-safe and
offline-exact) and a **live tier**: "ambient population, props, background vehicles. Stateful, reset
on seek, and **explicitly not frame-accurate under scrub**." An ambient `EntityWorld` character is
live. `EntityWorld::seek` exists to make a seeked frame *plausible* -- it is why a character does not
snap back to its t=0 pose -- not to make it reproducible.

**Regression:** the test asserts the boundary rather than equality. Everything outside the live tier
replays exactly; an entity that is not live and moves is named in the failure. Negative-controlled by
classifying nothing as live.

**Residual risk:** the live tier is identified by "driven by `EntityWorld`". If a *baked* actor were
ever driven through the same path it would be silently excused by this test.

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

- Composition-side static-object proof (node flattening, terrain grounding, sequencer writes). The
  renderer side is closed; the Glowmere UFO matrix in Phase 10.1 is the remaining half.
- Full reference/minimal renderer path and immutable frame snapshot/replay.
- All-object cull reason history and complete GPU object generation/offset audit.
- Generic transparency/depth isolation and water mask/depth leakage proof.
- Progressive RendererQA enablement levels 0 through 15.
- Glowmere UFO close-up matrix and the water canonical regression matrix. (Glowmere's seek replay is
  now covered; the alien's is closed.)
- Full sanitizer and resource-lifetime suites without environment timeout/benchmark interference.
- Final CPU/GPU performance remeasurement and diagnostic overhead measurement.

## Final classification rule

A subsystem may be promoted to `PASS` only when its relevant failure mode has a controlled reproducer,
a clear state transition/root cause, a repair where needed, and a regression test or documented
measurement. Everything else remains `PARTIAL`, `UNKNOWN` or `NOT ISOLATED`.
