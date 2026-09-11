# Renderer Stabilization QA: 2026-09-11

This document supersedes the older Glowmere product TODO for the current renderer QA pass. The
active authored project is [The Living Constellation](../examples/constellation/constellation.json);
Glowmere remains a regression scene because it exercises terrain, water, vegetation and the
imported UFO asset. The attached stabilization brief is the acceptance contract for this pass.

## Current baseline

- Current worktree contains user/agent changes from earlier scene and painterly experiments. Do
  not reset or discard them. Inspect `git status --short` before editing.
- The renderer is C++23/CMake/Ninja, Dawn WebGPU on Metal. Current machine is Apple M2 Max;
  this is not evidence for a base M2.
- Constellation at 1280x800, realtime, 120 frames: zero GPU errors, 11 draws, 3,121 triangles,
  9 particle systems, 4.19 ms median GPU and 5.90 ms median wall time over the warmed sample.
  It currently has no mesh entity instances in that run, so it is not a meaningful transform,
  character or water stress test.
- Glowmere painterly at 1280x800, realtime, 120 frames: zero GPU errors, 152 draws, 428,633
  triangles, 2,329 visible / 114,283 culled instances, 23.79 ms median GPU and 28.27 ms median
  wall time over the warmed sample. This is the current broad renderer baseline, not a 60 FPS sign-off.
- The final full release suite discovers 1,510 tests: all 1,510 passed, with four optional tests
  skipped. The Syphon burst test passes after the notification-to-texture retry hardening. The
  skipped tests are two Khronos sample imports, external ffmpeg encoding and NDI runtime support.

## Implemented in this pass

- [x] `scene::updateRigs()` no longer treats camera-frustum culling as authored invisibility.
  Camera culling suppresses drawing, but rigs for `visible` entities continue evaluating against
  the absolute timeline. This prevents a character from freezing at a frustum boundary and then
  popping when it returns.
- [x] Regression test: `camera culling does not freeze an authored-visible rig` in
  `tests/unit/test_skeleton.cpp`, `[scene][animation]`.
- [x] Focused unit suite passes: 57 assertions across 9 animation tests.
- [x] Existing GPU skinning suite passes: 47 assertions across 3 cases.
- [x] Constellation and Glowmere 120-frame headless baselines render with zero GPU errors.
- [x] Full release suite completed green: 1,510 passed, four optional skips, zero failures.
- [x] The shadow-workload timing threshold also passes in the final suite; its earlier isolated
  miss was timing variance, not a reproduced renderer correctness failure.
- [x] Terrain runtime visibility regression fixed: view-distance culling no longer writes
  `Entity::visible`, so terrain and water return when the camera comes back. The existing terrain
  shadow test now covers leave/return behavior and passes 103 assertions.
- [x] Previous-model history now advances for every entity, including camera-culled entities.
  This prevents false motion-vector streaks when an object moves while hidden and re-enters without
  moving. The new GPU regression compares that re-entry frame with a fresh renderer.
- [x] Renderer temporal history resets on backward timeline time. Previous camera/model matrices
  are cleared on reverse/seek discontinuities so the first reversed frame does not inherit false
  motion. Regression compares reused and fresh renderers at the same timestamp.
- [x] Water color output now matches the conventional `SrcAlpha` blend state. The shader was
  returning RGB already multiplied by alpha, so the fixed-function blend multiplied alpha twice;
  shallow water and shoreline colors were darkened. Shader compilation and Glowmere rendering pass
  after removing the extra RGB multiplication.
- [x] Static-camera transform invariant regression added. A camera move and return leaves the
  entity's authored TRS unchanged and restores the original image; renderer suite passes 58
  assertions across 5 cases.
- [x] Syphon latest-wins burst test hardened against the transport's notification-before-texture
  transient. Three consecutive isolated runs pass; the test still fails on a deadline if a frame
  remains unreadable.

Water audit note: the initial suspicion that `shaders/water.wgsl` used clip-space coordinates for
`screenUv` was disproven. Its `@builtin(position)` fragment input is framebuffer coordinates,
and the same `in.clip.xy * frame.targetSize.zw` convention is used by the established PBR path.
No water shader change was made without a reproducer; water-mask/shoreline image coverage remains
an open QA item.

## Architecture trace

### Transform and camera ownership

- `scene::Transform::matrix()` in `src/scene/scene.cpp` is the canonical TRS-to-model conversion.
- `scene::Entity::transform` is authoritative world space; imported glTF hierarchies are flattened.
- `Composition::nodeWorldTransform()` composes scene-node parents before flattening.
- `SceneRenderer::render()` writes `ObjectUniforms::model`, `normalMatrix`, `prevModel`, IDs and
  joint count into one aligned object slot. Procedural instances use instance records and their
  own shader path; they receive identity object matrices from the scene renderer.
