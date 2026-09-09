# The procedural graph

The graph (ADR-028) is an **authoring layer**. Evaluating it emits the flat scene data the engine
already renders — procedural objects, fields, splines, SDF objects, material programs, particle
systems and modulation routes — into a `GraphOutput` that the composition installs. The runtime
never sees the graph: it is evaluated when it changes, not per frame.

```
nodes + links  --evaluate-->  GraphOutput { procedurals, fields, splines, sdfs,
                                            materialPrograms, particles, routes, warnings }
                                   |
                                   +--> composition nodes + parameters --> renderer
```

Headers: `src/graph/graph.hpp`. Implementation: `src/graph/graph.cpp` (structure, hashing,
evaluation, JSON, library), `src/graph/builtin_nodes.cpp` (the node types),
`src/graph/detail.hpp`/`detail.cpp` (internal helpers). Tests: `tests/unit/test_graph.cpp`
(`[graph]`). Examples: `examples/graphs/*.graph.json`.

## 1. Model

- A **node** has a unique `name`, a `type` from the registry, an editor `position`, a `params`
  JSON object, an `enabled` flag and (for subgraphs) a `subgraph` file reference. Its type
  (`NodeTypeInfo`) declares typed input pins, typed output pins and parameter descriptors; its
  behaviour is a `NodeEvaluator`, a pure function of (resolved inputs, params, time) -> outputs.
- A **link** connects `fromNode.fromPin` to `toNode.toPin`. `connect()` refuses unknown nodes or
  pins, incompatible types and cycles. A link into a single input pin replaces the previous one;
  a `multi` pin keeps every link and passes the values **in link order** (which is the order the
  links were made — that order is meaningful for deformer stacks, merges and SDF combinations).
- **Pin types**: `float`, `vec2`, `vec3`, `color`, `transform`, `pointCloud`, `spline`, `field`,
  `mesh`, `sdf`, `material`, `particles`, `volume`, `int`, `bool`, `string`, `any`. Compatible
  means: equal types, `any` on either side, `int` -> `float`, or `float` -> `vec3` (broadcast).
  The value is converted to the destination type when it is linked.
- **Categories**: generators, distributions, spatial, points, attributes, fields, effectors,
  deformers, sdf, materials, particles, volumes, audio, time, math, logic, output, subgraph.
- **Names**: `addNode` derives a node's name from the leaf of its type (`distributions/radial` ->
  `radial`, then `radial2`, `radial3`, ...) and fills `params` with the type's declared defaults.
  Names never contain `.`, `/` or spaces (the link format is `"node.pin"`); offending characters
  are replaced with `_`. `addNode` returns a pointer into `Graph::nodes` that a later
  `addNode`/`removeNode` invalidates — set a node up before adding the next, or use `findNode`.

### Evaluation and invalidation

`Graph::evaluate(output, time, depth)` validates the graph, clears `output`, computes a
topological order (Kahn, stable in node order) and runs the nodes in it.

- `nodeHash(node)` is an FNV-1a over the node's type, name, enabled flag, subgraph reference, its
  whole parameter block and the hashes of its upstream nodes in link order. The name is included
  because emitted objects are named after their node.
- A node runs when it is `dirty` or when its hash changed since the last evaluation. Because
  upstream hashes are folded in, changing one node re-runs it **and everything downstream**;
  siblings keep their cached outputs. `markAllDirty()` forces a full re-run, `dirtyCount()`
  reports how many nodes are pending.
- **Output and subgraph nodes always run**: evaluation is incremental, emission is always
  complete, so `GraphOutput` is a full picture of the graph after every call.
- A **disabled** node does not run; its outputs are its pins' declared defaults, so downstream
  nodes see empty geometry rather than stale data.
- An evaluator failure is collected in `output.warnings` as `node 'x' (type): message`, the
  node's outputs are reset to their defaults, and the rest of the graph still evaluates.
- Modulation of a non-structural parameter never re-evaluates the graph: it flows through the
  emitted objects' own parameters (see §5).
- Time-dependent nodes (`time/*`, `fields/sample`, `effectors/effector`) are evaluated at the
  time passed to `evaluate`. The graph is not re-evaluated per frame; call `markAllDirty()`
  first when cached values must follow a new time.

### The evaluator input encoding

