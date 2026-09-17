# Shadow Lab

> Why is this surface lit when it should be shadowed, or shadowed when it should be lit?

Lab #5 of the Engineering Lab Suite (ADR-260, ADR-261). Registered in `src/labs/lab.cpp`; opens on
`examples/labs/shadow-lab.scene.json`; cases in `examples/labs/shadow/cases.json`.

The map came first and it is the deliverable, not the scaffolding. Everything below is a stage an
object or a pixel passes through between "there is a caster in the scene" and "this pixel is
darker", with the file and the decision. Four things §15 asks about **do not exist in this engine**
and are written down under their own heading rather than filled in.

---

## 1. The pipeline, stage by stage

Every stage is named by the file and the symbol that makes the decision. Line numbers rot; symbols
are checked by `tests/unit/test_lab_registry.cpp`.

### 1.1 Does this light get a shadow map at all?

`ShadowRenderer::update` (`src/rendering/shadow_renderer.cpp`)

    light.enabled && light.castsShadow && light.shadowStrength > 0

then, per type:

| type | views | note |
|---|---|---|
| Directional | `QualitySettings::cascadeCount`, clamped 1..4 | **one cascaded directional light per frame** -- `directionalDone` skips every later one |
| Spot | 1 perspective map | |
| Point, Rect, Disk, Tube, Sphere | 6 cube faces | only if six fit in what is left of `kMaxShadowViews` = 8; a light that does not fit goes without rather than getting a partial cube |

The budget is eight atlas layers, shared. Three cascades plus one point light is nine, so the point
light silently goes without. That is the atlas occupancy figure this engine can actually state: see
§4.

`scene.environment.shadowCascades` overrides the tier's `cascadeCount` when it is non-zero
(`SceneRenderer::render`), so a scene can ask for fewer or more cascades than its tier.

### 1.2 How far do the shadows reach?

`directionalShadowRange` (`src/rendering/shadow_math.cpp`), ADR-112. Two rules, smaller wins:

1. `clamp(max(sceneRadius * 3, near * 20), near * 4, max(cameraFar, near * 4))` -- the world's size
   clamped to the camera's.
2. `texelTarget * kShadowRangeReference / 2.12` -- the range at which the **coarsest** cascade's
   texel is still no larger than `shadowTexelTarget` metres.

`kShadowRangeReference` is 2048 whatever the tier renders at, deliberately: how far shadows reach is
composition and must not change between a preview and a final; only how sharp they are differs.

**On the lab fixture and on Glowmere alike this is rule 2, and it is 77.28 m.** `shadowTexelTarget`
is 0.08 at every tier, so `0.08 * 2048 / 2.12 = 77.28`, and no scene in this repository has a camera
whose `world` rule is smaller. The number worth keeping in your head while reading anything below is
that **shadows in this engine stop at 77 m**, and fade out from 63.4 m (`SHADOW_RANGE_FADE = 0.18`
of the last split, eased `t * t`, in `shaders/shadows.wgsl`).

### 1.3 Where do the cascade boundaries fall?

`cascadeSplits` (`src/rendering/shadow_math.cpp`): the practical split scheme (Zhang 2006), a blend
of logarithmic and uniform by `lambda`, **hard-coded to 0.85**.

Measured on the lab fixture (`AVGEN_SHADOW_STATS=1 ... --size 1280x720`), camera near 0.5 m,
range 77.28 m, 3 cascades. The frame's **aspect** is part of this: the fit is the sub-frustum's
bounding sphere, so a wider frame gives a wider sphere -- the same fixture at the aspect
`--lab-case` defaults to reports half-extents of 6.75 / 20.81 / 80.94 m instead. Quote the size
with the numbers or the numbers do not reproduce.

    splits 6.19 / 19.99 / 77.28 m
    view 0  depth  0.50.. 6.19 m  half-extent  7.19 m  texel 0.0070 m
    view 1  depth  6.19..19.99 m  half-extent 22.31 m  texel 0.0218 m
    view 2  depth 19.99..77.28 m  half-extent 86.88 m  texel 0.0848 m