- `Camera::view()` is `glm::lookAtRH`; projection is `glm::perspectiveRH_ZO`, matching WebGPU 0..1
  depth. One view-projection feeds culling, clusters, shadows, depth reconstruction, AO, picking,
  volumetrics and debug overlays.

### Culling and animation

- `Composition::update()` poses characters before `cullEntityNodes()`. Skinned entity AABBs are
  derived from the current joint palette; unskinned entities use their mesh bounds. Both retain
  the existing 25% extent + 0.25 m conservative pad.
- `scene::updateRigs()` evaluates visible rigs using timeline time and explicit distance policy.
  It now ignores `cameraCulled` for pose eligibility, but true `visible=false` and authored
  `cullDistance` still hold a rig.
- Remaining risk: malformed/partially weighted meshes fall back to bind-pose bounds, and the
  current residual pad is still a heuristic for interpolation/numerical edge cases. Add a frustum
  edge image regression before calling animated culling complete.
- GPU skinning uploads palettes through `SkinningRenderer`; the renderer does not pose rigs.

### Render order and depth

The main scene order is background layers, shadows, background clear, optional depth prepass,
linear-depth resolve, AO/shadow mask, opaque geometry, SDFs, skybox, water, grids/particles/
blended geometry, volumetrics, debug, post layers, DoF/motion/bloom/grading, auxiliary debug,
tone mapping and composition overlay.

- Opaque lit: `LessEqual`, depth writes enabled.
- Blended lit/grid/water: `Less`, depth writes disabled.
- Depth-only: `Less`, depth writes enabled.
- Skybox: `LessEqual`, depth writes disabled.
- Water is a geometry-bound scene pass after opaque geometry and sky, not a global post effect.
  It reads linear opaque depth for thickness/shoreline, blends HDR color, adds emission, and does
  not write depth. Water entities are generated per terrain chunk and share the chunk visibility.

### Frame resources and timeline

- Frame uniforms are written once per frame; entity object data is staged into aligned dynamic slots.
- Joint palettes use a dynamic-offset buffer managed by `SkinningRenderer`.
- Previous model matrices are keyed by entity name for velocity; duplicate entity names are a
  remaining diagnostic risk because names are not guaranteed to be globally unique by the renderer.
- `Engine::seekSeconds()` resets audio, modulation, cues and entity state; simulation catches up or
  resets on backward time. Character animation uses absolute render time, which is deterministic.

## Prioritized TODO

### P0: prove and fix catastrophic correctness

- [x] Fix reversible terrain/water visibility. Previously the terrain update path used
  `visible = visible && on` and set distant terrain `visible=false`; neither state was restored
  when the camera returned. Runtime camera state now uses `cameraCulled`, and distant chunks also
  clear `castsShadow` for that frame. Regression: `[composition][terrain][shadows]`.
- [x] Static-object transform regression covers camera motion, a far-from-origin object, resize and
  timeline seek. It preserves authored TRS and restores the image after returning to the original
  camera/resolution. Object-slot/readback identity remains a separate diagnostics task.
- [ ] Add targeted transform diagnostics for selected entity name/ID: world transform, model
  translation, camera position, camera/view-projection terms, culling state and object slot index.
  Log only changes or invalid values, not every frame.
- [x] Duplicate entity names are rejected at the scene boundary; existing unit coverage protects
  the stable model-history key assumption.
- [x] Renderer boundary validation rejects non-finite camera view/projection and entity model
  matrices before GPU submission. Focused NaN/Inf regression passes; broader joint/bounds validation
  remains open.
- [x] Skinning upload validation rejects non-finite current/previous joint palettes before GPU
  staging. Invalid rigs are skipped for that frame with a targeted warning; the malformed-palette
  GPU regression passes without WebGPU errors.
- [x] Camera view construction now chooses a fallback up axis when forward and authored up are
  parallel. This prevents `lookAtRH` from generating NaN matrices for top-down/edge-on shots. The
  camera unit suite and disc-emitter GPU regression both pass.
- [ ] Audit object uniform ring/dynamic offsets and per-frame writes under rapid scene changes.
  Use object IDs and a two-frame alternating transform test to detect stale object data.
- [x] Fix stale model history across camera culling. `prevModelsNext_` was previously populated
  only when `makeItem()` submitted a camera/shadow draw; it is now populated from all scene entities
  before submission. Regression: `[gpu][motion][blur]` re-entry case.

### P1: animated characters and culling

- [x] Camera culling no longer freezes timeline pose evaluation.
- [ ] Build an animated-bounds image regression at a frustum edge: bind pose outside/inside versus
  posed limb crossing the plane. Compare culling enabled/disabled and prove the visible pixels remain.
