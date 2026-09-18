# Visibility / Culling Lab

What decides that an object is not drawn, where that decision lives, and what to read when
something is missing from a frame.

This is the lab's own documentation. The suite-wide lab documentation, the registry and the
launcher belong to the lab-suite architecture and are deliberately not described here.

## 1. The pipeline, as it actually is

There are **two independent visibility stacks**, and they share no code. Knowing which one an
object is in is the first question, because the answer decides which half of this document applies.

### A. Entities — CPU, per object, AABB against the frustum

An `scene::Entity` is an authored mesh node, an imported glTF object, a character, or one terrain
chunk. Terrain chunks are entities; scattered vegetation is not.

| Order | Decision | Where |
|---|---|---|
| 1 | `cameraCulled = !aabbVisible(planes, bounds)` — world AABB against six planes | `Composition::cullEntityNodes`, `src/scene/composition.cpp:1847` |
| 1a | skipped entirely when `viewportHeight_ == 0` — **nothing is culled at all** | `src/scene/composition.cpp:1850` |
| 1b | water style is never entity-culled; terrain LOD owns it | `src/scene/composition.cpp:1861` |
| 2 | terrain chunk past `viewDistance` → `cameraCulled`, `castsShadow = false`, no mesh picked | `Composition::updateTerrainLod`, `src/scene/composition.cpp:5384` |
| 2a | terrain chunk past `shadowDistance` → drawn, but excluded from every cascade | `src/scene/composition.cpp:5382` |
| 2b | terrain chunk outside the frustum → `cameraCulled`, LOD mesh still picked (it may cast) | `src/scene/composition.cpp:5396` |
| 3 | `drawable()`: `visible && mesh < meshes_.size() && indexCount != 0` | `src/rendering/scene_renderer.cpp:2097` |
| 4 | `!drawable(entity) \|\| (cameraCulled && trustCull)` → not submitted | `src/rendering/scene_renderer.cpp:2719` |
| 4a | `trustCull = toggles_.culling && toggles_.cameraMotion` — a frozen view ignores the cull | `src/rendering/scene_renderer.cpp:2718` |
| 5 | object-uniform budget spent → dropped, and the loop `break`s | `src/rendering/scene_renderer.cpp:2637` |
| 6 | `!toggles_.water` / `!toggles_.transparency` → dropped **after** taking a slot | `src/rendering/scene_renderer.cpp:2730`, `:2745` |
| 7 | shadow-only pass: `castsShadow`, not Grid/Water/Blend, and some cascade sees it | `src/rendering/scene_renderer.cpp:2762`, `:2765` |

Note the **two different frustums**: `cullEntityNodes` uses the viewport's exact aspect, while
`updateTerrainLod` deliberately widens to an aspect floor of 2.5 so a chunk is never culled out of a
frame that turns out to include it. That asymmetry is intentional and documented at the site.

### B. Procedural instances — GPU, per instance, sphere against the frustum

Everything scattered: vegetation, rocks, debris. `shaders/cull.wgsl`, one four-dispatch chain per
object per frame, deterministic prefix-sum compaction with no atomics.

| Order | Decision | Where |
|---|---|---|
| 1 | `!visible \|\| instances.empty() \|\| meshHash == 0` | `ProceduralRenderer::update`, `src/rendering/procedural_renderer.cpp:1390` |
| 2 | more than `kMaxProceduralObjects` → warn and `break` | `:1393` |
| 3 | mesh generation failed → skipped | `:1401` |
| 4 | `objectFullyCulled` — the whole record set provably fails; no dispatch, no draw | `:1395`, `rendering/visibility.cpp` |
| 5 | per instance: sphere vs six planes | `shaders/cull.wgsl:217` |
| 6 | per instance: `dist - radius > maxDistance` | `shaders/cull.wgsl:222` |
| 7 | per instance: `screenRadius < minScreenRadius`, asymmetric hysteresis (ADR-082) | `shaders/cull.wgsl:224` |
| 8 | depth-band density thinning (ADR-038) — **fires even with culling off** | `shaders/cull.wgsl:235` |
| 9 | LOD rung assignment — demotes, never rejects | `shaders/cull.wgsl:249` |
| 10 | shadow pass and `!castsShadow` | `src/rendering/procedural_renderer.cpp:2017` |
| 11 | a far rung empty for `kEmptyLevelFrames` → its indirect draw is skipped | `:2056` |

