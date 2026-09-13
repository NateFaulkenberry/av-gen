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
- `[x]` Complete and supported by code, tests or documented measurements. **Also used for an item
  whose honest deliverable turned out to be a determination rather than a thing** -- "this control
  cannot exist and here is why", "this value has no second copy", "this comparison would be
  unvalidatable". Those are answers, and leaving them open would misreport the state of the work as
  surely as ticking something unbuilt would.
- `[!]` Blocked: it *could* exist, and doing it needs a change somewhere else first. Distinct from
  the above -- `[!]` is an item waiting on a decision, `[x]` is an item that has had one.

## Working rules

- Preserve existing user and agent work. Inspect `git status --short` before editing.
- Begin each implementation slice with one local hypothesis and one cheap discriminating check.
- Do not disable a system permanently to make a symptom disappear.
- Do not add scene-specific hacks such as `if (scene == ...)` or asset-name exceptions.
- Keep diagnostics developer-only, selective and low overhead when disabled.
- Use release for acceptance validation; use debug and sanitizers for diagnosis.
- A bug is not marked fixed until the reproducer, root cause, fix and regression evidence are recorded.
- Update this document as tasks complete. Add links to commits, tests, captures and forensic findings.
- **Do not transcribe the code into an assertion.** A check that restates a descriptor, a size or a
  constant passes for exactly as long as both copies are edited together, and fails only when someone
  edits one -- which is a change detector, not a contract. Where a property matters, assert it from
  *outside*: parse both declarations and compare them (the layout guards), or measure the behaviour
  the property is supposed to produce (the arm/pass join, the target clears). Where it does not
  matter enough to check from outside, document it and say so.
- **Every control and every diagnostic is checked against the case it is not for.** A test that only
  enables the thing it is testing passes for a control that does nothing, and a count cannot tell a
  view that colours by id from one that draws every object the same. This rule has caught more
  mistakes in this investigation than any other -- including several of its own tests.

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

**Re-measured 13 September**, same commands, same machine, after the water repair and with nothing
else running on the GPU:

| Scene | GPU median | Wall median | p10 / p90 | Draws | Triangles | Against 12 Sep |
|---|---:|---:|---|---:|---:|---|
| `glowmere-stylized.json` | 18.61 ms | 22.13 ms | 20.93 / 23.57 | 141 | 430,233 | reproduces (-1.4%) |
| `constellation.json` | 6.62 - 10.75 ms | 9.79 - 14.62 ms | 7.3-8.4 / 15.7-16.4 | 11 | 3,121 | **the median is not a statistic for this scene** |
| `renderer-qa.json` | 1.77 ms | 2.83 ms | 2.41 / 3.37 | 8 | 7,961 | reproduces (+4%) |

**Constellation's median is unusable as a baseline, and this is a measurement defect rather than a
regression.** Five runs of the identical command gave GPU medians of 6.62, 9.04, 10.62, 10.68 and
10.75 ms while `p10` and `p90` stayed put (7.3-8.4 and 15.7-16.4). The scene is *animated*: its
particle systems fill over the first seconds and its volumetrics vary with what has been emitted, so
a 120-frame window never reaches a steady state and the median lands wherever the workload happened
to be. A 400-frame run medians *lower* (9.04, `volume` 5.24) than a 120-frame one (10.75, `volume`
7.14), which is the giveaway -- a warm-up effect would go the other way.

So the 6.09 ms recorded on 12 September and the 10.7 ms typical today are the same scene measured at
different points of its own animation. **For this scene compare `p10`/`p90`, or a fixed frame index,
never the median.** Glowmere and RendererQA reproduce within a few percent and their medians are
sound.

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
| `SYM-STATIC-1` | A static object appears to move as the camera moves | Glowmere (`visitor`), RendererQA | any camera motion | **Not reproduced on any of the four axes.** 680 renderer frames, 4,488 RendererQA comparisons and 75,939 Glowmere comparisons under camera motion with time held still; a 240-frame excursion returning byte-identical; and now 1,733 comparisons across timeline seeks, a 40-step scrub, playback, six resolution changes and three scene reloads, all bit-identical. Each axis negative-controlled separately. **Likely explanation:** the `visitor` is an animated procedural that turns and hovers ~2.4 cm/1.5 s, which at 190 m with no animation cue reads as drift. |
| `SYM-ANIM-1` | A character's pose jumps when the playhead is scrubbed | `examples/characters/alien.scene.json` | any seek | **Reproduced and fixed.** Frame 500 reached from frame 100 differed from frame 500 reached directly by 98 joint matrices. Root cause: the authored animation state's phase origin was the engine's first update. See the report. |
| `SYM-ANIM-2` | The alien flickers or disappears near a frustum edge | Glowmere, alien | camera edge | **Not reproduced.** A 65-position sweep across the edge, each step rendered against a no-cull control, shows culling never removes a pixel the character would draw. Note the alien cannot discriminate bind-pose from posed bounds (a T-pose bind is wider); that property is tested separately against a rig that reaches past its bind pose. |
| `SYM-WATER-1` | Water leaks past or intersects terrain incorrectly at a shoreline | Glowmere | shoreline, grazing angles | **Reproduced and fixed.** 135 of 3,538 drawn water vertices on `defaultWorld()` stood over dry ground carrying up to 2.94 m of claimed depth against a 0.75 m shore fade -- so the fade that hides the sheet's deliberate one-cell overhang returned fully opaque. A dry corner now reports the depth at itself, which is none. Separately, a six-view GPU shoreline test proves the renderer draws no water on dry land, negative-controlled by disabling water's depth compare. Mask/foam visualisations are still not built. |
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
- `[x]` Decide which evidence is automated, manual visual review, GPU capture or performance
  measurement. Decided, and the distribution is lopsided on purpose.

  **Automated is the default and covers everything that can be stated as a claim** -- which turned
  out to be almost all of it, including several things that looked like they needed eyes: whether a
  diagnostic view is its own picture, whether a shoreline holds still, whether an id keeps naming the
  same object. The rule that made that possible is that the assertion has to be about a *measurement*
  rather than an appearance, and where an appearance was the only available instrument the answer was
  to find a different measurement, not to fall back on looking.

  **Manual visual review is used for one thing and named as such:** the QA baseline document, where a
  person decides whether a scene looks like the thing it is meant to look like. No defect in this
  investigation was found that way, and none of the regressions depend on it.

  **GPU capture (a Metal frame debugger trace) is used for nothing, and that is a real gap** rather
  than a decision -- it is the tool that would answer "what did the driver actually do", and no
  finding here needed that question. It is the first thing to reach for if a symptom ever survives
  every measurement in this document.

  **Performance measurement is kept strictly separate from correctness** and is never an assertion:
  the numbers live in Phase 0.1 and Phase 11, with their conditions, and the one place a timing was
  nearly used as evidence -- Constellation's GPU median -- is recorded as *not a stable statistic*
  rather than quietly dropped.

## Phase 1: Architecture map and state ownership

### 1.1 Trace the real frame pipeline

- `[~]` Document the actual application-to-GPU flow:
  `Application -> Engine -> Scene -> entities/components -> transforms -> animation -> visibility -> render extraction -> render queue -> GPU object data -> camera -> passes -> depth -> opaque -> terrain -> water -> transparent -> particles -> shadows -> post-processing -> output`.
  - `[x]` Existing high-level flow is documented in [architecture.md](architecture.md) and [rendering.md](rendering.md).
  - `[x]` Replace the conceptual flow with a code-level map naming the actual functions and files.
    The map is in the report; it names the functions rather than the concepts.
  - `[x]` Record ordering differences between live, headless, capture and test paths.

**The four paths, and where they differ.** All of them call the same `SceneRenderer::render(encoder,
scene, time, target, shaderInputs)`, which is why a difference between them is never in the pass
order:

| Path | Entry | Encoder | After render | Waits |
|---|---|---|---|---|
| live | `Application::runLive` | the application's, shared with the UI overlay | UI draws into the same encoder, then present | no |
| headless | `Application` headless loop | its own per frame | nothing | at submit |
| capture | `SceneRenderer::renderToImage` / `renderToImageFloat` | its own | `Finish`, `Submit`, `collectFrameTimings`, `waitForQueue`, `collectFrameTimings` again, then read the texture back | **yes** |
| test | the same `renderToImage` | same | same | yes |

Two differences are real and worth stating because both have produced confusion:

1. **The capture path blocks and the live path does not.** `renderSubmitted` finishes, submits,
   waits for the queue and collects timings twice -- deliberately, so an offline frame's CPU
   breakdown does not stop at the last pass encoded (ADR-077). A test measuring wall clock is
   therefore measuring something the live path never pays.
2. **The live path shares its encoder with the UI.** The overlay draws after the renderer into the
   same encoder, so a live frame contains commands the capture path never encodes. Nothing the
   renderer owns is affected, but a live-versus-capture *image* comparison is not comparing equals.

There is no separate "test path": tests call the capture path, which is why a bug reproducible in a
test is reproducible in an offline render by construction, and why one that only appears live is
either the UI, the shared encoder, or the fact that live frames have a real frame delta and captures
usually do not.
- `[x]` Identify every render pass, its inputs, outputs, clears, readbacks and resource ownership.
  The pass table in the report: all nine render passes with their colour targets, load/store, depth
  and what each owns, taken from the descriptors rather than from memory -- and, since `SYM-AUX-1`,
  with the auxiliary viewer on the correct side of the tone map. **No pass performs a readback in a
  live frame**; the only blocking waits in the renderer are error scopes at pipeline creation.
- `[x]` Record where culling, LOD, animation, water, shadows, particles and post effects execute
  relative to extraction and submission. Animation is posed by the scene *before* the renderer is
  called and the renderer never poses a rig. Culling of entities happens in the composition, before
  submission, and its verdict arrives on the entity. LOD is decided on the **GPU**, inside the
  procedural cull pass, after the CPU has recorded the draw -- which is why it has no honest CPU-side
  arm and why its instance counts are a readback rather than a number. Water, particles and
  transparency draw *inside* the scene pass rather than owning one, which is why their arms remove
  draws and not passes -- asserted, not asserted-about, by the arm/pass join in Phase 7. Shadows and
  post own their own passes.

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
  - `[x]` **`CompositionNode::transform` is a derived copy, not an authoritative value.**
    `Composition::applyParameters` re-derives it from `nodes/<name>/position|rotation|scale` every
    frame, so a direct write to the node field does not survive one update. Found by a negative
    control: perturbing the node field by a millimetre did **not** fail the static-transform
    regression, and perturbing the parameter did. The authoritative value for an authored node is the
    parameter; the node field is a cache of its base. (`ui::setNodePosition` already writes both, and
    says why.)
  - `[x]` Audit the remaining writers: animation, terrain grounding and sequencer evaluation.
    **None of the three writes an entity's world transform, and each has one owner.**
    *Animation* owns `SkinnedRig::{pose, palette, previousPalette}` and nothing else -- it poses
    joints, and a skinned glTF node's own transform is ignored on import as the specification
    requires, so the entity transform places the character and the joint matrices do the rest.
    *Terrain grounding* does not write a transform either: a walker's height comes from
    `EntityWorld`'s navigator, built on **the same `WorldMap` and ecology the terrain mesh was built
    from**, so there is one description of the ground rather than two kept in step -- pinned by
    `[gpu][composition][forensics]`'s "a walker's height is the ground's, every frame, with no second
    writer". *Sequencer evaluation* writes **parameters**, never scene objects, which puts it
    upstream of the derived-copy rule rather than in competition with it.
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
  - `[x]` Trace seek, reverse, pause, loop and frame-rate paths for duplicate time writes.
    One writer: `Engine::seekSeconds`, which every one of those paths funnels through -- a loop wrap
    is a seek, a reverse is a seek, `play()` from a parked playhead is a seek to the play start. It
    takes the position the **transport** decided rather than the one it was asked for (a seek past
    the end used to leave the two disagreeing), and then resynchronises everything downstream of it
    in one place: the player, the offline analysis cursor, modulation, sources, the music detector,
    cues, the entity world, the scheduled event tier, the skinning rigs' previous palettes, and the
    timeline clock *immediately* rather than on the next frame, so nothing that reads between a seek
    and the next update sees the second the playhead has left. The two defects Phase 3.4 found were
    both things missing from that list, not second writers competing with it.
- `[x]` Confirm authoritative owners for bounds, visibility, material state, object IDs, render IDs,
  GPU indices and pass state.

| Value | Authoritative owner | Derived copies, and the rule |
|---|---|---|
| Mesh bounds | `MeshData::vertices` | `Scene::meshBounds` caches per mesh, invalidated by `meshVersion`. Non-finite vertices are *dropped and counted*, never silently folded away (Phase 9.3) |
| Cull bounds | the mesh bounds and the entity transform | `scene::entityCullBounds`, one function since two copies of the rule were found drifting; recomputed per update |
| `Entity::visible` | the node's `visible` flag, through `applyParameters` | the entity copy, re-derived every update like every other derived copy |
| `Entity::cameraCulled` | whichever pass culled this frame (`cullEntityNodes`, the water path) | runtime only, never serialised, rewritten every update; "off screen" and "not in the scene" are deliberately different claims, and only the second stops a shadow |
| Material | `material/<program>/*` and `nodes/<name>/material/*` | `Entity::material`, re-derived per update; the renderer refuses a non-finite one by name and substitutes magenta |
| Object id (picking) | the entity's index | `packPickId(Entity, i)` in the object uniform and the identifier target. **Entity zero's id is `0`, the same as the cleared target** -- what makes it findable is the *material* id beside it being one-based, so the packed word is non-zero. Anything reading that target must test the word, not the low half |
| GPU object slot | the renderer, per frame | `objectIndex++` in submission order in `makeItem`. **Not stable between frames**: it is where this frame put the object, which is why the diagnostic carries it and the snapshot compares objects by *name* |
| Pass state | `SceneRenderer::PassToggles` | one owner, and since Phase 8.3 one *enumeration* -- `passArms()` -- that the CLI, the panel and the bisection all read rather than each keeping a list |

- `[x]` Record each derived copy, update timing, lifetime, thread, frame boundary and synchronization
  rule. The composition table is in 1.3 (every entry: written by `applyParameters`, read by the
  renderer and by culling, refreshed every update, lifetime one frame, main thread only); the
  renderer-side table is directly above. The one synchronisation rule worth stating separately is the
  one `SYM-TERRAIN-1` violated and which now holds: **re-rendering a frame must reproduce it.**

### 1.3 Audit duplicated state

- `[~]` Search for duplicated position, rotation, scale, world matrix, camera position/orientation, bounds, visibility and animation time.
  - `[x]` **Culling bounds were computed twice.** `Composition::cullEntityNodes` contained the posed
    box, the conservative pad and the world-AABB corner transform written out in full, once for
    EntityWorld-driven characters and once for authored mesh nodes. Two copies of a rule is two
    places for it to drift, and neither copy was reachable by a test. Extracted to
    `scene::entityCullBounds` (`src/scene/scene.hpp`), both call sites replaced, and the property is
    now unit-tested directly. Full release suite unchanged at 1,681 passing.
  - `[x]` **The search is done for the composition path, and the answer is one rule rather than a
    list of special cases.** `Composition::applyParameters` rebuilds *every* scene object from a
    parameter plus the node's authored `*Rest` snapshot on every update. Two members of this family
    were found by accident and each cost an investigation; the rest were found by reading the one
    function that owns them.
- `[x]` Search for duplicated material, object ID, render ID, GPU index, buffer offset and generation
  values. The answer is short, and the interesting part is what is *absent*.

  **Material** is duplicated exactly once, by the derived-copy rule, and is additionally fingerprinted
  (`materialHash`) in the diagnostic -- deliberately a hash rather than a copy of every field, because
  the question a diff asks is "is this the same surface" and one number answers it without the
  snapshot growing a second material. **Object id** exists twice by construction and the two are
  different numbers: the pick id in the identifier target, and the GPU object slot. That is not a
  duplicate to collapse -- one is identity and the other is location -- but it is a trap, and it is
  why the `Ids` view and the object-depth view key on different things and had to be reconciled.
  **Buffer offset** is the slot times a fixed stride, computed in one place. **There is no generation
  counter anywhere**, which is the finding rather than an omission: the renderer's reuse boundary is
  the owning `Scene` pointer plus a local version, not a per-frame generation, so there is nothing to
  duplicate and nothing to keep in step -- and the scene-swap defects this investigation fixed were
  all cases of that boundary being keyed on too little.
