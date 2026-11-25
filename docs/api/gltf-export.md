# The glTF export contract

What a model must be for AV Gen to load it, and what the engine does with each part of a glTF 2.0 file. Written
from the loader's behaviour (ADR-2160); an art tool that exports to this contract gets a model that loads with no
warnings. `asset.import` loads the model before it succeeds and reports what it found (`import`: meshes,
entities, materials, textures, lights, cameras, rigs, joints, clips, triangles, bounds, `warnings`,
`entityNames`, `materialNames`, `rigDetail`, `clipDetail`), so the import summary is the check: **an export is
right when `import.warnings` is empty and the counts are what you made.**

## Files

- **`.glb`** (one binary file) or **`.gltf`** with its `.bin` buffers and image files beside it. `asset.import`
  copies a `.gltf`'s dependencies into the project with it (`copied` lists them). Buffers may be embedded
  (`.glb`, data URIs) or external files; external images are read from the file path relative to the `.gltf`.
- **Images**: PNG or JPEG (decoded by stb_image), embedded in the `.glb` or as files. KTX2/Basis, WebP and DDS
  are not supported (the texture is skipped with a warning). HDR images are refused as material textures. A
  URI with a byte offset is not supported.
- **Required extensions.** A file whose `extensionsRequired` lists an extension outside the supported list
  below **does not load at all**: the parser refuses it and `asset.import` fails. Draco compression is the common
  case (exporters mark it required).

## Coordinates and units

- **Y-up, right-handed, metres**, as glTF defines them. The loader applies no axis or unit conversion: 1 unit
  is 1 metre in the world, and `scene.create_node` reports the node's world `bounds` in metres. Blender's glTF
  exporter writes this with its default `+Y Up` option and the scene's unit scale at 1.0.
- Node transforms (TRS or a matrix) are composed down the hierarchy and baked into each mesh's world transform
  inside the asset. The asset is then placed as one node (`position`, `rotationDegrees`, uniform `scale`).
- The scene used is the file's default scene (`scene`), else scene 0; a file with no scenes uses every node that
  is nobody's child. Hierarchies deeper than 256 are cut with a warning.

## Supported

| Feature | What the engine does |
|---|---|
| Meshes | Triangle lists only (`mode` 4). `POSITION` (required), `NORMAL` (generated, smooth, when absent or malformed), `TEXCOORD_0`, indices (generated when absent). Other attributes (`TANGENT`, `COLOR_n`, `TEXCOORD_1+`) are not read. A primitive with another topology, no positions or out-of-range indices is skipped with a warning. |
| PBR metallic-roughness | `baseColorFactor` (alpha is opacity), `metallicFactor`, `roughnessFactor`, and the base colour, metallic-roughness, normal (with `scale`), occlusion (with `strength`) and emissive textures. Texture sampler wrap modes are kept; a `NEAREST` mag filter is kept. Textures must use `TEXCOORD_0`. |
| Alpha | `alphaMode` OPAQUE, MASK (with `alphaCutoff`) and BLEND; `doubleSided`. |
| Emission | `emissiveFactor` and the emissive texture, scaled by `KHR_materials_emissive_strength`. A material with a zero emissive factor and no emissive texture emits nothing whatever its strength. |
| Glass | `KHR_materials_transmission` (factor; the transmission texture is ignored with a warning), `KHR_materials_ior`, `KHR_materials_volume` (thickness, attenuation colour and distance). |
| Skins | One rig per skin, up to 256 joints. **Four influences per vertex**: `JOINTS_0` and `WEIGHTS_0` (weights are renormalised); `JOINTS_1`/`WEIGHTS_1` and beyond are dropped with a warning. `inverseBindMatrices` are read (identity, with a warning, when malformed). The rig includes the joints' ancestors, so an armature node above the skeleton can carry root motion. |
| Animation | **Skinned animation only**: translation, rotation and scale channels whose target is a joint of a skin (or an ancestor of one), with STEP, LINEAR and CUBICSPLINE interpolation. Each glTF animation becomes a clip on every rig it drives (`clipDetail` names them). |
| Lights | `KHR_lights_punctual` point, spot and directional lights become scene lights named `<node>/<light>`, placed with the node. Intensity, colour, range and cone angles are copied as the file states them; the loader converts no photometric units. |
| Cameras | Perspective cameras are read and counted; orthographic cameras are ignored with a warning. Use `camera.set` to frame a shot. |
| Other extensions | `KHR_texture_transform` is accepted but ignored (warning per texture). `KHR_mesh_quantization` is accepted by the parser. |

## Dropped

| Feature | What happens |
|---|---|
| Morph targets (shape keys) | Ignored with a warning (`morph targets ignored`); their animation channels are dropped. |
| Node (object) animation | A channel whose target is not a joint of any skin is dropped; a clip left with no channel is dropped with a warning, and a file with animations but no skin warns that they are ignored. Bake object motion into an armature, or animate the node through the API (keyframes, modulation). |
| Draco (`KHR_draco_mesh_compression`) | Not supported. Required (as exporters write it): the file does not load. |
| GPU instancing (`EXT_mesh_gpu_instancing`) | Not supported. Required: the file does not load; optional: the instancing attributes are ignored and the mesh draws once at its node. |
| Unlit (`KHR_materials_unlit`) | Not enabled: an unlit material is shaded as PBR (silently), and a file that requires it does not load. |
| Other material extensions | Clearcoat, sheen, specular, iridescence, anisotropy and the rest are not parsed: silently ignored, and refused when required. |
| Non-triangle primitives | Points, lines and strips are skipped with a warning. |

## Names are addresses

- **Node names** become entity names (`import.entityNames`; unnamed nodes are `node<index>`, and a mesh with several
  primitives gives `<node>/prim<n>`). Mesh names are not used.
- **Material names** are kept on every entity (`import.materialNames`; unnamed materials are
  `material<index>`). They are how the scene document addresses part of an asset: for example a glTF node's
  `waterMaterials` maps a material name to a water surface, and a name that matches nothing is reported with the
  names that would have. **Give every material a stable, unique name** in the art tool; renaming it breaks every
  reference to it.
- **Animation names** become clip names (`clipDetail`; unnamed animations are `animation<index>`), and skin names
  rig names (`rigDetail`).

## Checklist for an exporter

1. Triangulated meshes with normals and one UV set; no Draco; PNG or JPEG textures.
2. Y-up, metres, transforms applied or in the hierarchy (either works). A skinned mesh node's own transform is
   ignored, as glTF specifies: place a character through its joints' hierarchy.
3. Named materials, named nodes.
4. Emission through `emissiveFactor` (non-zero) and `KHR_materials_emissive_strength` for values above 1.
5. Characters: one skin, at most 256 joints, at most four weights per vertex; animation on the armature only.
6. Import it with `asset.import` and read `import.warnings`: empty means the engine took everything you exported.