`NodeEvaluator` receives `inputs` flattened in input-pin order:

- a single pin contributes exactly one value — the linked value, or the pin's declared default
  when nothing is linked (pins whose default is empty are how an evaluator tells "unlinked");
- a `multi` pin contributes its linked values in link order followed by one `std::monostate`
  terminator, so several multi pins on one node stay unambiguous.

`graph::detail::Inputs` decodes it (`single(pin)`, `multi(pin)`, `has(pin)` and the typed
accessors); `graph::detail::Params` reads parameters, falling back to the registry descriptor's
default. Enum parameters are `int` with `choices` labels, and a **string matching a label is
accepted too**, so hand-written graph files stay readable (`"kind": "cylinder"`).

## 2. Three encodings the header cannot express

**(a) Deformers are JSON on an `any` pin.** Every `deformers/*` node outputs a `PinType::Any`
value holding the JSON text of a `scene::Deformer`, using the member names of
`scene/procedural.cpp`'s deformer block (`kind`, `enabled`, `amount`, `space`, `speed`, `phase`,
`axis`, `center`, `falloff`, `frequency`, `displacementAxis`, `scale`, `seed`, `axisMask`,
`pattern`, `field`, `alongNormal`) plus the Path fields `spline`, `pathOffset`, `pathScale` and
`pathRoll`. `output/procedural` collects them from its multi input `deformers` in link order,
parses them and fills the emitted stack (at most `scene::kMaxDeformers` = 8; the rest is dropped
with a warning). Deformers that name a field or a spline take the name from a linked
`fields/*` / `generators/spline` node — emit that field or spline as well (`output/field`,
`output/spline`) so the renderer can bind it.

**(b) Effectors come in two flavours.** `effectors/effector` applies its field to a point cloud
**on the CPU at evaluation time** (static shaping: the result is baked into the cloud). Live GPU
effectors are per-frame motion and are declared on `output/procedural`: its multi `fields` input
names the fields (which are emitted with the object so the renderer can bind them by name) and
its `effectors` parameter is a JSON array whose entry *i* configures linked field *i*:

```json
[{"op": "positionOffset", "blend": "add", "strength": 1.5, "weight": 1.0,
  "axis": [0, 1, 0], "scaleAxis": [1, 1, 1], "enabled": true, "target": ""}]
```

**(c) `output/procedural` prefers a Distribution spec.** Every `distributions/*` node exposes two
outputs: `points` (the evaluated cloud, for the points/attributes/effector operators) and `spec`,
a `string` holding its `scene::Distribution` as JSON (the member names of `scene/procedural.cpp`
plus the spline fields). When `spec` is linked, the emitted object uses that distribution — the
renderer keeps generating placements procedurally, and `distribution/*` parameters stay
modulatable. When only `points` is linked, the cloud is arbitrary (it went through operators),
so it is emitted as a **grammar of Place rules** instead: one `Place` per point carrying its
transform, branched from a `root` rule, with `distribution.kind = Grammar`. That path is bounded
at 4096 placements; a larger cloud is truncated with a warning. With neither linked the node
warns and emits the default radial distribution.

## 3. Output naming and parameter paths

- `output/procedural`, `output/sdf`, `output/particles`, `output/material`, `output/volume` name
  what they emit after their `name` parameter, or after the output node itself when it is empty.
- `output/field` and `output/spline` keep the **incoming value's** name (a `fields/*` node names
  its field after itself, `generators/spline` likewise) unless `name` is set, so the names that
  deformers, effectors, compound fields and SDF displacements reference stay valid.
- Emitted parameters follow the engine's existing paths: `procedural/<name>/...`,
  `field/<name>/...`, `spline/<name>/...`, `sdf/<name>/...`, `particles/<name>/...`,
  `material/<name>/...`.
- Fields and material programs are emitted **by name**: emitting the same name twice replaces the
  earlier one instead of duplicating it (a field can be linked both to `output/field` and to an
  `output/procedural` `fields` pin).

## 4. Signals and modulation routes

`audio/signal` and `time/beatPhase` exist for routing. They carry a static float (their `value`
parameter) so they can also feed the math nodes.

- A **direct link from a signal node into a parameter pin of an Output node** emits a
  `params::ModRoute` from the signal to the emitted parameter and leaves the static value alone
  (the route modulates whatever the spec and parameters produced).
