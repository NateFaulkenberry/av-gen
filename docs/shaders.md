# Shaders

Decision: ADR-006. Research: `docs/research/shaders.md` and the WGSL re-routing in
`docs/research/architecture-options.md` §12.

## Now (milestone 0.1)

- Language: WGSL. Files under `shaders/`, loaded at runtime by `gpu::ShaderLibrary` (search
  order in `docs/build.md`). Edit a shader and restart; hot reload is milestone 0.4.
- `#include "file.wgsl"` at the start of a line is expanded textually (depth limit 8, cycles are
  caught by the depth limit). Included blocks are delimited with comments so Dawn's line numbers
  stay meaningful.
- Diagnostics: compile errors return an `Error` with `file:line:col: error: message`; warnings are
  logged. `SceneRenderer::init` fails (and the app exits with a message) on any shader error.
- Files: `common.wgsl` (uniform structs incl. lights, vertex stage, env rotation, hash noise),
  `pbr.wgsl` (glTF metallic-roughness: GGX + height-correlated Smith + Schlick, punctual lights,
  split-sum IBL, derivative-based normal mapping, occlusion, emissive, alpha mask/blend, unlit),
  `grid.wgsl` (procedural anti-aliased grid, additive), `skybox.wgsl` (far-plane fullscreen
  triangle sampling the prefiltered cube), `environment.wgsl` (IBL preprocessing passes:
  equirect→cube, irradiance, GGX prefilter, BRDF LUT), `tonemap.wgsl` (ACES fitted, sRGB).

## Binding contract

| Group | Binding | Stage | Content |
|---|---|---|---|
| 0 | 0 | vertex+fragment | `FrameUniforms` (uniform) |
| 1 | 0 | vertex+fragment | `ObjectUniforms` (uniform, dynamic offset) |
| 2 | 0..5 | fragment | material sampler; baseColor, metallicRoughness, normal, emissive, occlusion `texture_2d<f32>` |
| 3 | 0..3 | fragment | IBL sampler; irradiance `texture_cube`, prefiltered `texture_cube`, BRDF LUT `texture_2d` |
| tonemap 0 | 0 / 1 | fragment | HDR `texture_2d<f32>` (unfilterable, `textureLoad`) / `TonemapUniforms` |
| env 0 | 0..3 | fragment | `EnvUniforms` (dynamic offset); sampler; source equirect `texture_2d`; source `texture_cube` |

Vertex inputs: `@location(0) position vec3`, `@location(1) normal vec3`, `@location(2) uv vec2`.

## Later

- 0.4: file watcher → recompile off-thread → pipeline swap at frame boundary, keep last-good
  pipeline, magenta error shader fallback; user-droppable ISF-style shaders (JSON header with
  typed INPUTS mapped to `Parameter`s, PASSES with persistence); GLSL bodies via Tint's SPIR-V
  reader (from-source Dawn), naga, or Slang → WGSL, chosen after prototyping.
- Reflection via Tint's inspector or the ISF header; parameters bind by name.
- Compute kernels in WGSL for particles (0.5); `@workgroup_size` lives in the shader.