- `[x]` Build a table for each duplicate:
  `value | authoritative source | derived copies | writer | reader | update timing | lifetime | thread | GPU sync risk`.
  The composition half is below; the renderer half is the table in 1.2. Every row on both is written
  on the main thread, refreshed every update, and lives one frame. **The GPU sync risk column is one
  sentence for the whole table:** nothing in it is read by the GPU across a frame boundary, because
  every value is rewritten before the frame that reads it -- with one exception, which is
  `SYM-TERRAIN-1`, now fixed: ambient occlusion's history was valid on the first render of a frame
  and dropped on the second, so a repeat was not a repeat.

**Derived copies on the composition path.** All of them: written by `Composition::applyParameters`,
read by the renderer and by culling, refreshed every update, lifetime one frame, main thread only.

| Value | Authoritative source | Derived copy |
|---|---|---|
| Node TRS | `nodes/<name>/position\|rotation\|scale` | `CompositionNode::transform`, then `Entity::transform` |
| Camera pose | `camera/mode`, `camera/position`, `camera/target` (or the orbit block) | `Scene::camera` |
| Light colour/intensity/range | `nodes/<name>/light/*` | `Scene::lights[i]` |
| Material tint/emissive/roughness/opacity | `material/<program>/*`, `nodes/<name>/material/*` | `Entity::material`, `ProceduralGeometry::material` |
| Procedural parameters | `nodes/<name>/procedural/*` + `node.proceduralRest` | `Scene::procedurals[i]` and its sub-objects |
| Spline parameters | `nodes/<name>/spline/*` + `node.splineRest` | `Scene::splines.splines[i]` |
| SDF parameters | `nodes/<name>/sdf/*` + `node.sdfRest` | `Scene::sdfs[i]` |
| Field parameters | `nodes/<name>/field/*` + `node.fieldRest` | `Scene::fields.fields[i]` |
| Particle system | `nodes/<name>/particles/*` + `node.particleRest` | `Scene::particles[i]` |

**The rule, stated once:** *the parameter is authoritative, the node's `*Rest` struct is the authored
baseline, and the object hanging off `Scene` is a per-frame derivation of the two.* Writing to a
`Scene` object directly is a write that does not survive one update. Pinned by
`tests/unit/test_composition.cpp`, `[scene][composition][forensics][derived]`, negative-controlled by
removing the re-derivation.

**A second trap in the same area, recorded because it has now caught three tests:** `setBase` alone
does not reach `applyParameters`, which reads the *final* value. The engine refreshes finals every
frame in its modulation pass; a composition updated on its own does not, so a test must call
`ParameterSet::resetFinals()` or its setup silently does nothing.
- `[x]` Explicitly audit the chain `scene transform -> render transform -> GPU transform ->
  camera-relative transform -> shader transform`. Walked end to end and asserted, not read:
  `[gpu][renderer][forensics][lifetime3_4]` follows world → camera-relative → clip → NDC → screen
  from the renderer's own diagnostic matrices and finishes **at the pixels**, with each object's
  footprint measured by hiding it and differencing rather than classified by colour. The
  camera-relative link in that chain is the view matrix's single subtraction and nothing else, which
  is asserted as such: orthonormal basis, determinant +1, the eye exactly at the view-space origin,
  and `basis * (world - eye)` equal to `view * world`. Negative-controlled by injecting a second
  subtraction, which fails exactly that stage, and by a 2 mm-per-frame accumulating creep -- the
  version that drifts rather than jumps, which a single-frame check cannot see -- which fails all six
  cases.
- `[x]` Identify any path where camera-relative conversion can be written back into authoritative
  scene state. **There is no such path and no such conversion** (Phase 3.2). The renderer takes
  `const scene::Scene&` throughout, so the type system carries the claim rather than a convention
  doing it.

### 1.4 Establish and enforce invariants

- `[x]` Document the invariant that scene world transforms remain authoritative and renderer-derived transforms are temporary.
- `[x]` Add development assertions that culling and rendering never mutate authoritative transforms.
  **Rendering needs no assertion: it cannot.** Every `SceneRenderer` entry point -- `render`,
  `renderFrame`, `renderToImage` -- takes `const scene::Scene&`, and the only `const_cast` anywhere
  under `src/rendering` is on the renderer's own LOD bookkeeping
  (`procedural_renderer.cpp`, `Impl::ObjectState::emptyFrames`), not on scene state. The invariant is
  held by the type system, which is stronger than a runtime check and free.
  Culling is the half that does write to the scene, and it is tested rather than asserted:
  `tests/unit/test_composition.cpp`, `[scene][composition][forensics][culling]` -- across four camera
  poses including one that rejects everything, every entity's transform and *authored* `visible` flag
  are unchanged while `cameraCulled` moves. Negative-controlled by making a cull clear `visible`,
  which fails it.
- `[x]` Add finite-value validation for transforms, matrices, bounds, camera state, materials and GPU
  upload structures. Six guards, and the reason there are six rather than one is the finding behind
  the whole of Phase 9.3: **`glm::min`/`glm::max` and `std::clamp` are comparisons, and a NaN loses
  every comparison**, so it is silently *discarded* rather than propagated. A single `isfinite` at
  the end of a pipeline cannot catch what a fold at the start threw away. The guards are at the folds:
  `entityCullBounds`, `MeshData::bounds`, `fitDirectionalCascade`, `packLight`, the material-to-
  `ObjectUniforms` packing, and now `waterUniformsFrom`.
  - `[x]` Camera/entity matrix and skinning palette validation exists.
  - `[x]` Complete joint, bounds, material and water validation coverage. Joints and bounds were
    already guarded; materials and water are the two this phase closed. **Water was the one that
    mattered most and looked safest.** Its packing is full of `std::max(x, 1e-3f)` and
    `std::clamp(x, 0.02f, 1.0f)` floors that read exactly like guards -- and `std::max(NaN, 1e-3f)`
    is NaN, so none of them were. It is also the surface where a single bad value does the most
    damage, because water shades a whole region rather than one object. The surface is refused
    *whole* rather than field by field: a water surface with one arbitrary field replaced is a
    surface nobody authored, and "the water looks wrong" is a harder report to act on than "the water
    is missing and the log says why". Twenty-eight fields plus the two frame-supplied values are
    poisoned one at a time, with a healthy surface as the control that says the guard is not simply
    refusing everything.
- `[~]` Document and test camera-relative origin ownership across entities, terrain, water and particles.
  The renderer audit found no camera-relative conversion or authoritative scene-transform write under
  `src/rendering`; the static-camera regression confirms authored entity TRS survives camera motion.
  Explicit origin ownership for terrain, water and particles is still open because no shared origin
  contract is currently documented.

## Phase 2: Immutable frame boundary and reference renderer

### 2.1 Design the render snapshot contract

The contract exists, in a different shape from the one this phase imagined, and the difference is
worth stating plainly: **the renderer consumes the `Scene` itself, by const reference, and records a
snapshot as it goes.** There is no separate extraction step that builds an immutable copy for the
renderer to draw from.

That answers the same questions more cheaply. Immutability is enforced by the type system rather than
by copying -- all three `SceneRenderer` entry points take `const scene::Scene&`, and the only
`const_cast` under `src/rendering` is on the renderer's own LOD bookkeeping -- and the per-frame
record is `RendererDiagnosticFrame`, which is written during the same walk that submits the draws, so
it cannot describe a different frame from the one that was drawn.

- `[x]` Define an explicit per-frame render snapshot or equivalent immutable contract.
  `RendererDiagnosticFrame` + `rendering::FrameSnapshot`, serialisable and diffable (Phase 9.1).
- `[x]` Ensure each renderable carries, directly or through stable references:
  - `[x]` object/entity ID -- `name` and `entityIndex`;
  - `[x]` mesh and material IDs -- `mesh`, and `materialHash`, a fingerprint of the surface rather
    than a copy of it: the question a comparison asks is "is this the same material", and one number
    answers it without the snapshot growing a copy of every material field;
  - `[x]` world position, rotation and scale -- carried as the `worldMatrix`, which is what the GPU
    receives, plus `worldPosition`;
  - `[x]` world/model matrix;
  - `[x]` world bounds, and the six signed frustum margins;
  - `[x]` visibility/cull reason -- `visible`, `cameraCulled`, `cullReason`, `submitted`;
  - `[x]` animation/skin state -- `rigIndex`, `jointCount`, `paletteVersion`, `paletteTime`;
  - `[x]` GPU slot/index metadata -- `objectSlot`.
- `[x]` Define which values are copied at extraction and which are resolved by the renderer.
  Everything in the snapshot is *copied at submission*, by value, at the moment the object's uniforms
  are written. Nothing in it is a reference into the scene, which is what lets a capture outlive the
  frame and be compared against one from another process.
- `[x]` Ensure the render frame consumes the snapshot without mutating scene state. By the type
  system; see above.
- `[x]` Add unit coverage for snapshot stability and repeated extraction.
  `[gpu][composition][forensics][snapshot]`: the same state captured twice reports no difference, a
  capture survives a round trip through a file, and each kind of change -- moved, culled, resubmitted,
  slot-swapped, mesh-swapped, material-swapped, gone, new, camera, arms -- is reported as itself.

### 2.2 Build the minimal reference renderer

- `[x]` Create a developer-only `ReferenceRenderer` path. `src/rendering/reference_renderer.{hpp,cpp}`
  with `shaders/reference.wgsl`. It is not a fallback, a quality tier or a preview, and the header
  says so: it draws flat colour and will never look like the picture.
- `[x]` Support only basic opaque mesh, solid material, depth and explicit camera matrices.
- `[x]` Keep the path free of culling, LOD, animation, water, transparency, particles, shadows, post
  FX, batching and temporal history. **Nothing it does not have can explain a difference** -- which is
  the only property that makes it useful as a second opinion. What it declines is *counted* and
  reported (`Counts`), because a scene of water and particles renders as nothing here, and a caller
  comparing against an empty frame would conclude the transforms agree.
- `[x]` Implement the explicit path: authoritative world transform → explicit view and projection →
  MVP → flat material → depth → draw.
- `[x]` Give the reference path deterministic object ordering and stable resource lifetime. Objects
  draw in scene order with no sorting, so the order is a property of the scene and not of a frame;
  meshes are uploaded per call rather than cached, deliberately -- a second renderer that shared the
  production caches could not be evidence about them.
- `[x]` Add a direct comparison harness for the same simple scene through both renderers.
  `[gpu][composition][forensics][reference]`, five camera views of `renderer-qa-minimal`, production
  stripped to a comparable arm set.
- `[~]` Compare transforms, visibility, object IDs, depth, geometry, material inputs and image hashes.
  **Transforms, visibility and geometry: yes, and per object.** Whole-frame coverage agreeing is a
  weaker claim than it looks -- two objects could swap places, or one be drawn twice and another not
  at all, and the union of the silhouettes would be unchanged. So each entity's own footprint is
  measured the only way that does not require the two renderers to agree about shading (hide it,
  difference the frames) and the footprints are compared to each other: same centroid to within 4 px,
  same area to within the difference two shading models make at an edge. A transform error is not a
  few pixels; it puts the object somewhere else. The instrument is required to tell two objects apart
  before any of that counts, or a footprint measure returning the whole frame would satisfy every
  check.

  **Object IDs, depth and material inputs: no, and they cannot be.** The reference path writes no
  identifier target, no linear depth and no material beyond a flat colour -- by design, since each is
  a thing that could explain a difference. Comparing them would mean building them, at which point it
  is no longer a minimal renderer. **Image hashes: deliberately not.** The two shade differently, so
  a hash comparison compares tone maps; coverage is the comparison that is about geometry.

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

- `[x]` Instrument camera world position and rotation. Position directly; rotation as the view
  matrix, which is what everything downstream consumes and what a capture can be compared on.
- `[~]` Instrument view, projection, view-projection, inverse-view and inverse-projection matrices.
  The first three are in the diagnostic frame and in a capture. The inverses are not recorded: they
  are derived on demand where they are needed (picking, the shadow fit), and storing a second copy of
  a value that is computed from a stored one is the duplicate-state pattern this investigation spent
  Phase 1.3 cataloguing.
- `[x]` Instrument near plane, far plane, aspect ratio, viewport width and viewport height.
  Recorded alongside the matrices and carried in a capture, and the *reason* is the difference
  between a useful diff and a useless one: a projection that changed because the window was resized
  and one that changed because the lens moved are the same sixteen numbers to a matrix comparison.
  `compareSnapshots` now names which input moved -- "the projection differs: field of view 0.7330 ->
  0.3665" -- and the field of view it records is the *effective* one, since the lens decides it
  unless `camera/lens/useExplicitFov` is set.
- `[x]` Verify multiplication order, handedness, forward direction, up axis, clip-space range and depth convention.
  Pinned in `tests/unit/test_camera.cpp`, `[scene][camera][forensics]`: handedness, view-space origin,
  0..1 depth in the conventional direction, aspect behaviour, finiteness, and the parallel
  forward/up fallback.
- `[x]` Verify that culling, shading, depth reconstruction, shadows, volumetrics, picking and overlays consume the same authoritative camera model.
  Established by the audit above: every one of them calls `Camera::view()` and `Camera::projection()`.
  Shadow views are the intended exception -- they are light views, built in `shadow_math.cpp`.
- `[x]` Add camera basis and frustum visualizations to the diagnostics path.
  `DebugViewOptions::frustum` (Phase 4.3): twelve edges through the inverse of the matrix the camera
  would draw with, plus the basis, at the aspect the frame is actually rendering at. It earns its
  place beside the view freeze, where it is the volume the cull actually used.

### 3.2 Camera-relative rendering audit

**There is no camera-relative rendering in this engine.** The audit found no conversion anywhere
under `src/rendering`: the view matrix is built from the camera in world space and world positions go
into it unmodified. Several items below therefore have nothing to verify, and are ticked as *audited
and absent* rather than left open -- an open box implying a subsystem that does not exist is its own
kind of wrong report. What *is* built is the regression that would catch one being introduced badly,
because the failure mode (a double subtraction) is silent and looks like a camera bug.

- `[x]` Identify every camera-relative conversion and its exact execution stage. There are none. The
  single subtraction that exists is the view matrix's own, and it is asserted as such: the view basis
  is orthonormal with determinant +1, the eye lands exactly on the view-space origin, and
  `basis * (world - eye)` equals `view * world`.
- `[x]` Verify scene state is never mutated by camera-relative conversion. Nothing converts, and the
  renderer takes `const scene::Scene&` throughout, so the type system carries the claim.
- `[x]` Verify camera-relative origin is shared consistently by entities, bounds, terrain, water and
  particles. There is one origin -- the world's -- shared by construction.
- `[x]` Add a regression that detects double subtraction across consecutive frames.
  `[gpu][renderer][forensics][lifetime3_4]`, and negative-controlled twice: injecting
  `translate(-camera.position) * view` fails exactly the camera-relative stage (the eye 9.34 units
  off the view origin), and a *per-renderer accumulating* creep of 2 mm a frame -- the version that
  drifts rather than jumps, and the one a single-frame check cannot see -- fails all six cases.
  380 frames over six laps of a closed camera path with a 10 km excursion inserted mid-run require
  the view and view-projection matrices to be **bit-identical** on every lap.
- `[x]` Add projected-position checks for world, camera-relative, clip and screen coordinates. The
  whole chain, ending at pixels: each object's footprint is *measured* by hiding that entity and
  differencing the two renders, then checked against the predicted centroid and against the
  screen-space box its own world bounds project to. Measured rather than classified by colour,
  because the first draft classified by hue and read a grey sphere lit by a blue environment as the
  blue one -- a "measured" centroid 28 px from its prediction. The drawn centroid repeats across six
  laps to within 0.015 px.

### 3.3 GPU object and buffer audit

- `[x]` Track entity ID, render-object ID, GPU object index, buffer offset, frame index and buffer
  generation for every submitted object. All of it is in `RenderObjectDiagnostic` and in a capture:
  entity index, pick id (derivable from it), object slot, **buffer offset** and the frame index on
  the frame around them. `bufferOffset` is the slot times the stride and is recorded anyway, because
  a capture read by a person should not require them to know the stride -- and because the slot is
  submission order and *not* stable between frames, so an offset that looks familiar across two
  captures is a coincidence worth being able to see. **Buffer generation is absent because there is
  no such counter** (Phase 1.3): the reuse boundary is the owning `Scene` plus a local version.
