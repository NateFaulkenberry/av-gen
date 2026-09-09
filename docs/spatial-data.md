# Spatial data: attributes, point clouds, operators, fields, effectors

The procedural pipeline (ADR-023) generates instances from a distribution. ADR-024 and ADR-025
put a typed point cloud between generation and the renderer's instance records, so operators,
fields and effectors can read and write per-point data:

```
distribution + variation  ->  PointCloud  ->  point ops (structural)  ->  InstanceRecord[]
                                                                           |
                                    effectors (per frame, GPU; CPU reference here)  <-  fields
```

Everything here is deterministic: all randomness is `noise::hashIndex(seed, id, channel)`, no
result depends on iteration order or wall time, and the same maths runs on the GPU
(`shaders/fields.wgsl`, `shaders/points.wgsl`) within float rounding. Headers:
`src/spatial/attributes.hpp`, `point_cloud.hpp`, `spatial_ops.hpp`, `field.hpp`, `effector.hpp`.

## Attributes (`spatial::AttributeSet`)

A structure-of-arrays table: named columns of one type each over a domain (`point`, `vertex`,
`primitive`, `instance`), every column with exactly `count()` rows.

| type    | storage        | components |
|---------|----------------|------------|
| `bool`  | `uint8_t`      | 1 |
| `int`   | `int32_t`      | 1 |
| `float` | `float`        | 1 |
| `vec2`  | `glm::vec2`    | 2 |
| `vec3`  | `glm::vec3`    | 3 |
| `vec4`  | `glm::vec4`    | 4 |
| `color` | `glm::vec4`    | 4 (linear rgb + alpha; views as vec4) |

- `add(name, type)` returns the existing column when the name and type match, fails on a type
  clash. `view<T>(name)` / `ensure<T>(name, type)` give typed spans; names are case-sensitive.
- `resize` grows with zeros or truncates every column; `clear` keeps the columns.
- Row operations move every column together: `keepRows(mask)`, `keepIndices(order)` (may
  duplicate rows), `append(other)` / `appendRows(other, indices)` (union of columns, missing
  values are zero).
- `readAsVec4` / `writeFromVec4` are the generic accessors: bool/int/float read into `x`,
  vec2/vec3 are zero-padded; writes truncate (bool = `x != 0`, int = trunc toward zero).
- `contentHash()` is FNV-1a over domain, count, and every column's name, type and raw bytes.
- JSON: `{"domain": "point", "count": n, "attributes": [{"name", "type", "values": [...]}]}`
  with `values` flat (count x components numbers; bool as 0/1).

### Attribute ops (`spatial::AttributeOp`)

`applyAttributeOp(set, op)` writes `target` (created as `float`, or as the source's type when a
source column exists). `source` defaults to `target`. Ops act per component of the target.

| kind (`"kind"`) | result |
|---|---|
| `set` | `value` |
| `add` | `target + value * source` (`source` only when it names an existing column, else `* 1`) |
| `multiply` | `target * value * source` (same rule) |
| `remap` | `source` from `[inMin, inMax]` to `[outMin, outMax]` (clamped when `clamp`) |
| `clamp` | `clamp(source, outMin, outMax)` |
| `normalize` | `(source - min) / (max - min)` over the column (0 for a constant column) |
| `smooth` | mean of rows `[i - radius, i + radius]` (windows shrink at the ends) |
| `noise` | `mix(source, fbm3(position * scale + offset, seed), amount)` (`positionAttribute`, default `position`) |
| `randomize` | `mix(source, value + hashIndex(seed, id, component) * range, amount)`; ids from `idAttribute` (int column, default `id`) else the row index |
| `lerp` | `mix(source, secondSource, amount)` |
| `fit` | column `[min, max]` fitted into `[outMin, outMax]` (outMin for a constant column) |
| `threshold` | `source >= value ? 1 : 0` |
| `compare` | `compare(source, value)` with `compare` 0 `<`, 1 `<=`, 2 `==`, 3 `>=`, 4 `>`, 5 `!=` |

JSON writes `kind`, `target` and every non-default field: `enabled`, `source`, `secondSource`,
`positionAttribute`, `value` [4], `amount`, `inMin`, `inMax`, `outMin`, `outMax`, `clamp`,
`scale`, `offset` [3], `range` [4], `seed`, `radius`, `compare`, `idAttribute`.
`attributeStats(buffer)` returns per-component min/max/mean for the inspector.

