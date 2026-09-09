# Splines (user guide)

Splines (ADR-026) are first-class spatial data: one curve representation that places instances,
deforms geometry along a path, emits particles, carries lights and drives the camera. A spline is
a named list of control points with an interpolation kind, sampled either by the uniform
parameter `t` in [0, 1] or by arc-length distance, with rotation-minimising frames so objects
placed along it never flip. Everything is pure and deterministic: the same spline gives the same
bytes live and offline, and the arc-length table is cached by a structural hash.

Header: `src/spatial/spline.hpp` (`avgen::spatial::Spline`, `SplineSample`, `SplineSet`,
`packSplineTable`).

## The model

```
control points (explicit or generated)  →  segments (kind)  →  dense table (arc length + frames)
        CPU (on change)                        pure                   cached by hash
```

| Member | Meaning |
|---|---|
| `kind` | `polyline`, `catmullRom` (default), `bezier`, `hermite` |
| `closed` | the last point joins the first (adds a segment; `t` and distance wrap) |
| `tension` | Catmull-Rom tangent scale; 0.5 = the standard uniform Catmull-Rom, 0 = smoothstep between neighbours |
| `points` | explicit control points (`generator = points`): `position`, `tangent`, `roll`, `scale` |
| `generator` | `points`, `line`, `circle`, `spiral`, `helix`, `bezier`, `noise` |
| `samplesPerSegment` | arc-length table resolution (clamped to 4..256 when building; validate accepts 1..256) |
| `up` | reference for the first normal (projected perpendicular to the first tangent) |

A control point's `tangent` is used by two kinds: Hermite reads it as the outgoing derivative;
Bezier reads it as the handle offset (out handle = `position + tangent`, the next point's in
handle = `position - tangent`, so the curve is C1 at every anchor). `roll` (radians) twists the
frame about the tangent and `scale` is a per-point factor; both interpolate linearly along the
segment parameter and are carried in every sample.

With `n` control points an open spline has `n - 1` segments and a closed one `n`. The uniform
parameter maps `t = (segment + u) / segments`, so `t = 0.25` on a four-segment spline is exactly
the second control point. Per kind, within a segment from `a` to `b`:

- **polyline**: linear.
- **catmullRom**: cubic Hermite with `m_a = tension * (b - prev)`, `m_b = tension * (next - a)`;
  neighbours wrap when closed and clamp at open ends.
- **bezier**: cubic Bezier through `a`, `a + a.tangent`, `b - b.tangent`, `b`.
- **hermite**: cubic Hermite with `m_a = a.tangent`, `m_b = b.tangent`.

## Generators

Any generator other than `points` produces `count` (>= 2) control points; the kind then
interpolates them like explicit points. Generated points carry central-difference tangents
(wrapped when closed, one-sided at open ends, divided by three for the Bezier kind), so
`hermite` and `bezier` kinds give the same smooth curve as `catmullRom` from a generator.

| Generator | Points |
|---|---|
| `line` | `count` points from `start` to `end` |
| `circle` | radius `radius` around `center` in the plane perpendicular to `axis`, from `startAngle`; a closed spline gets `count` distinct points (no duplicate seam), an open one runs to `startAngle + 2 pi` inclusive |
| `spiral` | `turns` turns with the radius growing linearly from `radius` by `radiusGrowth` |
| `helix` | `turns` turns of radius `radius` rising `height` along `axis` |
| `bezier` | `count` samples of the cubic `p0 p1 p2 p3` |
| `noise` | a line from `start` to `end` jittered by `fbm3Vec(position * noiseScale, seed) * noiseAmount` |

`noiseAmount > 0` applies the same jitter to every generator (explicit points included), which
is how a corridor or rail gets organic wobble that is stable per `seed`.

The circle-family plane basis matches the procedural radial distribution: `u` is +X projected
perpendicular to the axis (+Y when the axis is X) and `v = axis x u`, so for the default `axis =
+Y` the first point is on +X and the angle increases right-handedly about the axis (towards -Z).

## Sampling

```cpp
spatial::Spline s;               // defaults: catmullRom, open, generator points
s.generator = spatial::SplineGenerator::Helix;
s.radius = 3.0f; s.height = 12.0f; s.turns = 2.5f; s.count = 48;
if (auto ok = s.validate(); !ok) { /* ok.error().message */ }

const spatial::SplineSample a = s.sample(0.5f);               // by uniform parameter
const spatial::SplineSample b = s.sampleByDistance(4.0f);     // by arc length
const float length = s.length();
const auto ring = s.samples(64);                              // 64 samples equally spaced by distance
```

