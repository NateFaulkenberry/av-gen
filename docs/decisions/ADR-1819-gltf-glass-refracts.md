# ADR-1819: Imported glTF glass refracts (KHR_materials_transmission, volume, ior)

- Status: Accepted (eng/render-borrow, 2026-10-29). Filament evaluation borrow item A4, approved by the owner.
- Builds on ADR-1603 (screen-space refraction through the shared refraction copy).
- Code:
  - `src/assets/gltf_loader.{hpp,cpp}`: the parser reads the three extensions; `GltfLoadOptions::importTransmission`
    (on by default) maps them onto `Material::refraction`.
  - `src/scene/scene_types.hpp` (`Refraction::transmission`, `ior`), `src/rendering/optics_pack.hpp` (`refraction1.w`;
    a glass's ior in `refraction0.y`).
  - `shaders/refraction.wgsl` (new): ADR-1603's refraction shading, moved out of `procedural.wgsl` unchanged into
    `refractSurface`, and `refractGlass`, the glass mode. `procedural.wgsl` and `pbr.wgsl` include it.
  - `shaders/pbr.wgsl`: `entityShade` (fs_main's body), `fs_main_refract`. `shaders/pbr_shade.wgsl`:
    `override LIT_TRANSMISSION` (false everywhere but the entity refraction pipelines).
  - `src/rendering/scene_renderer.{hpp,cpp}`: the entities' refraction lanes, `ensureEntityRefractPipelines`, the
    refractive list, the phase and the draws after the copy.
- Tests: `tests/unit/test_gltf_glass.cpp`, `tests/rendering/test_gltf_glass_gpu.cpp` (`[adr1819]`); the fixture
  `tools/make_glass_test_glb.py` (glass, plain, glass-blend, glass-thin; built into `build/*/test-fixtures/glass/` by CMake; nothing downloaded or committed).

## Problem

AV Gen ignored `KHR_materials_transmission` and its companions (the importer warned "advanced KHR_materials_*
extensions ignored", and the parser did not even read them). An imported glass drew as an opaque, often white,
material: the Filament evaluation's S3 lab showed its glass sphere and slab as solid, where Filament refracts them.
AV Gen already had screen-space refraction, but only for procedural surfaces (ADR-1603).

## Decision

- **The importer maps the extensions as glTF defines them** (`GltfLoadOptions::importTransmission`, on):
  - transmission = the factor (a transmission texture is not read; warned), ior = `KHR_materials_ior` (1.5 default);
  - **no `KHR_materials_volume` (or `thicknessFactor` 0) is thin-walled**: the view passes straight through, unbent.
    The spec says so, and Filament's solid-sphere model moves nothing at thickness 0 either;
  - with a volume, the view refracts in at the surface by the ior, travels `thicknessFactor` (scene units, the node's
    scale not applied) along the inward normal, and the exit point is projected and read (Filament's
    `refractionSolidSphere`); absorption = -ln(attenuationColor) / attenuationDistance per channel over that path.
  - The alpha mode is left as exported (see the BLEND fallback below).
- **Glass mode** (`refractGlass`, `shaders/refraction.wgsl`): glTF's transmission replaces the diffuse, so the pipeline
  that draws an imported glass compiles with `LIT_TRANSMISSION`, which scales the diffuse by 1 - transmission. The
  surface shows its specular reflection plus what it transmits x (1 - Fresnel at the ior's F0) x its base colour.
  `LIT_TRANSMISSION` is false (and compiled out) in every other pipeline. ADR-1603's `refractSurface` is back to its
  pre-ADR-1819 body: the procedural and water refraction are unchanged.
- **The silhouette is covered, not stepped.** A tessellated sphere's interpolated normals turn away from the eye a
  facet before its triangles end: on the S3 sphere 2,127 pixels (a ~2 px band) shaded with N.V < 0, where the Fresnel
  is 1 and nothing is transmitted, so the first cut drew a dark rim stepped along the facet edges. The smooth surface
  those normals describe ends where N.V = 0; the pixel's share of it is N.V over its screen gradient (+0.5), and the
  rest of the pixel is what lies behind (the copy, unbent). A flat face, or the middle of a curve, has coverage 1.
- **Refractive entities draw as ADR-1603's procedurals do**: out of the depth prepass and the opaque pass, after the one
  refraction copy (shared with water and procedural refraction), their depth laid first and then their colour, through
  the frame group that binds the copy. Static meshes only; a skinned glass draws as it did.
- **The fallback is the old picture**: where the ladder turns refraction off (Low, Emergency) or the pipelines failed:
  - an OPAQUE-exported glass draws as the opaque material it imported as before this ADR, byte for byte (tested);
  - **a BLEND-exported glass keeps its exported blend.** Blender and others write `alphaMode: BLEND` (alpha 0.25 in S3)
    as the fallback for viewers without the extension. `SceneRenderer` decides per frame: with refraction it stages
    the glass as opaque (alpha 1, no blend dither in its depth) and lists it with the opaque entities, so it reaches
    the refraction phase; without, it stays in the blended list exactly as on main (tested: hash-equal to the
    blended import).
- **Dispersion** stays a procedural-material feature: glTF has no field for it here, and the importer sets none.
- **Specialised like its neighbours**: the refraction pipelines carry the scene's lit-feature mask (ADR-1815). The
  general shader on the glass's pixels cost +1.38 ms on the S3 lab; specialised, +0.62.

## Investigation (the coordinator's review of the first cut)

1. **The emissive arch vanished behind the glass.** The arch is an opaque entity, drawn before the copy, and it is in
   the copy: drawing the glass as the raw copy showed it whole. It vanished because the first cut bent every glass by
   ADR-1603's screen offset ((ior - 1) x 0.08 = 0.04 of the screen along the normal), which on the S3 sphere magnified
   about its centre about 2.1x across and 1.4x down, pushing the arch's image out of the sphere. S3's glass has no
   volume, so in glTF it is thin-walled and must not bend at all; now it does not, and the arch shows through. Water
   and procedural refraction read the same copy, taken after the opaque world, the sky and the atmosphere, so they see
   the arch too. **What none of the three can see** is anything drawn after the copy: particles, ribbons, shells,
   blended meshes, the wire and grid, and the Environment renderers. A glass (now writing depth) also hides those
   when they are behind it, where the blended fallback drew them over it. That is the usual screen-space limitation;
   left as it is.
2. **The dark, stepped rim**: the facet-normal band above. Not the refraction read and not a depth mismatch.
3. **Low and Emergency**: the BLEND fallback above.
4. **The "framing shift"** reported with the first cut was a misreading. S3's camera is authored (no auto-framing),
   and outside the glass, its shadows and its reflections S3 is pixel-identical before and after (the pillars, ring
   and banks: 0 pixels over 2 levels). The fixture's frames are pixel-identical outside the sphere and its shadow.
   Scene bounds do not depend on the alpha mode, and the reference scenes have no glass: unchanged (below).

## Consequences

- The fixture: a solid glass sphere (transmission 1, ior 1.5, a 1 m blue-green volume) in front of red and white
  stripes shows them bent and tinted (red-green spread 49.6 inside the sphere, against 0.9 for the opaque import);
  `glass-thin.glb` (no volume) shows them where they are (a mean difference of 2.2 levels from the bare stripes,
  against 39.5 for the solid sphere); refraction off equals the opaque import's hash; the BLEND glass with refraction
  off equals the blended import's hash.
- **A model without the extensions is unchanged**: the fixture's `plain.glb`, and the five reference scenes (offline,
  960x540, ADR-1818 off, against main's capture): Aurora, Event Horizon, Rainbow World and Talking Heads SAME, Rift
  within 1 level (its run-to-run variation).
- **The S3 lab** (a glass sphere and slab, transmission 0.95, ior 1.5, exported BLEND, no volume): on main the
  exporter's pale 25%-alpha fallback; now clear thin-walled glass, the arch and the wall seen through it, the
  silhouette soft. Review: `~/Desktop/av-gen-review/103-render-improvements/a4-gltf-glass/` and its `fixes/` folder.
- **Cost.** `--ab norefraction` at 1080p, interleaved, 5 pairs each. The arm is now the glass's own Low fallback
  (S3's glass is BLEND-exported, so with refraction off it draws as the blend): +0.07 ms (inside a 4.2% floor) and
  +0.07 ms (inside an 11.4% floor) -- under the evaluation's 0.5 ms bar. Against an opaque glass with no phase at all
  (the first cut's arm) the phase measured +0.62 and +0.64 ms: the copy of the frame, the split of the scene pass, the
  depth lay-down and the reads (ADR-1603's procedural refraction measured 1.2-2.5 ms for the same mechanism). A scene
  with no refractive entity pays nothing (no phase).
- **Known limitations**: skinned glass and transmission textures are not refracted; a volume's thickness ignores the
  node's scale; things drawn after the copy are not seen through any refraction (above).