Step 11 is the one place an instance can be in the GPU's visible list and still not be drawn: the
suppression is driven by an **asynchronous** readback, so an instance arriving in a suppressed far
rung appears a frame or more late. Rung 0 is exempt.

### C. SDF objects

A separate, much smaller path: `SdfRenderer::update` rejects on `!visible`, a slot budget, a failed
`validate()`, a **screen-rect** test (`projectedRect`, the only frustum test SDF has), an SDF tree
too large to pack, and an empty mesh. `src/rendering/sdf_renderer.cpp:411-471`.

### D. Policy that lifts the above

`scene::DetailLimits` (ADR-186/191), applied by zeroing uniforms rather than branching
(`src/rendering/procedural_renderer.cpp:1619`):

- `proceduralDistanceCull = false` → `maxDistance` and `minScreenRadius` written as 0.
- `proceduralLodRungs = false` → thresholds written as 0, ending the ladder at rung 0.
- **Frustum culling is never lifted by any policy.**

## 2. Unsupported — stages this engine does not have

Written down rather than filled in. An honest map beats a lab that pretends to test what is absent.

- **Occlusion culling — absent entirely.** No Hi-Z, no depth pyramid, no visibility buffer, no
  occlusion query. The only `occluder` in the tree is GTAO's horizon march and the contact-shadow
  ray, which are shading. Nothing is ever hidden because something else is in front of it. Adding it
  would mean a depth pyramid over the prepass and a second cull dispatch reading it; the compaction
  chain already has the right shape to carry the extra test.
- **Hierarchical culling — absent.** There is no BVH, octree or cluster hierarchy over either
  entities or procedurals. Both stacks are flat linear sweeps: every entity is tested every frame,
  every instance is one GPU thread. `objectFullyCulled` is the only hierarchy there is, and it is
  one level deep.
- **Cluster / meshlet culling — absent.** Instances are culled, triangles within an instance are not.
- **Visibility masks — absent.** There is no per-object layer or channel mask. `castsShadow` is the
  only per-object per-pass gate, and it is a bool rather than a mask.
- **A representation ladder that can cull — present but not wired.**
  `rendering::RepresentationSelector::decide` has a `Representation::Culled` state and a
  `policy.cullRadius` (3.0 px Preview, 1.0 px High). It has **no call sites outside tests**. If you
  are hunting for why something is not drawn, it is not this.

## 3. Reason codes

`src/rendering/visibility.hpp`. Each name corresponds to a branch you can point at above.

| Code | The decision |
|---|---|
| `VISIBLE` | compacted into a rung's visible list |
| `FRUSTUM_CULLED` | `dot(n, centre) + w < -radius` on one of six planes |
| `DISTANCE_CULLED` | `dist - radius > maxDistance` |
| `SCREEN_SIZE_CULLED` | `screenRadius < minScreenRadius` |
| `DEPTH_BAND_THINNED` | ADR-038 band density below 1 |
| `OBJECT_FULLY_CULLED` | whole-object rejection; no dispatch, no draw |
| `OBJECT_NOT_DRAWABLE` | hidden, no records, or the mesh failed to generate — **including an asset that failed to load** |
| `OBJECT_BUDGET_EXCEEDED` | past `kMaxProceduralObjects` |
| `SHADOW_ONLY_REJECTED` | a shadow pass and `castsShadow` is false |
| `INVALID_BOUNDS` | no usable sphere |

**Codes from the proposed vocabulary that are deliberately absent**, because no decision here
produces them (ADR-182 — a code that can never be returned makes the diagnostic look more complete
than the engine is):