- `sample(t)`: `t` is clamped for open splines and wrapped (`t - floor(t)`) for closed ones. The
  result interpolates the dense table linearly (positions, tangents, normals), then renormalises
  and re-orthogonalises the frame.
- `sampleByDistance(d)`: clamped or wrapped to `[0, length]`, binary search on the distance column.
- `samples(n)`: `n >= 1` samples by distance; open splines use `k * length / (n - 1)` (first and
  last at the ends), closed ones `k * length / n` (no duplicate end). `samples(1)` is the start.
- `position(t)`, `tangent(t)` are shortcuts; `segmentCount()` and `controlPoints()` expose the
  generated points.
- `prepare()` builds the table eagerly; every sampler calls the same `ensureCache()` lazily and
  rebuilds only when `structuralHash()` changed (hash over every member, points included). A copy
  carries its cache. The spline is not internally synchronised: share a `const` spline between
  threads only after `prepare()`.

`validate()` rejects fewer than two control points (after generation), generator `count < 2`,
negative `radius`, `samplesPerSegment` outside 1..256 and any non-finite value. Degenerate splines
(one point, all points coincident) still sample without failing: length 0, a single frame.

## Frames

Every sample carries a right-handed orthonormal frame:

| Axis | Vector | Meaning |
|---|---|---|
| **z** | `tangent` | unit direction of travel (analytic derivative; chord fallback where it vanishes) |
| **y** | `normal` | rotation-minimising normal plus `roll` |
| **x** | `binormal` | `cross(normal, tangent)` |

`SplineSample::rotation()` is `quat_cast(mat3(binormal, normal, tangent))`, so `rotation *
(0,0,1) == tangent` and `rotation * (0,1,0) == normal`: the same "+Z forward, +Y up" convention as
the procedural orientation modes and composition nodes. An instance placed with this rotation
looks along the spline with its top towards the normal; a ribbon lies in the binormal direction.

The normals come from parallel transport by the double-reflection method (Wang et al. 2008)
along the dense table, which minimises twist between consecutive samples: no Frenet flips at
inflections, no dependency on the world up in the interior. The first normal is `up` projected
perpendicular to the first tangent (a stable perpendicular is chosen when they are parallel).
Closed splines receive a linear roll correction along the distance so the transported frame at
the seam coincides with the start frame; then the interpolated per-point `roll` rotates the normal
about the tangent (right-handed, so `roll = pi / 2` on a +Z line turns the +Y normal to -X).

GPU tables (`packSplineTable`, `SplineSampleGpu`, 64 bytes per entry, `kSplineGpuSamples = 512`
by default) are `samples(count)` packed as `position.w = distance`, `tangent.w = scale`,
`normal.w = t`, `binormal.w = 1` (roll already applied); shaders interpolate between neighbouring
entries by distance and re-orthogonalise like the CPU sampler does.

## JSON

Every member is written; every member may be omitted when reading (defaults apply). Enum names
are lower-case camel; vectors are 3-element arrays.

```json
{ "name": "rail", "kind": "catmullRom", "closed": true, "tension": 0.5,
  "generator": "circle", "count": 32, "radius": 6, "center": [0, 1, 0], "axis": [0, 1, 0],
  "startAngle": 0, "noiseAmount": 0.3, "noiseScale": 0.3, "seed": 7,
  "samplesPerSegment": 16, "up": [0, 1, 0] }
```

```json
{ "name": "cameraPath", "kind": "hermite", "generator": "points",
  "points": [
    { "position": [0, 2, -10], "tangent": [0, 0, 12], "roll": 0.0, "scale": 1 },
    { "position": [4, 3, 0],   "tangent": [0, 0, 12], "roll": 0.2, "scale": 1 },
    { "position": [0, 2, 10],  "tangent": [-6, 0, 6], "roll": 0.0, "scale": 1 } ] }
```

Other keys: `start`, `end` (line/noise), `radiusGrowth`, `turns`, `height`, `p0`..`p3` (bezier
generator). `SplineSet` holds the named splines of a scene (`find(name)`, `indexOf(name)` = -1
when missing); systems reference a spline by name.

## How other systems use splines (ADR-026)

Distributions, the path deformer and the particle emitter are implemented (details below);
lights and the camera describe the intended contract with the spline API.

