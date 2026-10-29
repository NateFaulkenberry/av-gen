# ADR-1819: Imported glTF glass refracts (KHR_materials_transmission, volume, ior)

- Status: Accepted (eng/render-borrow, 2026-10-29). Filament evaluation borrow item A4, approved by the owner.
- Builds on ADR-1603 (screen-space refraction through the shared refraction copy).
- Code:
  - `src/assets/gltf_loader.{hpp,cpp}`: the parser reads the three extensions; `GltfLoadOptions::importTransmission`
    (on by default) maps them onto `Material::refraction`.
  - `src/scene/scene_types.hpp` (`Refraction::transmission`), `src/rendering/optics_pack.hpp` (`refraction1.w`).
  - `shaders/refraction.wgsl` (new): ADR-1603's refraction shading, moved out of `procedural.wgsl` unchanged into
    `refractSurface`, plus the glass mode. `procedural.wgsl` and `pbr.wgsl` include it.
  - `shaders/pbr.wgsl`: `entityShade` (fs_main's body), `fs_main_refract`. `shaders/pbr_shade.wgsl`:
    `override LIT_TRANSMISSION` (false everywhere but the entity refraction pipelines).
  - `src/rendering/scene_renderer.{hpp,cpp}`: the entities' refraction lanes, `ensureEntityRefractPipelines`, the
    refractive list, the phase and the draws after the copy.
- Tests: `tests/unit/test_gltf_glass.cpp`, `tests/rendering/test_gltf_glass_gpu.cpp` (`[adr1819]`); the fixture
  `tools/make_glass_test_glb.py` (built into `build/*/test-fixtures/glass/` by CMake; nothing downloaded or committed).

## Problem

AV Gen ignored `KHR_materials_transmission` and its companions (the importer warned "advanced KHR_materials_*
extensions ignored", and the parser did not even read them). An imported glass drew as an opaque, often white,
material: the Filament evaluation's S3 lab showed its glass sphere and slab as solid, where Filament refracts them.
AV Gen already had screen-space refraction, but only for procedural surfaces (ADR-1603).

## Decision

- **The importer maps the extensions onto ADR-1603's refraction** (`GltfLoadOptions::importTransmission`, on):
  - strength = (ior - 1) x 0.08 (0.04 for ior 1.5): the screen offset per unit of view-space normal;
  - thickness = the volume's `thicknessFactor` (0, a thin wall, absorbs nothing);
  - absorption = -ln(attenuationColor) / attenuationDistance per channel (Beer-Lambert);
  - the transmission factor is kept. A transmission texture is not read (warned).
- **Glass mode** (transmission > 0): glTF's transmission replaces the diffuse, so the pipeline that draws an imported
  glass compiles with `LIT_TRANSMISSION`, which scales the diffuse by 1 - transmission. The surface then shows its
  specular reflection plus what it transmits, tinted by its base colour. ADR-1603's opacity mix stays for every other
  refractive surface. `LIT_TRANSMISSION` is false (and its code compiled out) in every other pipeline, so no other
  shading changes.
- **Refractive entities draw as ADR-1603's procedurals do**: out of the depth prepass and the opaque pass, after the one
  refraction copy (shared with water and procedural refraction), their depth laid first and then their colour, through
  the frame group that binds the copy. Static opaque meshes only; a skinned or alpha-blended glass draws as it did.
- **The fallback is the old picture**: where the ladder turns refraction off (Low, Emergency) or the pipelines are not
  ready, an imported glass draws as the opaque material it imported as before this ADR, byte for byte (tested).
- **Dispersion** stays a procedural-material feature: glTF has no field for it here, and the importer sets none.
- **An exporter's BLEND fallback is a glass**: a transmissive material exported with `alphaMode: BLEND` (Blender and
  others write that for viewers without the extension) imports as an opaque glass that refracts, as Filament's
  importer treats it; with the importer flag off it is the blended material it was.
- **Specialised like its neighbours**: the refraction pipelines carry the scene's lit-feature mask (ADR-1815). The
  general shader on the glass's pixels cost +1.38 ms on the S3 lab; specialised, +0.62.

## Consequences

- The fixture (a glass sphere, transmission 1, ior 1.5, a blue-green volume, in front of red and white stripes): the
  stripes show through the sphere, bent and tinted (red-green spread 49.7 inside the sphere, against 0.9 for the opaque
  import); with refraction off the frame equals the opaque import's hash.
- **A model without the extensions is unchanged**: the fixture's `plain.glb`, and the five reference scenes (offline,
  960x540, with ADR-1818 off so only this ADR differs, against main's capture): Aurora, Event Horizon, Rainbow World
  and Talking Heads SAME, Rift within 1 level (its run-to-run variation).
- **The S3 lab** (Filament's reflection lab: a glass sphere and slab, transmission 0.95, ior 1.5, exported BLEND): on
  main they drew as the exporter's pale 25%-alpha fallback; now they are clear glass that refracts the floor and the
  wall behind them. Review: `~/Desktop/av-gen-review/103-render-improvements/a4-gltf-glass/`
  (`s3-reflect-lab-BEFORE.png` / `-AFTER.png`, `s3-reflect-lab-sphere-crop-before-after.png`,
  `s3-reflect-lab-slab-crop-before-after.png`, `fixture-glass-sphere-BEFORE-opaque.png` / `-AFTER-refracting.png`).
- **Cost: about 0.6 ms on S3, marginally over the evaluation's 0.5 ms bar.** `--ab norefraction` (1080p, interleaved):
  +0.62 ms (3 pairs, 7.5% floor) and +0.64 ms (5 pairs, at load average 43). It is the refraction phase itself: the copy
  of the frame, the split of the scene pass (its five targets stored and reloaded on a tile GPU), the depth lay-down
  and the refraction reads; ADR-1603's procedural refraction measured 1.2-2.5 ms for the same mechanism. A scene with
  no refractive entity pays nothing (no phase).
- **Known limitations**:
  - where the ladder turns refraction off (Low, Emergency), a BLEND-exported glass now falls back to the opaque material
    rather than to its blended fallback (`s3-reflect-lab-refraction-OFF-fallback.png`). Restoring the blend there means
    reclassifying entities between the opaque and blended lists by tier; offered as a follow-up;
  - a camera that frames the scene's bounds can move slightly when a blended glass becomes an opaque one (the S3 frames
    show it); the cause was not investigated;
  - skinned and alpha-blended (non-transmissive) glass, and transmission textures, are not refracted.
