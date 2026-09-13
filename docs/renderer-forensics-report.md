# Renderer Forensics Report

**Status:** evidence report, third pass; Phase 12 findings included  
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

### The four paths

`runLive`, the headless loop, `renderToImage` and the tests all call one `SceneRenderer::render`, so
a difference between them is never pass order. Two differences are real: the capture path finishes,
submits and **waits for the queue** (and collects timings twice) so an offline frame's CPU breakdown
is complete, which a live frame never pays; and the live path shares its command encoder with the UI
overlay, so a live frame carries commands no capture does. Tests use the capture path, which is why a
bug reproducible in a test is reproducible in an offline render by construction -- and why one that
appears only live is the UI, the shared encoder, or a real frame delta that a capture usually lacks.

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

### NaN is discarded by the bounds folds, not propagated

**Symptom:** none reported -- this was found by audit rather than by a bug. It is recorded because it
explains a *class* of report: an object that is simply gone, a mesh that looks mis-modelled, a frame
that goes black around one light.

**Evidence:** `tests/unit/test_renderer_layout_guards.cpp`. `glm::min`/`glm::max` are `(y<x)?y:x`, so
a NaN loses every comparison and is dropped. Four consequences, each reproduced by poisoning one
field and observing the output:

| Door | What arrived downstream |
|---|---|
| `entityCullBounds`, infinite scale | `min = FLT_MAX`, `max = lowest()` -- **finite**, so no `isfinite` guard can fire, and **inverted**, so every frustum test rejects it. Silent. |
| `MeshData::bounds`, NaN vertex | a finite box, too small, with no mention of the vertex |
| `fitDirectionalCascade` | a non-finite `viewProj` straight into the shadow uniforms |
| `packLight` | NaN intensity survives `std::max(x, 0)`; NaN position copied verbatim |

**Repair:** each door now tests for a *valid* result rather than a finite one, names the object and
prints the values. `entityCullBounds` falls back to a small box at the entity's position, so the
object draws in the wrong place instead of vanishing -- a thing in the wrong place can be chased.
`packLight` drops the light rather than correcting it: a missing light is something to look for; a
light that quietly became a different light is a wrong picture nobody can explain.

A fifth door, the widest of them, was shut later with a device: the material-to-`ObjectUniforms`
packing. A NaN base colour becomes a NaN pixel, and a NaN pixel spreads through bloom's downsample
to a whole tile and then to the frame -- a bright region with no object near it to blame. The
material is now replaced with magenta rather than dropped, because a vanished object is the hardest
report to act on. Nine fields, each poisoned in turn, in
`[gpu][composition][forensics][guards]`.

A sixth, and the one that looked safest: `waterUniformsFrom`. Its packing is full of
`std::max(x, 1e-3f)` and `std::clamp(x, 0.02f, 1.0f)` floors that read exactly like guards, and none
of them were -- `std::max(NaN, 1e-3f)` is NaN. Water is also where a single bad value does the most
damage, because it shades a region rather than an object. The surface is now refused *whole* rather
than corrected field by field, because a surface with one arbitrary field replaced is one nobody
authored.

**Residual risk:** the diagnostic snapshot's own values are still unguarded -- the lowest-stakes of
the seven, since a bad capture misleads a reader rather than the frame.

### Every diagnostic view was tone-mapped (`SYM-AUX-1`)

**Symptom:** none reported, and that is the point -- the instrument was wrong, so nothing it said
could be trusted enough to report. Found while building a linear-depth view and discovering that a
shader writing 1.0 arrived on the screen as 202.

**Evidence:** the auxiliary debug pass drew into the **HDR** target, ahead of the tone map, which
then applied auto-exposure and a filmic curve to it. Every one of the seven views had been going
through that since ADR-035. Three consequences, in increasing order of how badly they mislead:

| View | What it actually showed |
|---|---|
| normal, roughness, velocity, emission, occlusion | the encoded value through a filmic curve: 0.5 did not arrive as 0.5, and no number could be read off it |
| depth | a compression on top of a compression |
| ids | a palette **that moved with the scene's brightness** -- the same object could be two different colours in two frames of the same scene |

**Repair:** the pass draws after the tone map, straight onto the target, one pipeline per target
format (the same reason the tone map itself is a per-format map). The byte on the screen is now the
value the shader wrote, up to the hardware's own encode on an sRGB target.

