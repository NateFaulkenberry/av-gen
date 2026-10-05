# ADR-1160: A raymarched SDF casts its shadow from the light's view

- Status: Accepted (proto/digital-mosh)
- Amends ADR-034 (raymarched SDFs appear in the shadow maps) and ADR-1002 (`castShadows`).
- Found by: DIGITAL MOSH, whose first frame of a tree on a plain under an 11-degree sun had no tree shadow at all.
  A raster pillar beside it threw a correct 25 m shadow.

## Problem

ADR-034 says a raymarched SDF is marched into the shadow maps. It was, but wrongly, in three ways:

1. **The rays started at the camera.** `fs_sdf_shadow` rebuilt each ray from `frame.cameraPos`. The shadow views
   keep the *camera's* `cameraPos` on purpose, because the deformers and the wind read a to-camera vector from it
   (`shadow_renderer.cpp`). So the light's map was filled by rays fired from the viewer's eye through the light's
   orthographic pixels.
2. **The quad was the camera's.** The shadow pass drew each object over `sdf.rect`. That rect is the camera's
   projection of the bounds, computed on the CPU, and in the light's map it covers an unrelated region. A
   `[.probe]` in `test_sdf_gpu.cpp` had recorded this half: switching the light's shadow off changed its frame
   by zero bytes.
3. **An off-screen SDF was dropped from every pass**, including the shadow pass. A caster outside the frame threw
   no shadow into it.

Once the first two were fixed, a fourth fault showed up in a comparison with a mesh caster:

4. **The tolerance grew with the light's distance.** The hit test is relative (`d < epsilon * t`). With `t`
   measured from a cascade's near plane, often hundreds of metres up the light, every caster came out about 15%
   fatter. On a sphere, the SDF's shadow covered 5,219 px and the mesh's 3,937 px.

## Decision

The shadow march is the light's:

- **Rays.** They start on the shadow view's near plane and travel to its far plane: parallel rays for a
  directional cascade, rays from the light for a spot or a cube face.
- **Distance.** `t` is measured from where the ray enters the object's bounds, and the camera's `maxDistance`
  does not apply.
- **The quad.** The shadow pipelines use their own vertex entry, `vs_sdf_shadow`. It projects the eight bound
  corners through `frame.viewProj`, which in a shadow pass is the light's, so each view gets its own rect. A
  corner at or behind the eye plane covers the whole view.
- **Off-screen objects.** When some enabled light casts, an object whose bounds are off the camera's screen
  stays in the item list as **shadow-only** (`onCamera` false). The lit pass and the depth prepass skip it, and
  `SdfStats::shadowOnlyObjects` counts it. `objects` and `raymarchObjects` count only what the camera draws.

The depth prepass is unchanged. It still marches from the camera eye, exactly as the lit pass does, so the two
depths keep agreeing.

## Consequences

- An SDF throws the shadow a mesh throws. Test: `A raymarched SDF casts the shadow a mesh casts, from any camera,
  caster on or off screen` compares a sphere SDF with an icosphere mesh from three vantages, one of them with the
  caster out of frame. The darkened area agrees within 1% (for example 3,971 vs 3,937 px, and 17,592 vs
  17,537 px off screen), and the centroids within half a pixel.
- The old probe is now an ordinary test: `A raymarched SDF reaches the shadow map at the tier's shadow budget`.
  An 8-step march and a 192-step march give different shadows, so the tier's `sdfShadowSteps` reaches the picture.
- **Every scene with a shadow-casting light and an SDF caster changes.** Its SDF shadows move to where the light
  puts them. Liminal's world SDF does not cast (`castShadows: false`), so it is unchanged.
- **Cost.** A shadow-only item is one bounded quad per shadow view, and nothing in the camera passes. Each view's
  quad is the caster's own footprint in that view, which is usually smaller than the old camera rect.

## Rejected alternatives

- **Per-view rects computed on the CPU.** That needs one uniform slot per object per view. The vertex stage
  already has the bounds and the view's matrix.
- **A full-screen quad in every shadow view.** It is correct, but it would cost a whole cascade per caster.
- **Make the shadow views' `cameraPos` the light's.** That would break the deformers and the wind, which need the
  camera's to-camera vector to deform a caster the same way in both passes (`shadow_renderer.cpp`'s note).
