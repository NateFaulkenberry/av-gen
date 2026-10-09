# ADR-1286: Cinematic water reflects and refracts the lit frame

- Status: Accepted (2026-10-09), proto/bio-valley (Deliverable A of the cinematic finishing pass, spec 07 §3)
- Code: `shaders/water.wgsl` (blocks marked `cinematic (ADR-1286)`), `src/rendering/water_renderer.{hpp,cpp}`,
  `src/rendering/scene_renderer.cpp` (the pass split and the frame copy), `src/scene/water_surface.{hpp,cpp}`,
  `src/scene/composition.cpp` (read and write).
- Tests: `tests/unit/test_water_cinematic.cpp`, `tests/rendering/test_water_cinematic_gpu.cpp` (`[adr1286]`).

## Context

The riverbank's water read as a flat blue sheet. Diagnostic renders (spec 07 §3.1, `finishing/A-water/diagnostics`)
found why:
- With the body colour and the reflection off, the surface is black. Its blue was entirely the environment cube
  reflected at a grazing Fresnel, which is the sky, not the forest that actually surrounds this river.
- The only colour the surface could carry was the cube. ADR-099's water samples the environment cube and nothing
  else, so the lit bank, its organisms, the canopy and the falls could not appear in it. The bank's light reached the
  water only through ADR-1281's emitter mirror, and that reflects emitter sprites, not geometry.
- Seeing through the water was an alpha blend over the bed, so depth showed as a single opacity, with no absorption
  by colour.
- The three ripple layers are one noise texture at three scales. There was no broad swell and no difference between
  calm and ruffled reaches, so the surface read as a uniform glossy sheet.

## Decision

An optional cinematic path, switched per water material by any of `ssr`, `transmission`, `undulation`,
`roughnessVariation` or `tintPatches` above 0:

- **The frame copy.** When a cinematic material is drawn, the scene pass is split just before the water (as it is
  around the SDF raymarch), and the lit HDR frame (opaque world, sky, atmosphere) is copied. The cinematic pipeline
  binds that copy beside its uniforms in group 2.
- **Screen-space reflection.** It is marched in world space against the linear depth, with quadratic step spacing
  and a binary refinement. Rays that leave the frame or find nothing fall back to the environment reflection. The
  reflection blurs with the local roughness and the hit's distance. `reflectionRays` rays per pixel have their
  normals spread along the view (`streak`), so a light reflects as a vertical streak, as it does on rippled water.
- **Transmission.** The bed is read from the copy at the refracted point and absorbed per channel over the view
  path (`absorption`, red first). The surface composites it itself, instead of using an alpha over the bed.
- **Surface structure.** A broad, flow-aligned swell (`undulation`) sits under the ripples, using bounded advection
  (ADR-914). Calm and ruffled regions (`roughnessVariation`) scale both the ripple amplitude and the roughness the
  reflection blurs with.
- **Luminous accents.** Broad patches of a second reflection tint (`tintPatches`, `reflectionTint2`) cover the
  environment reflection. Glints (`glints`) are fine facets that catch what the reflection found, in its colour. They
  are not random dots: where nothing bright is reflected, there is no glint.

With every switch at 0, the surface is drawn by the pipelines it always was, built from the source with the
cinematic blocks removed, and the pass is not split.

## Consequences

- A scene that does not ask for the path is byte-identical. The GPU test holds the live renderer to a renderer
  given the stripped shader, with every shaping setting off its default.
- `WaterUniforms` grows to 320 bytes, and the dynamic-offset stride to 512.
- Screen-space reflection cannot reflect what is not on screen: the canopy above the frame and the sky behind the
  camera fall back to the environment cube. The riverbank tints that fallback dark (the forest's colour), which is
  an artistic approximation. A traced reflection against scene geometry would remove it and is not built.
- The emitter sprites of the ecosystem are drawn after the scene pass (ADR-1200), so they are not in the copy. Their
  reflection is still ADR-1281's mirror. The two systems are complementary: geometry through SSR, emitters through
  the mirror.
- Cost is offline-only in intent. Per water pixel it is `reflectionRays` times `ssrSteps` depth reads. It is not
  measured this phase (spec 07: performance is out of scope).