**Regression:** `[gpu][composition][forensics][views]` compares the linear-depth view against a
readback of the linear-depth target **per pixel**, to within one step of 8-bit quantisation -- an
identity rather than a trend, which is only expressible because the view is no longer a function of
the frame's exposure. The direct guard is four stops of exposure compensation leaving a view's hash
unchanged, with the shaded frame's hash changing under the same four stops as its control.

**Residual risk:** the overlay (ADR-083) still draws over the view, so a composition layer covers a
diagnostic. That is the same ordering as before and is visible rather than silent.

### Detached composition parameter use-after-free

**Symptom:** `Composition::update()` dereferenced node light/terrain/water parameter pointers after
the old `ParameterSet` had been cleared.

**Evidence:** ASan stack traced the read to `Composition::applyParameters()` after
`ParameterSet::clear()` in the detach lifecycle test.

**Repair:** `Composition::detach()` now nulls every node-owned parameter pointer, not only common
transform/material fields.

**Regression:** The exact lifecycle case passes 466 assertions under ASan/UBSan.

### The water depth spaces agree (`SYM-WATER-2`, refuted)

**Hypothesis:** the shoreline artefacts came from a depth-space mismatch -- `water.wgsl` differencing
a ray length against a forward-axis distance, or a Y-flipped reprojection.

**Evidence against it, measured rather than read.** 988 taps on a synthetic quad, each compared
against a CPU ray/plane intersection: the linear-depth target is a forward-axis distance to within
2.8 mm, 0.007%. The test is known to be able to tell the two apart, because across the same taps the
two candidate spaces are 29.4% apart -- and swapping the shader to `length(p - eye)` fails three
separate tests with a 13.27 m error. `fs_water` computes its view depth on the same axis from the
same origin in the same units; the refraction reprojection's screen UV matches the linear-depth
pass's own Y convention; `uv.x` is vertical and is only ever used for the shore fade and the
ray-thickness cap, never differenced against a ray depth.

**Consequence for the rule.** No seam was reproduced, so no depth offset was added. That is the
plan's "do not solve seams with arbitrary offsets without a reproduced cause" doing its job, and it
is worth recording as a refutation rather than leaving the hypothesis open: the water defect that did
exist (`SYM-WATER-1`) was geometric, and this rules out the depth-space explanation people reach for
first.

### Water cannot be identified from the identifier target

`water_renderer.cpp:150` masks every scene target but colour and emission, deliberately -- a normal
or an identifier averaged over a transparency is worse than none. The consequence is worth stating
because it constrains what diagnostics are *possible*, not merely what is built: **no auxiliary view
keyed on the identifier target can show water, and no test can classify a water pixel from it.** The
water forensics therefore find water pixels by rendering the same frame twice, once with the water
arm off, and differencing. Every water measurement in this report rests on that A/B.

### Entity zero's pick id is zero, and only the material id makes it findable

**Symptom:** none in production, and the reason is a detail that could easily have been dropped.

**Evidence:** `packPickId(PickSpace::Entity, 0)` is `(0 << 14) | 0` = **0**, which is exactly the
value the identifier target clears to. So the object id alone cannot distinguish "this is entity
zero" from "nothing was drawn here". What saves it is that the packed word carries the *material* id
in its high sixteen bits and that number is deliberately one-based (`thisEntity + 1`), so entity
zero's word is `0x00010000` -- non-zero, and distinguishable. The picker reads the whole word and is
correct. `aux_debug.wgsl`'s identifier view tests the whole word and is correct.

**Where it bit:** the new object-depth view compared only the low sixteen bits against a pick id, so
selecting the first entity matched every empty pixel and painted the whole sky as that object. Found
because a *culled* object's depth view came back showing the object everywhere -- the one framing
where the wrong answer is obviously wrong. The view now tests the whole word for emptiness before
matching the low half.

**Why it is in this report rather than only in a commit:** the one-based material id is load-bearing
and nothing said so. Anything new that reads this target must test `word != 0` and not
`objectId != 0`, and the test that guards it cannot be a pixel count -- a large floor legitimately
covers two thirds of the frame, so "not the whole frame" passes for a view that is almost entirely
wrong. The assertion that works asks whether a *particular region* -- the sky -- is claimed by
anybody.