## Point clouds (`spatial::PointCloud`)

An `AttributeSet` over the point domain whose core columns always exist:

| column | type | default | meaning |
|---|---|---|---|
| `position` | vec3 | 0 | object-space position |
| `rotation` | vec4 | (0,0,0,1) | unit quaternion xyzw |
| `scale` | vec3 | 1 | |
| `id` | int | row | stable per generator; filters keep it, generators of rows continue after the max id |
| `seed` | int | 0 | `reseed(g)`: `pcg3d({g, id, 0x9E3779B9}).x` |
| `density` | float | 1 | |
| `color` | color | (1,1,1,1) | base colour multiplier |
| `emissive` | vec3 | (1,1,1) | emissive multiplier |
| `velocity` | vec3 | 0 | |
| `normal` | vec3 | (0,1,0) | |
| `bounds` | vec3 | 0.5 | half-extent (x scale in `bounds()`) |
| `index` | float | row / (n - 1) | normalised index u; `renumberIndices()` recomputes it |

`random(i, channel) = hashIndex(uint(seed[i]), uint(id[i]), channel)`, so a point's randoms
follow its id through filters and sorts.

`projectInstances(cloud, records, extraLane)` writes the 96-byte `InstanceRecord` per row:
`position.w` = density, `scale.w` = index, `random` = channels 0..3, `color.a` = float(id),
`emissive.a` = `extraLane` attribute's x (0 when unbound). `cloudFromInstances` is the inverse
for tools (random lanes are not invertible; seeds stay 0).

## Point ops (`spatial::PointOp`)

`applyPointOps(cloud, ops)` runs the enabled ops in order; the first failing op stops the list
and reports its index. Ops are structural: `ProceduralGeometry` rebuilds when any op field
changes (`pointOpHash` covers every field). JSON writes `kind` plus non-default fields; reading
accepts every field. `amount` scales `translate`, `rotate`, `noise`, `scatter` and `randomize`
(0 = no-op) and is the modulatable `ops/<slot>/amount` parameter.

Random ops draw `hashIndex(uint(seed[i]) + op.seed * 0x9E3779B9, uint(id[i]), channel)`; with
`op.seed = 0` the point's seed column is used as-is.