- `[x]` Add debug object-ID coloring with stable IDs for the test geometry. The `Ids` view is
  asserted to be its own picture and to separate objects into distinct values; `[gpu][composition]
  [forensics][ids]` asserts the property that actually matters, which is not that ids *exist* but
  that **a given id keeps naming the same object** while the camera orbits, the timeline runs and
  objects enter and leave the frame. An id that silently re-pointed would give a stable-looking
  picture and a wrong selection -- which this numbering had once, when a click on a scattered tree
  resolved as whichever entity shared its index.

  Both halves of the word are cross-examined against each other: the low sixteen bits are the pick
  id and the high sixteen the material id, derived from the same index by *different* arithmetic, so
  requiring `index == material - 1` is a check on the packing rather than a restatement of it. Ids
  are resolved through the space tag, and the test found its own first assumption wrong -- it began
  by assuming every id was an entity and the QA scene's procedurals failed it immediately, which is
  the tag doing the job it was added for.

  **Water is asserted as the exception rather than left to be rediscovered:** its pipeline masks
  every scene target but colour and emission, so it writes no identifier, and every water entity's
  id is required to be absent from everything the frame wrote.
- `[x]` Audit uniform/storage buffers, dynamic offsets, ring buffers, staging buffers, bind groups,
  views and frame allocators. Enumerated below with the one column that turns an inventory into an
  audit: **what invalidates it.** A list of resources is a transcription; a list of invalidation
  rules is a thing that can be wrong.

| Resource | Kind | Written | Invalidated by |
|---|---|---|---|
| `frameUniforms_` | uniform, one record | once per frame | nothing; rewritten whole every frame |
| `objectUniforms_` | uniform, dynamic offset `slot * kObjectStride` | once per frame, staged then one write | the slot count; stride is `static_assert`ed and `% 256`-checked |
| `lightBuffer_`, `clusterBuffer_`, `clusterParams_` | storage | once per frame from the scene's lights | nothing; the froxel build is a pure function of this frame |
| skinning joint buffer | storage, dynamic offset per rig | only for rigs whose `paletteVersion` moved | the owning `Scene`, the rig count, the joint count -- and, since Phase 4.5, held deliberately by the freeze arm |
| `normalRough_`, `velocity_`, `emission_`, `ids_`, `linearDepth_` | render targets | every frame, cleared first | resize; all five are recreated together so a stale extent cannot survive in one of them |
| `auxDebugUniforms_`, `tonemapUniforms_` | uniform | only while their pass runs | the target *format*, which is why both pipelines are per-format maps |
| mesh vertex/index/skin buffers | storage | on upload | the owning `Scene` **plus** the local id and version -- the three-part key that fixed the cross-scene collisions |
| `whiteSrgb_`, `flatNormal_`, `blackCube_`, `blackLut_`, `linearDepthDefault_` | 1x1 stand-ins | at init | never; they exist so a missing binding is a defined colour rather than a validation error |
| readback staging | transient | per readback | mapped with `WaitAny` and destroyed; **offline and test paths only**, never a live frame |

  Three findings rather than a list. **There is no frame allocator and no ring buffer in the
  renderer** -- the readback ring belongs to the frame timeline, which measures rather than draws.
  **Every dynamic offset is a slot times a compile-time stride**, both checked against 256 and
  against the record size by the layout guards. And **the only resources keyed on more than their own
  identity are the ones that had the collision defect**, which is the audit's most useful output: the
  three-part key is the repair, and everything else is single-owner by construction.
  - `[~]` Object uniform slot stride and capacity now have compile-time guards, and focused GPU
    diagnostics verify stable object-slot assignment -- including under alternating transforms and a
    coming-and-going third object, where a stale or swapped slot would show. Resource *reuse* across
    resize, reload, seek and scene swap is now covered by Phase 3.4's interleaved sequence with the
    device error count asserted zero throughout. Full pass bind-state auditing remains open, and is
    the transcription-shaped item this phase has deliberately not done.
- `[x]` Verify CPU/WGSL structure size, alignment, offsets, padding, type widths and matrix layout.
  `tests/unit/test_renderer_layout_guards.cpp`, `[unit][renderer][forensics][layout]`. Rather than
  restating sizes in a second place, it *parses both declarations* -- a WGSL layout engine (uniform
  address space: vec3 aligns to 16, matrix column stride, array element stride rounded to 16) and a
  C++ one (Itanium ABI, glm's real alignments, array extents resolved against constants scraped from
  the headers) -- flattens both to leaf scalars at absolute offsets and compares offset, width and
  type class, then the member names in order. **21 structure pairs agree.** Nine dynamic-offset
  strides are checked for `% 256` and against their record size.

  Its controls are the argument for the approach: adding a `vec4` to the WGSL `WaterUniforms` is
  caught while the existing `static_assert(sizeof == 192)` still passes, and swapping two same-typed
  members is invisible to every byte-level check and caught only by the name comparison.

  Left open deliberately: `GpuLight`, `ShadowUniforms`/`ShadowViewGpu` and `WindUniforms` report
  `alignof == 4` because `glm::vec4` is not over-aligned in this build. Harmless today -- all are
  16-byte-multiple sized and uploaded at 0 or a 256-multiple -- and the fix is `alignas(16)` on each,
  matching what `spatial::FieldGpu` already does.
- `[x]` Add an alternating-transform two-object test to detect stale or swapped GPU data.
  `tests/rendering/test_gpu.cpp`, `[gpu][renderer][forensics][objects]`, 399 assertions: two cubes of
  different sizes exchange places for 24 frames, then a third comes and goes for 12 more so a
  departing object's slot is inherited. Every frame asserts each object's *diagnostic* world matrix
  and position are its own, this frame, and that no two objects share a slot. Negative-controlled by
  forcing the slot to 0, which fails on the first frame.

  **Recorded because it was tried first and is wrong:** comparing *pixels* against a fresh renderer
  cannot answer this. Two cubes exchanging places is motion, and a running renderer carries
  previous-frame matrices, an AO history and an adapting exposure that a cold one does not -- 22 of
  24 frames differed with the object data perfectly correct. Those subsystems have their own
  coverage; the question here is answered by the object state, not by the frame.
- `[x]` Fix mesh, texture and environment/IBL upload identity/version collisions by keying renderer
  caches to the owning `Scene` as well as local IDs/versions. GPU regressions cover distinct
  same-version geometry and HDR data through one renderer, both matching fresh-renderer results.
  The broader buffer-layout audit remains open.

### 3.4 Frame synchronization and resource lifetime

`tests/rendering/test_resource_lifetime_gpu.cpp`, `[gpu][renderer][forensics][lifetime3_4]`: six
cases, 4,345 assertions. An interleaved sequence of resizes (seven awkward sizes), camera cuts,
forward and backward seeks, repeated draws of one `FrameTime` and reload/scene-swap across the QA
set -- around 190 transitions with 14 checkpoints against a fresh engine *and* a fresh renderer --
plus a descending-then-ascending timeline sweep, and the camera-relative chain of Phase 3.2. Four
negative controls in `scene_renderer.cpp`, applied and removed: a double subtraction fails exactly
the camera-relative stage; per-renderer origin creep of 2 mm a frame fails all six cases; a draw
matrix diverging from the recorded diagnostic fails *only* the two pixel-stage checks, which is the
discrimination the screen stage exists for.

**It found two defects, both now fixed, and both the same mistake:** a piece of state whose name says
"a frame ago" while nothing resynchronised it across a jump. The particle pools survived a seek
(`resetTemporalHistory` reset the AO history and not them, and `ParticleRenderer::resetAll` had said
in its own comment since it was written that it is used on seek restarts, with no caller). And
`SkinnedRig::previousPalette` survived a seek, so the first frame after every scrub carried joint
motion vectors for a jump nobody made -- 49 of 49 joints, worst element 71.5 units on a ~100-unit
character, which motion blur duly drew. It also found `SYM-TERRAIN-1`, since fixed: see the report.


- `[x]` Document when CPU state updates, GPU data is written, GPU consumes it, GPU finishes and
  memory is reused. One frame, in order:

  1. **CPU state updates.** `Engine::update` poses rigs, walks entities, re-derives every scene
     object from its parameters, and culls against the camera. The renderer is not involved and the
     scene is not yet read by it.
  2. **GPU data is written.** `SceneRenderer::render` stages and writes: frame uniforms, lights and
     froxels, one object slot per drawable, skinning palettes for rigs whose version moved. All
     `queue.WriteBuffer`, all before any pass is encoded.
  3. **GPU consumes it.** The passes are encoded and submitted as one command buffer.
  4. **GPU finishes.** Nothing waits for it in a live frame. The engine does not read back, and the
     only blocking waits are error scopes at pipeline creation.
  5. **Memory is reused.** Next frame, by overwriting: every per-frame buffer is rewritten whole
     before it is read again, which is what makes step 4's not-waiting safe. Resources that outlive a
     frame -- meshes, textures, palettes -- are keyed on the owning `Scene` and are dropped when it
     goes.

  Stating the sequence is what made `SYM-TERRAIN-1` legible: ambient occlusion's history is carried
  between one frame's step 5 and the next frame's step 1, and re-rendering a frame re-entered the
  sequence without restoring it. With that fixed, **a frame is a pure function of steps 1 and 2**,
  which is what every repeated-render comparison in this document now asserts.
- `[x]` Audit textures, buffers, bind groups, pipelines, materials, meshes, animation buffers, depth
  textures, water textures and post-process targets. The table above is the buffer and texture half.
  **Bind groups** are cached per resource identity and rebuilt when the view they name changes -- the
  auxiliary debug group is the one that had to learn this, since it caches five target views and a
  resize replaces all of them. **Pipelines** are created once, except the two that are per target
  format (tonemap, aux debug) and are therefore maps rather than handles. **Materials** are not GPU
  resources at all: they are packed into the object slot each frame, which is why their guard is in
  the packing rather than on an upload. **Water textures** are the scene targets shared with
  everything else -- water owns no target of its own, which is the same fact that makes the water
  mask undrawable and every water measurement an A/B.
- `[x]` `FrameTimeline` uses a four-slot non-stalling resolve/map ring and waits for in-flight maps
  during destruction. A 32-frame GPU stress regression proves sustained slot reuse, readback
  completion and zero timeline overflow. The broader resource inventory is now written out above --
  and its most useful output is that **this ring is the only one in the renderer**: the frame
  timeline measures rather than draws, so nothing a frame's picture depends on is ring-buffered.
- `[~]` Repeated render-target replacement is covered by an eight-size alternating GPU regression and
  by Phase 3.4's interleaved walk through seven awkward sizes with the device error count asserted
  zero every round; a renderer taken through other sizes must come back drawing what a renderer born
  at that size draws, with a third fresh renderer agreeing with both. The texture and buffer
  inventory is written out above. **Live-path asynchronous replacement remains genuinely open**: every
  test here goes through `renderToImage`, not the swapchain, so a surface reconfigure racing a frame
  is untested.
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
- `[x]` Stress resource reuse through resize, scene reload, timeline seek/reverse, camera cuts and
  frame-index reuse. The interleaved sequence above does all five together rather than one at a time,
  which is the point: each is already covered alone, and the defects it found were only reachable by
  a walk that crossed them. Its one exclusion is stated and reasoned -- the terrain scene, because
  it was excluded from its checkpoints while `SYM-TERRAIN-1` stood, and is checkpointed again now
  that it is fixed.
  Resize now clears previous view/model history in addition to AO history, with a motion-blur
  reused-versus-fresh renderer regression. An explicit reset API now covers in-place scene reloads
  and the application camera-cut action. Timeline seek/reverse stress and frame-index reuse remain
  open.
  - `[x]` Repeated frame indices are deterministic: the renderer drops AO history on a non-advancing
    index, and a same-index replay with motion blur matches a fresh renderer.
  - `[x]` Added a seek-only transport discontinuity revision and wired both live/headless render
    loops to reset temporal state when it changes, including forward seeks whose render time rises.
  - `[x]` Run longer application-level seek/scrub/reload sequences and capture their frame hashes.
    Eighteen rounds, ~190 transitions, 14 hash checkpoints against a fresh engine and renderer, with
    `errorCount()` asserted zero every round; plus a thirteen-point descending sweep, the same points
    ascending, and eight repeats of one frame index interleaved with a differently-sized draw.
- `[x]` Add a conservative synchronization option to the reference path if evidence points to reuse
  hazards. **The evidence points elsewhere, so no option was added.** The reference path has no reuse
  to be hazardous: it uploads every mesh per call and caches nothing, deliberately, so a
  synchronisation knob there would protect state it does not keep. The hazard the investigation did
  find -- `SYM-TERRAIN-1` -- was not a reuse hazard either, and not a synchronisation problem at
  all: it was a temporal-history guard that treated a repeated frame like a discontinuity. A
  conservative synchronisation option would have made the symptom no better and the design worse.
- `[x]` Add validation for resources destroyed, replaced, resized or rebound while still referenced.
  **Dawn's own validation is the check, and it is asserted rather than assumed**: `errorCount() == 0`
  after every resize, reload and scene swap in the interleaved sequence, and after every auxiliary
  view at every size. That is precisely the class of error a use-after-free on a GPU resource
  produces, and it is stronger than an engine-side assertion because it is the driver's opinion
  rather than ours. An *additional* engine-side lifetime assertion was considered and declined: it
  would duplicate a check the device already performs, and nothing in this investigation has been
  blocked by not having one. The use-after-free that *was* found (detached composition parameters)
  was CPU-side and found by ASan, which is the right tool for that half.
- `[x]` Sanitizer coverage exists for selected animation/sequence paths; expand it to renderer
  resource lifetime and the relevant suites. ASan/UBSan over the GPU paths this work changed --
  guards, target contracts, identifiers, ordering, bisection, the diagnostic views and the debug
  overlays -- **20,501 assertions across 9 cases, no findings**, with the new material guard's own
  message visible in the log, which is what says the guard path was actually walked under the
  sanitizer rather than merely compiled.

  **The full GPU suite under ASan is not practical and the number is worth recording:** it completes
  roughly six cases an hour, so the whole tag is a five-hour run. Targeted coverage of the changed
  paths is the trade, and stating it is the point -- an "ASan clean" claim that quietly meant a
  tenth of the suite would be the kind of reassuring, wrong instrument this investigation exists to
  remove.

## Phase 4: Renderer Forensics developer mode

### 4.1 Mode and panel

- `[x]` Add a developer-only `Renderer Forensics` mode that does not alter production scene behavior.
  It is a set of renderer arms rather than a mode: nothing about the scene changes, and with every
  arm on the frame is the production frame. That is the property that matters -- a "mode" that drew
  differently would be a second renderer to keep correct.
- `[~]` Add a panel for isolation toggles, diagnostic views, selected-object inspection, frame capture and snapshot replay.
  Isolation toggles and selected-object inspection are in the Performance panel's "Renderer
  forensics" section, with a loud line while any arm is off -- a frame with a subsystem removed is
  not a frame to judge the renderer by, and the panel is not always on screen. Frame capture and
  snapshot replay wait on Phase 9.1. Diagnostic *views* are the existing debug-draw options in the
  World window.
- `[x]` Selected-object transform diagnostics are available through `SceneRenderer`'s last-frame
  snapshot and the existing World-panel selection. The Performance panel shows world/camera state,
  model matrix, culling/submission state and GPU object slot; change-only logging is enabled.
- `[x]` Make all controls truthful: every enabled control must isolate or visualize a real path.
  Each arm is asserted to remove the thing it names, and the two that cannot be implemented honestly
  -- terrain and LOD -- are absent rather than inert. See Phase 4.2.
- `[x]` Record toggle state in captured frame metadata. `FrameSnapshot::toggles`, and a comparison
  reports an arm difference before anything else, because a comparison across different arms is not
  a comparison.

### 4.2 Core and feature isolation controls

`SceneRenderer::PassToggles` is the one place these live, reachable from the CLI as
`--disable <list>` so the same arms drive a headless A/B. Each is asserted to remove the thing it
names in `tests/rendering/test_composition_gpu.cpp`, `[gpu][composition][forensics][isolation]` --
the plan's rule is that a control that does nothing is worse than a missing one, because somebody
turns it off, the symptom stays, and a subsystem is wrongly cleared.

- `[x]` Minimal rendering path. `ReferenceRenderer` (Phase 2.2). It is not an arm on this list,
  deliberately: the others remove one thing from *the* renderer, while this is a second renderer
  entirely, so it is reached by constructing it rather than by a flag. Compared against production
  per object, on coverage rather than colour.
