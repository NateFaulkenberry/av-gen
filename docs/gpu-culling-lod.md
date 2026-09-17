# GPU culling and LOD for procedural instances

ADR-029. Shader: `shaders/cull.wgsl`. Renderer: `src/rendering/procedural_renderer.{hpp,cpp}`.
Data: `scene::LodSettings` in `src/scene/procedural.hpp`. Tests:
`tests/rendering/test_culling_gpu.cpp` (`[culling][gpu]`), `tests/unit/test_lod.cpp` (`[lod]`).

An object with `lod.cull` or `lod.lodCount > 1` gets a compute pass that decides, per instance,
whether it is drawn at all and at which level of detail, then compacts the survivors into one list
per level. The draw becomes one `drawIndexedIndirect` per level. Nothing is read back to decide
anything: the CPU never looks at an instance.

Off by default. With the defaults (`cull = false`, `lodCount = 1`) no pass is encoded, no buffer is
allocated and the draw is the same `DrawIndexed(indexCount, instanceCount)` it has always been —
existing scenes render bit-identically.

## The passes

Encoded in `ProceduralRenderer::update()`, in this order, into the caller's command encoder:

1. **the effector pass** (`shaders/points.wgsl`, unchanged) — only for objects with effectors.
2. **the cull pass** (`shaders/cull.wgsl`) — one compute pass for every culling object, four
   dispatch stages. It binds the object's *live* record buffer when the effector pass ran, so it
   sees the moved/scaled records rather than the base ones (see "Effectors" below).

Inside the cull pass, per object:

| Stage | Dispatch | What it does |
|---|---|---|
| `cs_cull_classify` | `(ceil(N/64), 1)` | world bounding sphere → frustum / distance / screen-size test → LOD level; writes `lodIndex[i]` (`0xFFFFFFFF` = culled) |
| `cs_cull_reduce` | `(blocks, lodCount)` | `blockSums[level * blocks + b]` = how many of block `b`'s records are at `level` |
| `cs_cull_top` | `(1, lodCount)` | exclusive scan of that level's `blockSums` in place; thread 0 writes the level's indirect args and the object's stats slot |
| `cs_cull_scatter` | `(blocks, lodCount)` | local scan + `blockSums` gives each record its rank; `visibleIndices[level * stride + rank] = i` |

`blocks = ceil(N / 1024)`; the scan is 256 threads × 4 elements per block, exactly the structure
`shaders/particles.wgsl` uses. The LOD level is the **y dimension of the dispatch**, which is why no
per-level uniform rebinding is needed. Dispatches inside one compute pass are ordered and their
storage writes are visible to later dispatches, so reduce feeds top feeds scatter.

### Cull pass bindings (group 0)

| Binding | Type | Contents |
|---|---|---|
| 0 | uniform | `CullParams` / `rendering::CullPassUniforms` (256 bytes) |
| 1 | storage, read | the object's instance records (base, or live after the effector pass) |
| 2 | storage, read_write | `lodIndex`: one u32 per record |
| 3 | storage, read_write | `blockSums`: `kMaxLodLevels × blocks` u32 |
| 4 | storage, read_write | `visibleIndices`: `lodCount` slices of `visibleStride` u32 |
| 5 | storage, read_write | indirect args: `kMaxLodLevels × 5` u32 (`drawIndexedIndirect`) |
| 6 | storage, read_write | the renderer's shared stats buffer, 8 u32 per object slot |

`CullParams` is `objectToWorld`, six frustum planes, `cameraPos.xyz` + `projScale`, `limits`
(maxDistance, minScreenRadius, source cull radius, object matrix scale), `thresholds` (the three LOD
distances, and in `w` the radius the ladder measures projected size with), `counts` (record count, lod count, visible stride, scan blocks), `flags` (cull enabled,
thresholds are screen radii, stats slot) and `indexCounts` (the index count of each level's mesh,
which `cs_cull_top` copies into the indirect args).

`visibleStride` is the record count rounded up to 64 elements, so each level's slice starts on a
256-byte boundary and can be bound directly by the draw.

### Draw bindings (group 1)

Binding 5 is new: `var<storage, read> visibleIndices: array<u32>`, vertex stage only (read-only
storage is allowed there; `read_write` is not). The bind group binds the LOD level's slice of the
object's visible buffer; objects that do not cull bind a shared 256-byte inert placeholder.

