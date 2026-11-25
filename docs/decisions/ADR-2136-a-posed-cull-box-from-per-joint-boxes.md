# ADR-2136: a skinned mesh's posed cull box comes from per-joint boxes, not from skinning every vertex

- Status: Accepted (2026-11-24), eng/crowd-bounds (Neon Dreams editor performance, fix 3 of
  `112-neon-dreams/02-ui-perf-investigation/REPORT.md`; approved by the owner).
- Code: `Scene::posedMeshBounds`, `Scene::jointBoxes`, `Scene::JointBox`/`JointBoxTable`, `Scene::jointBoxBuilds` and
  the free `scene::skinnedVertexBounds` in `src/scene/scene.{hpp,cpp}`; a comment in `SceneRenderer`'s diagnostic.
- Tests: `tests/unit/test_posed_joint_bounds.cpp` (`[adr2136]`); the ADR-1082 cases in `tests/unit/test_skeleton.cpp`
  and `test_camera_lab_frustum.cpp` are unchanged and pass.
- Supersedes ADR-1082 in part: its cache (per rig and mesh, by palette content) stays; what a miss computes changes.

## Context

`Scene::posedMeshBounds` (ADR-1082) CPU-skinned every vertex of a skinned mesh, once per pose, to get the box the
camera cull tests (`entityCullBounds`, called by `Composition::cullEntityNodes` for every entity every frame, and by
the renderer's per-object diagnostic). A crowd re-poses every frame, so every frame is a miss. Neon Dreams has 756
rigs (721 posed) of 90-joint aliens with 2-4 meshes of up to 11.5k vertices: the investigation's main-thread sample put
**about 44 ms of a 141 ms CPU frame** in this one function, in the editor and in every render.

Who reads the box (all checked):

- `Composition::cullEntityNodes` -- `Entity::cameraCulled`. That flag decides the camera passes' draw list, and which
  pass of the shadow list a caster goes through (`SceneRenderer`: a visible entity always offers itself as a caster;
  a culled one is tested against the cascades with its **bind** box, `casterState`, which this does not touch).
- `SceneRenderer`'s per-object diagnostic: `worldBounds` (unpadded) and `frustumMargins` (the cull's padded box).
  Built in headless runs and renders (`diagnosticRecords_` defaults on) and for a named entity in the editor. The
  snapshot baselines (`examples/qa/baselines`) do not compare bounds; `stateHash` is diagnostic only.
- Nothing else: shadow-cascade fitting and `Scene::bounds()` use bind boxes, LOD and the caster size floor use
  `meshBounds`, the path tracer ignores `cameraCulled`, and picking, framing and the proximity tools do not call it.

## Decision

1. **One box per joint, built once per mesh.** On the first ask (and after a mesh edit -- `meshVersion` and the
   mesh's vertex/skin fingerprint, as ADR-1806) every vertex with a positive weight on a joint goes into that joint's
   box, twice: in the joint's bind frame (`inverseBind * v`, so a bone's box is long and thin along the bone) and in
   bind model space. Vertices the skin leaves out (total weight <= 1e-6) are left out here too.
2. **Per pose, carry and union.** For each joint, both boxes are carried by its palette matrix (the bind-frame box by
   `palette * inverse(inverseBind)`) to their exact axis-aligned boxes (centre moved, half extents through the absolute
   3x3), intersected with each other, and unioned over the joints. Then a pad of `1e-5 x` the box's magnitude plus
   1e-6 absorbs float rounding.
3. **Why it is never smaller.** A skinned vertex is `sum(w_k M_k v) / sum(w_k)`: a convex combination of its joints'
   transforms of it. Each `M_k v` lies in joint k's carried boxes (both of them, so in their intersection), hence in
   the union's box, and a box is convex. The frame a box is held in affects only the tightness, never the correctness
   (`M F (F^-1 v) = M v`), so a mesh's table built from the first rig that asks serves every rig that draws it.
4. **The exact skin stays, as `scene::skinnedVertexBounds`**, the reference the test checks against.
5. **The diagnostic uses the new box too.** Its `worldBounds` for a skinned entity is now this conservative box,
   not the exact skin. Keeping it exact would put the per-vertex skin back into every headless frame and every render,
   which is where most of the 44 ms was; the margins have to be the cull's own box anyway (ADR-1082, §37).
