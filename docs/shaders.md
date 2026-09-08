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
- Files: `common.wgsl` (uniform structs, vertex stage, hash noise), `mesh.wgsl` (Lambert + GGX
  specular + hemispheric ambient + fresnel rim + emissive), `grid.wgsl` (procedural anti-aliased
  grid with radial fade, additive), `tonemap.wgsl` (fullscreen triangle, ACES fitted, sRGB).

## Binding contract

| Group | Binding | Stage | Content |
|---|---|---|---|
| 0 | 0 | vertex+fragment | `FrameUniforms` (uniform) |
| 1 | 0 | vertex+fragment | `ObjectUniforms` (uniform, dynamic offset) |
| tonemap 0 | 0 / 1 | fragment | HDR `texture_2d<f32>` (unfilterable, `textureLoad`) / `TonemapUniforms` |

Vertex inputs: `@location(0) position vec3`, `@location(1) normal vec3`, `@location(2) uv vec2`.

## Later

- 0.4: file watcher → recompile off-thread → pipeline swap at frame boundary, keep last-good
  pipeline, magenta error shader fallback; user-droppable ISF-style shaders (JSON header with
  typed INPUTS mapped to `Parameter`s, PASSES with persistence); GLSL bodies via Tint's SPIR-V
  reader (from-source Dawn), naga, or Slang → WGSL, chosen after prototyping.
- Reflection via Tint's inspector or the ISF header; parameters bind by name.
- Compute kernels in WGSL for particles (0.5); `@workgroup_size` lives in the shader.
