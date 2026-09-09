# Fields on the GPU

How `Scene::fields` (ADR-025) reach the GPU and who samples them: the procedural renderer
(effector pass, Field deformer, emissive field, Point billboards), the particle renderer (field
forces) and, later, material programs. The CPU side (`spatial::sampleScalar/Vector/Color`,
`applyEffectorsToRecords`) and the GPU side (`shaders/fields.wgsl`, `points.wgsl`) implement the
same maths in the same operation order; `tests/rendering/test_fields_gpu.cpp` and
`test_points_gpu.cpp` hold them to 1e-4 (noise kinds 1e-3).

## Files

| File | Role |
|---|---|
| `shaders/noise.wgsl` | `pcg3d`, `hash01`, `valueNoise`, `fbm3`, `fbm3Vec`, `curlNoise` (central differences, eps 0.01), `voronoiF1` — the transliteration of `core/noise.hpp`. Included once per module through `fields.wgsl`. |
| `shaders/fields.wgsl` | `FieldGpu`, `FieldBlock`, `fieldWeight/fieldScalar/fieldVector/fieldColor(i, p)`, `fieldTypeOf(i)`. |
| `shaders/points.wgsl` | The effector compute pass (`cs_effectors`). |
| `shaders/procedural.wgsl` | Field deformer (kind 5), emissive field, Point billboard. Includes `fields.wgsl`. |
| `shaders/particles.wgsl` | Field forces in `cs_simulate`. Includes `fields.wgsl`. |
| `src/rendering/field_uniforms.hpp/.cpp` | `FieldUniforms`: packs `Scene::fields` into the `FieldBlock` uniform every frame; name → slot map. |
| `src/rendering/procedural_renderer.*` | Effector pass, live record buffers, deformer/field uniforms, Point mesh, stats, `readInstanceRecords` (tests). |
| `src/rendering/particle_renderer.*` | `ParticleUniforms::fieldForces`, field block at compute binding 8. |

## The FieldBlock

`FieldUniforms` (owned by `SceneRenderer`, created before the particle and procedural renderers
so their bind groups can reference its buffer) re-packs the scene's fields every frame with
`spatial::packField(field, time, &set)` — fields animate (`tau = speed * t + phase`, waves travel
with `t`), so there is nothing to cache.

```
struct FieldBlock {            // 5136 bytes, uniform
    count: u32, pad x3,
    fields: array<FieldGpu, 16>,   // 320 bytes each, spatial::FieldGpu
};
```

`FieldGpu` (320 bytes, all members 16-byte aligned, see `spatial/field.hpp`): `kind`, `type`,
`falloffKind`, `seed` (u32), `worldToLocal` (mat4), `localToWorldRow0..2` (rotation-only rows, scale
sign kept, used to rotate vectors back to world), `strengthInnerOuterTau`, `axisRadius`,
`pointLength`, `sizeSoftness`, `freqExpInvertBias`, `wave0` (amplitude, wavelength, speed, width),
`wave1` (origin, geometry, shape, t), `colorA`, `colorB`, `curve`, `noiseCombineMix`, `children`
(ivec4 slots, -1 = none).

Slot rules:

- slot `i` is `Scene::fields.fields[i]` for `i < 16`, so the child slots `packField` resolves through
  `FieldSet::indexOf` are correct;
- a disabled field keeps its slot but is packed as a zero-strength scalar constant (every sample
  reads 0) and `FieldUniforms::slotOf(name)` returns -1 for it, so effectors, deformers and forces
  referencing it are skipped;
- fields beyond the 16th are not uploaded (`slotOf` -1, one warning);
- `FieldSpace::Local` is not interpreted on the GPU: the packed transform always applies and the
  caller decides what `p` is (every current consumer samples at a world position).

Sampling with an invalid slot (`i < 0` or `i >= count`) returns 0.

## Bindings

| Consumer | Group / binding | Stages |
|---|---|---|
| procedural draw | group 1 binding 3 (`fieldBlock`) | vertex + fragment |
| effector pass (`points.wgsl`) | group 0: 0 `PointsParams`, 1 base records (read), 2 live records (read_write), 3 `fieldBlock` | compute |
| particle simulate | group 0 binding 8 (`fieldBlock`) | compute |
| parity test harness | group 0 binding 0 | compute |

`fields.wgsl` references a module-scope `fieldBlock: FieldBlock` that the including module declares
at whatever group/binding it uses (WGSL allows any declaration order). A renderer constructed
without a `SceneRenderer` creates its own zeroed block (count 0).