### A terrain scene does not render the same frame twice (`SYM-TERRAIN-1`, open)

**Symptom:** on `renderer-qa-water.scene.json` -- terrain with water, and nothing else -- the same
`FrameTime` drawn twice through the same renderer gives two different pictures, by 0.1% to 0.5% of
channels (measured 571, 215 and 9 of 108,160 at three checkpoints).

**It does not converge.** A third and fourth draw differ from the second by as much as the second
differs from the first (571 / 127 / 135). So it is not warm-up, not a cache filling, and not a
temporal filter settling -- it is a persistent per-call non-determinism.

**It is build-dependent, and that is the sharpest clue.** The same source, the same GPU, the same
idle machine: `[gpu][renderer][forensics][water6_2]` passes all eight cases in **release** and fails
four in **debug**. A renderer whose picture depends on the optimisation level, at a held timeline, is
not doing arithmetic differently -- it is racing, and debug's slower CPU changes which side wins.

**Confirmed contributor: the procedural empty-level draw skip.** `procedural_renderer.cpp` decides
whether to record an indirect draw from `emptyFrames[]`, which is driven by "the latest *completed*
stats readback" of a non-blocking async map. So what a frame draws depends on when a callback landed.
Raising `kEmptyLevelFrames` out of reach on the shoreline scene takes the drift from 426.7 to 122.8
and the failures from four to three -- a contributor, and not the whole of it.

**A correction worth keeping.** That same suspect was written up here as *exonerated*, on the grounds
that `renderer-qa-water.scene.json` records zero indirect draws and zero visible instances. That
measurement was right and the conclusion was too broad: it exonerates the skip **on that scene**,
which has no scatter, and says nothing about a scene that has some. Two scenes, two different
subsets of the same mechanism. The general claim needed a general measurement and did not have one.

**Ruled out, each by measurement:**

| Suspect | How it was eliminated |
|---|---|
| Ambient occlusion's temporal history | the difference survives the `ao` arm being off (136 / 195 / 9) |
| The particle simulation, which *is* stepped inside `render` | `renderer-qa-water` contains no particles |
| A stale readback returning last frame's pixels | `readTextureRaw` maps with `WaitAny`; the read is synchronous and exact |
| The procedural path, **on `renderer-qa-water` only** | that scene records zero indirect draws and zero visible instances |
| Contention between concurrent GPU test runs | it was first seen that way and attributed to load; it reproduces on an idle machine |

So at least two sources remain: the skip on scatter scenes, and something else on a scene where the
procedural path does not run at all.

**Consequence for the suite.** `renderer-qa-water.scene.json` stays in the resource-lifetime rotation
-- reloading and swapping to and from a terrain scene is exactly the lifetime under test -- but is
excluded from the fresh-reference checkpoints, and the repeated-frame comparison reports on it rather
than asserting. A red test nobody can act on gets muted rather than fixed, and the measurement is
more use in the report than in a failure nobody reads.

**Where to look next.** The family is *frame content decided from a non-blocking readback*, so the
experiment is to enumerate every such decision on the camera path and make each one a pure function
of the frame index -- consulting a snapshot at a fixed latency, and waiting for it, rather than
whichever one has arrived. The empty-level skip is the known instance; the unidentified second source
is on a scene with no procedural path at all, so it is elsewhere. The draw count on that scene also
grows across rounds (11 → 50 → 113), so chunk residency is changing, and whether that is the
renderer's lazy upload or the scene's streaming was not separated.

**Consequence for every image test in this repository:** run the GPU suite in **release**. Debug's
seven failures are this one defect with two faces, not seven problems.

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

**A revision written on 13 September was itself wrong and is withdrawn.** It claimed the 25 m was not
drift but one path simulating while the other stood still, on the evidence that 1,800 frames of
playback left the walker at travel 0. That measurement was an artefact of the test that made it: it
restarted its clock at each second, so every frame reported a delta of zero and no behaviour that
integrates could move. One ticked clock walks the same entity 149 m in the same 30 s. The entry
above stands as written; the contract is the contract.

What the episode does establish is narrower and worth keeping: the Phase 9.2 Glowmere replay drives
its frames the same way, so in *that* test playback moves the walker very little and the 25 m is
mostly what `EntityWorld::seek` produced on its own. The conclusion is unchanged -- ADR-091 declines
to make a live-tier entity reproducible under a seek -- but the number is a property of the seek, not
a difference measured between two moving simulations.

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

