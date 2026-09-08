# Assets

Decision: ADR-005. Research: `docs/research/assets.md`.

## Milestone 0.2

`src/assets/` (part of `avgen_core`, GPU-free):

- `assets::loadImage` / `loadImageFromMemory` (stb_image): 8-bit PNG/JPEG/TGA/BMP/PSD → RGBA8
  tagged sRGB or linear by the caller; Radiance `.hdr` → RGBA32Float. `writePng`, `encodePng`,
  `writeHdr` (stb_image_write) serve captures and test fixtures.
- `assets::loadGltf` (fastgltf 0.9): `.gltf`/`.glb` with external buffers and images resolved
  relative to the file. Imports every triangle primitive as a `MeshData` (positions, normals or
  generated smooth normals, `TEXCOORD_0`, 32-bit indices), one `Entity` per primitive with a
  flattened world transform, metallic-roughness materials with all five texture slots
  (`KHR_materials_emissive_strength`, `KHR_texture_transform` parsed and warned), samplers → wrap
  and filter modes, `KHR_lights_punctual` (directional/point/spot with world position and
  direction), perspective cameras. Everything is built into a local scene and merged on success,
  so a failing file never disturbs the current scene. Unsupported features (animations, skins,
  morph targets, Draco, KTX2/WebP, non-triangle topologies, orthographic cameras, extra UV sets)
  are reported as warnings, not errors.

`scene::GltfScene` (a `SceneController`) wraps a loaded file: frames it with an orbit camera from
its bounds, adds a key light if the file has none, and exposes the curated parameter surface
described in `docs/architecture.md` §4. Non-uniform scale with rotation is decomposed by
`Transform::fromMatrix` (shear discarded).

Runtime assets: `shaders/*.wgsl` (see `docs/shaders.md`), the audio file, the glTF scene and the
`.hdr` environment map chosen by the user (`--scene`, `--env`, the File menu, or file drop).
Nothing binary is committed; `tools/make_test_audio.py` generates the test track and the tests
build GLB and image fixtures in memory. Khronos sample assets (DamagedHelmet, MetalRoughSpheres,
BoxTextured) and Poly Haven HDRIs were used for manual verification.

## Measured (Apple M2 Max, Debug)

| Asset | Result |
|---|---|
| DamagedHelmet.glb (3.8 MB, five 2048² PNGs) | 1 entity, 5 textures, 835 ms (PNG decode dominated; ~4x faster in Release) |
| MetalRoughSpheres.glb (11 MB) | 5 entities, 2 textures, 176 ms |
| BoxTextured.glb | 3.4 ms |
| studio_small_09_1k.hdr (1024x512) | environment processed in 127 ms |

## Later

meshoptimizer (vertex cache, LOD, `EXT_meshopt_compression`), KTX2/Basis via libktx, EXR via
tinyexr, ozz-animation for skins and animations, an asset registry with GUID + path references
and async loading with placeholders, hot reload via efsw.
