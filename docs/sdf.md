# Signed distance fields (CPU side)

Decision: `docs/decisions/ADR-027-sdf-architecture.md`. Research: `docs/research/sdf-and-implicit-geometry.md`.
Header: `src/spatial/sdf.hpp`. Implementation: `src/spatial/sdf.cpp`. Tests: `tests/unit/test_sdf.cpp` (`[sdf]`).

An SDF is a tree of `SdfNode`s. `SdfTree::evaluate(p, time, fields)` returns the signed distance at a
tree-local point (negative inside); `normal` differentiates it; `meshSdf` turns it into a `MeshData`;
`packSdfTree` flattens it into the `SdfNodeGpu` array that `shaders/sdf.wgsl` interprets, and
`evaluatePacked` is the CPU copy of that interpreter (tests hold both paths to within 1e-5).
Everything is pure and deterministic. Any node may carry a `name` (ADR-1001); a named node's
parameters are `node/<name>/<field>` instead of `node/<index>/<field>`. JSON names are lower-case camel (`roundedBox`, `smoothUnion`,
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
| `stairs` (ADR-1040) | size = (run, rise, half width), count (steps >= 1), height (underside thickness; 0 = solid to y = 0) | a flight climbing +X from x = 0: the signed distance to the infinite zig-zag of risers and treads (three steps about the point's diagonal coordinate `u = (x run + y rise)/(run^2 + rise^2)`), inside when `y < rise (floor(x/run) + 1)`; `max` with `max(-x, x - count run)` and the underside (`-y`, or the sloped plane `(slope x - y - height)/sqrt(1 + slope^2)`); extruded by `|z| - size.z`. Exact at treads and risers, a bound elsewhere |

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
| `morph` (ADR-1001) | `a = clamp(amount, 0, k-1)`, `i = floor(a)`: `c_i + (c_{i+1} - c_i)(a - i)`. Only `c_i` and `c_{i+1}` (only `c_i` at an integer `a`) are evaluated and packed: a settled morph costs one child. One float switches or morphs between structural states |

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
| `fold` (ADR-1001) | axis (plane normal, normalised), offset | `q = p - 2 min(dot(p, n) - offset, 0) n`: the half-space behind the plane is reflected in front | unchanged (exact while the content does not cross the plane) |
| `recurse` (ADR-1001) | count (levels after the first, 0..8), scale (> 0), translation, rotation, size (fold mask) | level 0 is `p`; `p_{l+1} = conj(R) fold(p_l) scale - translation`, `fold` = `|p_i|` where `size_i > 0` | `min_l child(p_l) / scale^l`: a bound (folds, rotations and uniform scale preserve distance) |
| `screw` (ADR-1040) | translation, count, offset (seam guard) | count 0: `k = rnd(p.T/|T|^2)`, `q = p - k T`. count n: `sector = 2 pi/n`, `a = atan2(p.z, p.x)` (0 at r = 0), `k0 = rnd(a/sector)`, winding `w = rnd((p.y - k0 T.y)/(n T.y))`, `q = (cos(a - k0 sector) r, p.y - (k0 + n w) T.y, sin(a - k0 sector) r)` | unchanged, then with `offset` > 0 `min(d, boundary + offset)`, boundary = distance to the cell's slab faces (or sector sides and winding) |
| `warp` (ADR-1040) | amount, frequency, size (per-axis gain), translation (phase), seed | `x = p frequency + translation`; `q = p + amount size (vec3(valueNoise(x), valueNoise(x + 31.7), valueNoise(x + 67.3)) 2 - 1)` | unchanged: a bound (Lipschitz about `1 + 2 amount frequency`); relax `stepScale` |
| `shell` (ADR-1040) | offset (wall thickness) | `p` | `|child| - offset/2` (exact onion) |

Twist and bend distort distances; ray marchers must under-step (a relaxation factor) or accept
artefacts. The under-step needed grows with `amount` times the structure's extent along the warped
axis (an unmeasured starting point: `stepScale` 0.8 for small amounts, lower as the artefacts --
holes and shimmering silhouettes -- appear). `displace*` breaks the bound by up to
`amount`. `repeat` of content that reaches past half its cell (a wall thicker than half the
spacing, an asymmetric doorway) is only correct near the cell centre; keep repeated content
inside its cell or use `mirror` + `repeat`. `rnd` is spelt `floor(x + 0.5)` on both paths because WGSL's `round` is half-to-even.

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

- the tree has more than `kMaxSdfNodes` = 96 nodes or is deeper than `kMaxSdfDepth` = 16 levels (root = 1;
  ADR-1001 raised them from 64 and 8, ADR-1005 the depth from 12 to 16);
- more than `kMaxSdfLoops` = 2 enabled `recurse` nodes are nested, or a `recurse` has `count` > 8;
- a node `name` is not letters, digits, `_` and `-`, is all digits, or is used twice in the tree;
- arity is wrong: primitives have children, a unary op does not have exactly one enabled child, a combination has 0 or more than 8 children;
- a parameter is not finite; `radius`, `height`, `rounding`, `size.xyz` or `count` is negative; a `scale` node's `scale` is not > 0; a `plane` has a zero axis;
- the packed program would exceed `2 * kMaxSdfNodes` nodes;
- **for the interpreter only** (`validate()`, the default, or `validate(SdfEvaluator::Interpreter)`):
  it would need more than `kMaxSdfStack` = 8 distance or point stack entries. In practice the point
  stack binds: no path may nest more than 8 unary operators.

**Compiled trees (ADR-1005).** A compiled tree (`compile: true` on the object, ADR-1003) is
straight-line WGSL with no stacks, so `SdfObject::validate` checks it with
`validate(SdfEvaluator::Compiled)`, which skips the two stack checks. Its unary nesting is then bound
only by the depth (up to 15 unary operators over a primitive). If a compiled object's variant is
unavailable (its compilation failed), the renderer falls back to the interpreter only when the tree
fits the stacks; otherwise the object is not drawn, with a warning, rather than drawn wrong. The
same tree with `compile: false` is refused when it loads.

`SdfNode::fromJson` also refuses nesting deeper than 12 levels. A disabled unary op may be the child
of another (it passes through); a disabled primitive or combination under a unary op is an error.

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
| recurse BEGIN/END | 0xFFFF / 1 | BEGIN also pushes a loop frame (level 0, acc = far, inv = 1). END pops the level's distance into `acc = min(acc, d inv)`; while `level < count` it steps `cur` to the next level, divides `inv` by `scale` and jumps back to the record after BEGIN (its index is in the END record's `fieldSlot`); then it restores `cur`, pushes `acc` and pops the frame |

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

## Scene objects and rendering

Header: `src/scene/sdf_object.hpp` / `src/rendering/sdf_renderer.hpp`. Implementation:
`src/scene/sdf_object.cpp`, `src/rendering/sdf_renderer.cpp`, `shaders/sdf.wgsl` (the interpreter)
and `shaders/sdf_raymarch.wgsl` (the pass). Tests: `tests/unit/test_sdf_object.cpp` (`[sdf]`) and
`tests/rendering/test_sdf_gpu.cpp` (`[sdf][gpu]`).

A `scene::SdfObject` is a tree plus a transform, a material and a render mode; `scene.sdfs` holds
them. `renderMode` decides how it reaches the screen:

- **`raymarch`** (default): nothing is meshed. Every frame the tree is packed and
  `shaders/sdf_raymarch.wgsl` sphere-traces it on the GPU inside `boundsMin..boundsMax`. Node
  parameters are live uniforms: modulating a radius costs nothing extra.
- **`mesh`**: `SdfObject::rebuild` meshes the tree with `meshSdf` at `resolution` and the result is
  drawn as an ordinary lit mesh (the entity `pbr.wgsl` pipelines). The mesh is cached by
  `meshHash`; a structural change re-meshes on the CPU, so modulating a node is expensive.

### JSON

```json
{
  "name": "blob", "visible": true,
  "tree": { "root": { "kind": "sphere", "radius": 1 } },
  "position": [0, 1, 0], "rotation": [0, 30, 0], "scale": [1, 1, 1],
  "material": {"baseColor": [0.2, 0.6, 0.9], "emissiveColor": [1, 1, 1],
               "emissiveIntensity": 0, "roughness": 0.4, "metallic": 0},
  "renderMode": "raymarch",
  "boundsMin": [-5, -5, -5], "boundsMax": [5, 5, 5],
  "resolution": 48, "maxSteps": 128, "epsilon": 0.002, "stepScale": 0.9, "normalEpsilon": 0.002
}
```

`rotation` is Euler degrees (the composition-node convention). Every member is optional and takes
the struct default when missing; `fromJson` validates the result, so a bad `renderMode`,
`resolution` or `stepScale` is an error rather than a clamp. `toJson` writes them all.

- `boundsMin/Max` is the tree-local box the object lives in. Raymarch: the ray enters and leaves it
  (nothing outside is drawn, and the projected box is the quad that gets rasterised, so a tight box
  is the single biggest performance lever). Mesh: the meshing domain.
- `maxDistance` (ADR-1002) caps the march in tree-local units (0 = the bounds only); the lit pass
  and the depth prepass apply it identically.
- `look` (ADR-1002, raymarch only, every term off at 0): `aoStrength`/`aoDistance` (5 taps along
  the normal), `edgeIntensity`/`edgeWidth`/`edgeColor` (emission where the normal taken at `edgeWidth`
  disagrees with the surface normal: creases and edges glow, flats do not; added to colour and to
  the bloom target),
  `shadowStrength`/`shadowSoftness`/`shadowDirection`/`shadowSteps` (a Quilez penumbra march
  towards a world direction). Occlusion and shadow scale the whole shaded colour (the cheap
  version: emission and fog included); edges are added after them.
- `compile` (ADR-1003, default false): draw the object with its tree compiled to WGSL instead of
  the interpreter. Roughly 15x faster per evaluation on the space example; a structural change
  (kind, child, `enabled`) compiles a new pipeline (seconds on first sight of a structure);
  parameter changes never do.
- `depthPrepass`, `castShadows` (ADR-1002, default true): whether the object is marched again into
  the depth prepass and into the shadow maps. Off saves a full march each; the lit pass still writes
  depth, but prepass readers (GTAO, the screen-space shadow mask, contact shadows) no longer see it.
  The shadow march is the light's (ADR-1160): each shadow view projects its own quad, rays start on the
  view's near plane, and a caster off the camera's screen still casts (a shadow-only item).
- `maxSteps`, `epsilon` (hit threshold, scaled by distance so it is screen-space constant),
  `stepScale` (relaxation; displaced or twisted trees need < 1) and `normalEpsilon` are raymarch
  only. `resolution` is mesh only.

### Structural hash and rebuild

`structuralHash` covers the tree, the bounds, the resolution and the render mode. The transform,
the material and the march settings are per-frame uniforms and are *not* in it.
`rebuild(time, fields)` compares it with `builtHash`: on a change it bumps `structureVersion` and,
in Mesh mode, re-meshes into `mesh` (`meshHash` = the new hash); in Raymarch mode it clears the
mesh. It returns whether anything changed. An animated displacement is frozen at the `time` given
in Mesh mode; Raymarch evaluates the tree every frame on the GPU.

### Parameters

`registerSdfParameters(params, rest, prefix)` registers, all under `prefix` and grouped by it:

| path | type |
|---|---|
| `visible` | bool |
| `transform/position`, `transform/rotation` (degrees), `transform/scale` | vec3 |
| `material/baseColor`, `material/emissiveColor` | colour |
| `material/emissive`, `material/roughness`, `material/metallic` | float |
| `bounds/min`, `bounds/max` | vec3 |
| `resolution` | int |
| `node/<i or name>/<field>` | per node, see below |
| `march/maxSteps` (int), `march/epsilon`, `march/stepScale`, `march/maxDistance` | float (ADR-1002) |
| `look/ao/{strength,distance}`, `look/edge/{intensity,width,color}`, `look/shadow/{strength,softness,direction,steps}` | ADR-1002 |

`<i>` is the node's **1-based pre-order index over enabled and disabled nodes**, so disabling a
node does not renumber its siblings. Only the members a kind uses are registered, plus `enabled`
on every node; the label is `"<kind>/<field>"` (e.g. `smoothUnion/smooth`) while the path stays
`node/1/smooth`. Fields: `radius`, `height`, `size`, `rounding`, `offset`, `translation`,
`rotation`, `scale`, `amount`, `smooth`, `frequency`, `speed`, `enabled`, and (ADR-1001) `axis`
(fold) and `count` (int: repeat, polarRepeat, recurse; a route into it rounds to the nearest). `SdfParameters` also
carries `nodeAmount`, `nodeRadius` and `nodeSmooth` vectors with one slot per node (null where the
kind has no such member) for cheap live modulation.

`applySdfParameters(p, rest, live)` sets `live = rest` (keeping its structure outputs and mesh
cache), copies every final into it — walking both trees in the same pre-order, so kinds and
children always come from `rest` — and returns whether `live`'s structural hash changed. Because
every tree parameter is structural, that is "true whenever a node parameter moved". In **Raymarch**
mode the caller can ignore it beyond bumping the version: the renderer repacks the node buffer
every frame, so node parameters behave as live uniforms. In **Mesh** mode it must call `rebuild`,
which re-meshes; that is the intended cost of modulating a meshed object.

Per frame the composition must, for each SDF object: `params.resetFinals()` as usual, then
`applySdfParameters(p, rest, live)`, then `live.rebuild(time, &scene.fields)` (cheap when nothing
changed), and put `live` in `scene.sdfs` with the parent transform already folded into
`live.transform`. `SceneRenderer::render` does the rest.

### Pass order and depth composition

`SceneRenderer::render` calls `SdfRenderer::update(scene, time, viewProj, fields)` before the lit
pass (packing, mesh uploads, uniform writes), then inside the lit pass, after the opaque entity
meshes and the procedural instances:

1. `SdfRenderer::drawMeshes(pass, scene, materialBindGroup)` — Mesh-mode objects, drawn with
   SceneRenderer's own lit opaque pipelines and object bind-group layout.
2. If `hasRaymarchWork()`, the lit pass **ends** and `SdfRenderer::encodeRaymarchPass` runs its own
   render pass on the same HDR colour and depth (both `Load`/`Store`), then the lit pass is
   re-begun with `Load` for the skybox, grid, particles and blended meshes. A frame with no
   raymarched object is encoded exactly as before, in one pass.

The raymarch pass needs a pass of its own for its timestamps (`SdfStats::raymarchMs`). Depth
composes in both directions because the fragment stage writes `@builtin(frag_depth)`: the hit is
taken to clip space with `frame.viewProj` and `clip.z / clip.w` is written, so an SDF surface is
occluded by (and occludes) meshes, procedural instances, other SDF objects and the particles drawn
afterwards. A miss `discard`s, leaving colour and depth untouched.

Bind groups of the raymarch pipeline (`shaders/sdf_raymarch.wgsl`):

| group | binding | contents |
|---|---|---|
| 0 | 0 | `FrameUniforms` (`common.wgsl`), shared with the entity pipelines |
| 1 | 0 | `ObjectUniforms` — model, normal matrix, material lanes (dynamic offset, 256-byte slots) |
| 1 | 1 | `SdfObjectUniforms` — worldToLocal, bounds, node offset/count/maxSteps, epsilon/stepScale/normalEpsilon/time, the NDC rect (dynamic offset) |
| 1 | 2 | `array<SdfNodeGpu>` read-only storage: every object's packed program, concatenated |
| 1 | 3 | `FieldBlock` (`shaders/fields.wgsl`) for `displaceField` |
| 1 | 4 | `SdfStepStats` (ADR-1002): 8 atomic u32, read-write storage, fragment only |
| 2 | 0-5 | material sampler + textures (`pbr_shade.wgsl`); never sampled here (texture mask 0) |
| 3 | 0-3 | IBL (`pbr_shade.wgsl`) |

Mesh-mode draws use group 1 binding 0 only, with SceneRenderer's `object-layout`, so they are
pipeline-compatible with the entity draws.

Shading: the fragment marches in the object's **local** space (so `epsilon` and `stepScale` are
invariant to a uniform object scale), takes the hit to world space, builds a tetrahedron-difference
normal from `sdfNormal`, flips it towards the eye and calls the shared `shadePbr` (lights, IBL,
emissive, fog) from `pbr_shade.wgsl` — the same function entities and procedural instances use, so
a raymarched and a meshed version of the same tree shade alike. `displaceField` samples
`fieldScalar(slot, worldPos)`, taking the local point to world with the object's model matrix,
because fields live in world space.