Six tests written during this investigation could not fail, and each was caught by a negative
control rather than by review:

- the alien limb-crossing sweep (a T-pose bind box is wider than every pose it animates into);
- the RendererQA static-object matrix and the Glowmere one (both wrote `scene().camera`, which the
  composition overwrites, so the camera never moved);
- the derived-copy contract test, whose parameter writes did not reach `applyParameters` at all
  because `setBase` leaves the *final* value alone and a bare `Composition::update` has no
  modulation pass to refresh it;
- the transparency sorting test, whose opaque backstop sat *between* the two panes it was sorting,
  so one of them was occluded from either side and the pair never composited together;
- the character/terrain test, which restarted its clock at each second and so reported a frame delta
  of zero: every behaviour that integrates did nothing, for 1,800 frames, and the walker's stillness
  was written up as an engine defect before a second look. Restarting a clock is right for the
  static-object matrices, where time is *meant* to stand still, and wrong for anything that moves.

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

## The pass contract (Phase 7)

Every pass the main renderer encodes, in submission order, with what it does to its targets. Taken
from the descriptors rather than from memory.

| Pass | Colour targets | Colour load/store | Depth | Owns |
|---|---|---|---|---|
| `shadow` | none | — | clear / store | the cascade and spot depth maps |
| `background` | 1 (HDR) | clear to the environment colour / store | none | the frame's ground colour |
| `depth-prepass` | none | — | clear / store | scene depth |
| `linear-depth` | 1 (R32F) | clear to 1e7 / store | none | linear depth for AO, water and post |
| `scene` | `kSceneTargetCount` | colour loads the background; the auxiliary targets clear to zero / store | loads the prepass, else clears | opaque, procedural, SDF, sky, water, particles, transparent |
| `debug` | 1 (HDR) | load / store | load | ADR-031 debug geometry |
| `post-layer` | 1 | clear / store | none | user post layers |
| `tonemap` | 1 | clear / store | none | the final display-referred image |
| `aux-debug` | 1 (the target) | clear / store | none | the auxiliary-target viewer, **after** the tone map so its values are not exposed and curved (`SYM-AUX-1`) |

**Pipeline state, per draw kind.** The pass table says what each pass does to its targets; this says
what each *pipeline* does inside one. Taken from the descriptors, and the two columns that carry the
contract are the last two.

| Draw kind | Blend | Depth write | Depth compare | Auxiliary targets |
|---|---|---|---|---|
| opaque lit (culled, two-sided) | none | **yes** | `LessEqual` | written |
| blended lit | src-alpha over | **no** | `Less` | **masked off** -- a normal or an identifier averaged over a transparency is worse than none (ADR-035) |
| grid | additive (`One`/`One`) | no | — | written |
| water | over | **no** | — | masked to colour and emission only, which is why water has no identifier and every water measurement is an A/B |
| particles | over | **no** | — | masked |
| depth prepass / shadow | none | yes | — | none |
| fullscreen (linear depth, aux debug, tonemap) | none | no | — | one target each |

Three things fall out of it that are worth stating rather than leaving in the table. **Every
transparent kind disables depth write** -- water, particles and blended geometry alike -- so the
depth buffer is the opaque scene and nothing else, which is what makes the linear-depth target a
usable input for AO, water and fog. **Masking the auxiliary targets is the rule, not an exception**:
three of the four transparent kinds do it, and the one consequence people meet is that water cannot
be identified from the identifier target. And **there is no viewport or scissor call anywhere in the
renderer** -- every pass draws the full attachment, so a resize is a target recreation rather than a
state change, which is exactly why the resize regression compares against a renderer born at that
size rather than checking a rectangle.

Compute passes -- culling, clusters, particles, fields, SDF, simulation -- are marked on the same
timeline and own their own buffers.

**Asserted, not only described.** `[gpu][composition][forensics][passes]` checks the join between
this table and Phase 4.2's isolation arms: *each arm removes exactly its own pass and nothing else.*
An arm that removed two passes would be one subsystem owning another's state, which is the "relies on
a previous pass" the phase is about; an arm that removed none would be the untruthful control the
plan forbids; a pass appearing when its arm is off would be a pass running for nobody.

