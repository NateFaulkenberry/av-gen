# ADR-005: Asset format

- Status: Accepted (2026-09-08)
- Research: `docs/research/assets.md`

## Problem

Choose the canonical 3D asset format and the loading libraries so that milestone 0.2 can import
meshes, materials, lights, cameras and animations, while milestone 0.1 needs no external assets.

## Alternatives considered

1. glTF 2.0 (.glb) canonical; loaders fastgltf, cgltf, tinygltf.
2. FBX via ufbx or Assimp.
3. OBJ via tinyobjloader.
4. Assimp as a universal importer.

## Decision

- **glTF 2.0 is the canonical scene/mesh format.** Other formats are imported by converting to
  glTF-equivalent in-memory structures, never as first-class scene formats.
- **Milestone 0.1 ships no asset loaders.** Geometry is procedural (a primitive mesh and a grid),
  generated in glTF's coordinate conventions (right-handed, +Y up, metres) so that imported assets
  later match.
- Milestone 0.2 adds fastgltf (MIT) as the loader, meshoptimizer (MIT) for vertex cache/overdraw
  optimisation and EXT_meshopt decoding, stb_image (public domain) for PNG/JPEG/HDR, tinyexr
  (BSD-3) for EXR, libktx (Apache-2.0) for KTX2/Basis GPU textures, and ozz-animation (MIT) for
  skeletal animation.
- Asset references in project files are project-relative paths plus a stable GUID; the asset
  registry hands out generational handles.

## Rationale

- glTF 2.0 is the open, modern, PBR-native format with ratified extensions for emissive strength,
  texture transforms, punctual lights, transmission, KTX2 textures and meshopt compression, and
  every DCC tool exports it.
- fastgltf is the fastest maintained C++ loader with a permissive licence; cgltf is the fallback.
- Deferring loaders to 0.2 keeps 0.1 small without constraining it: the mesh and material structs
  are designed around glTF's vertex attributes and metallic-roughness material model now.

## Consequences

- FBX is an import path via ufbx in 0.3 or later, not a runtime format.
- Assimp is avoided (large, frequent security releases).
- HDR environment maps and IBL preprocessing use Filament's `cmgen` output format in 0.2.

## Rejected alternatives

- Assimp: heavy, frequent CVEs, brings its own scene model.
- tinygltf: v3 moved to C11 with an API break; fastgltf is faster and C++.
- OBJ as canonical: no materials beyond MTL, no animation, no hierarchy.

## Revisit triggers

- fastgltf abandoning C++17/20 compatibility; cgltf is the fallback.
