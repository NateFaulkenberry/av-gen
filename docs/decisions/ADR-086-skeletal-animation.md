# ADR-086: Skeletal animation is an engine substrate, not a character feature

Status: accepted
Date: 2026-09-11

## Context

Two lines in `src/assets/gltf_loader.cpp` said what this engine could not do:

```
"{} animation(s) ignored (not supported in 0.2)"
"{} skin(s) ignored (skinning not supported in 0.2)"
```

and there was no joint or skin code in any shader to make them wrong. A rigged glTF loaded, drew,
and stood there in its bind pose. `assets/imported/alien.gltf` — one mesh, one skin, 49 joints,
three clips (`Idle` 3.63 s, `Walk` 1.10 s, `Run` 0.90 s, 150 channels each) — is the first asset
this engine has been asked to animate, and it will not be the last. The thing missing was a
substrate, not a way to make one alien move.

Two constraints shaped every decision below.

**Determinism.** This engine's offline renders are compared byte for byte against live frames
(ADR-012: nothing outside `RealtimeClock` reads a system clock). An animation system that advances
a clip by `deltaTime` is deterministic only for a *fixed* frame rate; the same performance sampled
at 24 fps and at 60 fps drifts apart, and the drift is invisible until somebody renders the shot.

**Cost.** The scene pass is fragment-bound (ADR-085). Skinning's costs are a per-frame CPU pose and
a per-frame palette upload, both proportional to joints × characters, and neither is visible in any
counter this engine already prints.

## Decision

### The pose is a pure function of the timeline

`scene::AnimationPlayer` stores **when a state was entered**, never how long it has been running:

```cpp
struct Playing { int state; double start; float speed; };
```

Local clip time is `(now - start) * speed`, wrapped or clamped by the state. A cross-fade is
`clamp((now - blendStart) / blendDuration, 0, 1)`. The only calls that mutate the player —
`play(state, now)`, `restart(now)`, `setSpeed(speed, now)` — each take the timeline second the
decision was made at, and `setSpeed` rebases `start` so local time stays continuous. Everything
else is a pure read.

The consequence is the property that was wanted: given the same decisions at the same timeline
seconds, the pose at *t* is identical however many frames were rendered to reach it. The unit test
`"the pose is a pure function of the timeline, not of the frame rate"` drives the same player
through a 400-frame wobbling cadence and a 100-frame 24 fps one and asserts exact float equality of
the result.

`play()` on the state already current is a **no-op** — it does not restart the clip — so a behaviour
may call it unconditionally every frame, which is how a behaviour wants to be written.

### Pose rate is quantised onto the timeline, not counted in frames

A rig's `updateHz` samples the player at `floor(t * hz) / hz`. A 20 Hz rig therefore produces
identical matrices in a 60 fps window and a 24 fps offline render, which "re-pose every third frame"
could never promise. `SkinnedRig::rateFor(distance)` is the whole distance policy in one testable
function: full rate inside `nearDistance`, `farHz` beyond it, not posed at all past `cullDistance`
or when no visible entity refers to the rig.

`scene::updateRigs(Scene&, FrameTime)` applies it. It is called from the **controller**, never from
the renderer: `SceneRenderer::render` still takes a `const Scene&`, so rendering a frame twice
cannot change it.

### The rig keeps the joints' ancestors

A skin's `joints` array is not the hierarchy that drives it. `alien.gltf` animates node 1
(`Alien_Low_Green`), which is the *parent* of the hips and is not in the skin's joint list; a rig
holding only the 49 named joints would import a character whose limbs move and whose body never
leaves the origin, with nothing in the log to say so. `Skeleton::joints` is therefore the joint
nodes **and every ancestor of one** (51 for the alien), topologically ordered; `Skeleton::palette`
maps the mesh's `JOINTS_0` indices into it.

A skinned mesh node's own transform is ignored on import, as the glTF specification requires: the
joint matrices already carry the file's scene space, and the entity's transform is left for whoever
places the character in the world.

### The skinned pipeline includes the static one

