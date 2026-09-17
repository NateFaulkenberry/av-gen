# ADR-262: The shadow pass is a second cull, and only half the engine knows it

**Status:** Accepted
**Date:** 2026-09-17
**Context:** Shadow Lab (ADR-260 §15, ADR-261). Full map: `docs/shadow-lab/README.md`.

## Context

The complaint the lab was opened on is shadow popping in Glowmere: shadows appearing and
disappearing, and edges that are not stable. The brief assumed the cause would be in the cascade
machinery -- the fit, the splits, the bias, the stabilisation. It is not. ADR-034's fit, ADR-112's
range rule and the texel snap are all in good order, and the two defects the lab reproduced are both
in **who is drawn into the maps**, which is a stage nobody had written down as a stage.

A shadow caster list is a second cull. This engine has three caster paths and they do not share a
rule:

* **entities** get ADR-046's second pass -- the opaque list, plus the entities the *camera* rejected
  that a shadow view's frustum still contains;
* **terrain chunks** are entities whose `castsShadow` is written per frame from distance to the
  camera eye (`terrain.shadowDistance`, default 140 m);
* **procedural instances** -- every scattered tree, plant, rock and blade of grass -- get nothing.
  `ProceduralRenderer` runs **one** cull, against the camera frustum, and `drawShadow` issues its
  indirect draws against the args that cull wrote.

Until this ADR the first of those was three lambdas inside `SceneRenderer::render`, a 1,450-line
function, where nothing else could ask it a question.

## Decisions

### 1. The caster rule is a function, not a lambda in the renderer

`rendering::casterState` in `shadow_math.hpp`, with `casterEligibility` and `entityWorldBounds`
beside it. `SceneRenderer::render` calls it and so does the new cascade overlay, so the diagnostic
and the frame cannot disagree about who cast (§37). It needs no device and is checked by the CPU
suite against thin, rotated, off-origin and oversized fixtures -- **not** a centred cube, which is
the shape that hid ADR-199's cull-radius bug for as long as it hid it.

`CasterState` names six outcomes and there is no "not submitted" catch-all, for the reason
`VisibilityReason` gives: every value but the first is one.

### 2. A camera-facing impostor faces the light it is being rasterised from

Rungs 2 and 3 of the LOD ladder are camera-facing quads built in the vertex shader from
`frame.cameraRight` / `frame.cameraUp` (`shaders/procedural.wgsl`). `ShadowRenderer::upload` hands
each shadow view a copy of the frame block with **only the view-projection and its inverse
replaced**, so those two axes stayed the camera's: the quad was oriented for the viewer and then
rasterised from the sun.

With the key at right angles to the lens -- an ordinary cross-light -- the card presented its edge
and cast nothing. Measured on the lab fixture's plan view, the same asset at the same scale at the
same depths: the impostor row darkened **0.45%** of the ground its shadow falls on where the
one-rung mesh row darkened **28.3%**. A stationary tree under a stationary sun had a shadow whose
size depended on where the camera was standing, which is a shadow that is not a fact about the
world.

`upload` now replaces `cameraRight` and `cameraUp` with the shadow view's own basis, read out of
the `lookAt` that defines the view rather than rebuilt beside it. After: 12.3% against an unchanged
28.3%. The control not moving is the half that says the change reached the impostor path and
nothing else.

**Only those two lanes.** `cameraPos` stays the camera's, because `deformWorld` and the wind take a
to-camera vector from it and a caster deformed differently in the shadow pass than in the camera
pass is a shadow that does not line up with its object. Nothing else in a depth-only pass reads
either axis: the other readers of `frame.cameraRight` in the tree are the debug point sprites, GTAO
and the shadow-mask pass, and none of the three runs inside a shadow view.

The impostor's shadow is still smaller than the mesh's, and that is correct. It is a flat card
standing in for a canopy and its shadow is now that card's silhouette. What was a *dependency on the
viewer* is now a *fidelity difference*, which is the thing an impostor is allowed to have.

### 3. The ecology's caster gap is recorded, measured, and not fixed here

Over a 40-degree pan of the lab fixture, `pair-instance` -- a procedural object carrying the same
glTF at the same scale as `pair-entity` twelve metres in front of it -- leaves the camera frustum
and stops casting, while the entity keeps its shadow. On Glowmere multicam the scale of it is
plainer: **62,284 procedural instances, 496 survive the camera cull, and 496 are what the shadow
maps are drawn from.** Those are one number, not two. The claim is not that all 61,788 should cast;
it is that no shadow view is ever asked.

