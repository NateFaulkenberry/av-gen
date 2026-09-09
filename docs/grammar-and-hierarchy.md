# Compositional grammar and hierarchical instancing (user guide)

Two ways to build a lot of structure from a little data, both pure, deterministic and
CPU-cheap:

- a **grammar** (brief §13, ADR-028) rewrites named rules into a set of placements — the
  `grammar` distribution kind of a procedural object;
- a **hierarchy** (ADR-029) composes placements with themselves (`hierarchy.recursionDepth`)
  or with another object's placements (`source.kind = "procedural"`).

Both feed the same pipeline as every other distribution, so variation, point ops, deformers,
effectors, fields, materials, parameters, presets and the timeline all work unchanged.

```
grammar.expand()  ─┐
distribution      ─┴→ placements → hierarchy (self / referenced object) → variation → ops → records
     CPU, pure                     CPU, pure                CPU (seeded)    CPU      GPU
```

Headers: `src/scene/grammar.hpp` (`avgen::scene::Grammar`, `GrammarRule`, `GrammarOp`) and
`src/scene/procedural.hpp` (`HierarchySpec`, `SourceSpec::reference`, `GenerationContext`).
Implementations: `src/scene/grammar.cpp`, `src/scene/hierarchy.cpp`.

---

## Part 1 — the grammar

A grammar is an `axiom` (the name of the rule to start from) and a list of named `rules`.
`expand()` walks the rules depth first from the axiom with the identity transform and returns a
point cloud: one point per `place`, in emission order.

Every rule carries two transforms. `pre` is applied **once**, before the rule does anything
(`T' = T * pre`) — the rule's own offset. `step` is applied **cumulatively** by the repeating
ops. Composition is the usual `outer * inner`: offsets add in the outer frame, rotations
compound, scales multiply.

| `op` | Meaning |
|---|---|
| `place` | emits one point at `T'`, with the point scale multiplied by `scaleAttribute` |
| `repeat` | repetition `k = 0 … count-1` at `T_k = T' * step^k`; expands **every** child at each `T_k` |
| `alternate` | the same walk, but expands only `children[k % n]` at step `k` |
| `branch` | expands every child at `T'` (no repetition) — the "and" of a grammar |
| `mirror` | expands the children as they are, then again reflected across the plane through the **grammar origin** with normal `mirrorAxis` |
| `choice` | expands exactly one child, picked by a seeded hash of the expansion path, weighted by `weights` |
| `conditional` | expands `children` while `depth < depthLimit`, otherwise `elseChildren` |

Recursion is a rule naming an ancestor. Expansion stops when the depth of a rule visit exceeds
`maxDepth` (so the recursion terminates without any cycle analysis) and truncates in emission
order at `maxInstances` (hard cap 1,048,576).

### Rule fields

| Field | Used by | Default | Meaning |
|---|---|---|---|
| `name` | all | — | unique; children reference rules by name |
| `op` | all | `place` | one of the seven names above |
| `count` | `repeat`, `alternate` | 4 | repetitions; 0 emits nothing |
| `step` | `repeat`, `alternate` | identity | `{ position, rotation (Euler degrees), scale }`, applied cumulatively |
| `pre` | all | identity | applied once before the children (including before `place`) |
| `children` | all but `place` | `[]` | rule names |
| `weights` | `choice` | `[]` | non-negative; missing entries count as 1; weight 0 is never picked; an all-zero list means equal weights |
| `elseChildren` | `conditional` | `[]` | expanded when `depth >= depthLimit` |
| `depthLimit` | `conditional` | 3 | the depth the condition compares against |
| `mirrorAxis` | `mirror` | `[1, 0, 0]` | plane normal, normalised on use |
| `seed` | `choice` | 0 | mixed with the grammar seed |
| `scaleAttribute` | `place` | 1 | multiplies the emitted point's scale |

Grammar fields: `axiom`, `rules`, `maxDepth` (default 8, at most 64), `maxInstances`
(default 100000) and `seed` (default 1). Missing JSON members take these defaults.

### Emitted attributes

Beside the core columns (`position`, `rotation`, `scale`, `id` = emission order, `seed`,
`index`, `color`, `emissive`, `density`, `bounds`), `expand()` writes three Int columns:

| Column | Meaning |
|---|---|
| `depth` | the depth the point was emitted at (the axiom is depth 0) |
| `rule` | the index of the emitting rule in `rules` |
| `branch` | the repetition index `k` of the nearest enclosing `repeat`/`alternate` (0 when there is none) |

They survive into the object's cloud, so point ops, the `extraLane` projection and effectors can
select by storey, by rule or by bay.

### Determinism

`expand()` is a pure function of the grammar — no clock, no call order, no global state. Two
expansions of the same grammar produce byte-identical clouds; the same grammar in a different
project produces the same cloud.

`choice` picks `u = pcg3d(grammar.seed ^ rule.seed, depth, path).x / 2^32` and walks the
cumulative weights. `path` is a **cumulative hash of the expansion path**: it starts at
`pcg3d(seed, 0x9E3779B9, 0).x` for the axiom and, when descending into child `c` at repetition
`k` of rule `r`, becomes `pcg3d(path, r * 65599 + c, k).x` (`k` is 0 for ops without
repetitions, and 1 for the mirrored copy of a `mirror`). Consequences worth relying on:

- the same rule reached through two different branches, or at two different repetitions of the
  same `repeat`, chooses **independently** — a row of 200 `choice` bays is 200 fresh dice;
- changing `grammar.seed` (or a rule's `seed`) reshuffles every choice at once;
- nothing else changes a choice: adding a rule after the ones in use, or editing an unrelated
  subtree, leaves the picks alone as long as the rule indices and the path are unchanged.

### Mirror

`mirror` reflects the *whole mirrored subtree* about the plane through the grammar origin with
unit normal `n = normalize(mirrorAxis)`, not about the rule's local frame. For every emitted
transform:

- position: `p → p - 2 n (n · p)`;
- rotation: conjugated by the reflection, `R M R`, which on the quaternion `(w, v)` is
  `(w, 2 n (n · v) - v)` — the axis is mirrored and the angle negated, so the result is still a
  proper rotation (determinant +1) and instances are not turned inside out;
- **scale stays positive**: the source mesh is never mirrored, only placed. A mirrored copy of a
  chiral shape is the same shape, moved — if you need a genuinely mirrored mesh, model both.

Nested mirrors apply innermost first.

---

## Part 2 — the hierarchy

### Self-recursion

`hierarchy.recursionDepth = d` makes the object a fractal of its own arrangement.

Let `B_i` be the flat placements (the distribution's, or the grammar expansion's, points with
this object's variation applied — the `distributionTransform` is *not* part of `B`). Let `L` be
the per-level transform built from `offsetPerLevel`, `rotationPerLevel` (Euler degrees) and the
uniform `scalePerLevel`. Then

```
level 0 = { B_i }
level k = { B_i * L * q  :  q in level k-1 }     (i-major order)
```

and the object's cloud is **level d only**: every point is a chain of `d + 1` placements
`B_i0 · L · B_i1 · L · … · B_id`, and there are `n^(d+1)` of them (`n` = the flat count). The
`distributionTransform` is applied once, at the end, to every point.

Level d only, not the union of every level: an object draws one mesh per point, and the
intermediate levels are the same chains cut short. So a "tree" that shows the trunk *and* the
branches *and* the twigs is either several objects (one at depth 0, one at depth 1, one at
depth 2) or a `branch` grammar, which emits at every level by construction. Self-recursion here
is the **fractal** form: a structure made of smaller copies of its own arrangement.

Truncation is exact and in emission order: point `(i, j)` of level k has id `i * |level k-1| + j`,
so keeping the first M points of each level keeps exactly the first M chains. The cloud is cut at
`min(hierarchy.maxInstances, 1048576)`.

| Field | Default | Meaning |
|---|---|---|
| `recursionDepth` | 0 | 0 … 4 (`kMaxHierarchyDepth`); 0 = no recursion, and then the cloud is bit-identical to the flat one |
| `maxInstances` | 100000 | truncation cap, 1 … 1048576 |
| `scalePerLevel` | 0.5 | uniform scale per level, > 0 |
| `offsetPerLevel` | `[0, 0, 0]` | offset per level, in the parent placement's frame |
| `rotationPerLevel` | `[0, 0, 0]` | Euler degrees per level |
| `colorPerLevel` | true | see below |

**Colour.** A chain's material variation comes from its **root** placement `i0` (the outermost
`B`): `hueShift`, `hueGradient`, `valueRandom`, `emissiveRandom` and `emissiveGradient` are all
evaluated at `i0`, so a whole chain shares one colour and the recursion reads as a branch rather
than as noise. When `colorPerLevel` is set and `d > 0`, one extra term rotates the hue by
`(i0 mod (d + 1)) / (d + 1)` turns — the root branches cycle through `d + 1` hue families. That
is the whole rule: deterministic, free, and off with one flag.

### A procedural source (one object inside another)

`source.kind = "procedural"` with `source.reference = "<name>"` makes another procedural object
of the same scene the source of this one. The referenced object contributes **both** its mesh and
its arrangement: this object's placement `P_i` composes the referenced object's whole cloud.

```
point (i, j) = P_i · C_j · C.sourceTransform          ids: i * |C| + j
```

where `C` is `reference`'s cloud, generated with the same context one level deeper and with its
own point ops applied. Details that matter in practice:

- **the referenced object's `sourceTransform` is baked into the points**, so the composed points
  draw `C`'s mesh exactly where `C` would draw it;
- colours multiply: this object's per-placement multiplier (from its root index, as above) times
  `C`'s per-point multiplier. `density` and the extra attribute columns (a grammar's
  `depth`/`rule`/`branch`, any user column) come from the inner point;