`shaders/pbr_skinned.wgsl` is `#include "pbr.wgsl"` plus a vertex entry point. `fs_main` and
`fs_depth` are not copies of the shading — they are the same functions, reached through a vertex
stage that poses the mesh first. ADR-023's shared shading survives by construction rather than by
discipline, and the GPU test asserts the consequence: a skinned mesh whose palette is the identity
renders **byte-identically** to the same mesh drawn statically.

The palette is a read-only storage buffer at `group(1) binding(1)`, beside the `ObjectUniforms`
that were always at binding 0, both bound with dynamic offsets. Group 0 (frame), 2 (material) and 3
(IBL) are the scene's own, unchanged — which matters, because WebGPU allows four bind groups and
there was no fifth to spend.

One rig's slice holds its matrices **twice**: this frame's pose, then the pose the rig was drawn
with last frame, so the velocity target (ADR-035) sees a running character's limbs move along their
own path instead of the body's. `ObjectUniforms::ids.w` — previously zero — carries the joint count,
so the shader finds the second half. A rig that did not re-pose has previous == current and
correctly reports no motion.

Slice size is the **largest rig in the scene**, not `kMaxPaletteJoints`: the alien's 49 joints cost
6,400 bytes, not 32 KB.

### Per-vertex influences live beside the vertices

`MeshData::skin` is a parallel `std::vector<SkinInfluence>` (four `uint16` joints, four `float`
weights, 24 bytes), not four more fields on `Vertex`. A terrain chunk's vertex stays 32 bytes and a
world with no character in it pays nothing. The skinned pipeline takes it as vertex buffer 1.

### The command stream of a frame with no character is unchanged

Every draw site in `scene_renderer.cpp` branches on `item.skinned()`, and the static branch is the
code that was there. A scene with no rig never binds a skinned pipeline, never sets a second vertex
buffer, and never writes the joint buffer.

## Consequences

**The seam.** A scene file declares the character; a behaviour drives it.

```json
{"name": "walker", "kind": "gltf", "asset": "alien.gltf", "scale": [0.01, 0.01, 0.01],
 "animation": {"state": "Walk", "blend": 0.35, "updateHz": 0, "cullDistance": 120}}
```

```cpp
scene::SkinnedRig& rig = scene.rigs[entity.rig];
rig.player.play("Run", time.renderTime);      // cross-fades; a no-op if already running
rig.player.play("Run", time.renderTime, 0.0f); // or snap
```

A composition node re-applies its authored `state` only when the request changes or a rebuild has
replaced the rig, so a behaviour that takes a character over keeps it.

**Measured** (M2 Max, 1920×1080, 60 frames after 20 warm-up, minimum and median; the GPU timestamp
counter is quantised at 65,536 ns so the minimum is the honest figure):

```
  1 static          GPU frame min 0.92 / med 1.25 ms   pose min   0.0 us   upload      0 B
  1 skinned         GPU frame min 0.98 / med 1.25 ms   pose min   3.9 us   upload  6,400 B
  8 skinned         GPU frame min 1.57 / med 2.03 ms   pose min  29.0 us   upload 51,200 B
 32 static          GPU frame min 2.42 / med 3.01 ms   pose min   0.0 us   upload      0 B
 32 skinned         GPU frame min 2.29 / med 3.21 ms   pose min 114.6 us   upload 204,800 B
 32 skinned @20Hz   GPU frame min 2.23 / med 3.80 ms   pose min   6.7 us   upload      0 B
```

Thirty-two 51-joint characters cost 114.6 µs of CPU to pose and are within the timestamp noise
floor on the GPU. The 20 Hz arm is the pose-rate policy working: 17× less CPU, and no upload at all
on the frames between grid cells.

**Not done.** Morph targets, `JOINTS_1` (a fifth influence and beyond is dropped, never the
heaviest — glTF orders them by weight), IK, root motion extraction, additive or layered blending,
and compute skinning. More than 64 rigs in one scene draw in the bind pose rather than failing.

**Unchanged by this ADR, and worth naming.** The engine is not byte-identical *across frame rates*
as a whole: a scene with no rig in it already differs between a 24 fps and a 48 fps render of the
same second (max channel delta 2), because passes with a temporal history have seen a different
number of frames. Freezing a character's pose entirely does not reduce that difference, so skinning
adds nothing to it. Identical `FrameTime` sequences do give byte-identical output, and that is what
the render-queue hash compares.