- `OCCLUSION_CULLED` — there is no occlusion culling.
- `OUTSIDE_RENDER_DISTANCE` — the same decision as `DISTANCE_CULLED`; naming it twice implies two.
- `LOD_REJECTED` — the ladder demotes, it never rejects. What removes a small instance is
  `SCREEN_SIZE_CULLED`, a different test with a different threshold and its own hysteresis.
- `NOT_SUBMITTED` — every code but `VISIBLE` is a not-submitted, so the word carries no information
  at the moment somebody is asking "why".

`DISABLED` is folded into `OBJECT_NOT_DRAWABLE` rather than kept apart: at the object level
`visible == false` and "no records" are reported at the same branch and are equally often the answer.

## 4. How the diagnostic is checked

Against **what was drawn**, never against a second implementation of the decision.

- `instanceVisibility` does not re-derive the verdict. It calls `cullLodLevel` — the function whose
  parameters the renderer hands the GPU — and the per-test comparisons only *attribute* that verdict
  to a stage. It asserts the two agree, so it can name a wrong stage but cannot report a different
  answer from the pass it describes.
- `tests/rendering/test_visibility_gpu.cpp` compares the reason, index by index over a 361-instance
  grove of the production tree, against `readVisibleIndices` — the compacted list that
  `drawIndexedIndirect` reads its instance through. Both arms are counted, so a diagnostic that said
  VISIBLE for everything (or nothing) fails immediately.
- The frame itself is checked in pixels, paired with a control framing that must draw nothing.

**A hazard found while building that check**: `readVisibleIndices` and `readCullCounts` are *not* a
reliable account of a frame in which the object was whole-object culled. `objectFullyCulled` skips
the cull dispatches as well as the draws, so both buffers keep whatever the last frame that ran the
pass left in them. Nothing is drawn — but a tool that reads the list and reports "these were drawn"
is reading history. Use the object-level reason for that frame.

## 5. The fixture

`examples/labs/visibility-culling-lab.scene.json`. Fixed seed, flat ground, fixed camera, no fog and
no volumetrics — anything that can fade an object out with distance makes "it disappeared"
ambiguous. Six scatter layers of real production assets (§29), each carrying the cull parameters
Glowmere gives it:

**Its light changed on 2026-09-18, and this is what that moved.** The fixture always carried a
top-level `"lights"` array and nothing read it (ADR-278); the scene was lit by `defaultKeyLight()`
instead. It now obeys its own file. Measured, one frame at 1920x1080, t = 1/60 s, arm and control
rendered by the same binary with `--disable post` and the control being this same file with the
`"lights"` key removed:

| | default key (what it got) | the file's key (what it gets) |
|---|---|---|
| direction | (-0.353209, -0.883022, -0.309058) | (-0.349843, -0.719676, -0.599730) — **19.2 degrees** apart |
| intensity | 3.0 | 4.0 |
| temperature | 5600 K | 6500 K |
| frame mean | 0.2112 | 0.2183 |
| rms contrast | 0.0790 | 0.0886 |
| p99 | 0.4187 | 0.4886 |
| bright centroid y | 0.5264 | 0.5345 |

**752,202 of 2,073,600 pixels differ (36.3%), worst channel 192 of 255**, and the difference's
bounding box is (0, 574)-(1919, 1079) -- the ground plane and everything standing on it, which is
the half of the frame a key light reaches. The p99 moving 0.4187 to 0.4886 is the extra stop.

**No culling measurement in this document moved, and that is measured rather than assumed**: the
identifier AOV (`--aov id`, which instance drew in which pixel) is **byte-identical** before and
after, on both frames, while the colour frames differ. Which objects survived the cull, and where
they landed, is exactly what that buffer is. The radius and threshold numbers in §6 come from
`tests/rendering/test_visibility_gpu.cpp` and `tests/unit/test_visibility_culling.cpp`, which build
their scenes in code and never load this fixture.