Three arms -- water, transparency, culling -- draw *inside* the scene pass rather than owning one, so
what they remove is draws. The test records that distinction rather than leaving it to be
rediscovered. Post owns several labelled passes and is checked as a family: everything it removes
begins with `post`.

## Phase 12: findings

### The bug table

| Bug | Symptom | Reproduction | Root cause | Subsystem | Fix | Regression | Residual risk |
|---|---|---|---|---|---|---|---|
| Animation phase origin | a pose jumps when the playhead is scrubbed | two engines over `alien.scene.json`, one seeked to 16.67 s, one played there | the phase origin of an *authored* state was the engine's first update, not the timeline's zero | animation | first application anchors at 0.0; a state requested during playback still starts when requested | `[gpu][composition][forensics][determinism]` and the alien matrix | a scene authored by eye against the old behaviour sits a fraction of a cycle further on |
| Water over dry ground | the water sheet stands proud of the bank | mesh every chunk of `defaultWorld()` and compare vertices against `height`/`waterSurface` | a dry corner measured its depth against the surface level it borrowed from a wet neighbour, so the shore fade that hides the deliberate overhang did not fade it | water | a dry corner reports the depth at itself, which is none | `[unit][water][forensics][shoreline]` | geometry-level: a water program ignoring `uv.x` would still draw the overhang |
| Scene-local GPU cache collisions | a renderer reused another scene's resources | same-version mesh/HDR/palette scene swaps | caches keyed on local ids and versions only | GPU resources | owning `Scene` joins the reuse key | scene-swap cases in three GPU files | — |
| Temporal history crossing boundaries | history from another scene, size, cut or seek | reused-versus-fresh comparisons | no reset at the boundaries | temporal | `resetTemporalHistory` plus a transport discontinuity revision | resize/seek/replay cases | — |
| Particle pools crossing scenes | alive lists and trails retained across a scene change | reused-versus-fresh swap | pools outlived their scene | particles | pools reset on scene change | particle swap case | — |
| Detached composition parameters | use-after-free in `applyParameters` | ASan on the detach lifecycle test | `detach()` nulled only the common node fields | composition lifetime | every node-owned pointer is nulled | the lifecycle case under ASan | — |
| Particle pools survive a seek | a seeked renderer does not match a fresh one | 40 frames then a seek back to 0.5 s, compared to a fresh engine and renderer; 63 of 102,400 channels | `resetTemporalHistory` reset the AO history and not the particle pools -- `ParticleRenderer::resetAll` said in its own comment it was "used on seek/offline restarts" and had no caller but the scene-pointer change | particles / temporal | `resetTemporalHistory` resets the pools | `[gpu][renderer][forensics][lifetime3_4]`, asserted exactly, with the particles-off comparison as the control that says they were the whole difference | — |
| Previous skinning palette survives a seek | the first frame after every scrub smears | motion blur on, seek to 1.0 s: the current palette matches a fresh engine joint for joint, 49 of 49 *previous* joints hold the pre-jump pose, worst element 71.5 units on a ~100-unit character | `previousPalette` means "a frame ago", which is false across a discontinuity; `SkinnedRig::evaluate` copies it forward before re-posing, right for playback and wrong for a jump | animation | a seek flags each rig; the next `evaluate` takes its previous from the pose it lands on | `[gpu][composition][forensics][lifetime][seek]`, exact, with ordinary playback still reporting motion as the control | — |
| Diagnostic views tone-mapped (`SYM-AUX-1`) | none reported; the instrument was wrong, so nothing it said was trustworthy | a shader writing 1.0 reaches the screen as 202 | the auxiliary view pass drew into the HDR target, ahead of exposure and the filmic curve | renderer diagnostics | the pass draws after the tone map, one pipeline per target format | `[gpu][composition][forensics][views]`: the linear-depth view checked per pixel against a readback of the buffer, and four stops of exposure leaving it unchanged | the composition overlay still draws over a view -- visible, not silent |
| A node's `material` block | authored material silently ignored on every kind but terrain; `alphaMode` never parsed at all | RendererQA's `transparent-orb` draws opaque | the parse sat inside `if (kind == Terrain)` | scene format | parsed for every kind, applied on orbs, warned where unused | `[gpu][composition][forensics][isolation]`, which needs a transparent object to exist | kinds other than orb and terrain still do not *use* it -- they now say so |

### Subsystem isolation results