- `[x]` Normal rendering path. The default, and the other arm of every A/B below.
- `[x]` Disable all culling. Draws everything whatever the cull decided; the draw count rises.
- `[!]` Disable LOD. Blocked, not refused: LOD lives in the procedural renderer's GPU cull pass
  rather than in the pass list, so an honest control is a change to that pass and not a flag here.
- `[x]` Disable animation. Skinned meshes draw in bind pose; the palettes are not uploaded at all.
- `[x]` Disable water.
- `[!]` Disable terrain. Blocked on a flag, and the reason is worth keeping: a terrain chunk arrives
  at the renderer as an ordinary lit entity with nothing saying where it came from, so the only
  available implementation is a name-prefix guess that lies at the first scene naming something
  `chunk`. The plan's own rule -- a control that lies is worse than a missing one -- is what keeps
  this unbuilt rather than what merely delays it.
- `[x]` Disable transparency.
- `[x]` Disable shadows. (Pre-existing.)
- `[x]` Disable particles. No simulation and no draw, so switching it back on does not reveal a
  system that has been running invisibly; the frame's particle stats read zero rather than last
  frame's.
- `[x]` Disable post FX. (Pre-existing.)
- `[x]` Disable VFX. **Resolved: no subsystem by that name exists.** Particles, volumetrics and post
  are the three things it would mean, and each already has its own arm -- so the useful answer was to
  find out that the question names a category rather than a system, not to add a fourth control that
  switches off the other three together and hides which one mattered.
- `[x]` Freeze animation. `animationMotion`: the palettes already on the GPU are held, so a character
  stops moving in place rather than snapping to a bind pose. See Phase 4.5.
- `[~]` Freeze camera. What is built is a **view and projection freeze**: the matrices the scene pass
  draws with are held while the camera moves, and the stale cull verdicts from the moved camera are
  ignored so the frozen view is self-consistent. The sky, the volumetrics and the particle systems
  read the live camera themselves, so the *frame* still changes -- the test asserts the matrices,
  which is what the control actually holds. Freezing those three is a separate control and is not
  built.
- `[x]` Freeze projection. Held by the same control.
- `[x]` Freeze view matrix. Held by the same control.
- `[x]` Freeze camera-relative origin. **Resolved: there is no camera-relative origin to freeze.**
  The Phase 3.2 audit found no camera-relative conversion anywhere under `src/rendering`, and the
  regression that would catch one being introduced badly exists anyway, because the failure mode is
  silent and looks like a camera bug.

### 4.3 Transform and geometry controls

The overlays live in `DebugViewOptions` (World window ▸ Debug) and are built by the pure
`buildDebugGeometry`, so each is checked without a device in `tests/rendering/test_debug_draw.cpp`,
`[debug][forensics]`. Every one is asserted against the case it is **not** for -- the world axes
against the selected entity's axes, the submitted filter against a cull in both directions, the id
colouring against plain bounds (a count cannot tell those two apart), the frustum against a moved
camera and a changed aspect. The first version of the frustum assertion indexed the wrong vertex and
failed, which is the control working.

- `[!]` Freeze all transforms. Not built. The renderer is handed a `Scene` whose transforms were
  derived upstream, so an honest freeze means caching each entity's matrix at arm time inside
  `SceneRenderer` and drawing from the cache -- the same shape as the view/projection freeze. Nothing
  in the investigation has needed it yet; the transform *history* answers the question it would have
  been armed for.
- `[!]` Freeze static transforms. Same, and additionally there is no flag on an entity saying it is
  static: the available test is "has no rig", which is not the same claim.
- `[x]` Show object origins. `entityOrigins`: the origin point and the object's three axes, in the
  culled colour when the cull dropped it.
- `[x]` Show world axes. `worldAxes`: the origin and its three axes, sized from the camera distance
  and floored so they survive a camera sitting on zero. Asserted to start at zero and to stay there
  when the entity moves -- they are the *world's* axes, and an overlay that quietly followed the
  selection would be the same picture with a different meaning.
- `[x]` Show transform history. `transformTrail` draws the recorded world path of the selected
  object; the record itself is `rendering::TransformHistory` (below, and Phase 9.3).
- `[x]` Show bounds and bounding spheres.
  - `[x]` Existing debug drawing covers ordinary and procedural bounds.
  - `[x]` Selected-object history is the trail and `TransformHistory::explain`; the culling reason is
    carried on every sample, so "when did it stop being drawn, and what did the cull say" is answered
    from the record rather than from the frame you happen to be on.
- `[x]` Show submitted geometry. `submittedOnly` restricts entity diagnostics to what survived the
  camera cull. It is the submitted *set*, not the triangles, and the header says so: "was this object
  handed to the GPU" is the question a missing object raises, and it is one the visualiser can answer
  honestly.
- `[x]` Show object IDs. `entityIds` colours each entity's bounds by the pick id the identifier
  target writes -- deliberately that id and not the loop index, because two numberings for the same
  object is how a click used to select the wrong tree. Culling does not repaint it: a red box would
  put a second meaning on the same colour.
- `[x]` Show frustum and camera basis. `frustum` draws the camera's twelve edges and its basis,
  through the inverse of the matrix the camera would draw with, at the aspect the frame is actually
  rendering at (the application overrides the option's default; a box drawn at 16:9 over a 2:1
  viewport is a wrong shape that reads as a culling bug). A degenerate projection draws nothing
  rather than a box at infinity. On a live camera this is the screen edge and says nothing; it earns
  its place beside Phase 4.2's freeze arm, where it is the volume the cull actually used.

### 4.4 GPU and depth controls

Seven auxiliary views already existed -- normal, roughness, velocity, emission, ids, occlusion, depth
-- and the plan's truthfulness rule applies to them as much as to the arms. They are now asserted:
`[gpu][composition][forensics][views]` requires every view to be its own picture (two views that hash
alike are the same buffer shown twice or two empty frames, and both are a diagnostic that lies), the
depth view to move when the camera does, and the id view to separate objects into distinct values
rather than a continuum.

**SYM-AUX-1: every diagnostic view was tone-mapped.** Found while building the linear-depth view,
and it is the largest defect in this phase. The auxiliary pass drew into the *HDR* target, before
pass 2 -- so every view went through auto-exposure and a filmic curve on its way to the screen. A
shader writing 1.0 landed on the screen as **202**. A normal encoded as 0.5 did not arrive as 0.5;
an identifier's palette moved with how bright the scene happened to be, which means two frames of
the same scene could colour the same object differently; and a depth could not be read as a number
at all. A diagnostic whose values are a function of the picture it is diagnosing is precisely the
instrument this investigation exists to remove, and it had been sitting under all seven views since
ADR-035. The pass now draws **after** the tone map, straight onto the target, one pipeline per target
format (the same reason the tone map itself is a map). The byte on the screen is now the value the
shader wrote. The regression guard is four stops of exposure compensation leaving a view's hash
unchanged, with the shaded frame's hash changing under the same four stops as its control.

- `[~]` Show GPU object index. The *slot* is in the per-object diagnostic and in a capture; the `Ids`
  view colours by entity pick id, which is a different number. A view keyed on the slot is not built.
- `[x]` Show buffer generation. **Resolved: there is nothing to show.** The renderer's reuse boundary
  is the owning `Scene` plus a local version, not a per-frame generation -- see Phase 1.3, where the
  absence is the finding rather than a gap.
- `[x]` Show frame index. On screen in the selected object's diagnostic readout, and in the capture.
- `[~]` Validate GPU object data. Camera, entity matrices and skinning palettes are guarded and
  refuse a non-finite frame by name; bounds, materials and water are not.
- `[x]` Show raw depth. The `Depth` auxiliary view, asserted to vary with the camera.
- `[x]` Show linear depth. `AuxDebugView::LinearDepth`, and the assertion is not a trend but an
  identity: the linear-depth target is read back and **every pixel** of the view is checked against
  the number it claims to be a picture of, to within one step of 8-bit quantisation. The
  "nothing was drawn" sentinel (1e7) is full white and its own value, so an empty sky and a surface
  at the far plane are not the same picture. The exponential `Depth` view is run through the same
  comparison as a control and must fail it by a wide margin -- without that, a linear view that had
  quietly become the exponential one would pass everything else.

  A far plane is not a scene: RendererQA's camera sees 2.9 km and its geometry is 30 m away, so a
  ramp over the far plane puts every surface in the bottom two of 256 steps. `setAuxDebugScale` says
  "show me the first N metres" without the view lying about what it shows -- the value stays
  proportional to distance and the constant is in the uniform rather than in somebody's head.

  **The instrument took three tries, and the two failures are the useful part.** The frame's *mean*
  measured the wrong thing entirely: pulling the camera back put less sky in shot, so the average
  fell while every surface got further away. Moving the camera along its own view axis so the centre
  ray stayed on one surface was better arithmetic and still wrong -- this scene has an object right
  in front of the camera, so the move passed *through* the surface being measured and the distance
  jumped the other way. What works is not a proxy at all: read the buffer, compare the picture to it.
  Both failures are the same mistake in different clothes, and it is the one this whole document
  keeps recording -- a measurement chosen for convenience rather than for what it is a measurement
  *of*.
- `[!]` Disable depth test. Blocked on a pipeline variant: depth state is baked into a pipeline, so
  an honest control means a second pipeline per material. Nothing in this investigation has needed
  one, which is why it is blocked rather than being built speculatively.
- `[!]` Disable depth write. Same.
- `[x]` Show depth discontinuities and object-specific depth. Two views. `DepthEdges` is a relative
  step over linear depth -- relative because an absolute threshold finds an edge at every surface
  once the camera is far enough away, which makes the whole frame an edge and says nothing -- with
  the sentinel clamped to the far plane first, or every silhouette against the sky saturates
  identically and hides the discontinuities *inside* the geometry, which is what a depth bug looks
  like. Asserted on the shape of its histogram: a flat majority and a lit minority, so a view that
  marked everything and one that marked nothing both fail. `ObjectDepth` is the selected object's
  depth with the rest of the scene removed -- the picture that answers "is it behind that" without
  everything else arguing. Nothing selected is an empty frame rather than object zero, which is
  checked, along with the lit region being neither empty nor the whole frame and a different
  selection being a different picture rather than the same one relabelled.

### 4.5 Animation and water controls

- `[x]` Freeze animation. `PassToggles::animationMotion`, and deliberately *not* the same control as
  `animation`. That one removes skinning from the frame and leaves a bind pose; this one holds the
  palettes already on the GPU, so a character stops moving **where it is**. "The pose is wrong" and
  "the pose is not changing" are different questions, and a scene where only one arm changes the
  picture says which one you are looking at. Implemented as "do not upload" rather than as a copy of
  the scene's palettes, because the palette on the GPU is already the thing being frozen and a second
  copy is a second thing to keep true; guarded on the scene, because a slice into another scene's
  palettes is not a frozen character but a wrong one.

  `[gpu][composition][forensics][isolation][animation]` asserts it three ways -- live, held and bind
  must all be different pictures -- because checking the freeze alone would pass for an arm that had
  quietly become the bind-pose one. It also asserts the rig's palette version still advancing while
  the frame is held, which is what makes the equality the *arm* holding the picture rather than the
  animation having stopped in the scene. **Two instrument faults had to be fixed first:** the alien
  scene's camera orbits, so the "animation is moving" precondition passed on camera motion alone, and
  the AO buffer is temporally jittered, so consecutive frames of a completely static scene do not
  hash alike. Both made the test report the freeze for a difference the freeze does not own.
- `[x]` Show skeleton and bones. `DebugViewOptions::skeletons`: a point per joint and a line to its
  parent, in world space, for every skinned entity. Drawn from the rig's **model-space** matrices and
  not from the GPU palette, which is `model * inverseBind` and whose translation is not where the
  joint is -- reading a bone position out of it is exactly the plausible-looking mistake a skeleton
  overlay exists to catch. The test builds a rig whose two are a metre apart and requires the overlay
  to use the right one, checks a joint lands at `entityTransform * jointModel`, and requires a rig
  that has never been evaluated to draw *nothing* rather than a rest pose the frame is not using.
- `[x]` Show animation time. In the selected object's diagnostic readout (`palette v… time …`), in
  the capture, and per sample in the transform history.
- `[x]` Show skinning state. The same readout: rig index, joint count, palette version and palette
  time, with the version being the thing that says whether the rig re-posed this frame.
- `[!]` Show water mask. **Cannot exist as an auxiliary view.** `water_renderer.cpp` masks every
  scene target but colour and emission, deliberately -- a normal or an identifier averaged over a
  transparency is worse than none -- so no view keyed on the identifier target can show water. The
  working instrument is the A/B against the same frame with the water arm off, which is what all of
  Phase 6.2 uses. Blocked on a design change, not on effort.
- `[x]` Show water depth. `AuxDebugView::LinearDepth` over the water surface, and Phase 6.2's
  reconstruction of it against the terrain field: mean 0.051 m, worst 0.370 m over 3,977 pixels.
- `[x]` Show terrain depth. The same view with the water arm off, which is the only way to see the
  bed -- and the A/B between the two is the water's own thickness.
- `[!]` Show intersection mask. The shoreline intersection is where water depth crosses zero, so it
  is derivable from the two views above; a dedicated mask needs the water pass to write one, which is
  the same design change the water mask is blocked on.
- `[x]` Disable water post effects. `PassToggles::water` removes the surfaces; `post` removes the
  chain. There is no separate "water effects" arm and the plan's own Phase 8.2 entry says why: there
  is no control that separates a surface's effects from the surface.
- `[x]` Ensure water diagnostics explain pixels only through water geometry, depth and masks. This is
  what Phase 6.2 does and the reason its every measurement is an A/B: with no identifier to ask, a
  water pixel is *defined* as one the water arm changes. Nothing in that phase classifies a pixel by
  its colour, and the one draft that did -- a hue test that read a grey sphere lit by a blue
  environment as the blue one -- was thrown away for it.

## Phase 5: Culling, LOD, animation and character isolation

### 5.1 Culling forensics

- `[x]` Compare culling on/off in controlled scenes. Three instruments, and each answers a different
  half of the question. The `culling` arm is asserted to *raise the draw count* -- it draws what the
  cull dropped. The `submittedOnly` overlay shows the set from the other side, and is checked in both
  directions with a culled object present-then-absent. And the object-depth view is joined to the arm
  so a culled object is missing from its own depth view and returns when the arm is disarmed; that
  direction pair is the point, since a view that always showed the object would pass the first half
  alone.
  - `[x]` Existing terrain, authored-node and animation culling regressions cover several cases.
- `[x]` Selected-object diagnostics now capture world bounds, camera position, visibility, cull reason,
  submission/object-slot state and six signed frustum margins. All-object change-only logging and
  richer explicit cull-reason codes remain open.
- `[x]` Record world bounds, render bounds, culling bounds, camera position and visibility result for
  each object. All of it is in `RenderObjectDiagnostic`, and **the plane-level rejection evidence is
  the six signed frustum margins** -- which is a better answer than a reason string, because a
  negative margin names *which* plane rejected the object and by how far, so "just outside" and "a
  kilometre outside" are different numbers rather than the same word. The culling bounds are the
  ones the cull actually used: `entityCullBounds` is one function, shared, since the rule was found
  duplicated, and it includes the conservative pad a posed character needs. Visibility and the cull
  verdict are recorded separately because they are different claims -- an entity can be `visible` and
  `cameraCulled`, and only the first is a reason to stop casting a shadow.
- `[x]` Verify culling never mutates scene transforms or authored visibility. See Phase 1.4: the
  verdict lands in `cameraCulled`, and the authored flag and the transform are untouched in both
  directions -- a cull cannot turn an object off, and cannot turn an authored-invisible one on.
- `[x]` Animated entity culling derives bounds from the current joint palette and applies a conservative
  residual pad before frustum testing. The renderer QA record and scene code establish the ownership
  and implementation; a pixel-level limb-crossing regression is still required below.
- `[x]` Add frustum-edge regression where a posed limb crosses the plane while bind pose does not.
  `tests/unit/test_skeleton.cpp`, `[scene][skeleton][culling]`, 218 assertions.

  **Disproven hypothesis, recorded so it is not retried** (an experiment, deliberately not shipped as
  a test -- a test that cannot fail must not be in the suite): the same regression written against
  the alien composition *cannot discriminate*. A T-pose bind box is **wider** than every pose the clip
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
  representative procedural scenes; canonical Glowmere/alien image toggles remain open. Note that
  terrain LOD *does* have a switch -- `nodes/<node>/terrainLod` -- which is how the calibration below
  was bounded; it is a scene parameter rather than a renderer arm.