- **Distribution along a spline** (`Distribution::kind = "spline"`, `docs/procedural-geometry.md`):
  `count` instances spread evenly by arc length over `[splineStart, splineEnd]` (fractions of
  `length()`; a reversed range runs backwards), or — when `spacing > 0` —
  `floor(length × |splineEnd - splineStart| / spacing) + 1` instances exactly `spacing` apart
  starting at `splineStart`. A closed spline whose span is a whole number of turns drops the
  duplicate seam instance (and its spacing count loses the `+ 1`). Each instance takes
  `sample.position` offset by `splineOffset` in frame space (x = binormal, y = normal,
  z = tangent), `sample.rotation()` composed with `roll` radians about the tangent when
  `alignToSpline` (identity rotation otherwise), and `sample.scale` as its scale. Without the
  spline (no context, unknown name) every placement is the identity, so the object still builds.
  `ProceduralGeometry::generateCloud(ctx)` looks the spline up through
  `GenerationContext::splines`, and `contextualHash(ctx)` mixes in the spline's
  `structuralHash()`, so editing the curve rebuilds the cloud.
- **Path deformer** (`Deformer::kind = "path"`): a curve deform, object space only (a `world`
  one is skipped). The coordinate along the deformer's `axis`, measured from `center`, maps to
  arc length `d = pathOffset + coord × pathScale`; `pathScale = 0` means "fit": the source's
  extent along the axis maps to the whole spline length. The perpendicular components ride the
  frame at `d`: with `u = normalize(cross(up, axis))` (`up` = +Y, or +X when the axis is within
  0.001 of ±Y) and `v = cross(axis, u)`,

  ```
  p' = S(d).position + (binormal × u' + normal × v') × S(d).scale     (u', v') = (u, v) rotated by pathRoll
  p_out = mix(p, p', amount)
  ```

  For the default `axis = +Z` the basis is `(u, v) = (+X, +Y)`, which coincides with the
  `(binormal, normal)` of a straight +Z spline: the deformer is then the identity up to
  translation. The CPU reference is `scene::applyPathDeformer` (and `deformPointWith`, which
  resolves the whole stack through a `DeformContext`); the GPU transliterates it in
  `shaders/procedural.wgsl` (deformer kind 6) from the packed table.
- **Particle emitter** (`EmitterShape::Spline`): each spawn hashes a `u` in [0, 1) and starts at
  `sampleByDistance(u × length).position`, jittered inside a sphere of radius `extent.x`; the
  emitter's `direction` is read in the spline frame (x = binormal, y = normal, z = tangent), so
  the default (0, 1, 0) rises along the normal and (0, 0, 1) follows the tangent. A missing or
  un-uploaded spline falls back to the `point` shape at the emitter position.
- **Lights**: a light group distributes `n` lights with `samples(n)`, orienting spot lights along
  the tangent and using `normal` as the light's up.
- **Camera** (`camera/mode = spline`): the camera sits at `sampleByDistance(splineT * length)`
  (or `sample(splineT)`), looks at the position a look-ahead distance further along
  (`sampleByDistance(d + lookAhead)`), and uses the sample normal rotated by an extra roll as up.
  `camera/splineT` is a normal parameter, so the timeline and audio can drive the ride.

All of these consume the same table, so the frame convention above (x = binormal, y = normal,
z = tangent) is the one to match on the GPU (`shaders/spline.wgsl`).

## On the GPU

`rendering/spline_buffers.hpp` (`SplineBuffers`, owned by `SceneRenderer`) packs every
`Scene::splines` entry with `packSplineTable` into one read-only storage buffer: a header of 16
`vec4` (x = length, y = closed, z = sample count, w = 1 when the slot holds a spline) followed by
16 × 512 `SplineSampleGpu` entries. Slot `i` is `scene.splines.splines[i]`; `slotOf(name)` returns
-1 for an unknown name or one beyond the 16-spline limit (the extras are logged once and are
simply unavailable to the shaders — their consumers fall back to "no spline"). The tables are
re-packed only when the set's combined structural hash changes, so a static set costs one upload.

`shaders/spline.wgsl` is included by `procedural.wgsl` (group 1, binding 4) and `particles.wgsl`
(group 0, binding 9). It offers `splineLength(slot)` and `splineSample(slot, distance)`, which
binary-searches the distance column, interpolates the two neighbouring entries and
re-orthonormalises the frame exactly like `Spline::sampleByDistance` — the CPU samples its own
denser table, so the two agree to the tables' interpolation error rather than bit for bit.
Distances clamp on open splines and wrap on closed ones (the last entry interpolates back to the
first). An invalid slot returns the identity frame at the origin.

The procedural renderer resolves a Path deformer's spline name to a slot and its `pathScale` to
units of arc length per object unit (the "fit" mode divides the spline length by the source mesh
extent along the deformer axis) before packing the deformer uniform; an unresolved or world-space
Path deformer becomes a disabled slot. `ProceduralStats::pathDeformers` counts the bound ones.