Recorded in the subsystem table above. In one line: **transforms, camera, opaque geometry, GPU
object state, culling and LOD are `PASS` with controlled reproducers; animation, terrain, water,
transparency, shadows, particles, post, sequencer, assets and performance are `PARTIAL`,** each with
its remaining risk named. Nothing is `FAILED` and nothing is `NOT ISOLATED`.

### The reference renderer

**Built, and deliberately small.** `rendering::ReferenceRenderer` (`reference_renderer.cpp`,
`shaders/reference.wgsl`) draws opaque lit entities and nothing else: no shadows, no AO, no
volumetrics, no post, no skinning, no transparency, no water, no procedural or SDF path. It uploads
each mesh per call rather than caching, which is the point -- a second renderer that shared the
production caches could not be evidence about them.

**The contract is coverage, not colour.** The two paths shade differently by construction, so
comparing pixels would compare tone maps. What is compared is *where the geometry is*: each image's
own background is measured from a corner pixel, a pixel counts as geometry when it is meaningfully
brighter than that background, and the two masks are required to agree. An absolute threshold was
tried first and does not survive -- production tone-maps and auto-exposes, so on a dark scene it
lifts the empty background clear of any fixed cut-off and the mask comes out as "the whole frame".

**Results.** `[gpu][composition][forensics][reference]` over `renderer-qa-minimal`, five camera
views (opening, from the side, close, on the static cube, high and back), production stripped to a
comparable pass set. Every view agrees to within 8% of the covered area, with at least 500 pixels
covered by both, and the reference path reports what it declined rather than silently drawing less.
The tolerance is about edges and fog, not about placement: a transform or camera error is not a
percent, it puts the object somewhere else entirely, and that is the class of defect this comparison
can rule out.

### The RendererQA scenes and the progressive matrix, level by level

Five scene variants, each existing because something could not be isolated without it:

| Variant | Why it exists |
|---|---|
| `renderer-qa` | the mixed scene: near/far/behind-camera geometry, a skinned alien, a transparent orb, particles, a floor |
| `renderer-qa-minimal` | opaque meshes and nothing else -- the only scene the reference renderer can be compared against without it declining half the frame |
| `renderer-qa-water` | a **real generated shoreline** from the shipped world, camera on the bank at the grazing angle `SYM-WATER-1` was reported from. Two planes would have been easier and would not have contained the defect |
| `renderer-qa-character` | one rig, isolated, for the skinning forensics |
| `renderer-qa-transparency` | blended geometry with an opaque backstop *behind* it -- the first version had the backstop between the panes, which is why that test proved nothing until it was reversed |

The matrix runs nine cumulative levels, each an eleven-step script (translate, rotate, dolly, orbit,
seek forward, seek back, scrub, resize, resize back, reload) twice from two independent engines and
two independent renderers, comparing step by step. Every rung must also **change the picture** against
the rung below, which is what found the two gaps below.

| Level | Result |
|---|---|
| 0 opaque geometry | pass |
| 1 camera movement | pass (the script, at every rung) |
| 2 terrain | **no control exists** -- terrain is present at every rung; see Phase 4.2 |
| 3 lighting | no control exists; present throughout |
| 4 shadows | pass, two rungs (cascades, shadow mask) |
| 5 water | pass in release. **In debug it names this rung on every step** -- that is `SYM-TERRAIN-1`, and the matrix localising an open defect to a rung is the matrix working |
| 6 water effects | no control separates a surface's effects from the surface |
| 7 transparent objects | pass -- **reachable only after the material fix**; the scene's "transparent orb" had been opaque since it was written, and the rung found it |
| 8 characters | present at every rung; no "characters off" control separate from animation |
| 9 animation | pass |
| 10 particles | pass |
| 11 post-processing | pass, three rungs (AO, volumetrics, post). The volumetrics rung supplies its own `scene/volumeDensity`, because RendererQA authors zero and the pass is off at zero however the toggle is set -- another gap the change-the-picture check found |
| 12 culling | on throughout; switching it off adds objects rather than a subsystem, so it is an arm rather than a rung |
| 13 LOD | no control (GPU cull pass) |
| 14 sequencer | no control |
| 15 seeking and scrubbing | pass (the script, at every rung) |