- `[x]` Instrumented LOD selection is covered by CPU/GPU count comparisons, camera-distance and
  screen-size threshold tests, per-instance spread migration and hysteresis checks.
- `[x]` Moving-camera threshold traversal proves deterministic, non-strobing transitions when spread
  and hysteresis are configured; matched image/performance calibration on `RendererQA` remains open.
- `[~]` Verify mesh/material replacement cannot use stale GPU state.
  The scene-owned cache repairs cover mesh, texture, environment and palette reuse across scenes, and
  the alternating-transform case covers object slots. What is *not* separately covered is a chunk
  swapping between its own LOD meshes mid-frame; the calibration below exercises it across five
  settings without a GPU error or a wrong triangle count, which is evidence rather than proof.
- `[x]` Calibrate LOD ratios against a moving camera and record image/performance tradeoffs.

**Terrain chunk LOD on Glowmere**, 1280x800, 60 frames, everything else fixed. Driven through
`nodes/valley/terrainLodDistance`:

| `lodDistance` | Triangles | GPU median |
|---:|---:|---:|
| 8 m | 402,201 | 18.48 ms |
| 35 m | 412,057 | 18.42 ms |
| **70 m (shipped)** | **430,233** | **18.61 ms** |
| 280 m | 529,305 | 20.51 ms |
| 2000 m | 544,281 | 20.25 ms |

**The conclusion is that this knob is not worth tuning on this scene**, and the numbers say why:
triangles move 35% across a 250x range while the frame moves 11%, not monotonically. The shipped
70 m already sits at the knee -- going coarser buys 28,000 triangles and 0.13 ms, which is noise.
Glowmere's frame is 15.7 ms of scene pass against 18.6 total and is fragment-bound, so removing
terrain vertices cannot help it. Anyone reaching for LOD to make this scene faster is reaching for
the wrong knob.

**A trap, recorded because it caught the person who had just written the rule down.** The first four
runs of this calibration changed `terrain.lodDistance` in the *scene file* and produced byte-identical
frames across a 250x range -- 430,233 triangles every time, to the digit. Nothing was broken: the
project's `parameters` block pins `nodes/valley/terrainLodDistance`, and `applyParameters` re-derives
the setting from the parameter every frame. That is Phase 1.3's rule exactly -- **the parameter is
authoritative and the scene value is its registered default** -- and it reads as a dead knob from the
outside.

### 5.3 Character isolation and skinning

`tests/rendering/test_character_forensics_gpu.cpp`, `[gpu][composition][forensics][character5_3]`:
four cases, 2,531 assertions.

- `[x]` Create a scene with only camera, ground, one character and one light.
  `examples/qa/renderer-qa-character.scene.json`, and the first test case asserts the scene really is
  that -- two entities, no procedurals, no particles, one light, one rig -- before anything else
  claims something about it.
- `[x]` Run the progression: animation off -> animation on -> culling -> shadows -> transparency -> post FX.
  Every rung renders the *same frame* with different arms, so a change is attributable to the arm
  rather than to the clip moving on, and each rung is rendered twice and required to reproduce.
  **The transparency rung is a null arm on this scene** -- identical hash, because a scene with one
  character has nothing blended in it -- and it is recorded as one and then tested separately against
  a blended object. That is the same "a level whose subsystem the scene does not contain proves
  nothing" the progressive matrix ran into.
- `[x]` Instrument character ID, skeleton ID, clip, animation time/delta, pose version, bone count, skinning buffer and GPU index.
  Including the skinning slice's alignment and that it is large enough for the current *and*
  previous palette, and that `paletteVersion` advances by exactly one per frame.
- `[x]` Reject or report NaN/Inf bone matrices, invalid quaternions/scales, invalid bone indices and invalid weights.
  420+ frames across Idle, Walk and Run plus a cross-fade: non-finite palette entries, collapsed or
  mirrored joints (determinant <= 0), runaway translations, non-unit quaternions, non-positive
  scales, joint indices past the palette, weights out of range or not summing to one.
- `[x]` Capture the exact animation/frame/state for invalid pose data. Every fault report carries the
  clip, the state, the frame index, the second, the clip time and the joint.
- `[x]` Test bind-pose, idle, walk, run, loop, pause, resume, seek, scrub, reverse, close/far camera and scene reload.
  The pose is asserted to be a pure function of the timeline on all of them -- played-to against
  seeked-to against a fresh engine -- which is the property the phase-origin repair established.

**Two things this found that are worth carrying.** `PassToggles::animation` does not stop the CPU
pose; it removes skinning from the *draw*, so the rig statistics keep whatever the last posed frame
left while the upload goes to zero. And a culled rig is only "not skinned" while shadows are off: with
a cascade on, a character behind the camera is still skinned for its shadow, which is correct.

### 5.4 Character terrain ownership

- `[~]` Document ownership of character X/Z, terrain height, root motion, visual offset and foot offset.
  X/Z is `EntityState::anchor + travel` -- the anchor is where the scene put it and navigation writes
  the travel. Y comes from the navigator, which is built from the same `WorldMap` the terrain was
  meshed from, so there is one description of the ground rather than two kept in step. Root motion,
  visual offset and foot offset are not yet traced.
- `[x]` Detect multiple systems writing Y in the same frame. Only the ground writes it.
  `tests/rendering/test_composition_gpu.cpp`, `[gpu][composition][forensics][character][terrain]`:
  over 1,800 frames, every live entity's height is compared against `TerrainQuery::heightAt` at its
  own X/Z, and each frame's height change is compared against the ground distance it covered. A
  second writer shows as height moving without the walker going anywhere. None does.
- `[x]` Add a terrain-crossing regression that proves no feedback loop or one-frame disappearance.
  The same test, and it is a real crossing: 149 m walked over 30 s with 29 m of height change, the
  walker never more than 0.75 m under the ground beneath it and never moving vertically by more than
  the slope it covered can explain.
- `[x]` Record whether animation, terrain, physics and sequencer writes are authoritative or derived.
  In Phase 1.2's audit, and the answer is uniform: **animation** is authoritative over joints and
  writes no transform; **terrain** is authoritative over the ground *once* -- a walker's height comes
  from the navigator built on the same `WorldMap` the terrain mesh was built from, so there is one
  description rather than two; **the sequencer** writes parameters, which puts it upstream of the
  derived-copy rule rather than in competition with it. There is no physics writer.

## Phase 6: Water, terrain, transparency and depth isolation

### 6.1 Water-only diagnostic scene

- `[x]` Create a scene with camera, terrain, water and one light. Two of them, and the difference is
  deliberate. `examples/qa/renderer-qa-water.scene.json` is a **real generated shoreline** -- the
  shipped world, which has a river in it -- with the camera standing on the bank at the grazing angle
  `SYM-WATER-1` was reported from, sky and volumetrics off. Two planes would have been easier and
  would not have contained the defect: the bug was a quad emitted one grid cell wide at a wet corner,
  which only a generated shoreline has. The *synthetic* beds the six-view shoreline test builds are
  the other half, and they exist for the opposite reason -- there the geometry is known exactly, so
  "did any water reach the dry side" is answerable without trusting the generator.
- `[x]` Keep characters, vegetation, particles, post FX and shadows disabled initially. The scene
  authors no characters or particles at all and switches the sky and volumetrics off; every
  quantitative test over it additionally runs with post and ambient occlusion off, which is not
  tidiness but necessity -- auto-exposure re-meters on content and the AO buffer is frame-jittered,
  so a tone-mapped 8-bit frame cannot be an instrument here.
- `[x]` Cover above-water, grazing, near-parallel and below-surface camera cases.
  - `[x]` Native water image tests cover these basic views.
  - `[x]` Steep, ordinary, shallow, grazing, reversed and oblique shoreline views, each asking the
    question that matters -- did any water reach the dry side -- rather than whether the view renders
    the same way twice. `tests/rendering/test_gpu.cpp`,
    `[gpu][renderer][water][forensics][shoreline]`. The regions are read out of a no-water control
    render (the land bed is red, the submerged bed green) so the test does no projection arithmetic
    of its own, and the two halves control each other: water must be absent on the land and present
    on the bed. Negative-controlled by setting water's depth compare to `Always`, which tints land at
    every one of the six views.
- `[~]` Progressively enable water geometry, depth, transparency, terrain intersection and shoreline
  effects. The progressive matrix runs this scene at its water rung and the arms cover geometry
  (`water`), depth (the linear-depth views) and transparency. What has **no separate control** is the
  surface's own effects -- the shore fade, foam and ripple are properties of the water program rather
  than passes, so "water geometry without shoreline effects" is not a state the renderer can be put
  in. Phase 6.2 measures them instead of switching them off: the four depth bands are chosen entirely
  above the 0.8 m fade precisely so the fade is not what is being measured.

### 6.2 Water mask and depth forensics

`tests/rendering/test_water_depth_forensics_gpu.cpp`, `[gpu][renderer][forensics][water6_2]`: eight
cases, all quantitative, all run with post and AO off against the scene-linear HDR target --
auto-exposure re-meters when frame content changes, and the AO buffer is frame-jittered, so a
tone-mapped 8-bit frame is not an instrument. **Water pixels are found by A/B against the same frame
with the water arm off**, because they cannot be found any other way (see the identifier finding
below). Six source-level negative controls were run and restored; two of the agent's own tests passed
their control and were rewritten before they meant anything.

- `[~]` Visualize water geometry mask, water depth, terrain depth, linear depth, reconstructed
  thickness, shoreline fade, foam/intersection mask and water object ID. Linear depth, depth edges
  and object depth are built (Phase 4.4). **Water object ID cannot exist**: `water_renderer.cpp:150`
  masks every scene target but colour and emission, deliberately, because a normal or an id averaged
  over a transparency is worse than none. So no view keyed on the identifier target can show water,
  and no test can classify a water pixel from it. That is a fact about the design, not a gap to fill.
- `[x]` Capture shoreline pixel values and spaces: water depth, terrain depth, linear depth, surface
  height, terrain position and camera depth. 3,977 water pixels within 50 m, each reconstructed from
  linear depth and compared against `TerrainQuery::heightAt`: mean 0.051 m, worst 0.370 m, no
  reprojection failures. Beyond ~50 m the decimated chunk mesh departs from the analytic field by up
  to 2.2 m, so the comparison is restricted to the near half and says so.
- `[x]` Audit every depth comparison for compatible spaces and nonlinear-to-linear conversion.
  **No mismatch found, and the audit is a measurement rather than a reading.** 988 taps on a
  synthetic quad against a CPU ray/plane intersection: worst error 2.8 mm (0.007%). The test is known
  to discriminate because the two candidate spaces -- `dot(p - eye, forward)` and `length(p - eye)`
  -- are 29.4% apart across those taps, and swapping the shader to the wrong one fails three tests
  with a 13.27 m error. The refraction reprojection's screen UV matches the linear-depth pass's own Y
  convention, so there is no flip; `uv.x` is vertical and is only ever used for the fade and the
  ray-thickness cap, never differenced against a ray depth.
- `[~]` Document water/terrain/transparent pass order and every depth/color read/write/clear. The
  pass table in the report covers the scene pass; the per-helper inventory is documentation rather
  than a test and is not written out.
- `[x]` Verify overlapping chunk sort order and chunk/world transforms. Synthetic red/blue sheets
  composite by view depth and not list order (49,953 against 878, a factor of 57), the hash is
  unchanged when the list is reordered, and on the real 21 chunks every water chunk's nearest ground
  chunk is the one it is named for. Reversing the comparator and removing the sort each fail it.
- `[x]` No water over dry terrain, on two instruments: pixels on synthetic shoreline geometry (the
  six-view GPU test) and geometry on the real generator (`[unit][water][forensics][shoreline]`,
  which found the defect).
- `[x]` Stable-edge, no-z-fight, terrain-through-water and seek-determinism image tests. The
  shoreline holds still across 12 frames with the timeline held (0 pixels change hands, drift
  exactly 0, and one frame of timeline later moves 3,495); no pixel flips twice under a 2 cm camera
  sweep; the same second reached by a direct seek and by three seeks is bit-identical, with 4.5 s
  later differing in 4,395 pixels; and the bed shows through shallow water and not deep
  (1.052 / 0.471 / 0.260 / 0.230 across bands entirely above the 0.8 m shore fade).
- `[x]` Do not solve seams with arbitrary depth offsets without a reproduced cause. No seam was
  found, so no offset was added -- which is the rule working rather than the rule being untested.

### 6.3 Transparency and post-processing isolation

- `[x]` Create opaque cube, transparent cube, terrain and water test scene.
  An opaque backstop and two transparent panes, which is the smallest arrangement in which sorting
  is a question with a wrong answer. `tests/rendering/test_gpu.cpp`,
  `[gpu][renderer][forensics][transparency]`.
- `[x]` Test depth test, depth write, sorting, camera movement and intersections.
  Three separable claims, each failing differently: a transparent surface does not write depth (the
  backstop behind two panes still changes the pixel); layers composite back to front (the nearest
  pane dominates, measurably -- blending is not commutative); and the order follows the camera, so
  crossing to the other side swaps which one dominates, checked at every step of a traverse rather
  than only at the ends. Negative-controlled by reversing the blended sort, which makes the far pane
  dominate and fails at every step.

  **A vacuous version of this test was caught by that control**, and it is the fourth of this shape.
  The backstop sat at the origin, *between* the two panes, so from either side one pane was occluded
  by it and the two never composited at all: the assertions were measuring which pane was on the
  camera's side, and reversing the renderer's sort did not disturb them. The backstop now sits
  behind both, and the ordering half runs with no backstop at all.
- `[x]` Disable all post-processing and run every known problem scene. The `post` arm, over the QA
  variants and the canonical scenes; every quantitative measurement in Phases 6.2 and 4.5 is taken
  with it off, for the reason above.
- `[~]` Re-enable bloom, tone mapping, colour grading, atmosphere/fog, volumetrics, water post FX and
  other screen-space effects one at a time. Volumetrics and the post chain are separate rungs of the
  progressive matrix and separate arms. **The chain's own stages are not individually armed** -- bloom,
  DoF and grading are one `post` flag -- and the plan should say so rather than imply a control that
  does not exist. Tone mapping is deliberately not an arm at all: it is not an effect but the
  frame's only path to display-referred pixels, and the one thing that *did* need to escape it now
  does (`SYM-AUX-1`).
- `[x]` Record the first enabled subsystem that changes the failure. This is what the progressive
  matrix reports by name, and Phase 8.3's bisection answers better: rather than sweeping arms by hand
  and forming an impression, it returns the **minimal set** of subsystems a symptom needs, with every
  member load-bearing. A sweep says "it changed at rung five"; the bisection says "it needs exactly
  these, and removing any one makes it stop".

## Phase 7: Render-pass state and pass contracts

- `[x]` Enumerate every pass's pipeline, bind groups, vertex/index buffers, dynamic offsets, blend,
  depth, stencil, viewport, scissor and target ownership. Two tables in the report: the nine render
  passes with their targets, load/store, depth and ownership, and a second for **pipeline state per
  draw kind** -- blend, depth write, depth compare and which auxiliary targets each masks. Three
  facts fall out of the second that were not obvious from any single descriptor: every transparent
  kind disables depth write, so the depth buffer is the opaque scene and nothing else (which is what
  makes linear depth usable by AO, water and fog); masking the auxiliary targets is the *rule* among
  transparent kinds rather than a water peculiarity; and **there is no viewport or scissor call
  anywhere in the renderer**, so a resize is a target recreation rather than a state change -- which
  is why the resize regression compares against a renderer born at that size instead of checking a
  rectangle. There is no stencil state to enumerate. Bind-group and vertex-buffer inventories are
  deliberately not transcribed (see the working rules).
- `[~]` The main `SceneRenderer` pass contract is traced: shadow and depth passes clear/store depth;
  linear depth writes R32F; the scene pass loads background color and clears auxiliary targets;
  water/blended pipelines disable depth writes; debug/post/auxiliary/tonemap passes load or clear
  their declared color targets. Remaining work is to extend this inventory through procedural,
  particle, SDF, water, post-layer and external renderer helpers and add state assertions where
  descriptors do not make the contract visible.