- [x] Choose the first architectural animated-bounds solution: pose before culling and derive
  skinned bounds from the current palette, with bind-pose fallback for invalid/partially weighted
  meshes. Do not permanently disable culling.
- [ ] Stress-test the posed-bounds path on large imported characters and measure its CPU cost;
  replace per-vertex evaluation with a cached conservative envelope only if evidence requires it.
- [ ] Verify character terrain grounding has one authority: world X/Z, terrain query, root motion,
  visual offset and foot offset must not overwrite one another in a feedback loop.
- [ ] Add tests for idle/walk/run, loop, transition, pause, seek, reverse, close/far camera and
  terrain crossing. Cover no T-pose, no one-frame disappearance, stable palette and deterministic
  replay.

### P1: water and terrain boundaries

- [x] Correct water alpha convention: `shaders/water.wgsl` now returns non-premultiplied RGB with
  alpha, matching `water_renderer.cpp`'s `SrcAlpha/OneMinusSrcAlpha` blend.
- [x] Minimal native water image regression covers compositing over an opaque bed, deterministic
  repeat rendering and a distinct no-water frame. Broader shoreline angle/camera cases remain open.
- [x] Native water image coverage now includes above-water, grazing, near-parallel and below-surface
  camera views with deterministic repeat checks. A larger authored shoreline scene remains open.
- [ ] Build a larger water QA scene with flat/steep/shallow/deep/angled shore cases.
- [ ] Visualize water geometry mask, opaque linear depth, reconstructed thickness, shoreline fade,
  foam mask and water object ID. Confirm all effects are zero outside water geometry.
- [ ] Add GPU image tests for no water over dry terrain, stable edge under camera motion, no z-fight,
  correct terrain-through-water depth and deterministic flow after seek.
- [ ] Verify water sort order for overlapping chunk surfaces and that chunk/world transforms match
  terrain visibility. Do not solve seams with arbitrary depth offsets.
- [ ] Audit water shader coordinate spaces: surface ripple/noise is world-space, screen depth/AO is
  screen-space, and object/world transforms are explicit. Document any intentional approximation.

### P1: render state and frame lifetime

- [ ] Enumerate every pass's pipeline, bind groups, vertex/index buffers, dynamic offsets, depth,
  blend, stencil, viewport/scissor and target ownership. Assert/clear state at pass boundaries where
  Dawn does not make state implicit.
- [ ] Stress resize, scene reload, timeline seek/reverse, rapid camera cuts and frame-index reuse.
  Check readback ring, uniform staging, joint palettes, water uniforms and bind groups for stale data.
- [x] Direct renderer reverse-time regression passes 42 assertions. Broader Engine-level seek,
  reload, resize and rapid-cut stress remains open.
- [x] TSan sequence seek/determinism coverage passes 148 assertions across 8 cases; TSan animation
  coverage passes 57 assertions across 9 cases.
- [x] ASan/UBSan focused animation and terrain checks pass; TSan sequence and animation checks pass.
  Broader resource-lifetime and full TSan suite coverage remains open.
- [x] Syphon burst synchronization is covered with a deadline-bounded retry; the in-process client
  no longer treats a transient nil texture after a frame notification as a permanent failure.

### P2: diagnostics and permanent torture scenes

- [x] Extend the existing `DebugViewOptions`/`DebugDraw` facilities with opt-in ordinary-entity
  bounds, origins/axes and selected-entity filtering. Camera-culled entities are highlighted red.
  Procedural bounds/IDs remain available through the original options; depth/motion/water masks
  still use auxiliary-target work below.
- [ ] Add runtime toggles for culling, animation, LOD, water, transparency, post, bloom, shadows,
  particles, terrain and VFX. These are isolation switches only, never production fixes.
- [ ] Create `RendererQA` with labeled static cube, glTF, disabled-animation mesh, terrain, water,
  transparent object, particles, character, UFO, near/far/behind-camera/extreme-angle objects.
  Include camera path, resize, seek and reverse controls.
- [ ] Make Glowmere regression checks explicit: UFO close-up/orbit stationarity, alien animation,
  shoreline stability and camera cuts. Keep it as a control even if Constellation is the active art.

### P3: performance and delivery

- [ ] Re-measure CPU/GPU frame time, p90/tail, draw calls, visible/culled renderables, animated
  characters/skinned vertices, water/shadow/post passes and buffer uploads after correctness fixes.
- [ ] Profile the dominant current scenes first. Constellation is currently particle/volume bound;
  Glowmere is scene-geometry bound. Do not optimize an unmeasured subsystem.
- [ ] Run the final suite and document known skips/failures. A finished renderer needs a clean or
  explicitly justified platform-gated test result, not repeated lucky reruns.

