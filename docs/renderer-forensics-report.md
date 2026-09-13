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
| Lights, materials, procedurals, splines, SDFs, fields, particle systems | **the parameter**, over the node's authored `*Rest` snapshot | the object hanging off `Scene` | The same re-derivation, for the whole family: `applyParameters` rebuilds each one every update. Pinned and negative-controlled in `[scene][composition][forensics][derived]`; the table is in the plan's Phase 1.3 |
| Composition camera | **`camera/mode`, `camera/position`, `camera/target`** (or the orbit block), not `scene::Scene::camera` | `view`, `projection` | Same re-derivation: writing `scene().camera` is overwritten before the frame is drawn, which made two forensic tests vacuous until they were caught |
| Camera pose | `scene::Camera` | `view`, `projection`, `FrameUniforms` | **No competing construction exists**: `Camera::view()` is the only `glm::lookAt*` outside shadow light-views and `Camera::projection()` the only camera `glm::perspective*`; conventions pinned by test |
| Animation time/pose | timeline/scene update and `scene::updateRigs` | skinning palette upload | Skinning tests and scene culling audit |
| Visibility/culling | composition/procedural cull paths | draw lists and GPU cull buffers | Culling/LOD suite; selected-object diagnostics |
| Object GPU slot | `SceneRenderer::makeItem` | aligned dynamic object buffer slot | Forensic object-slot regression |
| Mesh/texture/IBL cache identity | owning `scene::Scene` + local version/id | renderer-owned GPU resources | Same-version scene regressions |
| Temporal history | renderer plus transport discontinuity signal | previous matrices, AO history, post history | Scene/resize/seek/replay regressions |

## Subsystem status

| Subsystem | Status | Evidence / remaining risk |
|---|---|---|
| Core transforms | `PASS` for renderer and composition paths, on all four axes | Renderer does not mutate authoritative entity TRS, proven over 680 frames of five camera motions with bit equality, and a 240-frame excursion returns byte-identical. The composition path holds across camera motion, timeline seeks, a scrub, playback, resolution changes and scene reloads -- 1,733 further comparisons, each axis negative-controlled. Full scene-writer audit remains open. |
| Camera matrices | `PASS` | RH/WebGPU 0..1 path, finite guards, camera-cut and motion sequences pass. Competing-path audit **closed**: there are none. One deliberate asymmetry recorded -- terrain culls at an aspect floor of 2.5 while entities cull at the exact viewport aspect. |
| Basic opaque geometry | `PASS` | Deterministic cube and full release suite pass. |
| GPU object state | `PASS` for audited slots/caches | Dynamic slot guards, stable object diagnostics and scene-owned cache fixes pass. Two objects exchanging places for 24 frames, plus a third coming and going, each keep their own matrix and their own slot -- the stale/swapped mechanism, tested directly and negative-controlled. Full buffer generation audit remains open. |
| Resource lifetime | `PARTIAL` | Timeline ring, target replacement, post transient release and scene swaps pass. Full asynchronous/live lifetime audit remains open. |
| Culling | `PASS` for the audited path, and it writes nothing it should not | Terrain/authored/selected diagnostics and plane margins pass. The cull box is now one function (`scene::entityCullBounds`) rather than two copies inside `Composition`, and a rig that reaches past its bind pose proves the box contains the pose; bind-pose bounds fail it. Culling's writes are also bounded: across four camera poses the verdict lands in `cameraCulled` while every transform and every authored `visible` flag is untouched, negative-controlled by making a cull clear `visible`. |
| LOD | `PASS` for transition stability | CPU/GPU threshold, spread and hysteresis tests pass. RendererQA image/performance calibration remains open. |
| Animation/skinning | `PARTIAL` | Palette validation, scene-owned palette cache, culling-freeze and phase-origin fixes pass; the pose is now a pure function of the timeline across seeks. The alien matrix covers Idle/Walk/Run against play, seek, a 30-step scrub, a parked playhead, two reloads and four camera distances, negative-controlled by restoring the phase-origin defect (98 of 147 joints, the original signature). Terrain crossing and water proximity are untested: that scene has neither. |
| Terrain | `PARTIAL` | Visibility leave/return regression passes. Larger terrain/water boundary QA remains open. |
| Water | `FAILED -> FIXED` for shoreline leakage; `PARTIAL` overall | `SYM-WATER-1` reproduced, root-caused and repaired: a dry corner of the water sheet claimed the depth of the level it borrowed from its neighbour, so the shore fade that hides the deliberate overhang did not fade it. Six-view GPU shoreline test proves the renderer draws no water on dry land and is negative-controlled by disabling water's depth compare, which tints land at every angle. Mask/foam visualisation and the real-world GPU shoreline case remain open. |
| Transparency/depth | `PASS` for sorting and depth-write; `PARTIAL` overall | Two transparent panes over an opaque backstop: the backstop shows through both, the nearest pane dominates the composite, and which one that is follows the camera across a traverse. Negative-controlled by reversing the blended sort, which fails at every step. Intersecting transparent geometry and per-pixel order-independent cases remain open. |
| Shadows | `PARTIAL` | Existing shadow regressions and full release suite pass. Workload timing can be contention-sensitive. |
| Particles | `PARTIAL` | Deterministic compaction, scene-owned pools and post/helper stress pass. Full camera/depth isolation remains open. |
| Post-processing | `PARTIAL` | Existing effect tests and transient target stress pass. Full pass-state and temporal history inventory remains open. |
| Sequencer/transport | `PARTIAL` | Seek-only discontinuity reset, repeated-frame determinism and the frame-100/500/100 replay pass. Full application scrub matrix remains open. |
| Assets | `PARTIAL` | Existing asset/import regressions pass; renderer asset-specific isolation is not complete. |
| Performance | `PARTIAL` | All three canonical scenes re-measured 13 September. Glowmere and RendererQA reproduce their baselines within a few percent. Constellation's median does not, and the investigation ended at a measurement defect rather than a regression: the scene is animated, so a 120-frame window never reaches steady state and the median lands wherever the workload was -- five identical runs gave 6.62 to 10.75 ms with `p10`/`p90` stable throughout. Compare its tails, not its median. Diagnostic overhead remains unmeasured. |

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