- `[x]` Verify every pass establishes the state it requires rather than relying on a previous pass.
  Asserted through the arms: `[gpu][composition][forensics][passes]` checks that each isolation arm
  removes exactly its own pass and nothing else. An arm removing two passes is one subsystem owning
  another's state. Water, transparency and culling draw inside the scene pass rather than owning one
  and are asserted to remove *no* pass; post owns several and is checked as a family.
- `[~]` Procedural draws reset local pipeline/material/mesh trackers at each helper entry and particle
  draws bind their render group and blend-specific pipeline per system. These contracts are visible in
  code; equivalent assertions/regressions for all helper pass boundaries remain open.
- `[x]` SDF state is rebuilt from the current scene each frame and water materials are uploaded each
  frame rather than retained as scene-local simulation state. Compile-time guards cover their
  dynamic uniform strides, and reused-versus-fresh SDF/water scene-swap image regressions pass.
- `[~]` Add pass-boundary assertions or explicit state setup where the API does not make state
  implicit. What is asserted is the arm/pass join above and the target contract below; per-helper
  boundaries (procedural, particle, SDF) remain visible in code only.
- `[x]` Verify render target load/store/clear behavior and resource transitions.
  `[gpu][composition][forensics][passes][targets]`. Two clears, checked as behaviour rather than as
  a descriptor transcription. **The identifier target**: draw an object, hide it, and the target must
  be empty -- if the scene pass loaded instead of clearing, the previous frame's identifiers would
  still be there, and the picker reads this target, so a stale id is a click that selects something
  no longer on screen. **The linear-depth target**: cleared to its 1e7 sentinel and not to zero,
  because a zero linear depth reads as *a surface at the camera*, which is the worst possible default
  for the three passes that consume it.