**Cascade 2 covers 74% of the shadowed depth and 87% of a 2048-map's worth of world.** That is what
`lambda = 0.85` does with a near plane three orders of magnitude below the far one: the logarithmic
half of the blend spends its resolution where the near plane is, which in an outdoor shot is empty
air. The near two cascades between them cover 0..20 m and, on the fixture's canonical frame, drew 4
and 8 entity casters against cascade 2's 30. This is not a defect -- it is the scheme behaving as
specified -- but it is the reason the coarsest texel is the only texel that matters here, and it is
why ADR-112 sizes the whole range from that one number.

### 1.4 What volume does a cascade cover?

`fitDirectionalCascade` (`src/rendering/shadow_math.cpp`).

* the sub-frustum between two view depths, corners interpolated along the camera frustum's edges;
* its **bounding sphere**, so the fit is invariant to camera rotation (this is what stops the edges
  swimming under a pan);
* radius quantised to 1/16 m, so it does not jitter;
* the light-space centre snapped to whole texels in a **rotation-only basis anchored at the world
  origin** -- the comment in that function records why a basis built around the cascade cannot work,
  and it is worth reading before anyone "simplifies" it;
* the light's near plane pulled back by `clamp(sceneRadius, radius*0.5, radius*3)` so casters behind
  the visible range still reach it.

The pull-back is why cascade 2's `depthRange` on the fixture is 434 m for a 174 m-deep box: the
scene radius is larger than `3 * radius`, so `back` clamps to `3 * radius` and 60% of the depth
buffer is spent on empty space behind the cascade. Documented rather than changed -- the clamp is
already the guard against it, and shortening it trades caster coverage for depth precision, which is
a decision that needs a scene that is hurting.

### 1.5 Which objects are drawn into the maps? (**the second cull**)

This is the stage with the most surface and the least symmetry, and it is where both defects below
live. There are **three independent caster paths** and they do not share a rule.

**(a) Entities.** `SceneRenderer::render`, now delegating to `rendering::casterState`
(`src/rendering/shadow_math.cpp`). Two passes:

1. the opaque draw list -- everything the camera kept that is `castsShadow`, not `MeshStyle::Grid`,
   not `MeshStyle::Water`, not `AlphaMode::Blend`;
2. a second pass over the entities the **camera** rejected, kept if any shadow view's frustum
   contains their world AABB (`anyCascadeSees`, ADR-046). A hill behind the camera casts into shot.

Then, per view, each candidate is tested against that cascade's own planes before it is drawn
(`aabbInsideFrustum`), so a caster is not drawn into a cascade that cannot see it.

**(b) Terrain chunks** are entities, and `Composition::flatten`
(`src/scene/composition.cpp`) writes their `castsShadow` per frame from **distance to the camera
eye**: `e.castsShadow = distance <= (terrain.shadowDistance > 0 ? shadowDistance : viewDistance)`.
Default 140 m, Glowmere and the lab fixture 150 m. There is **no hysteresis and no fade** on that
boundary. It is not a popping source in any scene here only because 150 m is comfortably past the
77 m the cascades reach -- a scene that shortened it below the shadow range would get a hard
on/off at a chunk boundary. Noted in §5 as a latent edge.

**(c) Procedural instances -- scatter, groves, grass, every glTF the world plants.**
`ProceduralRenderer::drawShadow` -> `drawImpl(..., shadowPass = true)`
(`src/rendering/procedural_renderer.cpp`).

**There is no second cull here at all.** `drawShadow` issues `DrawIndexedIndirect` against
`im.indirectArgs` -- the *same* buffer the camera pass and the depth prepass read, whose instance
counts were written by one compute cull run against **the camera frustum**
(`ProceduralRenderer::update` builds `planes` from `scene.camera.projection(aspect) *
scene.camera.view()`, one set, once). The only shadow-specific gate is `object.castsShadow`, which
is per object, not per instance.

So: **a tree the camera cannot see casts nothing, however plainly its shadow falls into shot.**
Entities get ADR-046; the ecology does not. Measured in §3.

**(d) SDFs** (`SdfRenderer::drawMeshes` and `drawRaymarchDepth`) are drawn into every view with no
cull of their own; the raymarched ones march at `sdfShadowSteps`.