### Water standing over dry ground at a descending shoreline (`SYM-WATER-1`)

**Symptom:** at a shoreline the water sheet stands proud of the bank -- opaque water over ground the
world calls dry. Reported against Glowmere; reproduced on `world::defaultWorld()`.

**Reproduction:** `tests/unit/test_world.cpp`, `[unit][water][forensics][shoreline]`. Mesh every
chunk with `buildChunkWater`, keep the vertices a triangle actually uses, and compare each against
`WorldMap::height` and `WorldMap::waterSurface` at its own position. **135 of 3,538 drawn vertices
stood above dry ground, the worst 2.94 m proud, carrying 2.94 m of claimed water depth. The shore
fade covers 0.75 m.**

**Observed state:** the vertex positions are correct and deliberate. `buildChunkWater` emits a quad
when *any* of its four corners is wet, so the sheet always reaches one grid cell past the true
shoreline -- that overhang is what keeps the edge sub-quad instead of a staircase, and a dry corner
takes a wet neighbour's surface level so the sheet stays flat to the bank rather than folding into
the ground.

What hides that overhang is the shader and nothing else: `shoreFade = smoothstep(0, edgeFade, uv.x)`,
where `uv.x` is the bed depth in metres carried on the vertex. The depth test cannot help, because
these vertices are *above* the terrain by construction.

**Root cause:** the dry corner computed its depth against the level it had just borrowed --
`max(borrowedSurface - localBed, 0)`. Where the borrowed surface is above the local ground, which is
routine on a descending river or a bank lower than the water upstream, the corner claims metres of
water. `smoothstep(0, 0.75, 2.94)` is 1, so the fade that was the only thing standing between the
overhang and the frame returned "fully opaque".

**Repair:** a dry corner reports the depth *at itself*, which is none. The borrowed position stays --
that is what keeps the sheet flat and the shoreline sub-quad -- and only the attribute changes. One
expression in `buildChunkWater`.

**Regression:** the same test, now asserting the invariant that makes the overhang safe: *a water
vertex standing above dry ground carries zero depth*. Negative-controlled by restoring the old
expression, which fails it with the same 135.

**Residual risk:** this is geometry-level evidence. It says the sheet no longer claims depth it does
not have; it does not prove the pixel is gone in every water program, because a program that ignored
`uv.x` would still draw the overhang. The GPU shoreline test below covers the depth-test half of the
question on synthetic geometry, not this one on a real world.

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

## A note on vacuous tests

Five tests written during this investigation could not fail, and each was caught by a negative
control rather than by review:

- the alien limb-crossing sweep (a T-pose bind box is wider than every pose it animates into);
- the RendererQA static-object matrix and the Glowmere one (both wrote `scene().camera`, which the
  composition overwrites, so the camera never moved);
- the derived-copy contract test, whose parameter writes did not reach `applyParameters` at all
  because `setBase` leaves the *final* value alone and a bare `Composition::update` has no
  modulation pass to refresh it;
- the transparency sorting test, whose opaque backstop sat *between* the two panes it was sorting,
  so one of them was occluded from either side and the pair never composited together.

The common shape is a test whose *setup* silently did nothing. None of them would have been found by
reading the assertions, because the assertions were correct. **A forensic test is not evidence until
it has been shown to fail**, which is why the status definitions in the plan require it.

## Open evidence gaps

- The Glowmere UFO close-up matrix. Everything else about the static object is closed: the renderer
  path, and the composition path across camera motion, seeks, scrub, playback, resize and reload.
- Full reference/minimal renderer path and immutable frame snapshot/replay.
- All-object cull reason history and complete GPU object generation/offset audit.
- Generic transparency/depth isolation. Water leakage is now proven on two instruments -- geometry
  on the real generator, pixels on synthetic shoreline geometry -- but not yet pixels on a real
  world's shoreline.
- Progressive RendererQA enablement levels 0 through 15.
- Glowmere UFO close-up matrix and the water canonical regression matrix. (Glowmere's seek replay is
  now covered; the alien's is closed.)
- Full sanitizer and resource-lifetime suites without environment timeout/benchmark interference.
- Diagnostic overhead measurement. (Frame-time remeasurement is done; see the plan's Phase 0.1 for
  the numbers and for why Constellation's median cannot be one of them.)

## Final classification rule

A subsystem may be promoted to `PASS` only when its relevant failure mode has a controlled reproducer,
a clear state transition/root cause, a repair where needed, and a regression test or documented
measurement. Everything else remains `PARTIAL`, `UNKNOWN` or `NOT ISOLATED`.