| kind | fields | effect |
|---|---|---|
| `transform` | `position`, `rotation` (Euler deg XYZ), `scale`, `pivot` | `p = pivot + R(S(p - pivot)) + position`; rotation premultiplied by R; scale x= S |
| `translate` | `offset`, `amount` | `p += offset * amount` |
| `rotate` | `axis`, `angle` (rad), `pivot`, `amount` | rotate positions about the axis through the pivot; rotations premultiplied |
| `scale` | `factor`, `pivot`, `scaleInstances` | `p = pivot + (p - pivot) * factor`; scale x= factor when `scaleInstances` |
| `noise` | `frequency`, `offset`, `seed`, `axisMask`, `amount` | `p += fbm3Vec(p * frequency + offset, seed) * axisMask * amount` |
| `randomize` | `randomPosition`, `randomRotation` (rad), `randomScale`, `randomUniformScale`, `seed`, `amount` | exactly `scene::variationTransform` on channels 16..25 (position 16-18, rotation 19-21, scale 22-24, uniform 25); ranges x amount |
| `scatter` | `range`, `seed`, `amount` | `p += (hash(40..42) * 2 - 1) * range * amount` |
| `filterDensity` | `threshold`, `probabilistic`, `seed`, `invert` | keep `density >= threshold`, or `density >= hash(44)` |
| `filterAttribute` | `attribute`, `value`, `compare`, `invert` | keep `compare(attribute.x, value)` (fails when the attribute is missing) |
| `filterDistance` | `pivot`, `minDistance`, `maxDistance`, `invert` | keep `min <= |p - pivot| <= max` |
| `filterProbability` | `probability`, `seed`, `invert` | keep `hash(43) < probability` |
| `filterBounds` | `boundsMin`, `boundsMax`, `invert` | keep inside the box |
| `delete` | `attribute` (default `delete`) | remove rows where `attribute.x != 0` (no-op when missing) |
| `sort` | `attribute`, `component`, `descending` | stable sort, then renumber indices |
| `merge` | | no-op on one cloud (`mergeClouds(a, b)` appends b with ids after a's max id) |
| `duplicate` | `copies`, `offset`, `axis`, `angle`, `pivot`, `factor` | copy k: `p = pivot + R^k((p - pivot) factor^k) + offset k`; ids `maxId + 1 + (k-1) n + row`; renumber |
| `sample` | `stride`, `start`, `count` | keep rows `i >= start, (i - start) % stride == 0`; or `count` rows evenly (`floor(k (n-1) / (count-1))`) |
| `attribute` | `attributeOp` | an attribute op |

Filters (including `delete` and `sample`) keep ids and the `index` column; `sort`, `duplicate`
and `merge` renumber indices.

## Fields (`spatial::FieldSpec`)

A field is sampled at a world point `p` and time `t` and yields a scalar, a vector or a colour.
`Scene::fields` holds them by name; effectors, field deformers, particle field forces and
materials refer to them by name. `FieldGpu` (320 bytes, 16 slots) packs one field for the
shaders.

Common fields: `name`, `kind`, `enabled`, `space` (`world`/`local`), `position`, `rotation`
(Euler degrees XYZ), `scale`, `strength`, `invert`, `falloff`, `speed`, `phase`.

Evaluation (the WGSL transliterates this order):

1. `q = worldToLocal * p` where `localToWorld = T(position) R(rotation) S(scale)`.
2. `n = normalize(axis)` (`(0,1,0)` for a zero axis).
3. `d` = falloff distance: `|dot(q, n)|` for `linearGradient`, `plane`, `direction`,
   `gradient`; `max(maxcomp(|q| - size), 0)` for `box`; `|q - point|` otherwise.
4. `w = strength * falloff.weight(d)`.
5. `tau = speed * t + phase`.
6. Scalar kinds: `v = shape(q)`, invert gives `1 - v`, result `v * w`. Vector kinds: unit (or
   zero) direction in local orientation, invert negates, result `R * (dir * w)` with `R` the
   rotation-only 3x3 (no scale). Colour kinds: `(rgb, w)`; invert swaps `colorA`/`colorB`.

| kind | type | value before invert/weight |
|---|---|---|
| `constant` | scalar | 1 |
| `linearGradient` | scalar | `saturate(dot(q, n) / length + 0.5)` |
| `radial` | scalar | `1 - saturate(|q - point| / radius)` |
| `box` | scalar | `1 - saturate(max(maxcomp(|q| - size), 0) / softness)` |
| `sphere` | scalar | `1 - saturate((|q - point| - radius) / softness)` |
| `plane` | scalar | `saturate(dot(q, n) / softness)` |
| `noise` | scalar | `fbm3(q * frequency + tau * (1, 0.7, 1.3), seed)` |
| `voronoi` | scalar | `saturate(voronoiF1(q * frequency + tau * (1, 0.7, 1.3), seed))` |
| `distance` | scalar | `saturate(|q - point| / radius)` |
| `sdfDistance` | scalar | 0 on the CPU (bound to an SDF by `reference` later, ADR-027) |
| `wave` | scalar | `amplitude * envelope(s) * shape(k s)`, see below |
| `direction` | vector | `n` |
| `radialVector` | vector | `normalize(q - point)` |
| `attractor` | vector | `normalize(point - q)` |
| `repulsor` | vector | `normalize(q - point)` |
| `vortex` | vector | `normalize(cross(n, q - point))` |
| `curlNoise` | vector | `curlNoise(q * frequency + tau * (1, 0.7, 1.3), seed)` (not normalised) |
| `spiral` | vector | `normalize(vortex + spiralBias * radialVector)` |
| `waveVector` | vector | wave value x outward gradient of the wave geometry |
| `constantColor` | colour | `colorA` |
| `gradient` | colour | `mix(colorA, colorB, saturate(dot(q, n) / length + 0.5))` |
| `radialGradient` | colour | `mix(colorA, colorB, saturate(|q - point| / radius))` |
| `noiseColor` | colour | `mix(colorA, colorB, fbm3(q * frequency + tau * (1, 0.7, 1.3), seed))` |
| `positionColor` | colour | `fract(q * frequency)` |
| `compound` | scalar (children decide) | combine of up to 4 `children` by name: `add`, `multiply`, `max`, `min`, `mix` (`mix(child0, child1, mix)`), `average`; then x `w` (colours: rgb combined, alpha = max x w). Depth is limited to 4; cycles yield 0 for the cyclic branch. |

Waves: `s = waveDistance(q) - waveOrigin - waveSpeed * t` with `waveGeometry` `planar`
(`dot(q, n)`), `radial` / `cylindrical` (distance from the axis line through `point` along
`n`), `spherical` (`|q - point|`); `k = 2 pi / wavelength`; `waveShape` `sine` (`sin(k s)`),
`pulse` (`exp(-(k s)^2)`), `triangle` (`4 |fract(k s / 2pi + 0.75) - 0.5| - 1`); envelope
`falloffCurve(smoothstep, saturate(|s| / waveWidth))`, or 1 when `waveWidth` is 0. Driving
`amplitude` from an audio onset makes each hit a wave that propagates outward.

Cross-type reads: scalar as vector `s * (R n)`; vector as scalar `|v|`; colour as scalar
`luminance(rgb) * a`; scalar as colour `(mix(colorA, colorB, saturate(s)), w)`; vector as colour
`(v * 0.5 + 0.5, w)`; colour as vector `(rgb * 2 - 1) * a`. A disabled field samples as 0.

### Falloff

`falloff: {kind, inner, outer, exponent, curve, noiseAmount, noiseScale}`. `weight(d)` is 1 for
`d <= inner`, 0 for `d >= outer`, and `curve(t)` with `t = (d - inner) / (outer - inner)` in
between (`outer <= inner` is a step at `inner`; `none` is 1 everywhere).

| kind | curve(t), 1 at 0 |
|---|---|
| `none` | 1 |
| `linear` | `1 - t` |
| `smoothstep` | `1 - t^2 (3 - 2t)` |
| `smooth` | `1 - t^3 (t (6t - 15) + 10)` |
| `easeIn` | `1 - t^3` |
| `easeOut` | `(1 - t)^3` |
| `easeInOut` | `1 - (t < 0.5 ? 4 t^3 : 1 - (2 - 2t)^3 / 2)` |
| `exponential` | `(1 - t)^exponent` |
| `customCurve` | cubic Bezier on y through `1, curve.x, curve.y, curve.z` at x = 0, 1/3, 2/3, 1 (`curve.w` unused) |
| `noiseModulated` | `smoothstep(t) * saturate(1 + noiseAmount (fbm3(q * noiseScale, seed) 2 - 1))` (q = local point) |

### Field JSON

Every field is written (defaults fill missing ones on read):

```json
{
  "name": "pulse", "kind": "wave", "enabled": true, "space": "world",
  "position": [0, 0, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1],
  "strength": 1.0, "invert": false,
  "falloff": {"kind": "smoothstep", "inner": 0, "outer": 20, "exponent": 2,
              "curve": [0.8, 0.6, 0.4, 0.2], "noiseAmount": 0.5, "noiseScale": 1},
  "speed": 0, "phase": 0, "axis": [0, 1, 0], "point": [0, 0, 0],
  "radius": 10, "length": 10, "size": [5, 5, 5], "softness": 0.5, "frequency": 0.2, "seed": 7,
  "spiralBias": 0.5, "waveGeometry": "radial", "waveShape": "pulse",
  "amplitude": 1, "wavelength": 4, "waveSpeed": 4, "waveWidth": 6, "waveOrigin": 0,
  "colorA": [1, 1, 1, 1], "colorB": [0, 0, 0, 1], "children": [], "combine": "add", "mix": 0.5,
  "reference": ""
}
```

`validate()` requires a name, non-zero scale, `0 <= inner <= outer`, `radius`, `length`,
`wavelength` > 0, `softness`, `frequency`, `waveWidth` >= 0, at most 4 non-empty compound
children that are not the field itself, and a `reference` for `sdfDistance`.

## Effectors (`spatial::Effector`)

An effector is a field name, an operation, a blend, `strength`, `weight` (mix factor), `axis`
(direction for scalar fields / rotation axis), `scaleAxis` and `target` (attribute op). They are
per-frame: `ProceduralGeometry::effectors` (at most 8) run on the instance records on the GPU;
`applyEffectors(cloud, ...)` and `applyEffectorsToRecords(records, ...)` are the CPU reference.
World-space fields are sampled at `objectToWorld * p`, and vector results are brought back into
object space with `inverse(mat3(objectToWorld))`; `local` fields sample the object-space point.

With `E` the existing value, `s` / `v` / `c` the scalar / vector / colour sample and `k` the
strength, each op has a raw value `R` and a natural result `N`:

| op | R | N |
|---|---|---|
| `positionOffset` | `v k` (scalar fields: `s k axis`) | `E + R` |
| `scale` | `s k scaleAxis` | `E (1 + R)` |
| `rotation` | angle `s k` about `normalize(v)` (vector fields, `s = |v|`) or `axis` | `angleAxis * E` |
| `velocity` | `v k` | `E + R` |
| `color` | `c.rgb` | `mix(E, c.rgb, c.a k)` (alpha untouched) |
| `emission` | `s k` | `E (1 + R)` |
| `density` | `s k` | `E R` |
| `attribute` | `s k`, `v k` or `c k` by field type (column created as float / vec3 / color) | `E + R` |

Blend: `add` gives `N`, `multiply` `E * R`, `replace` `R`, `min` / `max` of `E` and `N`, `mix`
`mix(E, N, weight)`. Rotation uses `N` for everything but `replace` (`angleAxis` alone) and `mix`
(slerp). Disabled effectors, missing or disabled fields, and (on records) `velocity` /
`attribute` are skipped and not counted.

JSON: `{"field", "op", "blend", "enabled", "strength", "weight", "axis", "scaleAxis", "target"}`.
`EffectorGpu` (48 bytes): op, blend, field slot (-1 when disabled or unbound), strength,
`axis + weight`, `scaleAxis`.

## Procedural objects

`ProceduralGeometry` gains:

- `source.kind = "point"` with `pointSize`: a camera-facing quad (`makePointQuad`), bounds
  half-extent `pointSize / 2`. Parameter `source/pointSize`.
- `deformers[].kind = "field"` with `field` and `alongNormal`: vector fields displace by
  `v * amount`; scalar fields by `s * amount` along the normal (or `axis`). Local-space field
  deformers sample the object-space point, world-space ones the world point.
- `ops` (point ops, structural), `effectors` (per frame), `emissiveField` +
  `emissiveFieldAmount` (emission x field sample, per frame), `extraLane` (attribute projected
  into `InstanceRecord::emissive.a`, structural).
- `generateCloud()` builds the base cloud (position/rotation/scale from the composed transform,
  `id` = index, `seed` = `variation.seed`, `density` 1, `index` u, colour/emissive from the
  material variation, `bounds` = source half-extent); `rebuild()` = generateCloud, point ops,
  `projectInstances(extraLane)`, bounds (+ the sum of |strength| of enabled `positionOffset`
  effectors), and keeps `cloud` when `count <= kKeepCloudMax` (262144).
- Parameters: `ops/<slot>/amount`, `ops/<slot>/enabled` (structural: changing them rebuilds),
  `effector/<slot>/strength`, `effector/<slot>/weight`, `effector/<slot>/enabled`,
  `emissiveFieldAmount` (per frame). Slots are 1-based and follow the authored lists.

```json
{
  "name": "swarm",
  "source": {"kind": "point", "pointSize": 0.05},
  "distribution": {"kind": "grid", "gridCount": [64, 1, 64], "gridSpacing": [0.3, 0.3, 0.3]},
  "ops": [
    {"kind": "scatter", "range": [0.1, 0.5, 0.1], "seed": 3},
    {"kind": "filterProbability", "probability": 0.8},
    {"kind": "attribute", "attributeOp": {"kind": "randomize", "target": "heat", "range": [1, 0, 0, 0]}}
  ],
  "effectors": [
    {"field": "pulse", "op": "positionOffset", "axis": [0, 1, 0], "strength": 0.5},
    {"field": "pulse", "op": "emission", "strength": 4}
  ],
  "emissiveField": "pulse", "emissiveFieldAmount": 2, "extraLane": "heat"
}
```

Particle systems take `fieldForces` (`field`, `mode` `force` / `velocity` / `turbulence` /
`kill`, `strength`, `mix`, `axis`, at most 4) with parameters `fieldForce/<slot>/strength`.

## Determinism rules

- Ids are assigned by the generator (`id = index`) and never change through filters and sorts;
  `duplicate` and `mergeClouds` continue after the max id.
- Every random is `hashIndex(seed, id, channel)` with documented channels: instance record
  lanes 0..3, material variation 4..6, variation / `randomize` 16..25, `scatter` 40..42,
  `filterProbability` 43, `filterDensity` 44, attribute `randomize` 0..3 per component.
- Structural hashes (`pointOpHash`, `FieldSpec::structuralHash`, `Effector::structuralHash`,
  `AttributeSet::contentHash`) are FNV-1a over every field so caches and tests can compare
  content, not identity.
- Noise (`core/noise.hpp`) is the single implementation shared with the shaders.