- `C`'s effectors and deformers are **not** inherited; this object's deformers apply to the
  composed result;
- the `bounds` column is the leaf primitive's half extent — every intermediate scale already
  lives in the point scale;
- chains may be up to `kMaxHierarchyDepth` (4) hops long. `ProceduralGeometry::validateReferences`
  rejects a missing reference, a cycle and an over-deep chain before anything is generated;
  `resolveSourceMesh()` walks the chain to the leaf primitive.

`generateCloud()` on a referencing object **regenerates the referenced object's cloud from the
context** rather than reading its built state, so it never matters in which order a composition
builds its objects, and nothing has to be rebuilt twice. `contextualHash(ctx)` mixes the
referenced object's `contextualHash` (recursively), so editing a leaf rebuilds everything that
references it.

Self-recursion and a procedural source compose: the recursion runs first (over this object's own
placements), then the referenced cloud is composed under every resulting point.

---

## JSON

The grammar block sits next to `distribution` in a procedural node and is used when
`distribution.kind` is `"grammar"`; the hierarchy block always applies.

### A fractal tower (self-recursion, depth 2)

Four blocks on a ring; each carries a copy of the ring at 45% scale, five units up and rotated
30°; each of those carries another. 4³ = 64 boxes from ten lines of JSON.

```json
{ "name": "tower", "kind": "procedural",
  "procedural": {
    "source": { "kind": "box", "size": [1.6, 4.0, 1.6] },
    "distribution": { "kind": "radial", "count": 4, "radius": 3.0, "plane": "xz", "orientation": "outward" },
    "hierarchy": {
      "recursionDepth": 2,
      "maxInstances": 20000,
      "scalePerLevel": 0.45,
      "offsetPerLevel": [0.0, 5.0, 0.0],
      "rotationPerLevel": [0.0, 30.0, 0.0],
      "colorPerLevel": true
    },
    "material": { "baseColor": [0.10, 0.10, 0.12], "emissiveColor": [0.55, 0.8, 1.0], "emissiveIntensity": 0.5 },
    "materialVariation": { "hueGradient": 0.2 }
  } }
```

Drive it live from `procedural/tower/hierarchy/depth`, `…/scalePerLevel`, `…/offsetPerLevel`
and `…/rotationPerLevel` — all four are structural, so a change regenerates the cloud (tens of
microseconds at this size) rather than tinting it.

### A grid cathedral (grammar: repeat, mirror, branch)

Six bays four units apart; each bay mirrored about the `z = 0` plane into two aisles; each aisle
places a column and, nine units up, an arch turned on its side. 6 × 2 × 2 = 24 instances.