- The route's target is `<kind>/<emitted name>/<sub-path>`, where the sub-path is the signal's
  `target` parameter when set, otherwise the pin's default mapping:

| output node | parameter pin | parameter sub-path |
|---|---|---|
| `output/procedural` | `emissive` | `material/emissive` |
| `output/procedural` | `roughness` | `material/roughness` |
| `output/procedural` | `metallic` | `material/metallic` |
| `output/procedural` | `hueShift` | `materialVariation/hueShift` |
| `output/procedural` | `radius` | `distribution/radius` |
| `output/procedural` | `emissiveFieldAmount` | `emissiveFieldAmount` |
| `output/field` | `strength`, `frequency`, `radius`, `speed`, `amplitude` | the same name |
| `output/spline` | `radius`, `turns`, `height`, `noiseAmount` | the same name |
| `output/sdf` | `emissive` | `material/emissive` |
| `output/particles` | `spawnRate`, `emissive`, `size` | the same name |
| any output node | `modulation` (multi) | the signal's `target` (required) |

  So `audio.bass -> columns.emissive` on an object named `columns` emits
  `audio.bass -> procedural/columns/material/emissive`. The route also carries the signal node's
  `amount`, `op` (add/multiply/replace/min/max), `polarity` and `component`.
- A **non-signal** value linked into a parameter pin sets the emitted value statically instead
  (e.g. `math/remap -> output/field.strength`). Route a signal *through* a math node when you
  want the value baked rather than modulated.

## 5. Subgraphs and recursion

`subgraph/instance` loads the graph file named by the node's `subgraph` reference, resolved
against (1) the path as given, (2) the directory of the graph most recently loaded from a file
(saved and restored around nested evaluation), (3) the directories last passed to
`scanGraphLibrary`; `.graph.json` is appended when the reference has no extension.

- The file's `exposed` list maps an exposed parameter name to an inner node's parameter. The
  instance's own `params` supply the values: a key matching an exposed name is written into the
  inner node before evaluation, so the instance's parameters are the subgraph's interface
  (`graph/<instance>/<param>` in the editor).
- The subgraph is evaluated at `depth + 1` (`Graph::kMaxSubgraphDepth` = 4) and its emissions are
  forwarded with every name prefixed `"<instance>_"`. References between emitted objects (field
  names, spline names, material programs, SDF displacement references, route targets) are
  rewritten with the same prefix, so a subgraph is self-contained.
- **Recursion** is a graph instantiating itself: the expansion stops silently when the next level
  would exceed the depth limit, so a self-referencing graph emits one object per level
  (`kMaxSubgraphDepth + 1` in total) and terminates deterministically.
- Subgraph files are loaded on every evaluation (no cache); depth and instance counts are small
  by construction.

## 6. Node reference

Pins are listed as `name` type; `(multi)` marks an input that accepts several links. Enum
parameters list their labels.

### generators

| type | inputs | outputs | parameters |
|---|---|---|---|
| `generators/grammar` | - | `points` pointCloud | `grammar`, `axiom`, `maxDepth`, `maxInstances`, `seed` |
| `generators/points` | - | `points` pointCloud | `count`, `seed`, `position` |
| `generators/primitive` | - | `mesh` mesh | `kind` (box\|cylinder\|sphere\|torus\|point\|procedural), `size`, `subdivisions`, `radius`, `height`, `radialSegments`, `heightSegments`, `caps`, `segments`, `rings`, `majorRadius`, `minorRadius`, `majorSegments`, `minorSegments`, `pointSize`, `reference`, `position`, `rotation`, `scale` |
| `generators/sdfPrimitive` | - | `sdf` sdf | `kind` (sphere\|box\|roundedBox\|cylinder\|capsule\|torus\|plane\|cone), `radius`, `height`, `size`, `rounding`, `axis`, `offset` |
| `generators/spline` | - | `spline` spline | `generator` (points\|line\|circle\|spiral\|helix\|bezier\|noise), `kind` (polyline\|catmullRom\|bezier\|hermite), `closed`, `count`, `start`, `end`, `radius`, `radiusGrowth`, `turns`, `height`, `axis`, `center`, `startAngle`, `p0`, `p1`, `p2`, `p3`, `noiseAmount`, `noiseScale`, `seed`, `tension`, `samplesPerSegment`, `up` |