### 1.6 The depth passes

`SceneRenderer::render`, one render pass per view, depth only, `depthOnlyPipeline_`, clear to 1.0,
into `shadows_->layerView(v)` -- one layer of a `texture_depth_2d_array`, `Depth24Plus`,
`quality.shadowResolution` square. Each pass binds `shadowFrameGroups_[v]`: **a copy of the whole
frame uniform block with the view-projection replaced** (`ShadowRenderer::upload`), so entities,
procedural instances and SDFs run their ordinary vertex shaders unchanged -- wind, world effects and
deformers included, which is how a swaying plant and its shadow stay in agreement.

That "replace only the matrices" trick is exactly where defect §3.2 lived: rungs 2 and 3 of the LOD
ladder are camera-facing quads built from `frame.cameraRight` / `frame.cameraUp`, and those two
lanes were **not** replaced.

### 1.7 The uniform block the shading pass reads

`ShadowUniforms`, 688 bytes, group 0 binding 3, mirrored by `shaders/shadows.wgsl`. Per view:
`viewProj`, and `params = (texel world size, depth bias, 1 when a cascade, far distance)`. Plus
`info = (atlas resolution, cascade count, PCF taps, PCSS on)`, `splits`, and `info2.x` = PCSS
blocker taps (ADR-227).

### 1.8 Selecting a cascade, per pixel

`cascadeFor(viewDepth, count)` in `shaders/shadows.wgsl`: the first cascade whose split is `>=` the
fragment's view depth. Mirrored on the CPU by `rendering::cascadeForDepth` for the diagnostics; the
comparison is `<=` in both, so a point exactly on a split belongs to the **nearer** cascade.

`shadowFactor` then:

1. looks the fragment up in cascade `index`;
2. if it is in the last `CASCADE_BLEND = 0.12` of that cascade's depth extent and there is another
   cascade behind it, looks it up in that one too and crossfades. This is what stops the split plane
   being a seam that sweeps across the ground as the camera dollies;
3. fades the whole term to 1.0 over the last `SHADOW_RANGE_FADE = 0.18` of the range, eased.

### 1.9 Bias

Two mechanisms, in two units, both in `shaders/shadows.wgsl` / `rendering::reportView`.

**Constant, slope-scaled**, in normalised depth:

    worldBias = texelWorldSize * 2 + 0.005            (metres)
    depthBias = worldBias / depthRange                (normalised)
    depth     = lookup.depth - depthBias * (1 + slope), slope = clamp(tan(acos(NdotL)), 0, 4)

**Normal offset**, in world units, applied *before* the projection:

    offset = normal * (texelWorld * (1 + 2 * (1 - NdotL)) * 1.4 + light.shadowBias)

Why it exists and how it was chosen (§28 asks for this paragraph explicitly): a depth map samples
the caster at texel centres, so a receiver that *is* the caster reads its own depth quantised to a
texel and shadows itself in stripes. The constant must exceed the depth error one texel of slope
produces, which is why it is **a multiple of the view's own texel** and not a number -- a cascade
covering 6 m and one covering 77 m would otherwise need different constants for the same picture.
The 5 mm floor covers a view whose texel is tiny, where two texels do not clear the depth buffer's
own quantisation. The division by `depthRange` is what makes one authored constant mean the same
visual bias in every cascade. On the lab fixture the three cascades come out at 0.0190 / 0.0486 /
0.1747 m -- a 9x spread in metres and a 1.3x spread once normalised, which is the conversion doing
its job.

The invariant it protects: **a lit surface does not shadow itself.** The cost it pays: peter-panning
proportional to `worldBias`, so cascade 2's 17 cm is the number to look at first if a contact looks
detached at range. Nothing here was changed by this lab.

### 1.10 Filtering

`pcf` -- 16-point rotated Poisson disc, rotation per pixel from interleaved gradient noise, `taps`
from the tier (6/12/20/24). Past 16 the disc is rotated a further half-spacing so the second sixteen
land between the first sixteen.

`pcss` (ADR-227) -- a blocker search over `info2.x` taps at a fixed radius, then `pcf` at a radius
the search chose. Reserved for cascaded lights when `softShadows` is on.