`ProceduralUniforms` (binding 2) became a **per-level** slot: the object's uniform buffer holds
`kMaxLodLevels` slots of 768 bytes and each level's bind group binds its own. Two fields of
`fieldInfo` carry the switches:

- `fieldInfo.z` — the camera-facing billboard path. Set for Point sources as before, and now also
  for LOD levels 2 and 3.
- `fieldInfo.w` — indirection. `0` means `instances[instance_index]`, exactly the pre-culling path;
  `1` means `instances[visibleIndices[instance_index]]`. It is uniform across a draw, so both
  paths stay coherent.

## The LOD rules

`scene::makeLodMesh(spec, level, impostorSize)` (in `src/scene/procedural.cpp`) generates the mesh
of each level from the same `SourceSpec`:

For a **generated primitive**:

| Level | Mesh |
|---|---|
| 0 | `makeSourceMesh(spec)` — the source, unchanged |
| 1 | the same generator at half the segment counts: `radialSegments`, `heightSegments`, `segments`, `rings`, `majorSegments`, `minorSegments` and box `subdivisions` halved, with floors of 3 for the radial-style counts, 2 for sphere rings and 1 for height segments and subdivisions |
| 2 | a camera-facing billboard quad of edge `2 × impostorSize × boundingRadius(spec)` — at `impostorSize = 1` it circumscribes the source's bounding sphere |
| 3 | the same quad at an eighth of that edge — a dot at distance |

For a **`Mesh` source** — every scatter layer, every city piece, every imported asset — levels 1, 2
and 3 are all simplifications built by `assets::buildLodChain` with `vegetationLodSettings()`
(ADR-085). **None of them is an impostor.** `scene::lodLevelIsImpostor(spec, level)` is the one
place that distinction is written down, and the renderer asks it rather than testing the level
index: an impostor's corners are offsets in the camera's basis from the instance centre and skip
the source transform and the deformer stack, and a simplified *mesh* drawn that way comes out
flattened, at the asset's authored size rather than the layer's, centred on the instance record.
That is what happened to rungs 2 and 3 of every imported asset between ADR-085 and the LOD Lab
(`docs/lod-lab/README.md` §3.2).

`boundingRadius` is `scene::sourceBoundingRadius`, which is `max(|vertex|)` — the radius about the
source's **own origin**, not the half-diagonal of its box (ADR-199). The renderer caches level
meshes under a key derived from the object's `meshHash`, the level and `impostorSize`, alongside the
source meshes.

The level of one instance (`cs_cull_classify`; CPU reference `rendering::cullLodLevel`, which the
tests compare against):

```
center          = objectToWorld * record.position
radius          = sourceCullRadius(source) × max|record.scale| × objectMatrixScale
lodRadius       = halfDiagonal(source)     × max|record.scale| × objectMatrixScale
distance        = |center − cameraPosition|
screenRadius    = radius    / distance × projScale,  projScale = height / (2 tan(fovY/2))
lodScreenRadius = lodRadius / distance × projScale

if cull:
    culled if  dot(plane.xyz, center) + plane.w < −radius  for any of the six planes
    culled if  maxDistance > 0 and distance − radius > maxDistance
    culled if  minScreenRadius > 0 and screenRadius < minScreenRadius

level = 0
for k in 0..min(lodCount − 2, 2):
    t = lodDistances[k]
    if t <= 0: break                                       # a zero threshold ends the ladder
    if lodByScreenSize: take = lodScreenRadius <= t         # thresholds descend
    else:               take = distance >= t                # thresholds ascend
    if not take: break
    level = k + 1
```

**Two radii, because there are two questions.** `radius` is the sphere the *rejections* use. It is
centred on the record position — which is where this pass puts it, and for a scatter that is the
point on the ground the thing was planted at — so it has to reach the furthest corner of the source
from the source's own origin, or an instance can be discarded with its canopy on screen. `lodRadius`
is the tight sphere about the source's box, and it is what the *ladder* measures with: the ladder is
not asking whether anything is on screen but how large the thing looks, and the difference between
the two rules is how far the artist put the geometry from its origin. For geometry centred on its
origin they are the same number. `thresholds.w` carries `lodRadius`; 0 means "use `limits.z`", which
is what this pass did before the two were separated (`docs/lod-lab/README.md` §2).

A threshold of `0` ending the ladder is what makes `lodCount > 1` with unset thresholds a no-op
(everything stays at LOD0) instead of collapsing to the last level.

The bounding sphere is the *source* bounds through the instance transform. World-space deformers
and the emissive field are not accounted for, so an object with a large world deformer can pop at
the frustum edge — one reason culling is opt-in per object.