The fix is a second visible list: another cull dispatch per object against the union of the cascade
frusta, its own indirect-args region and its own per-level bind groups, consumed by `drawShadow`
alone. That is real work inside `procedural_renderer.cpp` and it changes the cost of every frame
with an ecology in it, so it is routed rather than rushed. The two cheap-looking alternatives are
recorded as wrong so nobody tries them: widening the single cull's frustum makes the **camera** pass
draw everything the light can see, and extruding the camera planes toward the light does the same
thing for the same reason -- and it is the camera pass that is triangle-bound.

A `[!shouldfail]` test carries the invariant, so the day somebody builds the second list the suite
says so.

### 4. What the corrected cull radius changed for casters

ADR-199's `sourceCullRadius` fix (390 -> 444 instances on Glowmere multicam) moved the caster list
by exactly the same 54 instances, **one for one**, and moved the entity caster list by **nothing**.
Both follow from the stages above rather than from a measurement: procedural instances have no
shadow-specific cull, so visible and casting are the same set by construction; and the entity second
cull tests a transformed AABB, which `sourceCullRadius` is not in the path of and never was.

So the radius fix repaired the half of Glowmere's shadow popping that is the ecology, silently,
because the ecology's caster list *is* its visibility list. It did not touch the atlas budget --
views, resolution and range are all independent of what is drawn -- and it did not touch entities.
It did not fix decision 3 either: the radius fix made the camera frustum's verdict correct, and
decision 3 is that the camera frustum's verdict is the wrong question for a caster.

### 5. The never-built mask path is a correct optimisation

`shadowMaskScale >= 1` is how a tier says "do not build a mask" and `high` and `offline` sit there. A
full-resolution mask is a second full-resolution pass over the same term; it costs more than it
saves, and an offline frame is byte-identical to the frame this code shipped without. The path is
exercised by `--quality-arm maskfull` / `maskconsume` with `shadowatlas1k` as its control, by four
tests in `test_shadows_gpu.cpp` including one that pins *"an offline render builds no shadow mask"*,
and by six more in `test_shadow_normals_gpu.cpp`. ADR-255 found and fixed the one real consequence
(`--aov shadow` would have exported a white texel) and ADR-258 measured the residual at 0.43%
against a 1.86% control.

Verdict: deliberate and correct, not a latent bug.

## Consequences

Glowmere's rendered output changes wherever a scatter layer's impostors fall inside the 77 m shadow
range -- the small layers, which are the ones whose instances reach rungs 2 and 3 at all. Those
shadows were absent or camera-dependent and are now present and stable. A canopy tree never reaches
rung 2 inside the shadowed range (it would need 970 m and the range is 77), so the large layers are
unchanged.

`ShadowView` grew `nearDistance`, `center`, `orthoRadius`, `right` and `up` -- all of them numbers
the fit already computed and threw away, published so a diagnostic does not have to derive them
again. `ShadowStats` grew a per-view report. `AVGEN_SHADOW_STATS=1` prints it.

Two byte offsets into `FrameUniforms` are now named constants on `ShadowRenderer` and asserted
against `offsetof` in `scene_renderer.hpp`, so inserting a field above them stops the build rather
than letting a shadow pass write a light direction into `cameraPos`.

## Recorded because it cost something to learn

**A `Distribution::center` does nothing for a Grid.** It is documented for Radial and Spiral and the
Grid is centred on the object's own origin; a lab fixture that placed three groves with it put all
three at the world origin, one of them fourteen metres from the camera, and the resulting frame read
as a scene-scale bug for half an hour.

**The LOD-2 impostor is sized from the raw asset and never sees `sourceTransform`.**
`makeLodMesh(spec, level, impostorSize)` takes the `SourceSpec` alone, and the billboard branch of
the shader scales the quad by `inst.scale` only. Every terrain scatter layer normalises its asset's
height onto `sourceTransform` -- so every scatter layer's impostors are drawn at the asset's
authored size, in the camera pass as well as in the shadows. Reproduced with a grid of
`CommonTree_1` shrunk to 0.5 m: at rung 2 they drew as a hedge of 7.3 m trees. `lod.impostorSize`
does not compensate. This is a camera-pass defect that happened to make a shadow experiment
unmeasurable; it is the LOD Lab's, and a `[!shouldfail]` test carries it.

**A probe placed where the thing being measured is invisible is a probe fault.** The first version
of the camera-independence test put its second camera at the sun's own altitude, from which a
shadow is exactly behind the object that casts it. It read the box, not the floor, and reported no
shadow where there was a perfectly good one.
