# ADR-912: A cut is not motion: the renderer drops its motion history on one

**Status:** Accepted
**Date:** 2026-09-26
**Found by:** the GV3 revision's render audit ("Engine gaps" item 1): every hard cut in Glowmere
Valley 3's final opens with a frame of full motion blur.
**Follows:** ADR-035 (the velocity target), ADR-040 (tile motion blur), ADR-245 (cameras and shots),
ADR-410 (the temporal ring)
**Implemented by:** `Scene::camera.cutSerial` (`src/scene/scene_types.hpp`); the cut found in
`Composition::applyParameters` and `Composition::markCameraCut` (`src/scene/composition.cpp`);
`Engine::markKeyedCameraCut` (`src/app/engine.cpp`) with `params::Track::jumpsWithin` and
`params::kCutRampSeconds` (`src/params/timeline.hpp`); the reset in `SceneRenderer::render`
(`src/rendering/scene_renderer.cpp`)
**Tests:**
- `tests/rendering/test_camera_cut_gpu.cpp`: "the first frame after a cut is drawn as a first frame:
  as sharp as a still, no smear"
- `tests/unit/test_follow_camera.cpp`: "a cut to another camera changes the cut serial; a blend, a
  same-camera shot and a steady frame do not", "a track jumps at a step or a cut's ramp, not along a
  dense fast move", and the keyed-cut section of "the engine records the follow camera's subject in
  HIST and finds a keyed cut"

## Context

The renderer draws motion from two frames: the velocity target is this frame's projection of every
surface minus last frame's (`prevViewProj_` and the previous model matrices), and the blur smears
each pixel along it. The only thing that reset that history was a seek, a scene swap or a resize.

So a cut was drawn as motion. Between the last frame of one shot and the first frame of the next,
the view-projection changes as much as it ever does, and the first frame of the new shot was blurred
along a "movement" from the old camera to the new one, clamped only by the blur's maximum radius. The
render audit measured it on the delivered film: in 7 of the first 8 cuts the new shot's first frame
has 20-40% of its neighbours' sharpness, and frame 1136 is a full-frame smear. By the variance of
the Laplacian on the audit's decoded frames, the first frames of the cuts at 18.92, 22.62, 26.31,
30.00 and 37.39 s have 6%, 6%, 19%, 22% and 6% of the sharpness of the frames that follow them
(frame 1136: 17 against 295-301). The audit's interim advice was to key `post/motionBlur/amount` to 0
on the first frame of every shot by hand.

The renderer cannot tell a cut from a fast move by itself: both are a large change of view between
two frames. Whoever knows the picture does not continue has to say so.

## Decision

**The scene says so.** `Scene::camera.cutSerial` changes whenever this frame's picture does not
continue the previous frame's. Its value means nothing; only a change does. A serial rather than a
per-frame flag, so a renderer that draws a paused frame twice, or skips a frame, still sees exactly
one cut.

**Who changes it.**
- **The composition,** when the active camera changes without a blend in progress: an authored shot
  (every GV3 cut), a directed Song Mode shot, an event camera claiming the frame with no blend, a shot
  list ending and handing back to the default camera. A blend starts from the outgoing camera's own
  pose, so it is motion and keeps its history. A new shot on the same camera is the same rig
  evaluated at the same instant, so it is not a cut either.
- **The engine,** when the timeline driving the camera on screen jumps between the previous update
  and this one: `Track::jumpsWithin` over the active camera's (and a blend's outgoing camera's)
  position, target, lens and offset tracks. A jump is a Step key whose value changes, or a segment no
  longer than two `kCutRampSeconds` that moves at least twenty times as fast as its neighbours. That
  is how a baked sequence cuts the main camera: `seq::Sequence`'s bake writes the outgoing pose a
  millisecond before the incoming one, and now takes that millisecond from the same constant. The
  cinematic bake's two keys at one instant are a zero-length segment and read the same way. A dense
  fast move, keyed a millisecond apart, is not a jump: its neighbours move as fast as it does.

**What the renderer drops:** `resetScreenHistory` -- the previous view-projection and models (so this
frame's velocity, and its blur, are zero), the AO history and the ADR-410 temporal ring. The AO
history has to go with the view-projection, not merely may: `gtao.wgsl`'s temporal pass reprojects
the previous result through `prevViewProj`, so a reset view-projection with the history kept would
blend the old shot's occlusion into the new one as though the camera had not moved. Not the particle
pools: a cut is a discontinuity of the picture, not of the world. That is the reset whose own comment
already listed "a camera cut" as a reason to invalidate the AO history; nothing had ever called it
for one.

**And the skinned rigs.** Their previous palette is scene state, so on a cut's frame the composition
holds every rig on its current pose (`SkinnedRig::hold`, which also bumps the palette version so the
renderer re-uploads it). The first frame of a shot draws no joint motion either.

## Consequences

- **Every hard cut's first frame is drawn as a first frame.** In the GPU test the cut frame is
  byte-for-byte the frame a fresh renderer draws at the incoming pose (5,477 lit pixels each); the
  same change of camera drawn without the serial changing -- the renderer as it was -- lights 14,751,
  a 2.7x smear. The frame after the cut blurs normally again: the history restarted at the new shot
  rather than switching off.
- **On GV3 itself:** rendering the frames around three of the cuts the audit measured (18.92, 22.62
  and 37.39 s; 960x540 on the project's offline tier), the first frame of each new shot now has
  99.7%, 103% and 100% of the sharpness of the two frames after it (variance of the Laplacian),
  against 6% at the same three cuts in the delivered final.
- **Scenes whose look changes:** every project with authored hard cuts (GV3's 39 cuts, the multicam
  films) and every Auto-director sequence with baked cuts on the main camera. The change is one
  frame per cut: sharp where it was smeared. On those frames, moving objects (walking characters,
  swaying plants) also lose their motion blur, because their previous transforms are part of the
  history dropped. One frame of a real shutter's object blur is lost at each cut; a frame of the old
  camera's smear is not drawn. The alternative -- evaluating the new camera one frame early to
  give the first frame its own camera motion -- needs every rig evaluable at another instant, and
  was not worth it for one frame.
- **The AO settles over its accumulation window at the start of each shot** (16 frames at the offline
  tier), exactly as it does at the start of any render. Before, it reprojected the outgoing shot's
  occlusion through the outgoing camera, which the neighbourhood clamp mostly rejected and partly
  ghosted.
- **Blends, joins and continuous takes are unchanged.** ADR-891's joins and ADR-892's continuous take
  keep the main camera on screen throughout, so neither ever changes the active camera.
- **The GV3 hand workaround is no longer needed:** do not key `post/motionBlur/amount` to 0 at each
  cut.
- `RenderStats::cameraCuts` counts the cuts a renderer has dropped its history for, over its life, so
  a render log or a test can check that a film's cuts reached it.
- **Not covered:** a camera discontinuity nobody declares -- a rig's position parameter moved by a
  modulation route that jumps, or a script teleporting the main camera without keys. There is no
  heuristic for those; the renderer still draws them as motion, as before.