### 1.11 The half-resolution mask (ADR-087) and the AOV (ADR-255)

`ShadowMaskRenderer` computes, per leading directional light (at most three), exactly the term the
lit pass would: the cascaded PCSS lookup minned with the contact march, at `shadowMaskScale` of the
frame, read back by a depth-aware four-tap upsample with a nearest-depth fallback and an explicit
`valid` flag.

`shadowMaskScale` is **1.0 at `high` and `offline`**, and `>= 1` is how a tier says *do not build
one*. See §6 for the verdict this lab was asked for.

### 1.12 Contact shadows

`contactShadow` in `shaders/shadows.wgsl`: a short screen-space march through the linear depth
target towards the light, `frame.shadowParams.z` steps. It is the one shadow term the cascades
cannot give -- a cascade fitted to the whole frustum always misses where a small part meets a
surface. `--quality-arm contact` removes it.

---

## 2. What does not exist

Written down rather than filled in, because an honest map beats a lab that pretends to test stages
that are not there.

**No per-cascade caster list for procedural instances.** §1.5(c). This is a gap, not an absence by
design: the architecture has one cull and three consumers.

**No shadow-map readback of any kind.** The atlas is created with
`RenderAttachment | TextureBinding` and no `CopySrc`, so no tool in this repository can read a
cascade's depth. "Shadow-map occupancy" in the sense of *what fraction of the layer a caster wrote
to* is therefore not computable here, and this lab reports the structural quantities instead (§4)
rather than inventing one.

**No cached or scrolling shadow maps.** Every cascade is cleared and redrawn every frame. So
"cascade stabilisation" here means only the texel snap and the bounding-sphere fit -- there is no
reprojection, no partial update, and nothing that could carry a frame of lag.

**No shadow-specific LOD.** A caster is drawn into a shadow view at whatever mesh the camera pass
chose. For an entity that is its only mesh; for a procedural instance it is the rung the **camera's**
projected size picked, which is §3.2's subject.

**No per-light cascade count and no per-light shadow distance.** One cascaded directional light per
frame, one range for the frame. `PunctualLight::shadowBias` and `shadowStrength` are the only
per-light shadow controls that reach the shader.

**No area-light shadow softness from the emitter's size.** `light.sizeSoft.w` (`softness`) drives
the PCSS penumbra and is authored, not derived from `width`/`height`/`radius`.

---

## 3. What this lab found

### 3.1 A procedural instance stops casting the moment the camera cannot see it

**Reproduction.** `examples/labs/shadow-lab.scene.json` carries a deliberate pair: `pair-entity`, a
glTF `CommonTree_1` at 1.9x, and `pair-instance`, a one-instance procedural object with the same
asset at the same scale and `lod.cull` on. Same size, same light, 12 m apart in depth. Yaw the
camera and read the counters.

**Measured**, lab fixture, 1280x720, 31 frames, only the camera yaw changed:

    yaw     instances reaching the shadow map    entity shadow draws    entity casters
      0                   7 of 7                          42                 36
    -10                   7 of 7                          42                 37
    -20                   7 of 7                          42                 37
    -30                   7 of 7                          46                 39
    -40                   6 of 7                          41                 38

At -40 degrees `pair-instance` -- the procedural half of the pair -- leaves the camera frustum
(`skipped` indirect draws go 20 to 30, which is its whole object being elided) and its shadow
stops being drawn. `pair-entity`, the same glTF at the same scale twelve metres in front of it,
is still a caster: the entity draw count moves only with terrain chunks entering and leaving.
That is the arm, in one scene, with its control beside it.

The fixture is deliberately sparse, so the *size* of the effect has to be read off a production
scene. Glowmere multicam, one frame, 1280x720:

    62,284 procedural instances in the scene
       496 survive the camera cull
       496 are drawn into the shadow maps

Those are one number, not two, and that is the finding: the ecology's caster list **is** its
visibility list. The claim is not that all 61,788 should cast -- most are far away or behind --
it is that no shadow view is ever asked. The CPU regression test below makes the point without
a device: a Glowmere canopy tree 80 m off to the side is rejected by the camera frustum and
contained by cascade 2, and the shadow passes draw the camera's verdict.