```json
{ "name": "cathedral", "kind": "procedural",
  "procedural": {
    "source": { "kind": "cylinder", "radius": 0.35, "height": 9.0, "radialSegments": 24, "caps": true },
    "distribution": { "kind": "grammar" },
    "grammar": {
      "axiom": "nave",
      "maxDepth": 8,
      "maxInstances": 4096,
      "seed": 3,
      "rules": [
        { "name": "nave", "op": "repeat", "count": 6, "step": { "position": [4.0, 0.0, 0.0] }, "children": ["bay"] },
        { "name": "bay", "op": "mirror", "mirrorAxis": [0.0, 0.0, 1.0], "children": ["aisle"] },
        { "name": "aisle", "op": "branch", "pre": { "position": [0.0, 0.0, 5.0] }, "children": ["column", "arch"] },
        { "name": "column", "op": "place" },
        { "name": "arch", "op": "place", "scaleAttribute": 0.6,
          "pre": { "position": [0.0, 9.0, 0.0], "rotation": [0.0, 0.0, 90.0] } }
      ]
    },
    "material": { "baseColor": [0.14, 0.13, 0.15], "roughness": 0.4 },
    "materialVariation": { "hueGradient": 0.15 }
  } }
```

`bay` mirrors about the plane through the **origin**, so the two aisles sit at `z = ±5` for every
bay, whatever `x` the repetition has reached. Swap `aisle`'s `op` to `"choice"` with
`"weights": [3, 1]` and a column becomes an arch one time in four, differently in every bay.

### A colonnade with capitals (a procedural source)

Two objects: `capital` arranges three blocks into one capital and is never placed on its own;
`colonnade` uses `capital` as its source, so each of its 24 ring placements carries the whole
three-block capital (72 instances), and a third object draws the shafts.

```json
{ "nodes": [
  { "name": "capital", "kind": "procedural", "visible": false,
    "procedural": {
      "source": { "kind": "box", "size": [1.3, 0.28, 1.3] },
      "distribution": { "kind": "linear", "count": 3, "start": [0.0, 8.7, 0.0], "end": [0.0, 9.3, 0.0] },
      "material": { "baseColor": [0.2, 0.19, 0.22], "roughness": 0.35 }
    } },
  { "name": "colonnade", "kind": "procedural",
    "procedural": {
      "source": { "kind": "procedural", "reference": "capital" },
      "distribution": { "kind": "radial", "count": 24, "radius": 14.0, "plane": "xz", "orientation": "outward" },
      "materialVariation": { "hueGradient": 0.3 }
    } },
  { "name": "shafts", "kind": "procedural",
    "procedural": {
      "source": { "kind": "cylinder", "radius": 0.35, "height": 8.6 },
      "sourceTransform": { "position": [0.0, 4.3, 0.0] },
      "distribution": { "kind": "radial", "count": 24, "radius": 14.0, "plane": "xz", "orientation": "outward" }
    } } ] }
```

`capital`'s own `distribution` is its internal arrangement; `colonnade`'s is where the capitals
go. Editing `capital` — its box size, its three-block spacing, its colour — rebuilds `colonnade`
too, because the reference is part of `colonnade`'s contextual hash. Set `capital`'s `visible` to
false if you do not also want it drawn at the origin.

---

## Limits, validation and cost

| Limit | Value | Where |
|---|---|---|
| grammar `maxDepth` | 64 | `Grammar::validate` |
| grammar / hierarchy / cloud instances | 1,048,576 | `Grammar::validate`, `HierarchySpec`, `kMaxCloudInstances` |
| reference chain | 4 hops (`kMaxHierarchyDepth`) | `validateReferences`, `resolveSourceMesh` |
| `recursionDepth` | 0 … 4 | `ProceduralGeometry::validate` |

`validate()` rejects a missing or dangling axiom, a duplicate rule name, a child naming a rule
that does not exist, a negative `count` or `depthLimit`, a non-positive `scaleAttribute`, a zero
`mirrorAxis`, a negative weight, a `recursionDepth` or `maxInstances` out of range, a
non-positive `scalePerLevel`, an empty or self-referencing `reference`, and an invalid grammar on
an object whose distribution is `grammar`.

Everything here is CPU work done once per structural change: expansion and composition are a few
million transform multiplies per second, and the 1M cap is the same one the rest of the point
pipeline uses. The per-frame cost is unchanged — the result is an ordinary instance buffer.

See also `docs/procedural-geometry.md` (the object and its parameters),
`docs/spatial-data.md` (point clouds, attributes and point ops), ADR-028 (structural-hash
invalidation) and ADR-029 (hierarchical generation).