Limits: 256 visible SDF objects per frame (256-byte uniform slots; extras are skipped with a
warning), 128 packed records per object, 8 distance and 8 point stack entries (`SdfTree::validate`
guarantees both). An object that fails `validate()` is skipped with a one-time warning; an
off-screen one is skipped silently; an invisible one is not packed at all. Node buffers are packed
fresh every frame (microseconds for a full 128-record program), so nothing has to invalidate them.

### Mesh mode details

`SdfRenderer` keeps one vertex/index buffer pair per object, keyed by name and re-uploaded only
when `meshHash` changes (`SdfStats::meshUploads` counts uploads); buffers unused for 120 frames are
dropped. An object whose mesh is empty (never rebuilt, or the surface lies outside the bounds) is
skipped. Meshed objects draw inside the lit pass and their draws and triangles are folded into
`RenderStats` totals as well as into `RenderStats::sdf`.

### Adding a node kind

Both interpreters must change together or the parity test fails:

1. `spatial::SdfNodeKind` in `src/spatial/sdf.hpp` — append the enum value (the order defines the
   GPU `kind` numbers, and primitives/combinations/unary ops are recognised by range, so add it to
   the right block), and add its name to `kKindNames` in `src/spatial/sdf.cpp`.
2. `src/spatial/sdf.cpp`: the distance in `primitiveDistance`, the fold in `combine`, the point
   warp in `warpPoint` or the term in `displace`/`finishUnary`; `sdfNodeIsPrimitive` /
   `sdfNodeMaxChildren` / `isCombination` / `isUnary` / `isDisplacement`; validation of the members
   it uses; the payload it needs in `packSdfTree` (`p0..p5`).