### distributions

| type | inputs | outputs | parameters |
|---|---|---|---|
| `distributions/spline` | `spline` spline | `points` pointCloud, `spec` string | `count`, `spacing`, `splineStart`, `splineEnd`, `alignToSpline`, `roll`, `splineOffset` |
| `distributions/grid` | - | `points` pointCloud, `spec` string | `count`, `center`, `orientation` (none\|outward\|inward\|tangent), `gridCount`, `gridSpacing` |
| `distributions/linear` | - | `points` pointCloud, `spec` string | `count`, `center`, `orientation` (none\|outward\|inward\|tangent), `start`, `end`, `orientAlong`, `spacing` |
| `distributions/radial` | - | `points` pointCloud, `spec` string | `count`, `center`, `orientation` (none\|outward\|inward\|tangent), `radius`, `startAngle`, `endAngle`, `plane` (xz\|xy\|yz) |
| `distributions/spiral` | - | `points` pointCloud, `spec` string | `count`, `center`, `orientation` (none\|outward\|inward\|tangent), `radius`, `radiusGrowth`, `turns`, `spiralHeight`, `spiralAngle`, `plane` (xz\|xy\|yz) |

### points

| type | inputs | outputs | parameters |
|---|---|---|---|
| `points/duplicate` | `points` pointCloud | `points` pointCloud | `enabled`, `copies`, `offset`, `axis`, `angle`, `factor` |
| `points/filterAttribute` | `points` pointCloud | `points` pointCloud | `enabled`, `attribute`, `value`, `compare` (less\|lessEqual\|equal\|greaterEqual\|greater\|notEqual), `invert` |
| `points/filterBounds` | `points` pointCloud | `points` pointCloud | `enabled`, `boundsMin`, `boundsMax`, `invert` |
| `points/filterDensity` | `points` pointCloud | `points` pointCloud | `enabled`, `threshold`, `probabilistic`, `invert`, `seed` |
| `points/filterDistance` | `points` pointCloud | `points` pointCloud | `enabled`, `pivot`, `minDistance`, `maxDistance`, `invert` |
| `points/filterProbability` | `points` pointCloud | `points` pointCloud | `enabled`, `probability`, `seed`, `invert` |
| `points/merge` | `points` pointCloud (multi) | `points` pointCloud | - |
| `points/noise` | `points` pointCloud | `points` pointCloud | `enabled`, `frequency`, `amount`, `offset`, `axisMask`, `seed` |
| `points/randomize` | `points` pointCloud | `points` pointCloud | `enabled`, `randomPosition`, `randomRotation`, `randomScale`, `randomUniformScale`, `seed`, `amount` |
| `points/rotate` | `points` pointCloud | `points` pointCloud | `enabled`, `axis`, `angle`, `pivot`, `amount` |
| `points/sample` | `points` pointCloud | `points` pointCloud | `enabled`, `stride`, `count`, `start` |
| `points/scale` | `points` pointCloud | `points` pointCloud | `enabled`, `factor`, `pivot`, `scaleInstances` |
| `points/scatter` | `points` pointCloud | `points` pointCloud | `enabled`, `range`, `seed`, `amount` |
| `points/sort` | `points` pointCloud | `points` pointCloud | `enabled`, `attribute`, `component`, `descending` |
| `points/transform` | `points` pointCloud | `points` pointCloud | `enabled`, `position`, `rotationDegrees`, `scale` |
| `points/translate` | `points` pointCloud | `points` pointCloud | `enabled`, `offset`, `amount` |

### attributes

| type | inputs | outputs | parameters |
|---|---|---|---|
| `attributes/op` | `points` pointCloud | `points` pointCloud | `kind` (set\|add\|multiply\|remap\|clamp\|normalize\|smooth\|noise\|randomize\|lerp\|fit\|threshold\|compare), `enabled`, `target`, `source`, `secondSource`, `positionAttribute`, `value`, `amount`, `inMin`, `inMax`, `outMin`, `outMax`, `clamp`, `scale`, `offset`, `range`, `seed`, `radius`, `compare` (less\|lessEqual\|equal\|greaterEqual\|greater\|notEqual), `idAttribute` |
| `attributes/stats` | `points` pointCloud | `min` vec3, `max` vec3, `mean` vec3, `count` int | `attribute` |