## Sampling semantics (both sides)

`q = worldToLocal * p`; `d` = falloff distance of the kind (gradient / plane / direction kinds:
`|dot(q, n)|`; box: `max(maxcomp(|q| - size), 0)`; everything else `|q - point|`);
`w = strength * falloffWeight(d)`. Scalar kinds return `shape(q) * w` (invert: `1 - shape`), vector
kinds `rotateToWorld(direction(q) * w)` (invert: `-direction`), colour kinds `(colour(q), w)`
(invert swaps A/B). Cross-type reads: scalar as vector = `s * nWorld`; vector as scalar =
`length(v)`; colour as scalar = `luminance * a`; scalar as colour = `(mix(A, B, saturate(s)), w)`;
vector as colour = `(v * 0.5 + 0.5, w)`. The falloff curves, wave shapes and per-kind formulas are
listed in the header comment of `shaders/fields.wgsl` and in `spatial/field.hpp`.

`NoiseModulated` falloff samples `fbm3(p * noiseScale, seed)` at the **world** sample position.

### Compound fields: one level

WGSL has no recursion, so `fieldScalar/Vector/Color` on a Compound slot combine its (up to four)
children with `basicScalar/Vector/Color` — the non-compound evaluation — and multiply by the
compound's own weight. A child that is itself a Compound evaluates as 0 on the GPU (the CPU
recurses to depth 8). Combine: 0 Add (sum), 1 Multiply (product), 2 Max, 3 Min, 4 Mix
(`mix(c0, c1, mix)` over the first two children), 5 Average; no valid children → 0.

## The effector pass

Objects with at least one *usable* effector (enabled, op not Velocity/Attribute, field bound to a
slot) get a `live` record buffer (same 96-byte `InstanceRecord` layout as the base buffer) and a
compute bind group. `ProceduralRenderer::update` encodes one compute pass per frame (`cs_effectors`,
64 threads per workgroup, one thread per record, one dispatch per such object) **before** the
scene pass; the draw then binds the live buffer (`groupLive`) instead of the base buffer. Objects
without effectors draw straight from the base buffer with no pass, so the existing showcase scenes
are unaffected (their frames are bit-identical to the previous renderer).

`PointsParams` (528 bytes): `objectToWorld`, `worldToObjectRotation` (transpose of the
rotation-only part — normalised columns — of `objectToWorld`), `info` (record count, effector
count) and eight `EffectorGpu` records (`op`, `blend`, `fieldSlot`, `strength`, `axisWeight`,
`scaleAxisPad`; 48 bytes, `spatial::EffectorGpu`).

Per record, in effector order (`pw = objectToWorld * position`; `s = fieldScalar(slot, pw)`):

| Op | Effect |
|---|---|
| PositionOffset | `position += R⁻¹ (v * strength)`, `v = fieldVector` for vector fields, `fieldScalar * effector.axis` for scalar / colour fields |
| Scale | `target = scale * (1 + s * strength * scaleAxis)` (Replace: `s * strength * scaleAxis`); `scale = blend(scale, target)` |
| Rotation | `rotation = axisAngle(normalize(v) or effector.axis, s * strength) * rotation` (zero axis: skipped) |
| Color | `color.rgb = mix(color.rgb, c.rgb, c.a * strength)` |
| Emission | `emissive.rgb *= 1 + s * strength` (Replace: `= s * strength`) |
| Density | `position.w *= s * strength` (Replace: `= s * strength`) |
| Velocity, Attribute | ignored (no lanes in a record) |

Blend: Add `a + b`, Multiply `a * b`, Replace `b`, Min, Max, Mix `mix(a, b, weight)`.

Timing: the pass carries both timestamps of a private `gpu::GpuTimer`
(`ProceduralStats::effectorPassMs`; -1 when no pass ran or timestamps are unavailable). The frame
timer starts at the scene pass, so total GPU time = `gpuFrameMs + effectorPassMs`.
`ProceduralRenderer::readInstanceRecords(name)` reads the live (or base) buffer back for tests.

## Field deformer (DeformerKind::Field, code 5)

