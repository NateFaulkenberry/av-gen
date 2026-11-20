# ADR-2110: Images referenced by URI are shared across assets by their resolved file

- Status: Accepted (2026-11-20), avagent/neon-dreams (Neon Dreams, iteration 8)
- Code: `src/assets/gltf_loader.cpp` (external images no longer loaded by the parser; `TextureData::source`),
  `src/scene/scene_types.hpp` (`TextureData::source`), `src/scene/asset_parts.{hpp,cpp}` (`TextureSharing`,
  `remapEntityTextures`, `remapMaterialTextures`), `src/scene/composition.cpp` (mesh assets and glTF nodes go through
  one `TextureSharing` per flatten).
- Tests: `[adr2110]` in `avgen_tests` (`test_texture_sharing.cpp`); `[gltf]`, `[assets]`, `[composition]`.

## Context

The Neon Dreams kit's 38 GLBs reference one shared surface atlas (`meshes/tex/nd_surface_{base,normal,orm}.png`) by
URI. The loader caches textures per file, and the flatten appended every asset's textures at an offset, so the atlas
was decoded and uploaded once per GLB: 214 scene textures where 87 are distinct.

## Decision

- fastgltf no longer loads external images (`Options::LoadExternalImages` dropped): an image by URI keeps its URI,
  `decodeImage` reads the file (its existing URI branch) and records the resolved path (`weakly_canonical`) in
  `TextureData::source`. Embedded and data-URI images have no source.
- The flatten gives each asset a texture table instead of an offset. `TextureSharing::add` appends an asset's textures,
  reusing the id of any texture with the same source and format already in the scene (an sRGB base colour and a linear
  use of one file stay two textures). Meshes keep their offsets.
- `AVGEN_TEXTURE_SHARING=off` is the measurement arm (every asset appends its own copies, as before).

## Consequences

Measured on Neon Dreams at 35.75 s (`--live-profile --mode headless`, 1080p, Ultra; two runs per arm, identical
numbers both times; captured frames bit-identical):

| | off (before) | on |
|---|---|---|
| scene textures after the flatten | 214 | 87 |
| GPU textures (Dawn's count) | 283 | 156 |
| GPU texture memory | 2,000 MB | 401 MB |
| first frame | 2.30 s | 0.66 s |
| flatten | 2.74 s | 2.39 s |

CPU memory is not reduced: each asset in the registry still holds its decoded copy (a shared decode cache is the next
step if that matters). Assets that embed their images in the GLB are not shared, by design.