### fields

| type | inputs | outputs | parameters |
|---|---|---|---|
| `fields/color` | - | `field` field | `kind` (constantColor\|gradient\|radialGradient\|noiseColor\|positionColor), `enabled`, `space` (world\|local), `position`, `rotation`, `scale`, `strength`, `invert`, `falloffKind` (none\|linear\|smoothstep\|smooth\|easeIn\|easeOut\|easeInOut\|exponential\|customCurve\|noiseModulated), `falloffInner`, `falloffOuter`, `falloffExponent`, `speed`, `phase`, `axis`, `point`, `radius`, `length`, `size`, `softness`, `frequency`, `seed`, `reference`, `colorA`, `colorB` |
| `fields/compound` | `fields` field (multi) | `field` field | `kind` (compound), `enabled`, `space` (world\|local), `position`, `rotation`, `scale`, `strength`, `invert`, `falloffKind` (none\|linear\|smoothstep\|smooth\|easeIn\|easeOut\|easeInOut\|exponential\|customCurve\|noiseModulated), `falloffInner`, `falloffOuter`, `falloffExponent`, `speed`, `phase`, `axis`, `point`, `radius`, `length`, `size`, `softness`, `frequency`, `seed`, `reference`, `combine` (add\|multiply\|max\|min\|mix\|average), `mix` |
| `fields/sample` | `field` field, `position` vec3 | `scalar` float, `vector` vec3, `color` color | - |
| `fields/scalar` | - | `field` field | `kind` (constant\|linearGradient\|radial\|box\|sphere\|plane\|noise\|voronoi\|distance\|sdfDistance), `enabled`, `space` (world\|local), `position`, `rotation`, `scale`, `strength`, `invert`, `falloffKind` (none\|linear\|smoothstep\|smooth\|easeIn\|easeOut\|easeInOut\|exponential\|customCurve\|noiseModulated), `falloffInner`, `falloffOuter`, `falloffExponent`, `speed`, `phase`, `axis`, `point`, `radius`, `length`, `size`, `softness`, `frequency`, `seed`, `reference` |
| `fields/vector` | - | `field` field | `kind` (direction\|radialVector\|attractor\|repulsor\|vortex\|curlNoise\|spiral), `enabled`, `space` (world\|local), `position`, `rotation`, `scale`, `strength`, `invert`, `falloffKind` (none\|linear\|smoothstep\|smooth\|easeIn\|easeOut\|easeInOut\|exponential\|customCurve\|noiseModulated), `falloffInner`, `falloffOuter`, `falloffExponent`, `speed`, `phase`, `axis`, `point`, `radius`, `length`, `size`, `softness`, `frequency`, `seed`, `reference`, `spiralBias` |
| `fields/wave` | - | `field` field | `kind` (wave\|waveVector), `enabled`, `space` (world\|local), `position`, `rotation`, `scale`, `strength`, `invert`, `falloffKind` (none\|linear\|smoothstep\|smooth\|easeIn\|easeOut\|easeInOut\|exponential\|customCurve\|noiseModulated), `falloffInner`, `falloffOuter`, `falloffExponent`, `speed`, `phase`, `axis`, `point`, `radius`, `length`, `size`, `softness`, `frequency`, `seed`, `reference`, `waveGeometry` (planar\|radial\|spherical\|cylindrical), `waveShape` (sine\|pulse\|triangle), `amplitude`, `wavelength`, `waveSpeed`, `waveWidth`, `waveOrigin` |

### effectors

| type | inputs | outputs | parameters |
|---|---|---|---|
| `effectors/effector` | `points` pointCloud, `field` field | `points` pointCloud | `op` (positionOffset\|scale\|rotation\|velocity\|color\|emission\|density\|attribute), `blend` (add\|multiply\|replace\|min\|max\|mix), `enabled`, `strength`, `weight`, `axis`, `scaleAxis`, `target` |

### deformers