**Root cause.** §1.5(c). One cull, camera frustum, three consumers. Provable from the code without a
device: `drawShadow` and `draw` reach the same `im.indirectArgs` at the same offsets, and
`ProceduralRenderer::update` builds exactly one `FrustumPlanes`, from `scene.camera`.

**Not fixed here, and the reason is stated rather than implied.** The correct fix is a second
visible list: another cull dispatch per object against the union of the cascade frusta, its own
indirect-args region and its own per-level bind groups, consumed by `drawShadow` alone. That is
roughly 150 lines inside `procedural_renderer.cpp`, which is the Visibility Lab's and the LOD Lab's
file this week, and it changes the cost of every frame with an ecology in it. The two cheap-looking
alternatives are both wrong and are recorded so nobody tries them: widening the single cull's
frustum makes the **camera** pass draw everything the light can see, and it is the camera pass that
is triangle-bound; and extruding the camera planes toward the light has the same problem for the
same reason. A regression test carrying the invariant is in place and marked `[!shouldfail]`, so the
day somebody builds the second list the suite says so.

### 3.2 A LOD impostor faced the camera while being rasterised from the light

**Reproduction.** `impostor-row` and `mesh-row` in the fixture: the same asset, the same scale, the
same three depths, under the same light. `impostor-row` has four rungs with thresholds that put it
on rung 2 -- the first camera-facing billboard; `mesh-row` has one rung and can never demote. The
ladder is the only difference between them.

**Measured**, plan view (`examples/labs/shadow/cases.json` case 3), dark fraction of the ground
rectangle each row's shadow falls on, threshold midway between lit and shadowed:

    before   impostor 0.0045     mesh 0.2826     over 18239 / 20026 pixels
    after    impostor 0.1234     mesh 0.2826

The impostors cast **0.45% of what the identical mesh row cast**. The control -- the mesh row, in
the same frames -- does not move at all across the fix, which is what says the change reached the
impostor path and nothing else.

**Root cause.** Rungs 2 and 3 of the ladder are camera-facing quads: `shaders/procedural.wgsl`, the
`proc.fieldInfo.z > 0.5` branch, builds the quad from `frame.cameraRight` and `frame.cameraUp`.
`ShadowRenderer::upload` gives each shadow view a copy of the frame block with **only the
view-projection and its inverse replaced**, so those two axes stayed the camera's. The quad was
therefore oriented for the viewer and rasterised from the light, and presented its edge to the sun
whenever the sun was at right angles to the camera -- which is the lab fixture's light by
construction and a common key angle by taste. A stationary tree under a stationary sun cast a shadow
whose size depended on where the camera was standing.

**Fix.** `ShadowRenderer::upload` now also replaces `cameraRight` and `cameraUp` with the shadow
view's own basis, read out of the `lookAt` that defines the view rather than rebuilt beside it.
`ShadowView` carries them; `scene_renderer.hpp` asserts the two byte offsets against `offsetof`.

Only those two lanes are replaced, deliberately. `cameraPos` stays the camera's, because
`deformWorld` and the wind take a to-camera vector from it, and a caster deformed differently in the
shadow pass than in the camera pass is a shadow that does not line up with its object. Nothing else
in a depth-only pass reads either axis: the only other readers of `frame.cameraRight` in the tree
are the debug point sprites, GTAO and the shadow-mask pass, and none of the three runs inside a
shadow view.

The impostor's shadow is still smaller than the mesh's (12.3% against 28.3%) and that is correct: it
is a flat card standing in for a canopy, and its shadow is now that card's silhouette rather than a
number that depends on the camera. What was a *dependency on the viewer* is now a *fidelity
difference*, which is the thing an impostor is allowed to have.

**How much of Glowmere this moves**, measured on the multicam frame: of the 496 instances the frame
draws, `lod=161/196/134/5` -- **139 are on rungs 2 and 3**. Every one of those was casting a shadow
whose size depended on the angle between that shot's camera and its sun, and is not any more. A
canopy tree never reaches rung 2 inside the 77 m shadowed range (it would need 970 m), so the large
layers are untouched and the 139 are the small ones: the grass, the weeds, the pebbles.