| Layer | Asset | Covers |
|---|---|---|
| `large-tree` | CommonTree_1 @ 14 m | Glowmere's `canopy`, verbatim — the popping case |
| `small-tree` | TwistedTree_2 @ 3.5 m | the same stage at a quarter of the size |
| `elongated` | Plant_1_Big @ 4 m | the shape a bounding *sphere* describes worst |
| `irregular` | Rock_Medium_1 @ 2.2 m | bounds that poorly describe a silhouette |
| `tiny` | Pebble_Round_2 @ 0.28 m | the only layer that reaches `SCREEN_SIZE_CULLED` |
| `scaled` | CommonTree_1, scale 0.35–2.4 | one layer crossing every threshold at different distances |

The ground is flat on purpose: on a hill the bottom of frame cuts each tree at a different offset,
which is exactly the ambiguity a lab exists to remove. The camera is 32 m up and level, which puts
the feet of the trees 40 m ahead about 11 m below the bottom plane — between the 7.5 m sphere the
renderer used to cull them with and the 14.9 m sphere that actually contains them.

## 6. The tree-popping defect

**Symptom.** Trees pop out of shots in Glowmere; the draw distance feels short.

**Root cause.** `shaders/cull.wgsl` centres each instance's bounding sphere on the *record position*
— for a scatter, the point on the ground the thing was planted at, which for a glTF tree is the foot
of its trunk. The renderer's mesh cache handed it the **half-diagonal of the source AABB** as the
radius. Those describe different spheres: the half-diagonal is the radius about the box's *centre*,
and about the origin it is only correct when the geometry is centred there. A tree standing on its
origin is the case where it is most wrong.

Measured on the production asset (`assets/quaternius/glTF/CommonTree_1.gltf`, Glowmere's `canopy`
layer, normalised to 14 m):

| | source units | at 14 m |
|---|---|---|
| radius the cull used (worst part's half-diagonal) | 3.918 | **7.550 m** |
| radius that contains the tree (furthest corner from the origin) | 7.724 | **14.885 m** |

The sphere covered 51% of the radius it needed, and the missing half was the canopy. A tree whose
foot passed a metre or two below the bottom of frame was thrown away with its crown on screen. The
same under-measured radius also halves the instance's projected size, so every survivor was demoted
down the LOD ladder earlier than it should have been — which is the second half of "the draw
distance feels short".

`TwistedTree_2` (Glowmere's `pines`) is worse still: bounds `[-6.62, -0.20, -5.54]..[3.94, 18.75,
3.66]`, half-diagonal 11.78 against a required 20.64 — a factor of 1.75.

**The engine already knew the right rule.** ADR-199 reached this exact conclusion for
`scene::ProceduralGeometry`'s own bounds and wrote it down — *"`sourceHalfExtent` returns
max(|vertex|) … the conservative version stays for culling"* — and `scene::sourceBoundingRadius` has
computed the origin-centred radius ever since. The renderer's mesh cache did not follow, so the
engine held two answers for one number and the GPU got the wrong one. The existing GPU culling
tests did not catch it because every fixture in them is a **centred box**, for which the two rules
agree exactly.

**Fix.** `rendering::sourceCullRadius(lo, hi)` — the distance from the origin to the furthest corner
of the box — computed once per cached mesh as `CachedMesh::cullRadius`, and used for the cull
uniform's `limits.z`, for `groupRadius` (ADR-108's shared multi-part sphere) and for the wind sim's
`sourceRadius`. `CachedMesh::radius` keeps its old value where it is used as a normal epsilon scale.

This is not padding and not a margin (§28): it is the tight bounding radius about the centre the
shader actually uses, and for geometry centred on its origin it is bit-identical to what was there
before.

**What it is not.** It is not an LOD fix. The LOD ladder's thresholds (28 / 11 / 4 px of projected
radius, `src/scene/composition.cpp:3512`) were authored against the under-measured radius, so they
now act on numbers roughly twice as large. That is a question for the LOD Lab, not an argument for
retuning them here.