| type | inputs | outputs | parameters |
|---|---|---|---|
| `deformers/bend` | - | `deformer` any | `enabled`, `amount`, `space` (local\|world), `speed`, `phase`, `axis`, `center`, `falloff`, `displacementAxis` |
| `deformers/displacement` | - | `deformer` any | `enabled`, `amount`, `space` (local\|world), `speed`, `phase`, `axis`, `center`, `falloff`, `scale`, `seed`, `pattern` |
| `deformers/field` | `field` field | `deformer` any | `enabled`, `amount`, `space` (local\|world), `speed`, `phase`, `axis`, `center`, `falloff`, `alongNormal`, `field` |
| `deformers/noise` | - | `deformer` any | `enabled`, `amount`, `space` (local\|world), `speed`, `phase`, `axis`, `center`, `falloff`, `scale`, `seed`, `axisMask` |
| `deformers/path` | `spline` spline | `deformer` any | `enabled`, `amount`, `space` (local\|world), `speed`, `phase`, `axis`, `center`, `falloff`, `spline`, `pathOffset`, `pathScale`, `pathRoll` |
| `deformers/sine` | - | `deformer` any | `enabled`, `amount`, `space` (local\|world), `speed`, `phase`, `axis`, `center`, `falloff`, `frequency`, `displacementAxis` |
| `deformers/twist` | - | `deformer` any | `enabled`, `amount`, `space` (local\|world), `speed`, `phase`, `axis`, `center`, `falloff` |

### sdf

| type | inputs | outputs | parameters |
|---|---|---|---|
| `sdf/difference` | `sdf` sdf (multi) | `sdf` sdf | - |
| `sdf/intersection` | `sdf` sdf (multi) | `sdf` sdf | - |
| `sdf/bend` | `sdf` sdf | `sdf` sdf | `amount` |
| `sdf/displace` | `sdf` sdf, `field` field | `sdf` sdf | `mode` (noise\|voronoi\|wave\|field), `amount`, `frequency`, `speed`, `seed`, `axis`, `reference` |
| `sdf/mirror` | `sdf` sdf | `sdf` sdf | `size` |
| `sdf/polarRepeat` | `sdf` sdf | `sdf` sdf | `count` |
| `sdf/repeat` | `sdf` sdf | `sdf` sdf | `size`, `count` |
| `sdf/transform` | `sdf` sdf | `sdf` sdf | `translation`, `rotation`, `scale` |
| `sdf/twist` | `sdf` sdf | `sdf` sdf | `amount` |
| `sdf/smoothDifference` | `sdf` sdf (multi) | `sdf` sdf | `smooth` |
| `sdf/smoothIntersection` | `sdf` sdf (multi) | `sdf` sdf | `smooth` |
| `sdf/smoothUnion` | `sdf` sdf (multi) | `sdf` sdf | `smooth` |
| `sdf/union` | `sdf` sdf (multi) | `sdf` sdf | - |

### materials

| type | inputs | outputs | parameters |
|---|---|---|---|
| `materials/material` | - | `material` material | `baseColor`, `opacity`, `emissiveColor`, `emissiveIntensity`, `roughness`, `metallic`, `doubleSided`, `unlit`, `program` |
| `materials/fromColor` | `color` color, `emissive` float | `material` material | `roughness`, `metallic` |
| `materials/program` | `material` material | `material` material | `name`, `program` |

### particles

| type | inputs | outputs | parameters |
|---|---|---|---|
| `particles/system` | `field` field, `spline` spline | `particles` particles | `enabled`, `capacity`, `seed`, `shape` (point\|sphere\|disc\|box\|spline), `position`, `extent`, `spawnRate`, `lifetimeMin`, `lifetimeMax`, `direction`, `spread`, `speedMin`, `speedMax`, `gravity`, `drag`, `turbulence`, `turbulenceScale`, `turbulenceSpeed`, `sizeStart`, `sizeEnd`, `colorStart`, `colorEnd`, `emissive`, `blend` (additive\|alpha), `softness`, `fieldMode` (force\|velocity\|turbulence\|kill), `fieldStrength` |

### volumes

| type | inputs | outputs | parameters |
|---|---|---|---|
| `volumes/fog` | `field` field | `volume` volume | `density`, `field` |

### audio

| type | inputs | outputs | parameters |
|---|---|---|---|
| `audio/signal` | - | `value` float | `signal`, `target`, `amount`, `op` (add\|multiply\|replace\|min\|max), `polarity` (unipolar\|bipolar), `component`, `value` |

### time