### 3.3 Found on the way and routed elsewhere: the impostor is sized without its source transform

`makeLodMesh(spec, level, impostorSize)` (`src/scene/procedural.cpp`) sizes the rung-2 quad as
`2 * impostorSize * sourceBoundingRadius(spec)` -- from the **raw asset**. The billboard branch of
`shaders/procedural.wgsl` then scales it by `inst.scale` alone and never applies
`proc.sourceMatrix`. Rungs 0 and 1 do go through `sourceMatrix`.

Every terrain scatter layer normalises its asset's height onto `sourceTransform`
(`Composition::flatten`: *"The layer says how tall the thing should be; the asset says how tall it
is... The normalisation goes on sourceTransform"*), so **every scatter layer's impostors are drawn
at the asset's authored size**. Reproduced with a grid of `CommonTree_1` shrunk to 0.5 m by
`sourceTransform`: at rung 2 they drew as a hedge of 7.3 m trees, fourteen times too tall, in the
camera pass as well as in the shadows. `lod.impostorSize` does not compensate -- setting it to 0.07
changed nothing in the frame, which is its own loose end.

This is a camera-pass defect that happens to break a shadow experiment, not a shadow defect. It is
recorded here because it is the reason `impostor-row` in the fixture uses authored thresholds and an
unscaled asset instead of a scatter layer's own 28/11/4, and it belongs to the LOD Lab.

### 3.4 What the corrected cull radius changed for casters

The Visibility Lab's `rendering::sourceCullRadius` fix (390 -> 444 instances visible on Glowmere
multicam) moved the caster list by **exactly the same 54 instances, one for one**, and moved the
entity caster list by **nothing at all**.

Both halves follow from §1.5 and neither needs a measurement to establish, which is the useful part:

* procedural instances have no shadow-specific cull, so "visible to the camera" and "drawn into the
  shadow maps" are the same set by construction. The radius fix is a caster fix with the same
  number on it.
* the entity second cull tests a **transformed AABB**, not a radius -- `entityWorldBounds` takes the
  mesh's eight local corners through the model matrix and re-bounds them. `sourceCullRadius` is not
  in that path and never was, so no entity's caster state could have changed.

So the honest answer to "did the radius fix repair shadow popping" is: **it repaired the half of it
that is the ecology, and it did so silently, because the ecology's caster list is its visibility
list.** It did not touch the atlas budget (views, resolution and range are all independent of what
is drawn) and it did not touch entities. It also did not fix §3.1, which is the same mechanism one
step further out: the radius fix made the camera frustum's verdict *correct*, and §3.1 is that the
camera frustum's verdict is the *wrong question* for a caster.

---

## 4. What the lab exposes

`AVGEN_SHADOW_STATS=1` prints one frame line and one line per view, from
`ShadowRenderer::stats()`. Every number is written by `rendering::reportView`, which is the same
call that writes the uniform the shader reads -- so a bias printed here cannot drift from the bias
subtracted there.

    shadows: 3 view(s) (3 cascade, 0 spot, 0 point) at 2048x2048; range 77.28 m, fade from 63.37 m,
    coarsest texel 0.0848 m; splits 6.19/19.99/77.28/77.28; 36 casters, 40 entity draws, 83 rejected
    by a view's own frustum; light (0.829, -0.559, 0.000) camera (0.00, 6.00, 14.00)
      view 0 cascade: depth 0.50..6.19 m, centre (-0.00, 5.89, 10.66), half-extent 7.19 m,
      texel 0.0070 m, depth range 35.94 m, bias 0.0190 m = 0.000530 normalised,
      normal offset 0.0098 m, 4 draws, 30 culled

Mapped to §15's list: **cascade ID** = `view N`; **shadow distance** = `range` and `fade from`;
**bias values** = both, in both units, plus the normal offset; **light direction** and **camera
position** on the frame line; **shadow-map occupancy** = views of eight, resolution, each view's
world half-extent and texel, and the draws and rejections each one took (see §2 for why the texel
occupancy is not here); **caster state** and **receiver state** through the overlays below.

### The cascade overlay