- `[~]` Verify depth prepass, terrain, water, transparent, particle, shadow, volume, debug and post
  pass interactions. Covered for water (Phase 6.2's eight cases) and through the arm/pass join for
  every arm that owns a pass; the pairwise interactions are not individually exercised.
- `[x]` Add raw/linear/object depth diagnostics to the pass-level test matrix. The object-depth view
  is joined to the culling arm in both directions: a culled object is absent from its own depth view
  and returns when the arm is disarmed. That direction pair is the point -- a view that always showed
  the object would pass the first half alone.
- `[x]` Test resize, target recreation and auxiliary debug target selection through all passes. A
  renderer taken through 320x240, 64x64 and 257x129 must come back to its original size drawing the
  *same picture* a renderer born at that size draws -- and a third, genuinely fresh renderer must
  agree with both, which is what makes the equality about the targets rather than about one renderer
  being self-consistently wrong. Every auxiliary view is then rendered at three sizes, none of them
  a multiple of anything convenient, with the device error count asserted at zero.

## Phase 8: RendererQA torture scene and progressive enablement

### 8.1 Complete `RendererQA`

- `[~]` Maintain `examples/qa/renderer-qa.json` as the permanent controlled test scene.
  - `[x]` Existing scene contains near/far/behind-camera geometry, skinned alien, transparent orb, particles and floor.
  - `[~]` Add explicit UFO/static object, flat/slope/irregular terrain, water shoreline/depth cases, LOD distance ladder, shadow casters and labeled camera positions.
    The static object is `static-cube` in the minimal variant, at Phase 2.3's documented
    `(10, 2, -20)` with a non-identity rotation and non-uniform scale. Terrain and a generated
    shoreline are in the water variant. **Labelled camera positions exist, in the harness rather than
    the scene** -- the reference comparison names its five (`opening`, `from the side`, `close`,
    `looking at the static cube`, `high and back`) and reports by name on failure, which is where a
    label is actually read. A LOD distance ladder is not
    added: the LOD ladder wants a control that does not exist yet (Phase 4.2), and camera positions
    live in the tests that use them rather than in the scene, where nothing reads them.
  - `[x]` Add stable object IDs and labels that map to the forensic panel. Node names are the
    identifiers throughout -- the panel's selection, the renderer's per-object diagnostics and every
    forensic test address objects by name, and the names in these scenes are chosen to say what each
    object is for.
  - `[x]` Add scene variants for minimal, water-only, character-only and transparency-only tests.
    `examples/qa/renderer-qa-{minimal,water,character,transparency}.scene.json`. Variants rather than
    a fatter QA scene, because `renderer-qa.scene.json` is the control the performance baselines are
    measured against. Each is asserted to contain the subsystem it isolates and to put something on
    screen -- `[gpu][composition][forensics][qa]` -- because a variant that loads and contains
    nothing is the same trap one level down.
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
- `[x]` Add scripted playback, pause, seek, scrub, reverse, scene reload and resolution changes.
  Phase 3.4's interleaved sequence over the QA set is exactly this script, and all seven happen in
  one walk rather than seven walks -- eighteen rounds of resize (seven awkward sizes), playback,
  camera cuts, forward and backward seeks, repeated frame indices, and reload or scene swap every
  third round, with fourteen checkpoints against a fresh engine and renderer. Doing them together is
  the point: each is covered alone elsewhere, and the two defects this found were only reachable by a
  walk that crossed them.

### 8.2 Run the progressive matrix

Built as `tests/rendering/test_composition_gpu.cpp`, `[gpu][composition][forensics][matrix]`: nine
cumulative levels, each running an eleven-step script -- translate, rotate, dolly, orbit, seek
forward, seek back, scrub, resize, resize back, reload -- twice, from two independent engines and
two independent renderers, comparing the frames step by step. 986 assertions. Levels 1 and 15 of the
list below are not rungs but that script, run at every rung, because a camera move and a time jump
are where an instability shows rather than a subsystem to switch on.

The comparison is *two whole runs*, not one frame rendered twice. Re-rendering a state was tried and
is wrong twice over: it ticks the clock, so an animated scene is legitimately a different pose, and
the particle simulation is stepped **inside** the render call, so rendering the same frame again
steps the world again. Both were reported by the matrix as "the first misbehaving level" before the
instrument was fixed. That second one is worth keeping: **a frame is not a pure function of the
scene state while particles are in it.**

Every rung is also asserted to *change the picture* against the rung below. A level whose subsystem
the scene does not contain draws the same frame as the level before it and can localise nothing, and
that check is what found the two gaps recorded below.

- `[x]` Level 0: basic opaque geometry.
- `[x]` Level 1: camera movement. (The script, at every level.)
- `[!]` Level 2: terrain. No isolation control exists (Phase 4.2); terrain is present at every rung.
- `[!]` Level 3: lighting. Same: no control, present throughout.
- `[x]` Level 4: shadows. Two rungs, the cascades and the shadow mask.
- `[x]` Level 5: water geometry. The rung runs against `renderer-qa-water.scene.json`, a generated
  shoreline from the shipped world, because the control scene has no water -- which is what this
  matrix found.
- `[~]` Level 6: water effects. Present in the same rung; there is no control that separates the
  surface's effects from the surface.
- `[x]` Level 7: transparent objects. Reachable only after the material fix -- the scene's
  "transparent orb" had been opaque since it was written.
- `[~]` Level 8: characters. The alien is in the scene at every rung; there is no "characters off"
  control separate from animation. `renderer-qa-character.scene.json` isolates one for Phase 5.3.
- `[x]` Level 9: animation.
- `[x]` Level 10: particles.
- `[x]` Level 11: post-processing. Three rungs: ambient occlusion, volumetrics, the post chain.
  The volumetrics rung supplies its own `scene/volumeDensity`, because RendererQA authors zero and
  the pass is off at zero however the toggle is set.
- `[~]` Level 12: culling. The control exists and is tested in the isolation case; it is not a rung
  here because culling is on throughout and switching it off adds objects rather than a subsystem.
- `[!]` Level 13: LOD. No control (Phase 4.2).
- `[!]` Level 14: sequencer. No control.
- `[x]` Level 15: timeline seeking and scrubbing. (The script, at every level.)
- `[x]` Record the first level where each instability appears. The matrix reports it by name. In
  release it reports none. In debug it names **level 5, water** on every step of the script, which is
  `SYM-TERRAIN-1`, since fixed -- the matrix compares two whole runs from two independent engines
  and renderers, so a scene that did not render the same frame twice could not pass it. That was the
  matrix working: it localised the defect to a rung, and the rung passes in debug now.

### 8.3 Automatic subsystem bisection

- `[x]` Add a machine-readable feature-group configuration for QA runs.
  `SceneRenderer::passArms()` -- name and member pointer per arm -- with `setPassArm` and
  `passArmNames` beside it. The CLI's `--disable <list>` now reads that table instead of keeping its
  own if-chain, which is the point: three enumerations of the same set is how an arm ends up
  reachable from one of them and not the others, and a bisection that cannot see an arm silently
  clears the subsystem behind it.
- `[x]` Implement binary isolation over feature groups where practical.
  `[gpu][composition][forensics][bisect]`. Greedy one-minimisation over `passArms()` -- with eleven
  arms that is cheaper than a proper ddmin and reaches the same one-minimal answer. Lives in the test
  rather than the renderer because it is a search *over renders*, and the thing that owns a render
  loop should own it.
- `[x]` Record the smallest reproducing subsystem combination. Tested against symptoms whose cause is
  known, which is the only way to check a search whose output is a claim about cause: particle
  dispatches resolve to exactly `{particles}`; shadow draws resolve to a set containing `shadows`
  where **removing any single member makes the symptom go away**, which is what makes the answer a
  claim rather than a list of what happened to be on. Two honest empty answers are asserted as well
  -- a symptom no arm can remove (opaque draws) returns none rather than picking whichever arm was
  tested last, and a symptom nothing produces gives up after one render instead of searching.
- `[~]` Ensure bisection results include scene revision, frame, toggles and capture artifact. The
  toggles are the result; the frame and scene are the caller's and are reported through Catch's
  `INFO`. Emitting a `FrameSnapshot` at the minimal combination is not wired up.

## Phase 9: Frame snapshots, determinism and guards

### 9.1 Frame snapshot and replay

`src/rendering/renderer_snapshot.{hpp,cpp}`, tested by
`[gpu][composition][forensics][snapshot]`.

- `[~]` Capture frame number, camera, view/projection, render objects, transforms, IDs, GPU indices, visibility, bounds, animation, water and pass state.
  All of it except water, which has no per-object diagnostic to capture. **Pass state is the
  isolation arms**, captured with the frame -- a capture taken with shadows off and compared against
  one taken with them on differs in every shaded pixel and no state, and somebody would spend an
  afternoon on it.
  One limit worth knowing before trusting a per-object diagnostic: the frame is built from
  `Scene::entities`, and a *procedural* node becomes a `Scene::procedurals` entry instead. On
  RendererQA that is three objects out of seven nodes; on Glowmere most of the geometry is
  procedural and is not in the capture.
- `[x]` Add freeze-after-capture inspection. A capture is a value: it is written to JSON, read back,
  and compared without a renderer or a GPU in the way.
- `[x]` Add replay of a captured frame without allowing unrelated application state to change it.
  Replay here means *compare against*, which is the useful half: a capture is state, not a command,
  so nothing about the application can change what it says.
- `[x]` Compare captured reference and production frame state before image comparison.
  `compareSnapshots` returns sentences naming the object and the field -- "'far-cube' moved", "'x'
  took GPU slot 4 instead of 3", "isolation differs: shadows was on and is now off". Each kind of
  difference is tested by making it on purpose, because a differ that answers "something changed"
  for everything would pass a test that only ever moved one thing.
  Monotonic bookkeeping is **off by default**: the rig palette version increments on every upload,
  so two arrivals at the same second legitimately disagree about it. That is the trap Phase 9.2 fell
  into when it tried to use the state hash as a replay identity.

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
- `[x]` Compare static transforms, animation state, camera state and deterministic object ordering.
  The first three are the matrices this phase already runs -- four axes of static transform with bit
  equality, the alien's pose at a second reached every way a playhead can reach it, and the camera
  conventions pinned in `[scene][camera][forensics]`. The fourth is
  `[gpu][composition][forensics][ordering]`: the same scene state produces the same slot assignment,
  the slots are a dense range from zero (which is what makes "slot times stride" an address rather
  than a number), they **ascend with entity index**, they survive a reload, and hiding an object
  closes the gap rather than leaving a hole. That ascending check is the one with teeth: without it
  every other assertion would pass for any fixed permutation the renderer invented and then
  repeated.
- `[x]` Diagnostic frames carry a deterministic CPU state hash over camera matrices, object
  transforms/bounds, frustum margins, visibility, GPU slot/submission state and selected rig palette
  metadata, and a regression verifies it is stable for the same state and changes with the camera.
  The frame-100/500 replay is above and passing. **Bone-matrix and scene-state hashes are refused on
  purpose**, and the reason is the trap recorded above: this hash folds a monotonic counter, so it is
  a change *detector* and not an identity. Hashing the bones would produce a second number with the
  same weakness, when what the phase actually needs -- and now has -- is the joint matrices compared
  element by element, which is what localised the animation defect to 98 of 147 joints. A hash can
  only ever say "something differs".
- `[~]` Log hash transitions with frame number and seek direction.
  The replay regression reports differing joint matrices and entity transforms by count at the
  divergent second, which is what localised the animation defect. Application-level logging of hash
  transitions during an interactive scrub is still open.
- `[x]` Add tests for timeline seeking, reverse playback, scene reload and renderer reuse. All four
  in Phase 3.4's interleaved sequence, and deliberately *together* rather than one at a time: each is
  already covered alone, and the two defects that sequence found were only reachable by a walk that
  crossed them. Reverse playback is its descending thirteen-point sweep, re-run ascending.
- `[x]` Distinguish same-GPU bit equality from cross-GPU perceptual comparison. **Everything in this
  repository is same-GPU bit equality, and there is no perceptual comparison anywhere.** Recorded as
  a position rather than a gap: there is one GPU here, so a cross-GPU tolerance could be written but
  not validated, and an unvalidated tolerance is worse than none -- it turns a real difference into a
  pass. What the investigation does instead, wherever bit equality is the wrong instrument, is
  compare *structure* rather than pixels: coverage against the reference renderer, footprints per
  object, state through `compareSnapshots`. Those survive a change of GPU in a way an image hash
  never will. If a second GPU is ever available, the first experiment is the static-object matrix,
  because it is the one whose failure would be unambiguous.

### 9.3 NaN/Inf guards and transform history

- `[x]` Keep existing finite camera/entity/palette guards.
- `[~]` Extend guards to bounds, materials, water state, packed GPU structures and all diagnostic
  snapshot values. Bounds and materials are done; water state and the diagnostic snapshot are not.

**The reason this phase mattered more than it looked.** `glm::min` and `glm::max` are `(y<x)?y:x`
and `(x<y)?y:x`, so a NaN loses every comparison it takes part in and is **silently discarded rather
than propagated**. Every bounds accumulator in the engine folds that way, which means a corrupt value
does not arrive downstream as a NaN a guard could catch -- it arrives as a *plausible finite number*,
or as an inverted sentinel that is finite and contains nothing.

Four doors were open and are now shut, each reporting the entity and the value:
`entityCullBounds` (an infinite scale returned `min = FLT_MAX, max = lowest()` -- finite, inverted,
culled everywhere, silent: the shape of a "the object is just gone" report), `MeshData::bounds`
(non-finite vertices dropped without a word, giving a box that is finite and too small),
`fitDirectionalCascade` (nothing stood between a cascade fit and the GPU) and `packLight` (whose
clamps cannot help, being comparisons).

A fifth is now shut, and it is the one with the widest blast radius: the
material-to-`ObjectUniforms` packing inside `SceneRenderer::render`. A NaN in a base colour does not
stay in its object -- bloom's downsample averages it across a tile, the tone map carries the tile to
the frame, and what arrives is a bright or black region nowhere near anything that could be blamed
for it. The material's own clamps cannot help, for exactly the reason the bounds folds could not:
`std::clamp` is comparisons, and a NaN loses every one of them. A bad material is now **replaced**,
loudly, with magenta at full roughness, and the reason it is replaced rather than dropped is that
dropping the draw makes the object vanish -- the single hardest report to act on, and one this
investigation has already spent time on. `[gpu][composition][forensics][guards]` poisons each of the
nine fields the packer reads in turn (a guard that checks the first three and not the ninth is a
guard that will be found by the ninth), requires the object to still be drawn and the frame not to
saturate, and carries the control that matters: a healthy material must render *identically* to
before, or a `checkMaterial` that returned "bad" for everything would pass every other assertion.

Still open, and both needing a device: water state (`waterUniformsFrom`) and the diagnostic
snapshot.
- `[x]` Report entity, frame, system, property and value when invalid data is found. Each of the four
  guards above names the object and prints the offending numbers.
- `[x]` Add selected-object transform history containing frame, world TRS/matrix, GPU TRS/matrix and
  camera state. `rendering::TransformHistory` (`src/rendering/transform_history.hpp`), a bounded ring
  of `TransformSample` fed from the renderer's published `RendererDiagnosticFrame` after each render.
  A sample carries all three layers at once -- the decomposed world transform, the matrix the GPU was
  handed, and the camera that projected it -- because a history carrying only the first could not
  separate "the scene moved it" from "the upload moved it". Rotation is kept as the normalised basis
  rather than a quaternion: a quaternion and its negation are the same rotation and would report a
  difference that is not one. The two diagnostic structures moved to `renderer_diagnostics.hpp` so
  that nothing reading a frame's diagnosis needs WebGPU in the link line, which is what lets this be
  checked in the unit binary.
- `[x]` Add screen-space projection history to distinguish correct camera parallax from transform
  corruption. Every sample projects the object's origin through **that frame's own**
  view-projection, and `TransformHistory::explain` turns the window into a sentence naming which of
  the three moved: the scene moved it, that is parallax, both moved and neither is isolated, or --
  the one this exists for -- *"moved on screen with an unchanged transform and an unchanged camera:
  nothing in the recorded state explains it"*. `tests/unit/test_transform_history.cpp` builds each
  case beside the control that produces the same screen motion for a different reason, because a
  verdict that said "it moved" for both would be exactly as useful as looking at the screen, which is
  the thing that already failed. The unexplained case is built by zooming: a camera input that is not
  the view matrix. `onScreen` is the clip test rather than a guess from the position, so an object
  behind the camera is not placed back on screen by a naive divide.

## Phase 10: Canonical regressions

### 10.1 Glowmere/UFO regression

- `[~]` Test static geometry with camera dolly, orbit, rotation, translation and a pass through it.
  `[x]` **Both halves of the transform path are now covered, and neither moves.**
  - Renderer: `tests/rendering/test_gpu.cpp` `[gpu][renderer][forensics][static]` -- 680 frames,
    five motions, authored TRS and the renderer's diagnostic world matrix bit-identical throughout,
    plus a projection cross-check and a byte-identical return after a 240-frame excursion.
  - Composition: `tests/rendering/test_composition_gpu.cpp` `[gpu][composition][forensics][static]`
    -- the same five motions through `Engine` over RendererQA, asserting both the authored node world
    transform *and* the flattened entity transform on every frame. 4,488 comparisons.
    Negative-controlled: a one-millimetre change to `nodes/near-cube/position` fails it.
  - `[x]` **The other three axes are covered.** `tests/rendering/test_composition_gpu.cpp`
    `[gpu][composition][forensics][static][transport]` -- 1,733 comparisons with the camera parked
    and the *other* variable moving: seven timeline seeks (forward, backward, past the end, to zero,
    between frames), a 40-step scrub that alternates direction, 30 frames of ordinary playback as the
    control, six resolution changes ending on the size it started with, and three scene reloads each
    re-checked at two times. Negative-controlled per section: perturbing the baseline by a millimetre
    after the first comparison fails all five independently.
  - `[x]` **The same matrix now runs on Glowmere itself**, with the visitor procedural the original
    report was about. The camera axis was already there; the transport, the resolution and a reload
    are now beside it, with the camera parked so a failure can say which variable moved the object.
    Six seek excursions (forward, backward, to zero, past the end) each returning to the second the
    baseline was taken at, a 24-step alternating scrub, five awkward resolutions ending on the one it
    started with, and two reloads that rebuild every node from disk. Glowmere is the harder case on
    purpose: 278 entities, a live entity tier that has to be excluded, and an animated procedural
    that legitimately moves. The claim is not "nothing moves" but the sharper one -- **a static node
    is at the same place at second four however the playhead reached it, whatever size the frame is,
    and after the scene has been rebuilt from disk.**
- `[~]` Capture world transform, GPU transform, camera, bounds, visibility, LOD and object ID.
  `RenderObjectDiagnostic` carries the world position and matrix, the world bounds, the six frustum
  margins, visibility, the cull reason, the submitted flag, the GPU object slot, the mesh and a
  material fingerprint; `RendererDiagnosticFrame` adds the camera and the numbers its projection was
  built from. All of it is written to and read from a `FrameSnapshot`, and diffed into sentences.
  **LOD is the gap**: it is decided per instance in the procedural renderer's GPU cull pass and never
  becomes a per-entity number, so there is nothing to capture without changing that pass.
- `[x]` Classify apparent motion as transform, camera, GPU, culling, LOD, shader or post-processing
  behavior. Two instruments, and between them they cover the classification the phase asks for.
  `TransformHistory::explain` separates the first three over time -- the scene moved it, that is
  parallax, or nothing in the recorded state explains it -- which is the distinction a single frame
  cannot make. The bisection (Phase 8.3) attributes the rest: it returns the **minimal** set of
  subsystems the symptom needs, so "is this culling or post" is answered by a search rather than by
  switching arms by hand and forming an impression.
- `[x]` Add a permanent regression test for the proven root cause. The static-object matrices on four
  axes, negative-controlled by a one-millimetre perturbation on each.

### 10.2 Alien regression

- `[~]` Test idle, walk, run, loop, pause, resume, seek, scrub, close/far camera, orbit, terrain crossing, water proximity and reload.
  `tests/rendering/test_composition_gpu.cpp`, `[gpu][composition][forensics][alien][animation]`, 310
  assertions. The scene authors Idle, Walk and Run on three nodes at once, so every second exercises
  all three. Covered: playing to a second versus seeking to it, a 30-step scrub followed by four
  seeks, holding a parked playhead for eight frames, two reloads, and four camera distances. Not
  covered: terrain crossing and water proximity, which this scene has neither of.
  The claim is the one the phase-origin repair established, extended to every clip and every route
  to a second: **the pose at a second is a property of the piece, not of how the playhead arrived.**
  Negative-controlled by restoring the phase-origin defect, which fails it with the same signature
  the original experiment found -- 98 of 147 joints.
- `[~]` Record animation time, pose version, bone matrices, bounds, visibility and GPU skinning state.
  Bone matrices and visibility are compared directly by the matrix above; palette version and time
  are in the diagnostic snapshot. Bounds and GPU skinning slices are not compared per frame.
- `[~]` Run culling-off, animation-off, post-FX-off and depth-off comparisons.
  The rigged entities are checked never to be culled while the camera is on them, at four distances
  over twenty frames each. The subsystem-toggle comparisons remain open.
- `[x]` Identify the first subsystem that changes flicker behaviour and fix its ownership/state flow.
  **Animation, and the ownership repaired was the phase origin.** A pose jumped when the playhead was
  scrubbed because the origin of an *authored* animation state was the engine's first update rather
  than the timeline's zero -- so the same second reached two ways gave two poses, 98 joint matrices
  apart, with every entity transform identical. The fix is ownership, not a conditional: the first
  application anchors at 0.0, while a state requested during playback still starts when requested.
- `[x]` Add the regression test and evidence capture.
  `[gpu][composition][forensics][alien][animation]`, and its claim is the general form of the repair:
  **the pose at a second is a property of the piece, not of how the playhead arrived.** Playing to a
  second versus seeking to it, a 30-step scrub followed by four seeks, a parked playhead held for
  eight frames, two reloads, four camera distances. Negative-controlled by restoring the defect,
  which fails it with the same signature the original experiment found.

### 10.3 Water regression

- `[~]` Test shoreline approach, parallel shoreline, above water, near water, crossing water and
  sloped terrain. Phase 6.2 covers six viewing situations over a generated shoreline -- the camera
  turned so the same water moves from the centre of the frame to its edge, a 2 cm camera sweep along
  the shore, four depth bands from shallow to deep, and the real 21-chunk world -- plus the six-view
  synthetic shoreline test from earlier. Crossing the surface (the camera passing through it) is not
  covered.
- `[x]` Compare water effects off/on, post FX off/on and depth visualizations. The water arm is the
  instrument for every measurement in 6.2 -- water pixels are *found* by A/B against the same frame
  with it off, because they cannot be found any other way. Post and AO are off throughout, and the
  reason is stated: auto-exposure re-meters when frame content changes, so an absolute threshold on a
  tone-mapped pixel compares two exposures rather than two surfaces. The depth visualisations arrived
  in Phase 4.4.
- `[x]` Identify whether leakage is geometry, depth reconstruction, stencil/mask, render target or
  shader coordinates. **Geometry.** `SYM-WATER-1` was a quad emitted when any corner is wet, with a
  dry corner reporting a borrowed depth. The depth-reconstruction hypothesis -- the one people reach
  for first -- is *refuted*, not merely untested: the linear-depth target is a forward-axis distance
  to within 2.8 mm over 988 taps, and the test is known to discriminate because the two candidate
  spaces are 29.4% apart there. There is no stencil or mask involved (water writes no identifier),
  and the reprojection's Y convention matches the linear-depth pass's own.
- `[x]` Add image and state regression coverage for the proven cause. Two instruments on the cause
  itself (pixels on synthetic shoreline geometry, and the geometry invariant on the real generator,
  which is what found it), and eight further cases in Phase 6.2 with six source-level negative
  controls run and restored.

## Phase 11: Performance safety and delivery

### 11.1 Diagnostic performance safety

- `[x]` Confirm diagnostics have negligible cost when disabled -- **and they are never disabled**,
  which is the more useful finding. The per-object diagnostic frame is built unconditionally for
  every entity every frame. Removing the string copies from Glowmere's 278 entities moved the wall
  median from 22.11/21.91 ms to 22.20/22.01 ms across paired runs: inside the spread. The honest
  claim is "below ~0.3 ms on the heaviest canonical scene", not "free".
- `[x]` Avoid per-object per-frame logging by default; log changes, invalid values and selected
  objects. Audited rather than assumed. There is exactly one per-frame log statement in the renderer
  and it satisfies all three clauses at once: `log::debug`, for the **selected** object only, and
  emitted only when the camera moved, the object's state changed, or the object went non-finite. The
  two counter dumps are environment-gated (`AVGEN_FRAME_COUNTERS`, `AVGEN_CPU_STAGES`) *and* every
  thirtieth frame. The finite guards log the values they refuse, which is the "invalid values" clause
  and the reason the ASan run doubles as a check on them.
- `[x]` Avoid unnecessary CPU/GPU synchronization, expensive bounds work and full-frame readbacks.
  **No full-frame readback happens in a live frame at all**: `readTexture8`/`readTextureF16` are
  reached only through `renderToImage*`, which is the offline and test path. Every blocking `WaitAny`
  in the renderer is a `PopErrorScope` at *pipeline creation*, not per frame. The cull-stats readback
  is deliberately non-blocking -- which is the right call for cost and is exactly what
  the right call for cost, and it was wrongly suspected of causing `SYM-TERRAIN-1` -- the
  synchronisation here is right, and the defect was elsewhere.
  The one genuinely per-object per-frame cost is the diagnostic's world bounds and six frustum
  margins, measured below.
- `[x]` Measure diagnostic overhead with the same canonical scenes and resolutions. Measured on the
  heaviest one, which is the only one where it could show: Glowmere's 278 entities, paired runs,
  22.11/21.91 ms against 22.20/22.01 ms with the string copies removed -- inside the run-to-run
  spread. The other two canonical scenes cannot sharpen that and the reason is already recorded:
  Constellation's median is not a stable statistic (an animated particle fill), so a difference of
  this size is unmeasurable there by construction. The claim stays "below ~0.3 ms on the heaviest
  canonical scene", not "free".

### 11.2 Correctness and performance validation

- `[~]` Re-measure CPU/GPU frame time, p90/tail, draw calls, visible/culled renderables, animated vertices, water/shadow/post costs and buffer uploads.
  Frame time, tails, draws and triangles re-measured on all three canonical scenes (Phase 0.1).
  Per-pass water/shadow/post costs are in the same output; buffer uploads and animated vertex counts
  are not reported by the harness and remain open.
- `[x]` Profile Constellation and Glowmere separately; do not generalize one workload to the other.
  They behave differently enough that one number for both would be meaningless: Glowmere is 84%
  `scene` and reproduces to 1.4%, while Constellation is volumetrics over an animated particle fill
  whose median is not a stable statistic at all. Both recorded in Phase 0.1.
- `[~]` Run release, debug, ASan/UBSan and TSan suites relevant to changed paths.
  Release: clean, every run -- and "use release for acceptance" stopped being a convention and became
  a finding when `SYM-TERRAIN-1` turned out to fail in debug and pass in release. **ASan/UBSan over
  the forensics unit tests: 1,822 assertions, 14 cases, no sanitizer findings**; over the changed GPU
  paths, 20,501 assertions across 9 cases, also clean. TSan beyond the transport contract remains.
  Earlier note retained: -- and the run doubles as a check on the new guards, whose messages appear
  in its log naming the light, the values, the entity and its transform. GPU forensics under ASan and
  a TSan pass over the same filters remain.

  One thing to know before running it: `cmake --build --preset asan` fails to *link* two auxiliary
  tools (`avgen_world_preview`, `avgen_help_lint`) on undefined `libavgen_core` symbols. It predates
  this work and does not touch the test binaries -- build `--target avgen_tests` or
  `--target avgen_render_tests` and the sanitizer suites run fine.
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
- `[x]` Investigate any order-dependent, contention-sensitive or intermittently failing test before
  sign-off. Four found, and **only one of them is a defect** -- the other three are the harness
  lying, which is why they are listed here rather than quietly worked around.

  1. **`SYM-TERRAIN-1` (a defect, since fixed).** Seven debug failures that release did not have.
     Not flakiness and not a race: ambient occlusion dropped its history on a repeated frame index,
     so the second render of a frame was a different picture from the first. Debug and release now
     agree, and the four wrong attributions made along the way are the more useful record -- see the
     report.
  2. **Concurrent GPU runs are not safe** (harness). Two test binaries at once produce failures in
     both -- 145 channels between two draws of one `FrameTime`, and checkpoint mismatches. `ctest`
     already serialises with `RESOURCE_LOCK gpu`; a manual parallel invocation does not, and there is
     one GPU, so running two does not make them finish sooner.
  3. **Editing a shader during a run invalidates the running binary** (harness, and the sharpest of
     the three). Shaders load from `AVGEN_SHADER_SOURCE_DIR` at *runtime*, so a `.wgsl` edit while a
     suite is in flight makes pipeline creation fail and reports **32 cases failing in
     `renderer.init()`** for no reason of their own. It cost an hour and a wrong conclusion before it
     was understood.
  4. **The unit binary takes a SIGPIPE in a single-process full run** (pre-existing, confirmed by
     stashing every change in this investigation). The fake-ffmpeg failure test writes to a child
     that has exited. `ctest`, which runs each case as its own process, is unaffected, so it does not
     block validation -- but `./build/release/tests/avgen_tests` with no filter cannot reach the end,
     and anyone who tries will think the suite is broken.

  The rule that falls out of 2 and 3, and is now in "Useful validation commands": a GPU result is
  only evidence when nothing else is running and nothing is being edited.

### 11.3 Architectural repair standard

- `[x]` For every defect, prefer repairing ownership/state flow over adding a conditional. Held for
  nine of eleven repairs, and the two exceptions are named rather than glossed.

| Defect | Repair | Kind |
|---|---|---|
| Scene-local GPU cache collisions | the owning `Scene` joins the reuse key | ownership |
| Temporal history crossing boundaries | reset at the boundary, plus a transport discontinuity revision | ownership |
| Particle pools crossing scenes | pools belong to the scene that made them | ownership |
| Particle pools surviving a seek | `resetTemporalHistory` calls the reset that already existed for it | ownership |
| Detached composition parameters | `detach()` invalidates every node-owned pointer, not some | lifetime |
| Animation phase origin | the origin is the timeline's zero, not the engine's first update | ownership |
| Water over dry ground | a dry corner reports the depth *at itself* rather than a borrowed one | ownership |
| A node's `material` block | a parse moved out of a branch it was never meant to be inside -- a conditional **removed** | ownership |
| Diagnostic views tone-mapped | the pass moved to the other side of the tone map | structural |
| Previous skinning palette on a seek | a flag, consumed by the next evaluate | **conditional** |
| Non-finite material | substitute and warn | **conditional (a guard)** |

  The palette flag is the one to argue with, and the alternative was tried on paper first: collapsing
  `previousPalette` onto `palette` inside `seekSeconds` is the ownership-shaped fix, and it is
  *wrong* -- at seek time the rig has not been re-posed, so it pins the pose being left and the next
  evaluate copies it forward again. The information genuinely does not exist until the next
  evaluation, so something has to carry "a discontinuity happened" across that gap. The guard is a
  conditional by definition; a guard that repaired ownership would not be a guard.
- `[x]` Consider a subsystem rewrite only when its invariant cannot be maintained incrementally,
  incremental fixes worsen the design, the replacement is smaller/cleaner and it can be tested
  independently. Applied, and it selected **nothing**.
- `[x]` Candidate rewrite areas, only if evidence supports them. Each considered against the evidence
  actually collected:
  - *Transform extraction*: no. The transform path is the most heavily pinned thing in the repository
    -- four axes of static-object matrix with bit equality, a per-object comparison against an
    independent renderer, and a full world-to-pixel chain -- and it has produced **no defects**.
  - *Render-object extraction*: no. Object state is already a single function (`makeItem`) shared by
    the camera and shadow paths precisely so the two cannot describe an object differently.
  - *GPU object buffers*: no. 21 structure pairs agree byte for byte against the WGSL, slots are
    dense and deterministic, and the alternating-transform case found nothing.
  - *Water pass*: no, and this is the one where the evidence changed the answer. The shoreline
    symptom looked like a depth-space problem in the water shader, which would have been a rewrite
    candidate; measurement refuted that (2.8 mm agreement, with the wrong space 29.4% away) and the
    real defect was one line of terrain geometry.
  - *Skinning*: no. Two seek defects, both in *when* state was resynchronised rather than in how
    skinning works.
  - *Culling*: no. The cull's own bounds rule was duplicated and is now one function; that is
    extraction of a rule, not a rewrite of a subsystem.
- `[x]` Document why a rewrite was or was not justified. **No rewrite is justified, and the shape of
  the evidence says why:** of eleven defects, nine were *state ownership at a boundary* -- a cache
  key, a history, a pool, a parameter lifetime, a phase origin, a palette across a jump -- one was
  geometry, and one was a pass on the wrong side of the tone map. Not one was a subsystem whose
  internal design could not hold its invariant. Rewriting any of them would have preserved every
  defect, because none of them live inside a subsystem; they live between two.

  `SYM-TERRAIN-1` was the last candidate for a discipline change and turned out not to need one
  either: it was one state machine conflating two cases, repaired in place.

## Phase 12: Final forensic report

The deliverable is [renderer-forensics-report.md](renderer-forensics-report.md); these are its
sections, and each is ticked when that section says something specific enough to be wrong.

- `[x]` Produce the final renderer architecture map with actual state flow and authoritative owners.
  "Architecture map", "The four paths" and "Ownership currently established".
- `[x]` Produce a bug table for every meaningful issue:
  `bug | symptom | reproduction | root cause | evidence | affected subsystem | fix | regression test | residual risk`.
  Eight rows, each with a named reproduction and a named regression test.
- `[x]` Produce subsystem isolation results for transforms, camera, basic geometry, GPU object state,
  culling, animation, skinning, water, depth, transparency, shadows, post FX, sequencer, assets and
  performance. The "Subsystem status" table, with every `PARTIAL` naming what is still unproven.
- `[x]` Explain the reference renderer contract, scope and comparison results. It is coverage and not
  colour, over five views of `renderer-qa-minimal`, and the section says why an absolute brightness
  threshold does not survive the comparison.
- `[x]` Document every RendererQA test and progressive matrix result. Two tables in the report: the
  five scene variants with *why each exists* (a variant that exists for no reason is a scene nobody
  maintains), and the sixteen matrix levels with their results -- including the three rungs that have
  no control and say so, and the two gaps the change-the-picture check found.
- `[x]` List all diagnostics, their intended use and overhead. "Diagnostics delivered" and
  "Diagnostics and their overhead", with the honest claim being "below ~0.3 ms on the heaviest
  canonical scene" rather than "free", because the difference is inside the run-to-run spread.
- `[x]` List automated, manual, GPU-capture and sanitizer regression coverage. A table in the report
  with all four and their standing, including the two honest entries: manual visual review found
  **no** defect in this investigation and no regression depends on it, and GPU capture covers nothing
  at all -- a real gap, named, with what it would be for.
- `[x]` Document performance impact and measurement conditions. In "Diagnostics and their overhead",
  including why Constellation's GPU median cannot be one of the numbers.
- `[x]` List remaining issues honestly; anything not proven fixed remains open. "Open evidence gaps",
  and every `PARTIAL` in the subsystem table.
- `[x]` State whether the renderer architecture is sound enough for continued production work, with
  evidence. "Is the architecture sound enough to keep building on": yes, with the reservation that
  the evidence is about the paths that have been walked.

## Completion gate

**All nine are answered, with evidence, in the report's "The completion gate, answered".** The two
worth reading first are the ones whose answer is not what the question assumed: a static object does
not move, and the three times it appeared to, the instrument was wrong rather than the renderer; and
water does not leak by depth reconstruction, which is refuted by measurement rather than left
untested.

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

**Debug and release now agree**, since `SYM-TERRAIN-1` was fixed; before it, debug failed seven
cases release passed on the same source and the same idle machine. Release is still the acceptance
configuration, and a divergence between the two is worth treating as a finding rather than as noise:
last time it was one. Two further rules, both learned the hard way and both costing a session's
worth of confusion each: **nothing else may be running** (`pgrep -f tests/avgen_render_tests`), and
**no source may be edited while a run is in flight** -- shaders are loaded from
`AVGEN_SHADER_SOURCE_DIR` at *runtime*, so editing a `.wgsl` mid-run invalidates the running
binary's pipelines and reports 32 cases failing in `renderer.init()` for no reason of their own.



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

## Session log: 12 September 2026 (second pass)

**Closed.** Phase 0.1 (baselines, conditions, symptom register), Phase 0.2 (evidence package and
status definitions), Phase 1.2 camera ownership, Phase 2.3 in full, Phase 3.1 matrix conventions,
Phase 5.1's limb-crossing regression, Phase 9.2's replay experiment, and the transform half of
Phase 10.1.

**One defect found and fixed:** the animation phase origin (`SYM-ANIM-1`). It was found by the
Phase 9.2 experiment on its first run, which is what the experiment was for.

**Three hypotheses disproven, recorded so they are not retried:**

1. *A limb-crossing regression can be written against the alien.* It cannot. A T-pose bind box is
   wider than every pose the clip animates into, so bind-pose and posed bounds agree there and the
   test passes with the posed-bounds path reverted. Use a rig that reaches past its bind pose.
2. *The diagnostic state hash can serve as a replay identity.* It cannot; it folds in a monotonic
   `paletteVersion`. It is a change detector.
3. *`CompositionNode::transform` is authoritative.* It is not; it is re-derived from
   `nodes/<name>/position` every frame. The parameter is the authoritative value.

**Also:** the cull-bounds computation was written out twice inside `Composition` and is now one
function, `scene::entityCullBounds` -- which is what made the Phase 5.1 property testable at all.

**Check 2 done: `SYM-STATIC-1` on Glowmere, against the `visitor`.**
`[gpu][composition][forensics][static][glowmere]`, 75,939 assertions. Five camera motions aimed at
the object the symptom was reported against -- a static procedural 190 m from the origin -- asserting
its authored node world transform, its flattened entity transforms and, the part RendererQA never
reached, `ProceduralGeometry::sourceTransform` and `distributionTransform`. **Nothing moves.**

Three things had to be got right before that meant anything, and each was wrong first:

1. **Time must be held still.** The symptom is that an object moves *as the camera moves*, so the
   camera has to be the only variable. Advancing the clock as well finds the `visitor` turning --
   which is the scene working, and an experiment that cannot tell that from drift answers nothing.
2. **A composition's camera is not `scene().camera`.** That field is re-derived from `camera/mode`,
   `camera/position` and `camera/target` on every `applyParameters`, so writing it directly is
   overwritten before the frame is drawn. Both composition tests were written that way first and
   were therefore **vacuous** -- asserting nothing moved while the camera in fact stood still. They
   now drive the parameters, select free mode, and `REQUIRE` that the camera took the pose, which is
   what makes the vacuity impossible rather than merely unlikely. This is also the answer to the
   11 September QA note that "camera overrides via the project's parameters did not move the camera":
   free mode has to be selected or the orbit computes over the writes.
3. **The `visitor` hovers.** With the camera parked and time advancing it turns *and* drifts about
   2.4 cm over a second and a half -- measured, and now asserted as a bounded hover rather than the
   "it does not travel" the test claimed first.

**That third point is the likely explanation for the original report.** A large, distant, slowly
turning object with a small periodic drift and no animation cue reads as a static object moving. The
first version of this very test made the same mistake.

**Check 1 done: the replay on Glowmere.** `[gpu][composition][forensics][determinism][glowmere]`.
278 entities, terrain, water, vegetation, a wind field and two characters. Exactly **one** transform
diverged -- the `wanderer` -- by about 25 m, with joints, visibility and culling flags all identical.

The investigation ended at a **contract, not a defect**: ADR-091 puts an ambient `EntityWorld`
character in the **live tier**, "stateful, reset on seek, and *explicitly not frame-accurate under
scrub*". Its `seek` makes the result plausible, not reproducible, and that is the decision. Recorded
because the next person to run this will read 25 m and call it a bug.

The test now asserts the **boundary** instead of blanket equality: every entity outside the live tier
must replay exactly, the live ones may differ, and an entity that is not live and moves is named in
the failure. Negative-controlled by classifying nothing as live, which fails it. The image hash is
deliberately *not* compared -- a live character 25 m away changes pixels, and demanding equality
there would be demanding what ADR-091 declines to promise.

**Next smallest falsifiable checks**, in the order I would take them:

1. Extend the static-object matrix along the axes it does not cover: timeline seek, scrub, scene
   reload and resolution change. The renderer has separate coverage for each; nothing crosses them
   with a static object.
2. Then `SYM-WATER-1`, which needs a scene before it needs a test.
3. Audit the remaining derived-copy pairs. Two have now been found by accident rather than by search
   -- `CompositionNode::transform` and `scene::Camera` are both re-derived from parameters every
   frame -- and Phase 1.3's table has not actually been built.

## Session log: 13 September 2026 (third pass)

**Closed.** `SYM-WATER-1` (reproduced, root-caused, fixed). The static object's remaining three axes
(seek, scrub, reload, resize) and Phase 1.3's duplicate search. Phase 1.4's mutation assertions and
Phase 5.1's "culling never mutates authored state". Phase 3.3's alternating-transform test. Phase
6.3's transparency isolation. Phase 6.1's shoreline view matrix. Most of Phase 10.2. The Phase
0.1/11.2 performance remeasurement.

**One defect found and fixed:** water standing over dry ground at a descending shoreline. A dry
corner of the water sheet borrowed a wet neighbour's *surface level* -- correct, it keeps the sheet
flat to the bank -- and then computed its depth attribute against that borrowed level, claiming up to
2.9 m of water over ground the world calls dry. The shader's shore fade, which is the only thing that
hides the sheet's deliberate one-cell overhang, covers 0.75 m; `smoothstep(0, 0.75, 2.9)` is 1. It
came out fully opaque.

**One measurement defect found:** the Constellation baseline's GPU median is not a statistic. Five
runs of the identical command gave 6.62 to 10.75 ms with the tails unmoved. The scene is animated, a
120-frame window never reaches steady state, and a 400-frame run medians *lower* than a 120-frame one
-- the opposite of a warm-up. Compare its tails or a fixed frame index.

**Two more hypotheses disproven, recorded so they are not retried:**

1. *Pixels can answer "is there stale GPU object data".* They cannot, when the test moves anything.
   Two cubes exchanging places is motion, and a running renderer carries previous-frame matrices, an
   AO history and an adapting exposure that a cold one does not: 22 of 24 frames differed with the
   object data perfectly correct. Ask the object diagnostics instead.
2. *`SceneRenderer` needs a runtime assertion that it does not mutate scene state.* It cannot mutate
   it. All three entry points take `const scene::Scene&` and the only `const_cast` under
   `src/rendering` is on the renderer's own LOD bookkeeping.

**Two more vacuous tests caught by negative controls**, bringing the total to five. The derived-copy
contract test, whose `setBase` writes never reached `applyParameters` because nothing refreshed the
*final* values. And the transparency sorting test, whose opaque backstop sat *between* the two panes
it was sorting, so one was always occluded and the pair never composited together -- reversing the
renderer's sort did not disturb it.

**Next smallest falsifiable checks**, in the order I would take them:

1. The alien matrix's two uncovered axes -- terrain crossing and water proximity -- which need a
   scene that has both before they need a test. Phase 5.4's character/terrain Y-ownership question is
   the same scene.
2. The water mask/depth *visualisations* (Phase 4.5, 6.2). The leakage question is answered on two
   instruments; what is still missing is the ability to look at a shoreline pixel and see which of
   water depth, terrain depth and the fade produced it.
3. Phase 2's immutable frame snapshot and reference renderer, which is the largest remaining
   structural item and the one every "compare reference to production" task below it waits on.

## Session handoff

At the end of every work session:

1. Update the nearest task status and add evidence links.
2. Record commands run and whether they passed, failed, skipped or were inconclusive.
3. Record any new hypothesis that was disproven, so it is not retried without new evidence.
4. Leave the next smallest falsifiable check in this document.
5. Do not mark a parent task `[x]` while any required child task remains `[ ]` or `[!]`.

## Carrying this into the rendering engine upgrade

Written 2026-09-13, when the checklist stood at 241 done, 44 partial, 13 blocked, 0 open. The
remaining items are not a backlog to burn down before the upgrade; sorted against it they fall into
three groups, and only one of them is worth doing first.

### The 13 blocked items are a design brief, not a backlog

Every one of them is blocked on the same missing thing: **a draw cannot say which subsystem it came
from.** Disable terrain needs a flag on the entity, because a chunk arrives as an ordinary lit
entity. Disable LOD needs a change to the GPU cull pass. The water mask and the intersection mask
need water to write an identifier it deliberately does not write. Matrix levels 2, 3, 13 and 14 are
blocked *on those four*.

So the single requirement that unblocks nine of the thirteen is: **every draw carries its origin --
which subsystem submitted it, and at which LOD.** Doing that in the current renderer is speculative
work on a structure that is about to change. Doing it *as part of* the upgrade is nearly free, and it
is the difference between a renderer that can be bisected and one that cannot. The remaining four
(freeze transforms, freeze static transforms, disable depth test, disable depth write) are pipeline
and ownership questions that the upgrade will answer one way or another anyway.

### Most of the 44 partials will be invalidated, and should not be finished now

The pass contract inventories, the panel, the isolation arms' remaining edges, the per-helper pass
boundaries, the GPU object index view -- all describe *this* renderer's internal structure. Finishing
them buys documentation of something about to be replaced. Leave them; the parts worth keeping are
the rules in "Working rules", which are about method rather than about this implementation.

### What was worth doing before the upgrade, and is now done

Three things, chosen because **none of them can be made afterwards**:

- **Frame-state baselines.** `examples/qa/baselines/*.snapshot.json` and
  `[gpu][composition][forensics][baseline]`: the state the renderer derives from three canonical
  scenes, compared field by field, reported as sentences. Not pixels -- an upgrade changes those by
  design. The terrain scene was excluded while `SYM-TERRAIN-1` stood; it is a candidate to add now
  that the scene renders the same frame twice.
- **A cost baseline with its conditions**, in [renderer-pre-upgrade-baseline.md](renderer-pre-upgrade-baseline.md),
  including the two caveats that stop it being misread: Constellation's median is not a stable
  statistic, and the two canonical scenes do not generalise to each other.
- **`SYM-TERRAIN-1` fixed**, rather than carried. It was the only known nondeterminism in the
  renderer, and every image comparison used to validate an upgrade is weaker while one stands.

### `SYM-TERRAIN-1` is fixed, and that was the right order

The only known nondeterminism in the renderer, and it turned out to be one state machine conflating a
*repeat* with a *discontinuity*: 1,874 differing channels before, 0 after, debug and release now in
agreement, and every assertion that had been softened to report it restored to exact.