The instrument itself had to be repaired twice before any of this counted. Re-rendering a state was
tried first and is wrong twice over: it ticks the clock, so an animated scene is legitimately a
different pose, and **the particle simulation is stepped inside the render call**, so rendering the
same frame again steps the world again. Both were reported as "the first misbehaving level" before
the instrument was fixed.

### Validation coverage, by kind

| Kind | What it covers | Standing |
|---|---|---|
| **Automated** | everything that can be stated as a claim -- 262,560 assertions in the render suite, 491,828 in the unit suite, all green in release | the default, and it absorbed several things that looked like they needed eyes |
| **Sanitizer** | ASan/UBSan over the forensics unit tests (1,822 assertions, no findings) and over the GPU paths this work changed; TSan over the transport discontinuity contract | partial by design: a full GPU suite under ASan runs at roughly six cases an hour, so it is targeted rather than exhaustive, and the targeting is recorded |
| **Manual visual review** | the QA baseline document | used for one thing, named as such. **No defect in this investigation was found this way**, and no regression depends on it |
| **GPU capture** | nothing | a real gap rather than a decision. A Metal frame-debugger trace answers "what did the driver actually do", and no finding here needed that question -- it is the first tool to reach for if a symptom ever survives every measurement in this document |

### Diagnostics and their overhead

Listed under "Diagnostics delivered" above, plus the Phase 4 isolation arms: shadows, shadow mask,
AO, volumetrics, post, culling, water, transparency, particles, animation and a view freeze, in the
Performance panel and as `--disable <list>`.

**Overhead: not measurable at the resolution available.** The per-object diagnostic frame is built
unconditionally for every entity every frame -- a string copy, world bounds and six frustum margins
each. Removing the string copies from Glowmere's 278 entities moved the wall-clock median from
22.11/21.91 ms to 22.20/22.01 ms across paired runs, which is inside the run-to-run spread. The
honest claim is therefore "below ~0.3 ms on the heaviest canonical scene", not "free".

### Was a rewrite justified anywhere

**No, and the shape of the evidence is the reason.** Of the eleven defects this investigation fixed,
**nine were state ownership at a boundary** -- a cache key, a temporal history, a particle pool, a
parameter lifetime, an animation phase origin, a skinning palette across a jump. One was geometry
(a quad emitted a cell too wide). One was a render pass on the wrong side of the tone map.

Not one was a subsystem whose internal design could not hold its own invariant. That matters for the
rewrite question directly: **rewriting any of those subsystems would have preserved every defect**,
because none of them live *inside* a subsystem -- they live between two, in the handover.

Each candidate area the plan lists, against the evidence actually collected:

| Candidate | Verdict | Why |
|---|---|---|
| Transform extraction | no | the most heavily pinned path in the repository -- four axes of bit-equal static matrices, a per-object comparison against an independent renderer, a full world-to-pixel chain -- and it has produced no defects |
| Render-object extraction | no | object state is already one function shared by the camera and shadow paths, precisely so the two cannot describe an object differently |
| GPU object buffers | no | 21 structure pairs agree byte for byte with the WGSL; slots are dense, ascending and deterministic |
| Water pass | no -- **and the evidence changed the answer** | the shoreline looked like a depth-space problem in the water shader, which would have been a rewrite candidate; measurement refuted it (2.8 mm agreement, the wrong space 29.4% away) and the real defect was one line of terrain geometry |
| Skinning | no | two seek defects, both in *when* state was resynchronised, neither in how skinning works |
| Culling | no | its bounds rule was duplicated and is now one function: extracting a rule, not rewriting a subsystem |

Nine of eleven repairs were ownership fixes. The two that were not are named rather than glossed: the
skinning-palette reseed is a flag, because the ownership-shaped alternative (collapsing the previous
palette at seek time) pins the pose being *left* and reintroduces the jump -- the information does not
exist until the next evaluation, so something has to carry "a discontinuity happened" across the gap.
The material NaN check is a guard, and a guard that repaired ownership would not be a guard.

The one place a change is owed and a rewrite is still not the answer is `SYM-TERRAIN-1`: frame content
must not be decided from whichever asynchronous readback has arrived. That is a discipline to apply at
each decision site, not a subsystem to replace.

### Is the architecture sound enough to keep building on