There was none: `ShadowView` carried four matrices and nothing drew them. Three switches now, in
`DebugViewOptions`, on the World panel, and in the `shadow` lab overlay profile:

* **Cascade volumes** (`shadowCascades`) -- the orthographic box each view rasterises into, through
  the inverse of the matrix the renderer **uploaded**, plus a point at the texel-snapped centre. The
  centre is the instrument for stabilisation: it moves in whole texels or not at all, so a crawling
  cascade can be watched crawl.
* **Cascade slices** (`shadowCascadeSlices`) -- the part of the camera frustum whose pixels select
  each view, in the same colour. This is the half that answers "why did the shadow change when I
  dollied": the boundary you can see sweeping is the near face of the next slice.
* **Shadow casters** (`shadowCasters`) -- every entity's world AABB coloured by
  `rendering::casterState`: green casts, amber casts although the camera rejected it, red does not.

`Cascade shown` restricts the first two to one layer.

Two properties of this overlay are load-bearing. It takes `std::span<const ShadowView>` from the
renderer and **fits nothing of its own** -- an overlay that fitted its own cascades would agree with
the renderer exactly until the day it mattered. And the caster colouring calls
`rendering::casterState`, which is the function `SceneRenderer::render` itself calls: the rule used
to be three lambdas inside a 1,450-line function, where a diagnostic could only reach it by writing
it a second time.

One honest limitation, stated in the code as well: `buildDebugGeometry` runs **before** `render()`
fits this frame's views, so the boxes are one frame old. Fitting a second set to draw would remove
the lag and make the overlay incapable of disagreeing with the renderer, which is the §37 trap; the
lag is the better of the two.

---

## 5. Latent edges, not reproduced

* **Terrain `shadowDistance` has no hysteresis.** §1.5(b). Harmless at 140-150 m against a 77 m
  range; a scene that shortened it below the range would get a chunk's worth of shadow appearing and
  disappearing on one frame as the camera dollies. No scene here does.
* **The texel snap can push a sphere's edge out of its own ortho box.** `floor` moves the centre up
  to one texel in -x and -y, so the +x/+y extreme of the fitted sphere can fall outside the box and
  read as unshadowed. One texel is 8.5 cm on cascade 2, at the very corner of a cascade, inside the
  crossfade band in depth but not in x/y. Not observed; recorded because it is a real consequence of
  the snap and somebody will find it eventually.
* **`back` clamps to `3 * radius`.** §1.4. 60% of cascade 2's depth range is behind the cascade on
  any scene whose radius is large, which is every world scene here.

---

## 6. The never-built mask path: the verdict

**It is a deliberate and correct optimisation, not a latent bug, and ADR-255 already found and fixed
the one thing that was wrong with it.**

`shadowMaskScale >= 1` is how a tier says *do not build a mask*; `high` and `offline` sit there. The
reasoning is sound on its face: the mask exists to compute a low-frequency term at a quarter of the
pixels, and a full-resolution mask is a second full-resolution pass over the same term -- it costs
more than it saves. A tier that renders for the eye rather than for the clock should compute the
term inline, and an offline frame is then **byte-identical to the frame this code shipped without**,
which is the property that matters most about an optimisation.

The path is not unexercised, either, which is the question I was asked to answer:

* `--quality-arm maskfull` runs the mask at scale 1.0 and consumed; `--quality-arm maskconsume` is
  the diagnostic no tier sets; `--quality-arm shadowatlas1k` is their positive control (ADR-182), and
  `examples/labs/rendering/cases.json` cases 5 and 6 are that pair, written down.
* `tests/rendering/test_shadows_gpu.cpp` has four mask tests including *"an offline render builds no
  shadow mask"* -- the tier's behaviour is pinned, not assumed.
* `tests/rendering/test_shadow_normals_gpu.cpp` has six more, including agreement across a cascade
  transition and under camera motion.
* ADR-255 found the real consequence -- `--aov shadow` at `offline` would have exported a 1x1 white
  texel -- and fixed it by running the pass for the export **without letting the lit pass consume
  it**, so the AOV cannot change the deliverable.

The residual cost is the one ADR-255 names and ADR-258 measured: the exported term is **recomputed**
rather than captured, and it agrees with the consumed one to 0.43% of pixels against a control that
moves 1.86%. That is a labelled approximation, not a hidden one.