| type | inputs | outputs | parameters |
|---|---|---|---|
| `time/beatPhase` | - | `value` float | `signal`, `target`, `amount`, `op` (add\|multiply\|replace\|min\|max), `polarity` (unipolar\|bipolar), `component`, `value` |
| `time/time` | - | `time` float | `scale`, `offset` |

### math

| type | inputs | outputs | parameters |
|---|---|---|---|
| `math/abs` | `a` any | `result` any | `a` |
| `math/add` | `a` any, `b` any | `result` any | `a`, `b` |
| `math/clamp` | `value` any, `min` any, `max` any | `result` any | `value`, `min`, `max` |
| `math/color` | - | `color` color | `color` |
| `math/constant` | - | `value` float | `value` |
| `math/cos` | `a` any | `result` any | `a` |
| `math/cross` | `a` any, `b` any | `result` vec3 | `a`, `b` |
| `math/divide` | `a` any, `b` any | `result` any | `a`, `b` |
| `math/dot` | `a` any, `b` any | `result` float | `a`, `b` |
| `math/floor` | `a` any | `result` any | `a` |
| `math/fract` | `a` any | `result` any | `a` |
| `math/length` | `a` any | `result` float | `a` |
| `math/max` | `a` any, `b` any | `result` any | `a`, `b` |
| `math/min` | `a` any, `b` any | `result` any | `a`, `b` |
| `math/mix` | `a` any, `b` any, `t` float | `result` any | `a`, `b`, `t` |
| `math/multiply` | `a` any, `b` any | `result` any | `a`, `b` |
| `math/normalize` | `a` any | `result` vec3 | `a` |
| `math/pow` | `a` any, `b` any | `result` any | `a`, `b` |
| `math/random` | `index` int | `value` float | `seed`, `index`, `min`, `max`, `channel` |
| `math/remap` | `value` any | `result` any | `value`, `inMin`, `inMax`, `outMin`, `outMax`, `clamp` |
| `math/sin` | `a` any | `result` any | `a` |
| `math/smoothstep` | `edge0` any, `edge1` any, `value` any | `result` any | `edge0`, `edge1`, `value` |
| `math/split` | `value` any | `x` float, `y` float, `z` float, `w` float | - |
| `math/subtract` | `a` any, `b` any | `result` any | `a`, `b` |
| `math/vec3` | `x` float, `y` float, `z` float | `result` vec3 | `x`, `y`, `z` |

### logic

| type | inputs | outputs | parameters |
|---|---|---|---|
| `logic/and` | `a` bool, `b` bool | `result` bool | `a`, `b` |
| `logic/compare` | `a` any, `b` any | `result` bool | `a`, `b`, `compare` (less\|lessEqual\|equal\|greaterEqual\|greater\|notEqual) |
| `logic/not` | `a` bool | `result` bool | `a` |
| `logic/or` | `a` bool, `b` bool | `result` bool | `a`, `b` |
| `logic/select` | `condition` bool, `a` any, `b` any | `result` any | `condition` |
| `logic/switch` | `index` int, `values` any (multi) | `result` any | `index` |

### output

| type | inputs | outputs | parameters |
|---|---|---|---|
| `output/field` | `field` field, `strength` float, `frequency` float, `radius` float, `speed` float, `amplitude` float, `modulation` float (multi) | - | `name` |
| `output/material` | `material` material, `modulation` float (multi) | - | `name` |
| `output/particles` | `particles` particles, `spawnRate` float, `emissive` float, `size` float, `modulation` float (multi) | - | `name` |
| `output/procedural` | `source` mesh, `points` pointCloud, `spec` string, `material` material, `emissive` float, `roughness` float, `metallic` float, `hueShift` float, `radius` float, `emissiveFieldAmount` float, `deformers` any (multi), `fields` field (multi), `modulation` float (multi) | - | `name`, `visible`, `position`, `rotation`, `scale`, `seed`, `randomPosition`, `randomRotation`, `randomScale`, `randomUniformScale`, `hueShift`, `hueGradient`, `valueRandom`, `emissiveRandom`, `emissiveGradient`, `extraLane`, `emissiveField`, `emissiveFieldAmount`, `effectors` |
| `output/sdf` | `sdf` sdf, `material` material, `emissive` float, `modulation` float (multi) | - | `name`, `visible`, `position`, `rotation`, `scale`, `renderMode` (raymarch\|mesh), `boundsMin`, `boundsMax`, `resolution` |
| `output/spline` | `spline` spline, `radius` float, `turns` float, `height` float, `noiseAmount` float, `modulation` float (multi) | - | `name` |
| `output/volume` | `volume` volume, `field` field, `modulation` float (multi) | - | `name`, `density` |

