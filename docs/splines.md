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

The integrations below are owned by their respective systems; this describes the intended
contract with the spline API.

- **Distribution along a spline** (`Distribution::kind = spline`): instances are placed by
  `samples(count)` (equal arc-length spacing) or at a fixed distance step; each instance takes
  `sample.position`, `sample.rotation()` (optionally with an extra orientation mode) and
  `sample.scale` times its own scale. Closed splines omit the duplicate end automatically.
- **Path deformer** (`Deformer::kind = path`): a curve deform. The source's axis coordinate (its
  +Z by default) maps to distance along the spline (`offset + z * stretch`), and the cross-section
  `(x, y)` maps onto `(binormal, normal)` of the frame at that distance, scaled by `sample.scale`:
  `world = position + binormal * x + normal * y`. Runs on the GPU from the packed table
  (`packSplineTable`) interpolated by distance; the CPU reference is `sampleByDistance`.
- **Particle emitter** (`EmitterShape::Spline`): spawn positions from `sampleByDistance(u *
  length)` with `u` random or audio-driven; the initial velocity is the tangent (optionally with a
  normal/binormal spread); closed splines wrap.
- **Lights**: a light group distributes `n` lights with `samples(n)`, orienting spot lights along
  the tangent and using `normal` as the light's up.
- **Camera** (`camera/mode = spline`): the camera sits at `sampleByDistance(splineT * length)`
  (or `sample(splineT)`), looks at the position a look-ahead distance further along
  (`sampleByDistance(d + lookAhead)`), and uses the sample normal rotated by an extra roll as up.
  `camera/splineT` is a normal parameter, so the timeline and audio can drive the ride.

All of these consume the same table, so the frame convention above (x = binormal, y = normal,
z = tangent) is the one to match on the GPU (`shaders/spline.wgsl`).