`packDeformer` writes the resolved slot in `params.x` and `alongNormal` in `params.y`; an unbound
name disables the slot. The shader always samples at the vertex's **current world position**
(`object.model * instance(p)` for local deformers, `p` for world ones) and displaces in the
deformer's space: vector fields `p += v * amount` where, for local deformers, `v` is rotated into
object space through the inverse of the object's linear part (transpose of its normal matrix), the
conjugate instance quaternion and the inverse instance scale; scalar fields `p += n * s * amount`
(`alongNormal`) or `p += axis * s * amount`. Colour fields read as scalars (luminance × alpha).

## Emissive field

`ProceduralUniforms::fieldInfo` = (emissive slot or -1, amount, point flag, 0). The fragment
multiplies the per-instance emissive by `1 + amount * fieldScalar(slot, worldPos)` when the slot
is valid (`ProceduralGeometry::emissiveField` / `emissiveFieldAmount`; amount 0 = off).

## Point billboards

A `PrimitiveKind::Point` source is the quad from `scene::makePointQuad(pointSize)` (4 vertices in XY,
normal +Z); the renderer creates it directly for Point kinds. With `fieldInfo.z = 1` the vertex
shader skips the finite-difference normal and the local deformers: the centre is
`object.model * instance.position` run through the **world** deformers (Field deformers
included), the quad is `centre + cameraRight * x * scale.x + cameraUp * y * scale.y` and the normal
points at the camera. `FrameUniforms` gained `cameraRight` / `cameraUp` (the first two columns of
the inverse view). Point objects always use the two-sided pipeline. The instance scale multiplies
the quad; the object matrix's scale does not.

## Particle field forces

`ParticleUniforms` carries `fieldInfo.x` = count and four `(mode, slot, strength, mix)` /
`(axis, 0)` pairs (enabled entries bound to a slot, in order). In `cs_simulate`, after the built-in
forces were integrated (`velocity += force * dt`) and before drag:

| Mode | Effect |
|---|---|
| Force (0), Turbulence (2) | `velocity += fv * strength * dt` |
| Velocity (1) | `velocity = mix(velocity, fv * strength, mix)` |
| Kill (3) | `fieldScalar(slot, position) >= 0.5` → the particle dies this step |

`fv = fieldVector(slot, position)` for vector fields; scalar and colour fields act along the
force's `axis` (`fieldScalar * axis`). No atomics: the stable compaction and draw order stay
deterministic (the particle tests render two fresh renderers and a second context to identical
frames).

## Adding a field kind

1. `spatial/field.hpp`: append to `FieldKind` (keep the scalar / vector / colour blocks contiguous
   or update `fieldTypeOf`), name tables, JSON, hash. The C++ enum order is the GPU code.
2. CPU: the shape in `sampleScalar` (`scalarShape`), `sampleVector` (`vectorDirection`) or
   `sampleColor` (`colorShape`), plus its falloff distance if it is not `|q - point|`. Reuse the
   existing `FieldGpu` lanes for parameters; extending the record means updating both structs and
   the 320-byte static_assert / WGSL layout.
3. `shaders/fields.wgsl`: add the `FIELD_*` constant and the same expression, in the same order of
   operations, to `scalarShape` / `vectorDirection` / `colorShape` (and `fieldDistance` if needed).
   No `?:` (use `select`), no recursion, `type`/`target`/`std` are reserved words.
4. `tests/rendering/test_fields_gpu.cpp` iterates every `FieldKind` below `Compound` automatically;
   set `isNoiseKind` if the kind is noise-based (1e-3 tolerance) and give `makeField` any new
   parameter a non-trivial value.

## The parity test

`test_fields_gpu.cpp` compiles `fields.wgsl` + a small kernel (`loadSource` resolves the includes,
`ShaderLibrary::compile` builds the module), uploads a `FieldUniforms` block and a list of
`(position, slot)` samples, and reads back `(scalar, weight)`, `vector` and `colour` per sample.
Cases: every kind with a rotated/scaled frame and smoothstep falloff; every `FalloffKind` on a
Radial field; wave geometries/shapes and inverted scalar/vector/colour fields; compounds with every
combine, a missing child and an empty compound; invalid slots; disabled fields and `slotOf`.
`test_points_gpu.cpp` compares the live record buffer with `spatial::applyEffectorsToRecords`
for a vortex offset + radial scale pair and for every effector op, renders 200k Point instances
deterministically across two renderers and a second context, checks the Field deformer and
emissive field change the image deterministically, and runs particle field forces (Force,
Velocity, Kill) against the no-force sequence. The `[.perf][fields]` case is the benchmark behind
`docs/performance/procedural-geometry.md`.