### subgraph

| type | inputs | outputs | parameters |
|---|---|---|---|
| `subgraph/instance` | - | - | - |

## 7. File format

A graph is JSON with `format: "avgen-graph"` and `version: 1`:

```json
{
  "format": "avgen-graph",
  "version": 1,
  "name": "Radial Columns",
  "description": "A ring of columns.",
  "nodes": [
    {"name": "ring", "type": "distributions/radial", "position": [-360, 100],
     "params": {"count": 16, "radius": 8.0, "plane": "xz"}, "subgraph": "", "enabled": true}
  ],
  "links": [{"from": "ring.spec", "to": "columns.spec"}],
  "exposed": [{"name": "count", "node": "ring", "param": "count"}]
}
```

- `params` is free-form JSON per node; missing keys fall back to the node type's declared
  defaults, so hand-written files only need what they change.
- Link endpoints are `"node.pin"` (node names never contain `.`).
- `fromJson` rejects a wrong format, a newer version, malformed nodes or links, and any graph
  that fails `validate()` (unknown types, duplicate names, dangling links, type mismatches,
  cycles, exposed parameters that do not resolve). `saveFile`/`loadFile` are the file wrappers;
  `loadFile` also records the file's directory for subgraph resolution.
- Unknown top-level keys are ignored, which is how the library's `category` field travels.

## 8. The graph library

`scanGraphLibrary(dirs)` lists every `*.graph.json` in the given directories (non-recursive):
`name`, `category` and `description` are read from the file (falling back to the file name),
`path` is the file, and `thumbnail` is the same stem with `.png` (`x.graph.png`, else `x.png`)
when it exists. Entries are sorted by category, then name, then path. Scanning also registers the
directories as search roots for subgraph references.

`examples/graphs/` is the starter library:

| file | what it shows |
|---|---|
| `radial-columns.graph.json` | primitive + radial distribution + material, exposing `count`, `radius`, `height` — the subgraph example |
| `spiral-tower.graph.json` | a spiral distribution with a twist deformer and an emissive material |
| `ring-stack.graph.json` | a ring duplicated on the point cloud and emitted as a grammar of placements |

## 9. Limits

| limit | value | where |
|---|---|---|
| subgraph nesting | 4 (`Graph::kMaxSubgraphDepth`) | `evaluate` fails deeper; recursion stops silently at the limit |
| deformers per object | 8 (`scene::kMaxDeformers`) | extra deformer links are dropped with a warning |
| GPU effectors per object | 8 (`scene::kMaxEffectors`) | extra `fields` links are dropped with a warning |
| compound field children | 4 (`spatial::kMaxCompoundChildren`) | extra links are dropped |
| grammar placements from a cloud | 4096 | larger clouds are truncated with a warning |
| SDF tree | 64 nodes, depth 8 | `SdfTree::validate` (the failure becomes a warning) |
| node hash recursion | depth 64 | cycle guard; `validate()` reports the cycle |

## 10. What the composition integration needs

`GraphOutput` is data only. Installing it means, per evaluation:

1. Removing the composition nodes and parameters a previous evaluation installed (graphs own
   their objects: hand edits to emitted data are overwritten).
2. Adding one `CompositionNode` per emitted object: `NodeKind::Procedural` from `procedurals`,
   `Field` from `fields`, `Spline` from `splines`, `Sdf` from `sdfs`, `Particles` from
   `particles`, plus `Composition::addMaterialProgram` for `materialPrograms`. The node's name is
   the emitted object's name, which is what the emitted route targets already assume.
3. Registering the usual parameters (`registerProceduralParameters` and friends) and adding
   `output.routes` to the `Modulator` after `bind()`, so the emitted routes resolve against the
   parameters just registered.
4. Surfacing `output.warnings` in the UI and the log.

Two things are not yet expressible: `GraphOutput` has no volume list (`output/volume` emits its
density field only), and the graph itself is not part of the scene file yet — a scene is either
graph-driven or flat (ADR-028).
