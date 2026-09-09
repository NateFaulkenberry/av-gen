# Procedural materials (user guide and shader spec)

Procedural materials (ADR-030) replace a material's fixed base colour, metallic, roughness,
emission and opacity with a small *program*: up to 16 ops over 8 vec4 registers, evaluated per
fragment by `shaders/material.wgsl` and, identically, on the CPU by
`scene::evaluateMaterialProgram` (`src/scene/material_program.hpp`). Programs are data: JSON in
the scene file, hot-editable uniforms on the GPU, deterministic everywhere.

```
inputs (position, normal, uv, ids, time, audio, fields)
      → ops write registers r0..r7 in order
      → outputs read registers: baseColor, metallic, roughness, emission, opacity
```

This document is the reference both the CPU implementation (`src/scene/material_program.cpp`,
`src/core/color.cpp`) and the WGSL transliteration follow. Every formula below is exact.

## The register model

- 8 registers `r0..r7`, each a `vec4<f32>`; all start at **zero** for every fragment.
- Ops run in array order. Each op reads `a = reg[srcA]`, `b = reg[srcB]`, `c = reg[srcC]`,
  its constants `k = constant`, `k2 = constant2`, `k3 = constant3`, `k4 = constant4`, the float
  `f = value`, and writes `reg[dst]`. A register may be both a source and the destination.
