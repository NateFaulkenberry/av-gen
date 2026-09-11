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
- The prior full release suite had one intermittent Syphon burst failure. Re-run the full suite
  after the current pass; do not call it green without seeing the result.

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

- [ ] Reproduce a static-object transform test across camera translate/rotate/orbit/dolly, resolution
  changes and timeline seek. Assert the authoritative `Entity::transform.position` and submitted
  `ObjectUniforms::model` translation remain constant. Include a far-from-origin object.
- [ ] Add targeted transform diagnostics for selected entity name/ID: world transform, model
  translation, camera position, camera/view-projection terms, culling state and object slot index.
  Log only changes or invalid values, not every frame.
- [ ] Audit duplicate entity names. Either enforce uniqueness at the scene boundary or replace the
  velocity-history key with a stable entity identity. Add a regression for two same-named objects.
- [ ] Add finite-value validation for transforms, quaternions, camera matrices, mesh bounds, joint
  palettes and object indices at the CPU/GPU boundary. Fail with object/rig name and frame time.
- [ ] Audit object uniform ring/dynamic offsets and per-frame writes under rapid scene changes.
  Use object IDs and a two-frame alternating transform test to detect stale object data.

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

- [ ] Build a minimal water QA scene with flat/steep/shallow/deep/angled shore cases and camera
  views above, below, grazing and near-parallel to the surface.
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
- [ ] Run ASan/UBSan and TSan-compatible CPU tests after each resource-lifetime change. Add a stable
  two-renderer same-frame hash test around every confirmed state bug.

### P2: diagnostics and permanent torture scenes

- [ ] Extend existing `DebugViewOptions`/`DebugDraw` facilities rather than creating a second debug
  system. Add bounds, object origins/axes, culling state, object ID, world position, normals, raw/
  linear depth, motion and water masks where the current debug targets can support them.
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