3. `shaders/sdf.wgsl`: the matching `SDF_*` constant and the same formula in `sdfPrimitive`,
   `sdfCombine`, `sdfWarp` or `sdfFinishUnary`. Watch the WGSL rules: `smooth` and `std` are
   reserved words, there is no ternary (use `select`), `round` is half-to-even (use
   `floor(x + 0.5)`, spelt `sdfRnd`) and `atan2(0, 0)` is undefined (guard it, as `polarRepeat`
   does) — the CPU is the reference in all three cases.
4. `src/scene/sdf_object.cpp`: list the members the kind exposes in `nodeFields`, so they get
   parameters and are copied by `applySdfParameters`.
5. Tests: a case in `tests/unit/test_sdf.cpp`, and one in the parity list of
   `tests/rendering/test_sdf_gpu.cpp` (CPU/GPU within 1e-4, 1e-3 for noise) — plus the JSON name
   in `docs/sdf.md`'s node reference above.

### Step statistics (ADR-1002)

The lit raymarch pass samples every 4th pixel in x and y (every raymarched object) and accumulates
the sum and maximum of the march steps, the ray count, the hits, and the rays that ran out of steps
(neither hit nor left the bounds or `maxDistance`) into a 32-byte storage buffer. The buffer is
cleared before the pass and copied into one of three MapRead slots; `collectTimings` maps and reads
them a few frames late. `SdfStats` carries `sampledRays`, `avgSteps`, `maxSteps`, `hitRatio` and
`exhaustedRatio`; the Render stats lines show them, and `--headless --frames N` prints their medians
("sdf march (median of N frames): ..."). A high `exhaustedRatio` means `maxSteps` is too low for the
view or `stepScale` too small; a high average with a low hit ratio means rays are grazing or
travelling far through empty space (lower `maxDistance`, or tighten the bounds).