6. **No pose-sharing cache.** ADR-2116's shared poses would let one box serve a group of rigs, but Neon Dreams shares
   only 10-13 of 756 poses at a time (phase buckets x rates), and a miss now costs about as much as the palette
   comparison the cache already does.

## Measurements

Neon Dreams, headless `--live-frame` at 1936x1262 from 25 s (the investigation's recipe), base 414d47790 against this
branch, interleaved A/B/A/B in one GPU-lock hold (load average 2.4-3.5, no other avgen process), 48 steady frames:

| | base | branch |
|---|---|---|
| `controller` (composition update, which runs the cull), steady frames | 90-96 ms | 49-51 ms (one 67) |
| frame wall median (two runs) | 570 / 565 ms | 510 / 518 ms |
| `cullEntityNodes`, main-thread `sample` (10 s, inclusive) | 577 samples, ~31 ms a frame | 25 samples, ~1.2 ms a frame |
| `Engine::update`, same sample | ~69 ms a frame | ~38 ms a frame |
| camera draws / shadow draws at 25 s | 3,544 / 4,416 | 3,547 / 4,416 |

What is left in `posedMeshBounds` is mostly the ADR-1082 palette `memcmp` and copy. Glowmere Valley 3 (35 skinned
draws) does not move: frame wall 74.1/74.8 against 73.6/73.3 ms, `controller` about 2.5 ms either way.

Looseness, over every clip of each rig sampled 24 times and 40 random poses per rig (every joint turned up to 69
degrees), as the most the box overreaches on one side as a fraction of the mesh's largest exact extent (mean / worst),
and its volume against the exact box (mean):

| rig | clips: overreach | clips: volume | random: overreach |
|---|---|---|---|
| Glowmere alien (`alien.gltf`) | 10% / 15% | x1.81 | 15% / 25% |
| Neon Dreams pilot / scout / elder | 7-16% / 41% | x1.45-1.82 | 15-23% / 46-67% |
| cow / horse / chick | 1-5% / 6% | x1.02-1.11 | 10-13% / 24% |
| astronaut drummer | 19% / 35% | x1.91 | 14% / 40% |

The worst cases are small accessory meshes weighted to distant joints, whose own extent is small. The camera cull
already pads every box by a quarter of its extent plus 0.25 m, so on a clip this roughly doubles a margin that was
already there.

Offline renders are bit-identical: 285 frames over six ranges (Neon Dreams 25-26.5 s, 45-46.5 s and 8-9 s; Glowmere
Valley 3; the farm animals; the alien squad; 960x540, 30 fps, PNG), same sequence hash and same per-frame PNG SHA-1 at
base and branch.

## Consequences

- The cull box of a skinned entity is larger than before: about 10% of the body's size per side on a clip, up to
  about 40% for an accessory mesh weighted to distant joints. The camera cull keeps a few more off-screen characters
  in the draw list near the frustum edges (+3 of 3,544 draws on Neon Dreams at 25 s). A character can no longer pop out because its box was too small
  -- that was already true, and is now true by construction with a margin.
- An entity whose box newly reaches the frustum moves from the shadow list's culled-caster pass (bind box against
  the cascades) to the visible pass (always offered). A caster is only rasterised where it is in a cascade, so this
  changes a shadow only where the bind-box test had dropped a caster whose pose reached into a cascade.
- Memory: one table per skinned mesh, two boxes and a 3x3 per influencing joint (about 100 bytes a joint).
- `posedBoundsComputes()` still counts misses; `jointBoxBuilds()` counts table builds and is tested to stay at one
  per mesh across poses and rigs.

## Alternatives considered

- **Joint spheres** (ADR-1082's rejected alternative): cheaper, but a sphere around a long bone is a much looser box.
- **Bind-frame boxes only.** Measured first: on the Neon Dreams pilot one axis came out x8.4 its exact extent (an
  accessory mesh), against a worst of x1.9 otherwise; intersecting with the model-space box is what is shipped.
- **Excluding small weights from a joint's box** (the usual game-engine trick) is tighter and not conservative: a
  vertex weighted 0.05 to a far joint is still moved by it.
- **Skinning a decimated vertex subset**: not conservative either.

## Revisit triggers

- A content type where the extra draws matter (the cull's culled fraction falls noticeably).
- GPU-driven culling or skinning, which would make the CPU box a coarse pre-pass only.