## Commands

```sh
cmake --build --preset release -j 4
./build/release/tests/avgen_tests '[scene][animation]'
./build/release/tests/avgen_render_tests '[gpu][skinning]'
ctest --preset release --output-on-failure
cmake --build --preset asan --target avgen_tests -j 4
./build/asan/tests/avgen_tests '[scene][animation]'

./build/release/src/avgen --headless --project examples/constellation/constellation.json \
  --frames 120 --fps 30 --size 1280x800 --tier realtime --capture /tmp/constellation-qa.png
./build/release/src/avgen --headless --project examples/world/glowmere-stylized.json \
  --frames 120 --fps 30 --size 1280x800 --tier realtime --capture /tmp/glowmere-qa.png
```

## Root-cause record

**Symptom:** animated characters could freeze or pop when camera-frustum culling changed.

**Reproduction:** an authored-visible rig with `Entity::cameraCulled=true` was passed to
`scene::updateRigs()`.

**Observed state:** `updateRigs()` rejected the entity because it conflated camera culling with
authored visibility, called `rig.hold()`, and left the palette at the previous timeline pose.

**Root cause:** culling state owned by the camera pass was incorrectly used as animation eligibility.

**Fix:** camera culling no longer excludes authored-visible rigs; explicit visibility and rig distance
policy still do.

**Regression:** `camera culling does not freeze an authored-visible rig`, 57 focused assertions pass.

**Still open:** the posed-bounds path needs a frustum-edge image regression and stress measurement;
invalid or partially weighted meshes still use bind-pose fallback.

### Terrain/water disappearance

**Symptom:** terrain chunks and their water could disappear permanently after leaving the authored
view distance or after a runtime visibility decision.

**Reproduction:** move the terrain camera beyond `terrainViewDistance`, update once, then restore
the view distance and camera and update again without rebuilding the composition.

**Observed state:** `Composition::updateTerrain()` set `Entity::visible=false` for distant chunks,
and the water helper used `visible = visible && on`. A later frame had no authored/runtime distinction
to restore the entity.

**Root cause:** transient camera culling was stored in the persistent authored visibility flag.

**Fix:** preserve `visible`; use `cameraCulled` for per-frame terrain/water suppression and clear
`castsShadow` for chunks beyond view distance.

**Regression:** the existing terrain shadow test now moves out and back and passes 103 assertions.

### False motion on camera re-entry

**Symptom:** a moving object could smear when it re-entered the camera even if it had stopped while
culled.

**Reproduction:** render an object, move it while `cameraCulled=true`, then clear culling without
changing its transform and render with motion blur enabled.

**Observed state:** `SceneRenderer::makeItem()` was the only writer to `prevModelsNext_`, so a culled
entity's previous model remained at the last submitted frame.

**Root cause:** temporal model history was incorrectly owned by render submission rather than by the
scene entity's frame state.

**Fix:** advance `prevModelsNext_` for every entity before camera/shadow submission; submitted draws
still read the prior frame from `prevModels_`.

**Regression:** `[gpu][motion][blur]` compares the re-entry image with a fresh renderer and passes.

### False motion after reverse seek

**Symptom:** reusing a renderer after moving timeline time backward produced a different frame than
a fresh renderer at the same scene state.

**Root cause:** previous view-projection and model histories described the forward frame, so the
first reverse frame was treated as motion instead of a temporal discontinuity.

**Fix:** clear temporal camera/model history whenever `renderTime` decreases.

**Regression:** `[gpu][motion][determinism]` reverse-timeline case passes 42 assertions.

### Camera forward/up singularity

**Symptom:** a top-down camera could make `renderToImage()` fail because the view matrix became
non-finite; the particle disc edge-on regression exposed it.

**Root cause:** `glm::lookAtRH` was given a forward direction parallel to the authored world-up
vector, leaving its lateral basis undefined.

**Fix:** `Camera::view()` selects a stable world-axis fallback up vector when the two directions are
near parallel.

**Regression:** camera tests pass 140 assertions and the disc-emitter test passes 42 assertions.

### Water alpha-squared compositing

**Symptom:** transparent water and shallow shoreline color could be too dark, especially where
alpha varied across the surface.

**Observed state:** `WaterRenderer` configured conventional non-premultiplied `SrcAlpha` blending,
but `fs_water` returned `color * alpha` in RGB. The blend therefore multiplied alpha a second time.

**Root cause:** shader output convention and pipeline blend convention disagreed.

**Fix:** return `vec4(color, alpha)` and let the pipeline apply alpha exactly once.

**Validation:** valid-WGSL shader test passed 13 assertions; current Glowmere rendered 120 frames
with zero GPU errors. Add an image-level water blend test before final sign-off.