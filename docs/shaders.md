# Shaders

Decisions: ADR-006 (engine shaders), ADR-014 (user shader contract). Research:
`docs/research/shaders.md` and the WGSL re-routing in `docs/research/architecture-options.md` §12.

## Engine shaders

- Language: WGSL. Files under `shaders/`, loaded at runtime by `gpu::ShaderLibrary` (search order
  in `docs/build.md`). `#include "file.wgsl"` at the start of a line is expanded textually.
- Files: `common.wgsl` (uniform structs incl. lights, vertex stage, env rotation, hash noise),
  `pbr_shade.wgsl` (the shared fragment shading `shadePbr(...)`: glTF metallic-roughness with GGX
  + height-correlated Smith + Schlick, punctual lights, split-sum IBL, derivative-based normal
  mapping, occlusion, emissive, alpha mask/blend, unlit, distance fog; declares the material and
  IBL bindings), `pbr.wgsl` (entity pipeline: common vertex stage + `shadePbr`), `procedural.wgsl`
  (ADR-023: instanced vertex stage reading `InstanceRecord`s from storage, the deformer stack —
  bend, twist, sine, noise, displacement — finite-difference normals, `pcg3d` value-noise fBM
  that `scene::fbm3` mirrors on the CPU; fragment = `shadePbr` with per-instance colour/emissive
  multipliers), `grid.wgsl`, `skybox.wgsl`, `environment.wgsl` (IBL preprocessing passes),
  `tonemap.wgsl` (ACES fitted, sRGB).
- **Hot reload (0.4):** the application watches these files (polling, 0.5 s) and calls
  `SceneRenderer::reloadEngineShaders()`. Each pipeline is rebuilt from its module; a shader that
  fails to compile keeps its previous pipeline and the error is shown in the Control window.
- Diagnostics: compile errors return an `Error` with `file:line:col: error: message`.

### Binding contract (engine passes)

| Group | Binding | Stage | Content |
|---|---|---|---|
| 0 | 0 | vertex+fragment | `FrameUniforms` (uniform) |
| 1 | 0 | vertex+fragment | `ObjectUniforms` (uniform, dynamic offset) |
| 2 | 0..5 | fragment | material sampler; baseColor, metallicRoughness, normal, emissive, occlusion `texture_2d<f32>` |
| 3 | 0..3 | fragment | IBL sampler; irradiance `texture_cube`, prefiltered `texture_cube`, BRDF LUT `texture_2d` |
| procedural 1 | 0 / 1 / 2 | vertex+fragment / vertex / vertex | `ObjectUniforms` (uniform, dynamic offset, 256-byte slots); `array<InstanceRecord>` (read-only storage, 96 B each); `ProceduralUniforms` (uniform, 528 B: timeInfo + 8 x 64-byte `DeformerUniform`) — groups 0, 2, 3 as above |
| tonemap 0 | 0 / 1 | fragment | HDR `texture_2d<f32>` (unfilterable, `textureLoad`) / `TonemapUniforms` |
| env 0 | 0..3 | fragment | `EnvUniforms` (dynamic offset); sampler; source equirect `texture_2d`; source `texture_cube` |

## User shaders (milestone 0.4)

A user shader is a `.wgsl` file that starts with an ISF-style JSON header in a block comment and
defines `fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32>`. The engine generates
the rest. Load one with `--shader file.wgsl` (background), `--post file.wgsl` (post effect), the
File menu, the Modulation window's Shaders tab, or by dropping the file on the window.

```wgsl
/*{
  "DESCRIPTION": "Plasma",
  "INPUTS": [
    {"NAME": "speed", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 10.0, "LABEL": "Speed"},
    {"NAME": "tint",  "TYPE": "color", "DEFAULT": [1.0, 0.5, 0.2, 1.0]},
    {"NAME": "center","TYPE": "point2D", "DEFAULT": [0.5, 0.5]},
    {"NAME": "invert","TYPE": "bool", "DEFAULT": false},
    {"NAME": "steps", "TYPE": "long", "DEFAULT": 4, "MIN": 1, "MAX": 16},
    {"NAME": "flash", "TYPE": "event"}
  ],
  "PASSES": [
    {"TARGET": "trail", "PERSISTENT": true, "FLOAT": true, "WIDTH": "$WIDTH/2", "HEIGHT": "$HEIGHT/2"},
    {}
  ]
}*/
fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> {
    let previous = textureSample(trail, linearSampler, uv);
    if (sys.passIndex < 0.5) { return vec4<f32>(previous.rgb * 0.95 + inputs.tint.rgb * sys.audio.y, 1.0); }
    return previous;
}
```

What the engine provides to the body:

| Name | Type | Meaning |
|---|---|---|
| `sys.time`, `sys.timeDelta`, `sys.frameIndex`, `sys.passIndex` | f32 | render time (audio position while playing), delta, frame, pass |
| `sys.renderSize`, `sys.passSize` | vec2 | layer output size, current pass target size |
| `sys.audio` | vec4 | rms, bass, mid, treble (0..1) |
| `sys.audio2` | vec4 | lowMid, highMid, onset strength, beat phase |
| `sys.beat` | vec4 | bpm, beat count, bar phase, track progress |
| `inputs.<NAME>` | per INPUT type | float→`f32`, long→`i32`, bool→`u32`, color→`vec4`, point2D→`vec2`, event→`f32` |
| `linearSampler` | sampler | clamp, linear |
| `inputImage` | texture_2d | Post stage: the HDR scene image; else 1x1 black |
| `audioSpectrum` | texture_2d | binCount x 1: r = log spectrum 0..1, g = linear magnitude |
| `<TARGET>` | texture_2d | one per named pass target; PERSISTENT targets hold last frame's result |

Rules: identifiers are WGSL identifiers and may not shadow generated names (`sys`, `inputs`,
`mainImage`, …); `std` is a WGSL reserved word. Passes run in order; a persistent target is
double-buffered so a pass may sample its own previous frame. The last pass must have no
`TARGET` (one is appended with a warning if missing). `uv` runs 0..1 with (0,0) top-left.

**Inputs are parameters** at `shader/<layer>/<input>`: they appear in the Parameters window,
can be modulated by any route (e.g. `audio.onset -> shader/plasma/flash`), are saved in projects
and presets, and keep their values across hot reloads.

**Stages.** Background layers draw full-screen inside the scene pass before geometry (depth
write off, so geometry occludes them). Post layers read the HDR scene and write a ping-pong HDR
target before tone mapping; several post layers chain in list order.

**Hot reload.** The file is polled every 0.25 s; on change it is re-parsed and recompiled. Parse
or compile errors keep the previous pipelines (or show a magenta stripe pattern when there were
none) and the message appears in the Shaders tab with Tint's line numbers; `bodyLineOffset` and
the generated prologue length map them back to the file.

**Examples:** `shaders/examples/plasma.wgsl` (single pass, audio-reactive) and
`shaders/examples/feedback.wgsl` (persistent half-resolution trail, beat-driven).

## Later

- GLSL/ISF bodies via a translation front end (Tint's SPIR-V reader with a from-source Dawn, or
  Slang → WGSL); Shadertoy wrapper; `image`/`audio` ISF input types.
- Compute passes in user shaders; user vertex/material shaders; reflection via Tint's inspector.
- Post layers become nodes of the 0.6 post-processing graph (bloom, DoF, grading).