The one thing I would still call open is smaller than the question: the mask is capped at three
directional lights and a fourth silently falls back to the unmasked path for **every** light, not
just the fourth. That is in the header and is correct behaviour; it is just not visible from any
counter a person can read at runtime.

---

## 7. Fixtures, cases and controls

`examples/labs/shadow-lab.scene.json`, with `examples/lightrigs/shadow-lab.rig.json`.

Flat terrain, no fog, no volumetrics, one key and one ambient, and **the key at azimuth -90,
elevation 34**: the sun travels exactly +X with no Z component. Two properties follow, and both are
the fixture rather than decoration. Every shadow lies to its caster's right, so yawing the camera
takes a caster off frame before its own shadow -- the only camera move that separates "this object
stopped casting" from "this object left the shot". And the light is perpendicular to the canonical
camera, which is the angle at which a camera-facing quad presents nothing to the sun. 34 degrees
makes the run 1.482 m of shadow per metre of height, so a caster's length on the ground is a number
the lab predicts rather than measures.

Flat on purpose: on a hillside every caster meets the ground at a different offset, so "the shadow
moved" and "the ground moved" are the same picture.

| fixture | what it is | why |
|---|---|---|
| `ground` | flat terrain, 420 m, `shadowDistance` 150 | the receiver, and the terrain-chunk caster path |
| `tree` | `CommonTree_1` at 1.9x, a Glowmere canopy tree | §29: the production asset the complaint is about |
| `character` | `alien-scout.glb`, idling | a skinned caster: the shadow passes bind the skinning pipeline |
| `building` | 7 x 9 x 7 m box | a hard silhouette with a long run |
| `thin-upright` | 6 x 6 x **0.04** m | thinner than cascade 2's texel (8.5 cm); the case a 1.4-texel normal offset moves clean through |
| `thin-flat` | 6 x **0.04** x 6 m | the same thickness in the other orientation, where the slope-scaled bias is at its smallest |
| `large-slab` | 16 x 10 m | wider than cascades 0 and 1 put together |
| `small-pebble` | 0.25 m | four times under the coarsest texel: it is here to be missing |
| `post-00..12` | 13 identical 0.35 x 4 m posts at z = +1 to -59, 5 m apart | the cascade-boundary row. Nothing about a post changes along it except its depth, so any change in its shadow is the cascade machinery. **The splits are view depth from the camera, which stands at z = +14** -- an obvious thing the first version of this row got wrong by starting at z = -20, which is already 34 m deep and past every split. As spaced now the posts are 13, 18, 23 ... 73 m deep: the 19.99 m split falls between post-01 and post-02, the range fade begins between post-10 and post-11, and the range itself ends just past post-12 |
| `pair-entity` / `pair-instance` | the same tree as an entity and as a culled procedural instance | §3.1's arm and its control, in one frame |
| `impostor-row` / `mesh-row` | the same tree on a four-rung ladder and on one rung | §3.2's arm and its control, at the same depths under the same light |

**Not a well-behaved cube in the middle of cascade 0** (ADR-182's lesson from the visibility bug):
four of the eleven are thin, oversized or sub-texel, two are real glTF assets, one is skinned, and
the posts exist to straddle boundaries rather than sit inside one.

Cases: `examples/labs/shadow/cases.json`, reachable as `avgen --lab-case shadow:<n>`. Every arm has
a control, and the controls are named as controls in the file.

---

## 8. Reading list

`src/rendering/shadow_math.{hpp,cpp}` -- the fit, the splits, the caster rule, the reports. No
device; checked by the CPU suite.
`src/rendering/shadow_renderer.{hpp,cpp}` -- views, atlas, uniforms, the frame-block copy.
`src/rendering/shadow_mask_renderer.{hpp,cpp}` -- ADR-087 and ADR-255.
`shaders/shadows.wgsl` -- selection, crossfade, range fade, bias, PCF, PCSS, contact march.
`src/rendering/scene_renderer.cpp` -- the caster lists and the depth passes. There is no frame
graph; pass order is hand-coded in `SceneRenderer::render`.