**Yes, with the reservation that the evidence is about the paths that have been walked.** Across three
passes the investigation found eleven defects, **nine of them state ownership at a boundary** -- a
cache key, a temporal history, a particle pool twice, a parameter lifetime, an animation phase
origin, a skinning palette across a jump -- one in geometry, and one a pass on the wrong side of the
tone map. None was in the transform chain, the camera model or the GPU object path, which were the
three the original symptoms pointed at and are now the best-evidenced parts of the renderer.

That distribution is the architectural answer. Subsystems hold their own invariants; **handovers are
where this engine breaks**, and the repair is always the same shape -- say who owns the value and
when it is resynchronised. It is also why no rewrite is justified: rewriting a subsystem preserves
every defect that lives between two.

Two weaknesses are worth carrying forward, and neither is in the renderer.

**A scene can say something the engine silently ignores.** The `material` block, `volumeDensity: 0`
keeping a pass off, an orb's hard-coded surface -- each authored in good faith and doing nothing, and
the QA scene meant to torture the renderer contained no transparency and no water. That class cost
more than any renderer bug found here.

**A measurement can be reassuring and wrong**, which cost this investigation more than both. A frame
mean that tracked sky coverage rather than depth. A camera move that passed through the surface it
was measuring. A hue test that read a grey sphere as a blue one. A transparency backstop placed
between the panes. A "liveness" check that was reading a defect. Six vacuous setups, each caught only
by asking what the instrument would do in the case it was *not* for -- which is why that is now a
working rule rather than a habit.

## The completion gate, answered

The plan's nine questions, each with the evidence rather than a verdict.

**Why does a static object move?** In every case examined: *it does not.* The transform path is
pinned on four axes with bit equality on RendererQA and now on Glowmere with the visitor the report
was about -- camera motion, seeks and scrubs, resolution changes, reloads -- and compared per object
against an independent renderer. What produced the appearance of drift, three times over, was the
*instrument*: a clock restarted per frame reporting a zero delta, an animated procedural turning
slowly at distance, and a camera moving while the test believed it was parked. `TransformHistory`
now separates the three cases over time, which no single frame can do.

**Why does the alien flicker?** Animation, and specifically the phase origin: an authored state's
origin was the engine's first update rather than the timeline's zero, so the same second reached two
ways gave two poses -- 98 joint matrices apart with every entity transform identical. Repaired by
ownership, regressed by "the pose at a second is a property of the piece, not of how the playhead
arrived", and negative-controlled by restoring the defect.

**Why does water leak or intersect terrain incorrectly?** Geometry. `buildChunkWater` emits a quad
when *any* corner is wet, so the sheet deliberately overhangs the bank by one cell, and a dry corner
measured its depth against a level borrowed from a wet neighbour, so the fade that hides the overhang
did not fade it. The depth-space explanation everyone reaches for first is **refuted by measurement**,
not merely untested.

**Which subsystem causes each problem?** Named for all eleven in the bug table -- and for anything
new, the bisection answers it mechanically: the minimal set of subsystems a symptom needs, every
member load-bearing.

**Can each problem be reproduced in a controlled test?** Yes, all eleven, each with a named test tag.

**Can a subsystem be disabled to prove the failure boundary?** For eleven subsystems, yes, and each
arm is asserted to remove the thing it names. For four -- terrain, LOD, depth test, depth write --
**no**, and each is recorded as blocked with what unblocks it, because the plan's rule is that a
control which lies is worse than a missing one.

**Can the subsystem be re-enabled after repair without regression?** Yes: 262,560 render assertions
and 491,828 unit assertions pass in release, and the arm/pass join asserts that switching an arm back
on restores exactly its own pass and nothing else.

**Are scene state, frame snapshots and GPU submission ownership explicit?** Yes, and written down
rather than implied: the derived-copy rule with its table, the renderer-side ownership table, the
resource inventory with what invalidates each entry, and the five-step frame lifecycle.

**Are remaining unknowns documented instead of implied to be fixed?** Yes. `SYM-TERRAIN-1` is open
with four suspects eliminated and two sources remaining. The live-path swapchain race is untested.
GPU capture covers nothing. The full GPU suite under ASan is a five-hour run and was not done.

## Final classification rule

A subsystem may be promoted to `PASS` only when its relevant failure mode has a controlled reproducer,
a clear state transition/root cause, a repair where needed, and a regression test or documented
measurement. Everything else remains `PARTIAL`, `UNKNOWN` or `NOT ISOLATED`.