- Disabled ops (`"enabled": false`) are skipped entirely (they are not packed for the GPU).
- Outputs name a register or `-1` (keep the material's own value):

| Output | Formula |
|---|---|
| `baseColor` | `max(reg.rgb, 0)` (values above 1 are allowed) |
| `metallic` | `clamp(reg.x, 0, 1)` |
| `roughness` | `clamp(reg.x, 0, 1)` |
| `emission` | `reg.rgb * emissionIntensity` (not clamped) |
| `opacity` | `clamp(reg.x, 0, 1)` |

`saturate(x)` below means `clamp(x, 0, 1)`; `mix(a, b, t)` means `a * (1 - t) + b * t`
(component-wise, `t` not clamped); `step(e, x)` is `1` when `x >= e`, else `0`.

## Inputs

`{"kind": "input", "input": "<name>", "dst": r}` loads one of:

| Name | vec4 layout | Notes |
|---|---|---|
| `worldPosition` | `(x, y, z, 1)` | fragment position, world space |
| `localPosition` | `(x, y, z, 1)` | object space (before the deformer stack) |
| `normal` | `(x, y, z, 0)` | unit world-space normal |
| `uv` | `(u, v, 0, 0)` | |
| `objectId` | `(id, id, id, id)` | scalar, broadcast |
| `instanceIndex` | `(i, i, i, i)` | normalised instance index in [0, 1], broadcast |
| `instanceId` | `(id, id, id, id)` | broadcast |
| `instanceRandom` | `(r0, r1, r2, r3)` | the instance record's four randoms |
| `instanceColor` | `(r, g, b, a)` | per-instance colour multiplier |
| `instanceEmissive` | `(r, g, b, a)` | per-instance emissive multiplier |
| `time` | `(t, t, t, t)` | seconds, broadcast |
| `audio` | `(rms, bass, mid, treble)` | |
| `audioBands` | `(lowMid, highMid, centroid, flux)` | |
| `beatPhase` | `(phase, pulse, onset, bar)` | |
| `viewDirection` | `(x, y, z, 0)` | unit vector from the fragment to the camera |
| `depth` | `(d, d, d, d)` | view depth, broadcast |

## Op reference

| Kind (JSON) | Reads | Writes `reg[dst]` |
|---|---|---|
| `input` | – | the input above |
| `constant` | – | `k` |
| `gradient` | a | `vec4(saturate(dot(a.xyz, k.xyz) * f + k.w))` |
| `noise` | a | `vec4(fbm3(a.xyz * f + k.xyz, seed))` — in [0, 1] |
| `voronoi` | a | `vec4(voronoiF1(a.xyz * f + k.xyz, seed))` — in [0, ~1.4] |
| `fresnel` | N, V | `vec4(pow(1 - saturate(dot(N, V)), f))` — the fragment's normal and view direction |
| `ramp` | a | 3-stop linear ramp of `t = saturate(a.x)`: `t < 0.5 ? mix(k, k2, t * 2) : mix(k2, k3, (t - 0.5) * 2)` (all four components) |
| `remap` | a | `u = (a - k.x) / (k.y - k.x)`; `out = u * (k.w - k.z) + k.z`; when `f > 0.5` clamp to `[min(k.z, k.w), max(k.z, k.w)]`; `k.y == k.x` gives `u = 0` (out = `k.z`) |
| `multiply` | a, b | `a * b` |
| `add` | a, b | `a + b` |
| `mix` | a, b | `mix(a, b, f)` |
| `mixBy` | a, b, c | `mix(a, b, c.x)` |
| `power` | a | `pow(max(a, 0), f)` per component |
| `smoothstep` | a | `smoothstep(k.x, k.y, a)` per component: `t = saturate((a - k.x) / (k.y - k.x)); t * t * (3 - 2 t)`; `k.x == k.y` degenerates to `step(k.x, a)` |
| `threshold` | a | `step(f, a)` per component |
| `hueShift` | a, b | `rgb = hueShift(a.rgb, f + b.x)` (OKLCH, turns), `w = a.w` |
| `saturate` | a | `rgb = saturate(a.rgb, f)` (OKLab chroma × f), `w = a.w` |
| `palette` | a | `rgb = k.xyz + k2.xyz * cos(2π (k3.xyz * (a.x + f) + k4.xyz))`, `w = 1` |
| `field` | – | `fieldColor(fieldSlot, worldPosition)`; scalar fields broadcast to all four components, vector fields `(x, y, z, 0)`; unknown slot (`-1`) → zeros |

Notes for the shader transliteration:

- `fbm3` and `voronoiF1` are the functions in `core/noise.hpp` / `procedural.wgsl` (3-octave
  value-noise fBM normalised to [0, 1); Worley F1 with per-cell jitter from `hash01`).
- `noise`/`voronoi` ignore `k.w`; `gradient` ignores the fourth component of `a` (only `a.xyz`
  enters the dot product, so a position's `w = 1` never leaks in).
- `gradient` doubles as "extract one lane and broadcast": `k = (1, 0, 0, 0)`, `f = 1` copies
  `a.x` into all four components (clamped to [0, 1]). Ops are component-wise, so scaling a colour
  by a scalar needs the scalar in every lane (scalar inputs and `noise`/`voronoi`/`fresnel`/
  `gradient` results already are).
- `hueShift` adds `b.x` to the turn count. Point `srcB` at an unwritten (zero) register when
  the shift is a constant; there is no "no source" sentinel.
- `power` uses `pow(max(a, 0), f)`; `pow(0, 0)` must be 1 on the GPU (the CPU's `std::pow`
  returns 1) — guard it explicitly in WGSL.
- All the colour ops call the functions in the "Colour utilities" section with the exact
  matrices and clamping shown there.

### Program data

```json
{ "name": "alienMetal",
  "ops": [ { "kind": "input", "input": "worldPosition" }, … ],
  "baseColor": 2, "metallic": -1, "roughness": 4, "emission": 7, "emissionIntensity": 3.0, "opacity": -1 }
```

Each op writes `"kind"` and only its non-default members: `enabled` (true), `dst`/`srcA`/
`srcB`/`srcC` (0), `value` (1), `constant`…`constant4` (`[0,0,0,0]`), `seed` (1), `input`
(`worldPosition`), `field` (""). `validate()` rejects more than 16 ops, register indices outside
0..7, output registers outside -1..7 and `field` ops without a name; `fromJson` validates.
`structuralHash()` covers every member (name, every op member, outputs, emission intensity).

### GPU packing (`MaterialProgramGpu`)

| Offset | Field | Contents |
|---|---|---|
| 0 | `outputs` (ivec4) | baseColor, metallic, roughness, emission registers |
| 16 | `opacityCountPad` (ivec4) | opacity register, op count, 0, 0 |
| 32 | `emissionIntensityPad` (vec4) | emissionIntensity, 0, 0, 0 |
| 48 | `ops[16]` | 112 bytes each |

Per op: `kind` (u32, enum order of `MaterialOpKind`), `input` (u32, enum order of
`MaterialInput`), `seed` (u32), `fieldSlot` (i32, -1 when unresolved or not a field op),
`registers` (ivec4 `dst, srcA, srcB, srcC`), `valuePad` (`value, 0, 0, 0`), `constant`,
`constant2`, `constant3`, `constant4`. Disabled ops are dropped and the rest packed
contiguously; slots past the count are zero. `packMaterialProgram(program, fieldSlotOf)`
resolves field names to slots through a callback.

## Example programs

The three below ship as a copy-and-paste library in `examples/materials/`
(`alien-metal.material.json`, `emissive-glass.material.json`, `bioluminescent.material.json`):
each file is one `MaterialProgram` document, ready to drop into a scene file's
`"materialPrograms"` array and name from a material's `"program"`. `examples/machine`
does exactly that with `alienMetal` on its ribs.

### Alien metal

Dark base, Voronoi cells drive roughness, Fresnel rim glows.

```json
{ "name": "alienMetal",
  "ops": [
    { "kind": "input",    "input": "worldPosition", "dst": 0 },
    { "kind": "constant", "dst": 1, "constant": [0.04, 0.05, 0.07, 1] },
    { "kind": "voronoi",  "dst": 2, "srcA": 0, "value": 3.0, "seed": 9 },
    { "kind": "remap",    "dst": 3, "srcA": 2, "constant": [0, 0.8, 0.15, 0.7], "value": 1 },
    { "kind": "constant", "dst": 4, "constant": [1, 1, 1, 1] },
    { "kind": "fresnel",  "dst": 5, "value": 4.0 },
    { "kind": "constant", "dst": 6, "constant": [0.2, 0.9, 0.6, 1] },
    { "kind": "multiply", "dst": 7, "srcA": 5, "srcB": 6 } ],
  "baseColor": 1, "metallic": 4, "roughness": 3, "emission": 7, "emissionIntensity": 2.5, "opacity": -1 }
```

### Emissive glass

Translucent tinted glass whose edges light up; opacity follows the Fresnel term.

```json
{ "name": "emissiveGlass",
  "ops": [
    { "kind": "constant", "dst": 0, "constant": [0.6, 0.85, 1.0, 1] },
    { "kind": "fresnel",  "dst": 1, "value": 2.0 },
    { "kind": "remap",    "dst": 2, "srcA": 1, "constant": [0, 1, 0.15, 0.95], "value": 1 },
    { "kind": "multiply", "dst": 3, "srcA": 0, "srcB": 1 },
    { "kind": "constant", "dst": 4, "constant": [0.05, 0, 0, 0] },
    { "kind": "constant", "dst": 5, "constant": [0, 0, 0, 0] } ],
  "baseColor": 0, "metallic": 5, "roughness": 4, "emission": 3, "emissionIntensity": 1.5, "opacity": 2 }
```

### Bioluminescent

Noise picks a hue from a cosine palette; the audio RMS (`audio.x`) scales the glow so the
surface pulses with the music.

```json
{ "name": "bioluminescent",
  "ops": [
    { "kind": "input",    "input": "worldPosition", "dst": 0 },
    { "kind": "input",    "input": "time",          "dst": 1 },
    { "kind": "input",    "input": "audio",         "dst": 2 },
    { "kind": "constant", "dst": 3, "constant": [0, 0, 0.15, 0] },
    { "kind": "multiply", "dst": 3, "srcA": 3, "srcB": 1 },
    { "kind": "add",      "dst": 3, "srcA": 0, "srcB": 3 },
    { "kind": "noise",    "dst": 4, "srcA": 3, "value": 2.0, "seed": 4 },
    { "kind": "smoothstep", "dst": 5, "srcA": 4, "constant": [0.35, 0.7, 0, 0] },
    { "kind": "palette",  "dst": 6, "srcA": 4, "value": 0.1,
      "constant": [0.2, 0.4, 0.5, 0], "constant2": [0.2, 0.4, 0.5, 0],
      "constant3": [1, 1, 0.5, 0], "constant4": [0, 0.15, 0.2, 0] },
    { "kind": "multiply", "dst": 6, "srcA": 6, "srcB": 5 },
    { "kind": "gradient", "dst": 7, "srcA": 2, "constant": [1, 0, 0, 0.2], "value": 1.6 },
    { "kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7 },
    { "kind": "constant", "dst": 1, "constant": [0.02, 0.03, 0.05, 1] } ],
  "baseColor": 1, "metallic": -1, "roughness": -1, "emission": 6, "emissionIntensity": 4.0, "opacity": -1 }
```

(The drifting position `p + (0, 0, 0.15 t)` is built by scaling a constant by `time` and adding
it to the position; the palette's `t` is the noise value plus a 0.1 offset. The `gradient` op on
the audio register computes `saturate(dot(audio.xyz, (1, 0, 0)) * 1.6 + 0.2)` — the RMS alone,
broadcast to all four lanes — so the multiply scales every colour channel by the same factor. A
`remap` of the audio register would have scaled r by rms, g by bass and b by mid. Keep the
palette's `b` term no larger than its `a` term: emission is not clamped, and the palette can
otherwise go negative.)

## On the GPU

`shaders/material.wgsl` is the transliteration of `scene::evaluateMaterialProgram`, and
`shaders/color.wgsl` of `src/core/color.cpp`. `tests/rendering/test_material_gpu.cpp` runs both
sides over the same contexts through a compute harness and compares the five outputs within 1e-4
(1e-3 where a `noise`, `voronoi` or noisy field op is involved).

### Buffers and bindings

Everything lives in the **material bind group (group 2)**, which every shading pass already binds
per material, so the procedural and SDF renderers need no plumbing of their own:

| Binding | Contents |
|---|---|
| 0..5 | sampler + the five glTF textures (unchanged) |
| 6 | `MaterialProgramBlock`: `count` + `array<MaterialProgramGpu, 8>` (14,736 bytes, uniform) |
| 7 | `MaterialSelect`: `program: i32` — a 16-byte slice naming the slot this material runs |
| 8 | `FieldBlock` — the entity pass only; `procedural.wgsl` and `sdf_raymarch.wgsl` bind their own at group 1 binding 3 |

`rendering::MaterialPrograms` (`src/rendering/material_programs.{hpp,cpp}`, the twin of
`field_uniforms.*`) packs `Scene::materialPrograms` into binding 6 every frame — programs are
hot-editable parameters, so the block is re-uploaded unconditionally, like the field block.
`slotOf(name)` is the name → slot map. **Slot i is `Scene::materialPrograms[i]`**; at most
`kMaxGpuMaterialPrograms` = **8** programs reach the GPU, and the rest warn once and resolve to -1
(their materials shade with their own values).

Binding 7 is the trick that keeps the per-object uniforms alone: the select buffer holds one
16-byte region per possible slot value (-1, then 0..7) at 256-byte (dynamic-offset-aligned) stride,
written once at construction. `SceneRenderer::materialBindGroup` resolves
`Material::program → slot` and binds that region, and the program slot is part of the bind-group
cache key. A material naming no program (or an unknown one) binds the `-1` region and the shader
skips the interpreter entirely.

Field references resolve through the *frame's* `FieldUniforms`: `MaterialPrograms::update` is
called immediately after `FieldUniforms::update`, and `packMaterialProgram` rewrites every `field`
op's name into that frame's slot (`fieldSlot`, -1 when the field is missing, disabled or past the
16-field GPU limit). A -1 slot samples as zeros.

### Where the program runs

`shadePbrInstanced` in `shaders/pbr_shade.wgsl` runs it **first**, before any texture fetch or
lighting, and its outputs replace `object.baseColor.rgb`, `object.baseColor.a` (opacity),
`object.material.x/y` (roughness/metallic) and `object.emissive.rgb * object.emissive.w`
(emission, which becomes a finished radiance with an intensity lane of 1). The per-instance
colour/emissive multipliers and the material textures then apply to the program's result, and the
alpha-mask test sees the program's opacity. With `materialSelect.program < 0` the whole block is
skipped and the shader is byte-for-byte the pre-ADR-030 one — the golden example hashes in
`tests/rendering/test_procedural_examples_gpu.cpp` are the guard.

The three passes differ only in the context they hand it (`MaterialInstanceInfo`):

| Pass | `localPosition` | instance lanes | `objectId` |
|---|---|---|---|
| `pbr.wgsl` (entities) | the vertex's object-space position (`VertexOut.localPos`) | none: index/id 0, random 0, colour/emissive 1 | `object.ids.x` = the entity's index in `Scene::entities` |
| `procedural.wgsl` (instances) | the source position **before** the deformer stack | the `InstanceRecord`: `scale.w` (normalised index), `color.a` (id), `random`, `color`, `emissive` | the object's index in `Scene::procedurals` |
| `sdf_raymarch.wgsl` (surfaces) | the local-space hit point | none | the object's index in `Scene::sdfs` |

`shadePbr` fills the rest from what it already has: `worldPosition`, `normal` (front-facing
corrected, **before** normal mapping), `uv`, `viewDirection` (fragment → camera), `depth`
(distance to the camera), `time` (`frame.params.x`) and `audio` / `audioBands` / `beatPhase`, which
are new `FrameUniforms` lanes filled from the render's `AnalysisFrame` (rms, bands 0/2/4 as
bass/mid/treble; bands 1/3 plus the spectral centroid and flux; beat phase, `1 - phase` as the
pulse, onset strength and a 4/4 bar phase). Without an analysis frame they are zero.

### Limits

- 8 programs per scene on the GPU, 16 ops each, 8 registers — the CPU limits (`kMaxMaterialOps`,
  `kMaterialRegisters`) with the program count added.
- 16 fields on the GPU (`spatial::kMaxGpuFields`), so a `field` op past the sixteenth reads zero.
- No textures as material inputs and no normal-map generation yet (ADR-030 "Consequences").
- The interpreter runs **per fragment**: see docs/performance/procedural-geometry.md
  ("Procedural materials") for what each op costs.

### Adding an op

Both sides move together; the enum order *is* the wire format.

1. `src/scene/material_program.hpp`: add the kind to the end of `MaterialOpKind` (appending keeps
   every packed program valid) and document its formula in the header comment.
2. `src/scene/material_program.cpp`: add its name to `kOpKindNames` and its case to `evaluateOp`.
   Nothing else changes — packing, JSON, validation and hashing are generic over the members.
3. `shaders/material.wgsl`: add the matching `MAT_OP_*` constant with the same ordinal and the
   matching branch in `evaluateMaterialProgram`. Watch the WGSL gotchas: `saturate` and `std` are
   taken, there is no ternary (`select(f, t, cond)`, and its arguments are both evaluated), no
   recursion, and `pow(0, 0)` is implementation-defined (`matPow1` guards it).
4. `docs/procedural-materials.md`: a row in the op reference table above.
5. Tests: a CPU case in `tests/unit/test_material_program.cpp` and a parity program in
   `tests/rendering/test_material_gpu.cpp` ("material program ops match the CPU interpreter").
   The parity probe writes r7 and points every output at it, so the op's `xyz` is compared
   unclamped through the emission lane.

A new **input** is the same shape: `MaterialInput` + `kInputNames` + `inputValue` on the CPU,
`MAT_IN_*` + `materialInputValue` in WGSL, and — if the value is not already in `MaterialContext` —
a lane in `MaterialInstanceInfo` (per-object) or `FrameUniforms` (per-frame), filled by each of the
three passes.

## Colour utilities (`core/color.hpp`, `shaders/color.wgsl`)

All RGB values are **linear**. Hue is in **turns**, wrapped with `h - floor(h)` to [0, 1).

### Conversions

- **HSV** (`rgbToHsv` → `(h, s, v)`): `M = max(r, g, b)`, `m = min(r, g, b)`, `C = M - m`;
  sector `h6 = 0` when `C = 0`, else `((g - b) / C) mod 6` when `M = r`,
  `(b - r) / C + 2` when `M = g`, `(r - g) / C + 4` when `M = b`; `h = fract(h6 / 6)`;
  `s = M > 0 ? C / M : 0`; `v = M`.
  `hsvToRgb`: `C = v s`, `h6 = fract(h) * 6`, `X = C (1 - |h6 mod 2 - 1|)`, `m = v - C`, then
  `(C, X, 0)`, `(X, C, 0)`, `(0, C, X)`, `(0, X, C)`, `(X, 0, C)`, `(C, 0, X)` for sectors
  0..5, plus `m` on every channel.
- **HSL** (`rgbToHsl` → `(h, s, l)`): same `h`; `l = (M + m) / 2`;
  `s = C / (1 - |2l - 1|)` when both `C > 0` and the denominator is `> 0`, else 0.
  `hslToRgb`: `C = (1 - |2l - 1|) s`, `m = l - C / 2`, same sector table.
- **OKLab** (Björn Ottosson): from linear sRGB
  ```
  l = 0.4122214708 r + 0.5363325363 g + 0.0514459929 b
  m = 0.2119034982 r + 0.6806995451 g + 0.1073969566 b
  s = 0.0883024619 r + 0.2817188376 g + 0.6299787005 b
  l' = cbrt(l), m' = cbrt(m), s' = cbrt(s)          (sign-preserving cube root)
  L = 0.2104542553 l' + 0.7936177850 m' - 0.0040720468 s'
  a = 1.9779984951 l' - 2.4285922050 m' + 0.4505937099 s'
  b = 0.0259040371 l' + 0.7827717662 m' - 0.8086757660 s'
  ```
  and back
  ```
  l' = L + 0.3963377774 a + 0.2158037573 b
  m' = L - 0.1055613458 a - 0.0638541728 b
  s' = L - 0.0894841775 a - 1.2914855480 b
  l = l'^3, m = m'^3, s = s'^3
  r =  4.0767416621 l - 3.3077115913 m + 0.2309699292 s
  g = -1.2684380046 l + 2.6097574011 m - 0.3413193965 s
  b = -0.0041960863 l - 0.7034186147 m + 1.7076147010 s
  ```
  White is `(1, 0, 0)`, black `(0, 0, 0)`. In WGSL use `sign(x) * pow(abs(x), 1.0 / 3.0)` for
  the cube root.
- **OKLCH**: `C = sqrt(a² + b²)`, `h = fract(atan2(b, a) / 2π)`; when `|a| <= 1e-8` and
  `|b| <= 1e-8` the hue is 0. Back: `a = C cos(2π h)`, `b = C sin(2π h)`.
- **sRGB**: `srgbToLinear(c) = c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055)^2.4`;
  `linearToSrgb(c) = c <= 0.0031308 ? 12.92 c : 1.055 c^(1/2.4) - 0.055` (per channel; the
  linear segment also covers negative values).
- **luminance** = `0.2126 r + 0.7152 g + 0.0722 b`.

### Manipulation (results clamped to `>= 0` component-wise)

| Function | Definition |
|---|---|
| `hueShift(rgb, turns)` | OKLCH `h = fract(h + turns)`, back to RGB, `max(·, 0)` |
| `hueShiftHsv(rgb, turns)` | HSV `h = fract(h + turns)`, back to RGB, `max(·, 0)` |
| `saturate(rgb, factor)` | OKLab `(L, a·factor, b·factor)`, back, `max(·, 0)` |
| `lighten(rgb, amount)` | OKLab `L += amount`, back, `max(·, 0)` |
| `contrast(rgb, factor, pivot = 0.5)` | `max((rgb - pivot) * factor + pivot, 0)` |
| `mixOklab(a, b, t)` | `oklabToRgb(mix(rgbToOklab(a), rgbToOklab(b), t))`, `max(·, 0)` |

A one-turn `hueShift` is the identity, half a turn is the perceptual complement, `saturate(·, 0)`
is the neutral grey of the same OKLab lightness.

### Cosine palette (Quilez)

`CosinePalette{a, b, c, d}.sample(t) = a + b * cos(2π (c t + d))` per channel, not clamped.
Defaults `a = b = 0.5`, `c = 1`, `d = (0, 0.33, 0.67)` give the familiar rainbow with period 1.

### Ramp

`Ramp` holds up to 8 `(position, colour)` stops with ascending positions in [0, 1].
`sample(t, offset)`:

1. `t += offset`.
2. Cyclic: `t = fract(t)`; otherwise `t = clamp(t, 0, 1)`.
3. Between two stops `s0 <= t <= s1`: `u = (t - s0.position) / (s1.position - s0.position)`
   (0 when the span is 0) and `blend(s0.colour, s1.colour, u)`, where `blend` is `mixOklab`
   when `perceptual`, plain linear `mix` otherwise.
4. Outside the stops: non-cyclic ramps hold the first/last colour; cyclic ramps interpolate
   across the wrap from the last stop to the first with
   `span = (1 - last.position) + first.position`,
   `local = t >= last.position ? t - last.position : t + (1 - last.position)`,
   `u = span > 0 ? local / span : 1`, `blend(last.colour, first.colour, u)`.
5. No stops → black; one stop → its colour.