## Parameters and JSON

```json
"lod": {
  "cull": false,
  "maxDistance": 0,
  "minScreenRadius": 0,
  "count": 1,
  "distance1": 0, "distance2": 0, "distance3": 0,
  "byScreenSize": true,
  "impostorSize": 1
}
```

Every key is optional; a file without a `"lod"` object gets the defaults. Modulatable parameters,
registered under the object's prefix:

| Parameter | Field | Range |
|---|---|---|
| `lod/enabled` | `cull` | bool |
| `lod/maxDistance` | `maxDistance` | 0..100000 (soft 0..500) |
| `lod/minScreenRadius` | `minScreenRadius` | 0..4096 px (soft 0..32) |
| `lod/distance1..3` | `lodDistances[0..2]` | 0..100000 (soft 0..500) |

`count`, `byScreenSize` and `impostorSize` are not parameters: they are structural or
mesh-shaping and stay authored in the scene file. Only `lodCount` enters
`ProceduralGeometry::structuralHash()` (it decides how many meshes are generated); everything else
is a per-frame uniform, so moving a threshold never rebuilds a cloud.

## Determinism

- The compaction is a prefix-sum scan with **no atomics anywhere**, exactly like the particle
  compaction. `visibleIndices` is therefore always in ascending record order, and the whole pass is
  a pure function of (records, camera, parameters, viewport). Two fresh renderers of the same scene
  at the same time produce identical buffers and identical frames — covered by "Culling is
  deterministic across fresh renderers".
- Culling changes *which* instances are drawn, never their relative order, so the depth-tested
  result of a fully visible scene is byte-identical to the direct path. That is asserted end to end
  in "Culling a fully visible scene renders exactly the uncalled image".
- With culling disabled the object never touches any of this: no cull buffers exist, the visible
  binding is the inert placeholder, `fieldInfo.w` is 0 and the single `ProceduralUniforms` write
  lands at offset 0 exactly as before.
- The stats (`ProceduralStats::visibleInstances`, `culledInstances`, `lodCounts`) come from an
  **asynchronous** readback of the shared stats buffer and lag the drawn frame by a frame or two.
  They are a readout, never an input. `ProceduralRenderer::readCullCounts`,
  `readVisibleIndices` and `readLodLevels` are the blocking, exact versions for tests and tools.
  Using that readback as an input is what cost a frame of geometry at every rung change: the
  renderer used to decide which levels to record a draw for from how long a level had been empty
  *in it*, and the frame an instance arrived on a level was exactly the frame it was wrong about.
  Which levels are recorded is now `rendering::objectLevelRange`, a proof over the object's own
  record bounds that never reads the readback at all.

## Effectors and the effector pass

The cull pass is encoded after the effector pass and binds the *live* record buffer whenever the
object has usable effectors, so an effector that moves, scales or offsets records is fully visible
to the culler in the same frame — no one-frame lag, no separate bounds bookkeeping. The draw reads
the same live buffer through the visible list, so the indirection composes with effectors without
either feature knowing about the other. The bounding-sphere radius uses each record's own
`scale`, which the Scale effector writes, so scale effectors are accounted for exactly; position
effectors move the centre for free.

## When to enable it

Enable `lod.cull` when the object has many instances and a meaningful part of them is off screen or
far away — a large field, a city, a world-scale scatter. The pass costs one dispatch chain over the
records (roughly 0.1 ms per 100k, 0.4 ms per 1M; see `docs/performance/procedural-geometry.md`), so
it pays for itself as soon as it removes a noticeable share of the vertex work, and it is a net loss
for a handful of instances that are always in view.

Enable `lodCount > 1` when the instances are the same object at wildly different distances. Set
`lodByScreenSize` (the default) and descending pixel thresholds — screen size is what actually
decides whether the detail is visible, and it stays correct when the field of view or the render
resolution changes. Use distances instead when the object should switch on a fixed world scale
regardless of the camera.

`minScreenRadius` is the cheapest single win on dense fields: dropping everything under a pixel or
two removes the sub-pixel instances that cost full vertex work and contribute aliasing.

## Integration

`ProceduralRenderer::setViewport(width, height)` must be called once per frame before `update()`
with the render-target size; it sets the aspect of the frustum planes and the pixel scale of
`minScreenRadius` and screen-size LOD. Without it the renderer assumes 1920×1080. It is ignored
entirely when no object culls.
