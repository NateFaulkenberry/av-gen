# Signed distance fields (CPU side)

Decision: `docs/decisions/ADR-027-sdf-architecture.md`. Research: `docs/research/sdf-and-implicit-geometry.md`.
Header: `src/spatial/sdf.hpp`. Implementation: `src/spatial/sdf.cpp`. Tests: `tests/unit/test_sdf.cpp` (`[sdf]`).

An SDF is a tree of `SdfNode`s. `SdfTree::evaluate(p, time, fields)` returns the signed distance at a
tree-local point (negative inside); `normal` differentiates it; `meshSdf` turns it into a `MeshData`;
`packSdfTree` flattens it into the `SdfNodeGpu` array that `shaders/sdf.wgsl` interprets, and
`evaluatePacked` is the CPU copy of that interpreter (tests hold both paths to within 1e-5).
Everything is pure and deterministic. JSON names are lower-case camel (`roundedBox`, `smoothUnion`,
`polarRepeat`, `displaceNoise`, ...).

## Node reference

`p` is the node's local point. Children of domain ops see the transformed point `q`. Unused
members are ignored (a Scale node's `translation` does nothing). Every node has `enabled`.

### Primitives (exact distances, Quilez)

| kind | members | formula |
|---|---|---|
| `sphere` | radius | `|p| - radius` |
| `box` | size (half extents) | `q = |p| - size; |max(q, 0)| + min(max(q.x, q.y, q.z), 0)` |
| `roundedBox` | size, rounding | `q = |p| - size + rounding; |max(q, 0)| + min(max(q.x, q.y, q.z), 0) - rounding` (total extent stays `size`) |
| `cylinder` | radius, height (along Y) | `h = height / 2; d = |(|p.xz|, p.y)| - (radius, h); min(max(d.x, d.y), 0) + |max(d, 0)|` |
| `capsule` | radius, height | segment from `(0, -h, 0)` to `(0, h, 0)` with `h = height / 2`: `p.y -= clamp(p.y, -h, h); |p| - radius` (the caps add `radius` beyond the segment) |
| `torus` | radius (major), rounding (minor) | `q = (|p.xz| - radius, p.y); |q| - rounding` |
| `plane` | axis, offset | `dot(p, normalize(axis)) - offset` (a zero axis is rejected by `validate`) |
| `cone` | radius, height | apex at `+height/2`, base of `radius` at `-height/2`; Quilez `sdCone` with `w = (|p.xz|, p.y - height/2)`, `q = (radius, -height)`: `a = w - q clamp(dot(w,q)/dot(q,q), 0, 1)`, `b = w - q (clamp(w.x/q.x, 0, 1), 1)`, `k = sign(q.y)`, `d = min(dot(a,a), dot(b,b))`, `s = max(k (w.x q.y - w.y q.x), k (w.y - q.y))`, result `sqrt(d) sign(s)` |

### Combinations (1..8 children, folded in order)

`d = c0`, then `d = op(d, ci)` for each further enabled child. `k = max(smooth, 1e-4)`.

| kind | op |
|---|---|
| `union` | `min(d, c)` |
| `intersection` | `max(d, c)` |
| `difference` | `max(d, -c)` (first child minus the rest) |
| `smoothUnion` | `smin(d, c, k)` |
| `smoothIntersection` | `-smin(-d, -c, k)` |
| `smoothDifference` | `-smin(-d, c, k)` |

with the polynomial `smin(a, b, k): h = clamp(0.5 + 0.5 (b - a) / k, 0, 1); mix(b, a, h) - k h (1 - h)`.
A combination with no enabled children evaluates to `1e9` ("far").

### Domain operations (exactly one child)

| kind | members | child point `q` | distance |
|---|---|---|---|
| `translate` | translation | `p - translation` | unchanged |
| `rotate` | rotation (Euler degrees XYZ, `glm::quat(radians(r))` = Rz Ry Rx like composition nodes) | `conj(R) * p` | unchanged |
| `scale` | scale (> 0, uniform) | `p / scale` | `child(q) * scale` (true distance) |
| `twist` | amount (radians per unit Y) | `a = amount p.y; (cos a p.x + sin a p.z, p.y, -sin a p.x + cos a p.z)` (Quilez opTwist) | unchanged: a bound only (Lipschitz > 1) |
| `bend` | amount (radians per unit X, about Z) | `c = cos(amount p.x), s = sin(amount p.x); (c p.x - s p.y, s p.x + c p.y, p.z)` (Quilez opCheapBend) | unchanged: a bound only |
| `repeat` | size (cell per axis, 0 = none), count (copies per side, 0 = infinite) | per axis with `size_i > 0`: `q_i = p_i - size_i clamp(rnd(p_i / size_i), -count, count)` (no clamp when count = 0), `rnd(x) = floor(x + 0.5)` | unchanged |
| `polarRepeat` | count (about Y; 0 = no-op) | `sector = 2 pi / count; a = atan2(p.z, p.x); a' = a - sector rnd(a / sector); (cos a' r, p.y, sin a' r)`, `r = |p.xz|` | unchanged |
| `mirror` | size (mask: > 0 mirrors that axis) | `q_i = |p_i|` on masked axes | unchanged |

Twist and bend distort distances; ray marchers must under-step (a relaxation factor) or accept
artefacts. `rnd` is spelt `floor(x + 0.5)` on both paths because WGSL's `round` is half-to-even.

### Displacements (exactly one child; `t = float(time)`)

| kind | members | `d +=` |
|---|---|---|
| `displaceNoise` | amount, frequency, speed, seed | `amount (fbm3(p frequency + vec3(speed t), seed) 2 - 1)` |
| `displaceVoronoi` | amount, frequency, seed | `amount (voronoiF1(p frequency, seed) - 0.5)` |
| `displaceWave` | amount, frequency, speed, axis (not normalised) | `amount sin(dot(p, axis) frequency + speed t)` |
| `displaceField` | amount, reference (field name) | `amount sampleScalar(field, p, time, fields)`; 0 when there is no field set or the name is unknown (GPU: `fieldSlot` = -1) |

Displacement breaks the distance bound by up to `amount`.

### Disabled nodes

A disabled node is absent: a disabled unary op is replaced by its child; a disabled primitive or
combination is skipped by its parent combination and is "far" (`1e9`) when it is the root or the
child of a unary op (the op then applies to `1e9`, e.g. `scale` yields `1e9 * scale`, on both paths).

### Normals

`SdfTree::normal` uses the tetrahedron technique: `n = normalize(sum_i k_i f(p + k_i eps))` with
`k` in `{(1,-1,-1), (-1,-1,1), (-1,1,-1), (1,1,1)}` (four evaluations; +Y when degenerate).

## Validation and limits

`SdfTree::validate` fails when:

- the tree has more than `kMaxSdfNodes` = 64 nodes or is deeper than `kMaxSdfDepth` = 8 levels (root = 1);
- arity is wrong: primitives have children, a unary op does not have exactly one enabled child, a combination has 0 or more than 8 children;
- a parameter is not finite; `radius`, `height`, `rounding`, `size.xyz` or `count` is negative; a `scale` node's `scale` is not > 0; a `plane` has a zero axis;
- the packed program would exceed `2 * kMaxSdfNodes` = 128 nodes, or need more than `kMaxSdfStack` = 8 distance or point stack entries (impossible within the depth limit, but checked).

`SdfNode::fromJson` also refuses nesting deeper than 8 levels.

## Packed execution (the GPU scheme)

`packSdfTree` flattens the effective (enabled) tree into `SdfNodeGpu` records; `evaluatePacked`
runs them and is the algorithm `shaders/sdf.wgsl` must transliterate.

State: `dist[8]` (distance stack, `sp` entries), `pts[8]` (saved points, `pp` entries) and `cur`,
the current point, initially the query point. Records are executed in order; `childCount` selects
the behaviour:

| record | childCount | action |
|---|---|---|
| primitive | 0 | `push(d(kind, cur))` |
| combination | n | `n == 0`: `push(1e9)`. Otherwise the top `n` entries `dist[sp-n .. sp-1]` are the children in evaluation order: `d = dist[sp-n]; d = op(d, dist[sp-n+j])` for `j = 1..n-1`; drop them and `push(d)` |
| unary BEGIN | 0xFFFF | before the subtree: `pts[pp++] = cur`; domain ops set `cur = warp(kind, cur)` (Scale divides by `scale`); displacements leave `cur` unchanged |
| unary END | 1 | after the subtree: `cur = pts[--pp]` (the op's own local point); then Scale does `dist[sp-1] *= scale`, displacements do `dist[sp-1] += term(cur)`, other domain ops nothing |

The packer emits combinations as **binary folds**: a combination with enabled children
`c0 .. ck-1` is emitted as `c0, c1, OP(2), c2, OP(2), ..., ck-1, OP(2)`; `k == 1` emits nothing; `k == 0`
emits a single `OP(0)`. The fold order equals `evaluate`'s, so results are bit-identical, and the
distance stack never exceeds the combination nesting + 1. Every unary op (domain and
displacement) is one BEGIN + subtree + END; a unary op over an absent child wraps `union` with
`childCount = 0`. The result is `dist[sp-1]` (`1e9` for an empty program). Over/underflow returns
`1e9` instead of reading out of bounds.

Packed count = primitives + 2 x unary ops + sum over combinations of `max(k - 1, [k == 0])`, so at
most `2 x 64 = 128`.

Record layout (112 bytes): `kind`, `childCount`, `fieldSlot` (index of `reference` in the field set
for `displaceField`, else -1), `seed`; `p0 = (radius, height, rounding, offset)`; `p1 = (size.xyz,
scale)`; `p2 = (axis.xyz, amount)`; `p3 = (translation.xyz, smooth)`; `p4` = the **forward**
rotation quaternion `(x, y, z, w)` for `rotate` (the interpreter applies its conjugate: `q = conj(R) *
cur`), otherwise `(frequency, 0, 0, 0)`; `p5 = (frequency, speed, float(count), 0)`.

Example: `translate { smoothUnion [ sphere, scale { box } ] }` packs to
`translate B, sphere, scale B, box, scale E, smoothUnion(2), translate E` (7 records).

## Meshing (naive surface nets)

`meshSdf(tree, boundsMin, boundsMax, resolution, time, fields)`:

1. Sample the SDF on the `(resolution + 1)^3` lattice over the bounds (resolution 2..256 and at most
   8,000,000 samples, i.e. resolution <= 199 in practice; bounds must have positive extent).
2. For every cell (x-fastest order) whose corners differ in sign, add one vertex at the mean of the
   linear crossings on its sign-changing edges (`t = a / (a - b)`), with the normal from
   `tree.normal` (epsilon = a quarter of the smallest cell edge) and `uv = 0`.
3. For every lattice edge with a sign change (lattice points x-fastest, then axis x, y, z) whose
   four surrounding cells exist, emit two triangles over the quad of their vertices; the quad runs
   counter-clockwise about the edge axis `a` (in the `(b, c)` plane with `b x c = a`) when the
   lower corner is inside, reversed otherwise, so winding is outward.

The result is closed wherever the surface lies strictly inside the bounds (each edge shared by two
triangles); vertices lie within a cell diagonal of the surface; the mesh is byte-identical across
calls. Surface nets are coarse at low resolutions and round sharp creases; ADR-027 caches the mesh
by `structuralHash`.

## JSON

A tree is `{"root": node}` (a bare node object is also accepted). A node is an object with `"kind"`
plus its non-default members and an optional `"children"` array; unknown members are ignored and
missing members keep their defaults. Vectors are 3-element arrays; `rotation` is Euler degrees.

```json
{
  "root": {
    "kind": "displaceNoise", "amount": 0.1, "frequency": 3, "speed": 0.5, "seed": 7,
    "children": [{
      "kind": "smoothUnion", "smooth": 0.3,
      "children": [
        {"kind": "sphere", "radius": 1},
        {"kind": "translate", "translation": [1.2, 0, 0],
         "children": [{"kind": "roundedBox", "size": [0.5, 0.5, 0.5], "rounding": 0.1}]},
        {"kind": "polarRepeat", "count": 6,
         "children": [{"kind": "translate", "translation": [2, 0, 0],
                       "children": [{"kind": "capsule", "radius": 0.2, "height": 1}]}]},
        {"kind": "torus", "radius": 2.5, "rounding": 0.15, "enabled": false}
      ]
    }]
  }
}
```

`structuralHash` covers every member of every node (kind, enabled, all parameters, `reference`,
child count) recursively; it changes whenever the JSON would.
