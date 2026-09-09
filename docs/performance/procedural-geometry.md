# Procedural geometry performance (ADR-023)

Probe: `avgen_render_tests "[.perf][procedural]"` (Release build; `tests/rendering/test_procedural_gpu.cpp`).
Apple M2 Max, macOS 26.6.2, headless 1280x720, GPU time from the frame timer (scene pass through
tone map, no post chain), mean of frames 30..89; CPU update = `ProceduralStats::cpuUpdateMs`
(uniform packing + instance-buffer bookkeeping; the instance buffer is uploaded once, on the first
frame, and never again because `structureVersion` does not change). Two runs; the 10k figures vary
with GPU clock state, so both samples are listed.

Source: 24-segment cylinder, 4 height segments, caps (175 vertices, 240 triangles per instance);
instances on a 100-per-row grid, one directional light, no IBL. Normals are recomputed by finite
differences of the whole deformer chain for every vertex (three chain evaluations), including
the "none" row, so the deformer rows measure only the extra ALU of the deformers themselves.

| Instances | Deformers | Logical triangles | GPU ms/frame (run 1 / run 2) | CPU update ms |
|---|---|---|---|---|
| 1,000 | none | 240 k | 1.02 / 1.00 | 0.001 |
| 1,000 | one (twist, local) | 240 k | 1.07 / 1.06 | 0.002 |
| 1,000 | three (twist, bend, sine-world) | 240 k | 1.53 / 1.54 | 0.003-0.004 |
| 1,000 | noise (one 3-octave fBM, three channels) | 240 k | 2.02 / 2.03 | 0.002 |
| 10,000 | none | 2.4 M | 3.61 / 3.61 | 0.004-0.010 |
| 10,000 | one (twist, local) | 2.4 M | 4.49 / 4.50 | 0.006 |
| 10,000 | three (twist, bend, sine-world) | 2.4 M | 5.63 / 4.93 | 0.002-0.004 |
| 10,000 | noise (one 3-octave fBM, three channels) | 2.4 M | 8.60 / 4.74 | 0.001-0.007 |

Reading the numbers:

- Ten thousand instances (2.4 M triangles, one draw call) cost 3.6 ms at 720p undeformed; the
  CPU side is microseconds per frame because nothing but two small uniform writes happens per
  object once the structure is uploaded.
- A noise deformer is the expensive kind: three channels x three octaves x eight hashed corners
  per evaluation, times three chain evaluations for the normal — about 1 ms per 1,000 instances
  of this mesh. Twist/bend/sine are a few tens of ALU each.
- The 10k rows are vertex-bound (1.75 M vertices x 3 chain evaluations); the fragment cost is
  the same as entities since both run `pbr_shade.wgsl`.

Obvious next steps when a showcase needs more: skip the finite-difference passes when the stack is
empty (the "none" row would drop by roughly a third), evaluate noise once per vertex and reuse it
for the tangent offsets (approximate normal), and fewer height segments for straight columns.

## Showcase scenes (Release, M2 Max, 2880x1800 window, default post chain)

| Scene | Procedural objects / instances | Result |
|---|---|---|
| The Temple | 5 objects, ~1,000 instances (64 columns, 12 inner, 5 rings, 225 tiles, altar) + 32k dust | 120 fps, GPU 2.7 ms, CPU 3.1 ms |
| Hyperspace | 1,200-instance spiral, 14 gates, 36 spokes, core + 65k streaks | 120 fps, GPU 3.0 ms, CPU 2.6 ms |
| Impossible Chamber | three nested wall scenes at 1×/2.5×/7× (30 procedural objects) | 120 fps, GPU 2.8 ms, CPU 3.7 ms |

## Fields and effectors (ADR-025)

Probe: `avgen_render_tests "[.perf][fields]"` (Release; `tests/rendering/test_points_gpu.cpp`).
Apple M2 Max, macOS 26.6.2, Dawn v20260907 (Metal), headless 1920x1080, mean of frames 30..89.
"scene+post" is the frame timer (scene pass through tone map); the effector pass and the
particle compute passes run before the scene pass and carry their own timestamps
(`ProceduralStats::effectorPassMs`, `ParticleStats::simulateMs`); "total" is the sum. Fields:
`curl` = CurlNoise (frequency 0.3, no falloff; six fbm3Vec evaluations = 54 value-noise lookups
per sample), `swirl` = Vortex (Smooth falloff 1..12), `bulge` = Radial (EaseInOut falloff 0..8).
Points are 0.02-unit unlit billboards scattered in a 16-unit box seen from 22 units.

| Case | scene+post ms | effector pass ms | particle sim ms | total GPU ms |
|---|---|---|---|---|
| 1M points, no effectors | 3.09 | – | – | 3.09 |
| 1M points, radial scale effector | 5.65 | 1.11 | – | 6.76 |
| 1M points, vortex offset + radial scale effectors | 5.35 | 1.42 | – | 6.77 |
| 1M points, curl offset + radial scale effectors | 5.62 | 4.61 | – | 10.23 |
| 100k boxes (24 vertices), no deformers | 1.13 | – | – | 1.13 |
| 100k boxes, world Field deformer (vortex) | 11.38 | – | – | 11.38 |
| 100k boxes, world Field deformer (curl) | 22.58 | – | – | 22.58 |
| 256k particles, no field forces | 0.77 | – | 0.08 | 0.84 |
| 256k particles, vortex force + curl turbulence | 1.34 | – | 0.83 | 2.17 |

Reading the numbers against the targets (1M points with a GPU field under 4 ms; 100k instances
under 6 ms):

- The effector pass itself is 1.1-1.4 ms for a million records with analytic fields (radial,
  vortex): 192 MB of record traffic plus the field maths, one thread per record. CurlNoise is
  the expensive kind (4.6 ms): 54 hashed value-noise lookups per sample for the central
  differences, the same cost structure as the noise deformer in the table above. The draw of a
  million billboards is 3.1 ms on its own at 1080p (raster/vertex bound; the scene+post column
  grows with the effectors because the radial scale enlarges the quads), so a million points
  with an analytic field cost 6.8 ms end to end and the field pass alone meets the 4 ms budget;
  curl noise does not.
- The Field deformer runs inside the vertex shader and, like every deformer, is evaluated three
  times per vertex for the finite-difference normal: 100k boxes x 24 vertices x 3 = 7.2M field
  evaluations per frame. A vortex field costs ~10 ms over the undeformed 1.1 ms, curl ~21 ms;
  the 6 ms target is not met with exact normals. The obvious next step (also listed for the
  noise deformer above) is evaluating the field once per vertex and reusing the displacement
  for the two tangent offsets, which drops the field cost by 3x at the price of normals that
  ignore the field's gradient; it should be a per-deformer quality switch rather than the default.
- Particle field forces are cheap: 256k particles with a vortex force and curl turbulence add
  0.75 ms of simulation (the curl again dominates); the stable compaction is unchanged.
- CPU cost is microseconds per frame in every case: the field block (5 KB) and the per-object
  uniforms are the only per-frame uploads.

## SDF objects (ADR-027)

Probe: `avgen_render_tests "[.perf][sdf]"` (Release; `tests/rendering/test_sdf_gpu.cpp`).
Apple M2 Max, macOS 26.6.2, Dawn v20260907 (Metal), headless 1920x1080, mean of frames 30..89.
"raymarch pass" is `SdfStats::raymarchMs`, the timestamped SDF pass alone; "frame" is the frame
timer (scene pass through tone map), so the difference is the rest of the scene. The camera sits
at z = 3 *inside* the object's 8-unit bounds, so the quad covers the whole screen and every one of
the 2.07 M pixels marches: this is the worst case, not a typical shot. `maxSteps` 128, `epsilon`
0.002, `stepScale` 0.9 (0.7 with noise, which is not Lipschitz-1). Three runs; the two heavy rows
swing by 20-30% with GPU clock state, so their range is given.

| Case | Packed nodes | Raymarch pass ms | Frame ms | CPU update ms |
|---|---|---|---|---|
| 1 sphere | 3 | 3.58-3.66 | 4.19-4.25 | 0.002 |
| 16 spheres, smooth union (4x4 fold) | 63 | 114.5-140.1 | 115.4-140.9 | 0.007-0.013 |
| 16 spheres, smooth union + 3-octave fBM displacement | 65 | 153.8-204.1 | 154.6-204.9 | 0.007-0.015 |

Reading the numbers:

- Cost is (pixels) x (steps) x (nodes), and nothing culls: a full-screen march of a 63-node
  program at 128 steps is ~1.7e10 node evaluations per frame. One sphere (3 nodes) at 3.6 ms is
  the floor and scales roughly linearly with the program length, so the useful lever is the
  bounds: a raymarched object that covers a quarter of the screen costs a quarter of this.
- The noise displacement adds ~40-60 ms: it is one 3-octave fBM per *step*, plus four more per
  hit for the tetrahedron normal, and it forces `stepScale` down to 0.7, which lengthens every
  ray. Displaced trees are the expensive kind here exactly as they are for the deformers above.
- The interpreter's distance and point stacks are dynamically indexed `array<f32, 8>` locals,
  which Metal keeps in scratch memory rather than registers; that, and the per-node branch chain,
  is why a node costs far more than the few ALU its formula needs. A per-tree specialised shader
  (generating WGSL from the tree, as `shader_layers` does for user shaders) is the obvious next
  step and would remove both.
- The CPU side is microseconds: packing 65 nodes and writing two 256-byte uniform slots.
- Mesh mode has no per-frame GPU cost beyond an ordinary mesh draw; its cost is the CPU
  surface-nets pass in `SdfObject::rebuild` (resolution^3 tree evaluations), paid only when the
  structural hash changes.

## Culling and LOD (ADR-029)

Probe: `avgen_render_tests "[.perf][culling]"` (Release; `tests/rendering/test_culling_gpu.cpp`).
Apple M2 Max, macOS 26.6.2 (build 25G83), Dawn v20260907 (Metal), headless 1920x1080, mean of
frames 30..89. "scene+post" is the frame timer (scene pass through tone map); the cull pass runs
before it and carries its own timestamp (`ProceduralStats::cullMs`). Two runs; the undeformed
"culling off" rows vary by up to 2x with GPU clock state, so both samples are listed.

- **100k boxes**: a 320 x 313 grid of unit boxes (24 vertices, 12 triangles each) at 1.2-unit
  spacing, camera inside the field at eye height looking along -Z, so roughly the half behind the
  camera plus the sides fall outside the frustum. Culling keeps 20,518 of 100,160 instances.
- **1M points**: a 1000 x 1000 grid of 0.02-unit unlit billboards at 0.04-unit spacing seen from
  22 units; 739,766 of 1,000,000 survive the frustum.

| Case | Instances drawn | scene+post ms (run 1 / run 2) | cull pass ms | total GPU ms |
|---|---|---|---|---|
| 100k boxes, culling off | 100,160 | 1.45 / 1.04 | – | 1.45 / 1.04 |
| 100k boxes, culling on | 20,518 | 0.69 / 0.69 | 0.062 | 0.75 / 0.75 |
| 1M points, culling off | 1,000,000 | 2.82 / 1.64 | – | 2.82 / 1.64 |
| 1M points, culling on | 739,766 | 1.85 / 1.51 | 0.38 / 0.35 | 2.23 / 1.86 |

Reading the numbers against the targets (cull pass under 0.5 ms at 100k, under 2 ms at 1M):

- The cull pass is **0.06 ms for 100k instances** and **0.35-0.38 ms for 1M**, comfortably inside
  both targets. It is bandwidth-bound on the record buffer: one thread reads a 96-byte
  `InstanceRecord` and writes 4 bytes of level, then the three compaction dispatches touch only the
  4-byte lists (1M records = 96 MB read for classify, ~12 MB for the whole scan). Scaling from 100k
  to 1M is a factor of 6, not 10, because the small case never fills the machine.
- **100k boxes with half the field off screen: 1.45/1.04 ms down to 0.75 ms end to end**, cull pass
  included. Removing 80% of the instances removes 80% of the vertex work; the pass costs 4% of what
  it saves. This is the case culling is for.
- **1M points break even at best.** The billboards are 4 vertices each, so the vertex work the pass
  removes (26% of the instances) is worth roughly what the pass costs plus the extra indirection in
  the vertex shader; the run-2 pair (1.86 vs 1.64) is a small net loss. Culling a point cloud only
  pays when much more than a quarter of it is off screen, or when `minScreenRadius` is doing the
  work — dropping sub-pixel points removes fragment work too, which is where the billboard cost
  actually is.
- The indirection itself (`instances[visibleIndices[instance_index]]`) is free within measurement
  noise: the fully-visible A/B in the `[culling]` tests renders the identical image and the
  100k "culling on" row is the same 0.69 ms in both runs while the direct path swings by 40%.

CPU cost is unchanged: the cull parameters are one 256-byte uniform write per culling object per
frame, and the stats readback is asynchronous (`ProceduralStats::visibleInstances` and friends lag
the drawn frame by a frame or two and nothing waits on them).

## Procedural materials (ADR-030)

Probe: `avgen_render_tests "[.perf][material]"` (Release build;
`tests/rendering/test_material_gpu.cpp`). Apple M2 Max, macOS 26.6.2, headless **1920x1080**,
GPU time from the frame timer, mean of frames 20..59, two runs. Scene: 102,400 instanced boxes
(12 triangles each, 1.2 M triangles) on a 320 x 320 grid filling the frame, one directional light,
no IBL, no post chain. The only difference between the rows is `Material::program`: the same
geometry, the same draw call, the same number of shaded fragments.

| Material program | GPU ms/frame (run 1 / run 2) | Added |
|---|---|---|
| none | 3.62 / 3.69 | – |
| 3 ops: `input worldPosition`, `gradient`, `ramp` | 4.85 / 5.18 | +1.23 / +1.48 ms |
| 16 ops incl. `noise`, `voronoi`, `palette`, `hueShift`, `saturate` | 21.66 / 21.78 | +18.04 / +18.09 ms |

(ADR-036 raised the budget from 16 ops to 48 and added layers; `docs/performance/surfaces.md` has
the re-measured table, including a 48-op layered program. The conclusions below did not change:
cost is proportional to the ops a program enables, and the noise ops dominate.)

Reading the numbers:

- **The interpreter itself is cheap.** A three-op program that loads the world position, takes an
  axis gradient and looks it up in a three-stop ramp costs **1.2-1.5 ms** for a full 1080p frame of
  covered pixels — under a nanosecond per shaded fragment, the loop overhead plus a dozen ALU. The
  no-program path is byte-for-byte the pre-ADR-030 shader (the golden hashes in
  `test_procedural_examples_gpu.cpp` still match), so a scene that names no program pays nothing.
- **The op you pick dominates, not the op count.** Of the 18 ms the heavy program adds, the
  `voronoi` op alone is most of it: Worley F1 evaluates 27 cells x 3 `hash01` calls = 81 `pcg3d`
  rounds per fragment. `noise` (3-octave fBM = 24 hashes) is the next tier, then `hueShift` and
  `saturate`, which each run a full linear-RGB -> OKLab -> RGB round trip with three cube roots and
  three cubes. Everything else - `gradient`, `ramp`, `remap`, `mix`, `multiply`, `smoothstep`,
  `threshold`, `power`, `fresnel`, `constant`, `input` - is a handful of ALU.
- **This is a fragment cost, so it scales with covered pixels (and overdraw), not with instances.**
  1280x720 is 44% of the pixels of 1080p and costs about that fraction. Budget a program against the
  frame's shaded area, and prefer driving an expensive pattern from a vertex-stage deformer (which
  runs per vertex) when it is low-frequency enough.
- A `field` op costs whatever its field kind costs (see "Fields and effectors" above): the material
  interpreter calls the same `fieldScalar` / `fieldVector` / `fieldColor` the deformers do.

Practical guidance: one noise or voronoi op per material is affordable at 1080p (about 2-3 ms and
12-15 ms respectively for a full-frame surface); two of each is not. Cache-style tricks do not apply
- every op is recomputed per fragment - so the lever is choosing cheaper ops, shrinking the covered
area, or moving the pattern into the geometry.

## Volumetrics and simulation (ADR-032)

Probes: `avgen_render_tests "[.perf][volume]"` and `avgen_render_tests "[.perf][simulation]"`
(Release; `tests/rendering/test_volume_gpu.cpp`, `tests/rendering/test_simulation_gpu.cpp`).
Apple M2 Max (38-core GPU, 64 GB), macOS 26.6.2, Dawn (Metal), headless, mean of frames 30..89.

### Volumetric fog at 1920x1080

The march runs at half resolution (960x540) into its own RGBA16F target and is composited into
the HDR target with a depth-aware upsample; "volume pass" is the two passes together
(`RenderStats::volume.volumeMs`), "scene+post" is the frame timer around everything else. The
scene is two unlit boxes at 4 and 40 units in front of the camera on a black background, so the
frame cost is almost entirely the fog. The density field is a `sphere` field 20 units across in
front of the camera; the noise row adds the 3-octave value fBM (`volumeNoise` 0.6).

| Case | scene+post ms | volume pass ms |
|---|---|---|
| fog off (baseline; no passes encoded) | 0.75 | – |
| 32 steps, no noise | 1.82 | 0.79 |
| 32 steps + fbm3 noise | 3.05 | 2.11 |
| 32 steps + noise + density field + emission | 3.15 | 2.18 |
| 64 steps, no noise | 2.46 | 1.49 |
| 64 steps + fbm3 noise | 5.14 | 4.16 |
| 64 steps + noise + density field + emission | 5.04 | 4.17 |

Reading the numbers:

- **The march is linear in the step count** (0.79 -> 1.49 ms plain, 2.11 -> 4.16 ms with noise)
  and half resolution buys the factor of four it should: 32 steps at 1080p costs what 32 steps at
  540p would cost at full resolution.
- **The noise is 70% of the cost.** A 3-octave value fBM is 24 hashed corner lookups per sample;
  at 32 steps x 518 k half-res pixels that is 400 M lookups a frame. `volumeDensityAt` therefore
  evaluates the height and field terms first and skips the fBM wherever they already leave the
  density at zero, which is why the "+ density field" rows cost only 0.07 ms more than the noise
  rows despite adding a field sample per step: the field zeroes most of the volume and the noise
  is not evaluated there.
- **A density field and emission are nearly free** next to the noise (a `sphere` field is a
  length, a saturate and a matrix multiply; the colour field is only sampled when
  `volumeEmission > 0`).
- **Fog off costs nothing**: `VolumeRenderer::update()` allocates nothing and `encode()` emits no
  passes, so the baseline row is the pre-ADR-032 frame exactly (the golden frame hashes in
  `test_procedural_examples_gpu.cpp` are unchanged).
- **Budget**: 32 steps with noise at 1080p is ~2.1 ms, inside the 1-3 ms the ADR asked for. 64
  steps with noise doubles that; use 64 only when the fog is thick enough for banding to show.

The showcase (`examples/machine`, 1280x720, `volumeSteps` 24, `volumeNoise` 0.45,
`volumeDensityField`/`volumeColorField` = `heat`, `volumeMaxDistance` 90): **0.39 ms/frame
without fog, 1.97 ms with it** — about 1.6 ms for the atmosphere, at 60 fps offline with zero
GPU errors.

### Simulated grid fields

One 60 Hz sub-step per frame (`maxSubSteps` 1), injection from a `box` field and advection by a
`direction` field, timed with `SimulationStats::simulateMs` (the whole frame's compute pass).
The whole sub-step chain is one compute pass — in a compute pass the usage scope is a single
dispatch, so the ping-pong buffers can swap roles between dispatches — and the finished state is
copied into the shared grid table afterwards.

| Grid | Stages (dispatches) | GPU ms per sub-step | Table |
|---|---|---|---|
| 32^3 scalar | inject + advect (2) | 0.096 | 0.13 MB |
| 64^3 scalar | inject + advect (2) | 0.262 | 1 MB |
| 64^3 scalar | inject + advect + 4 Jacobi diffusion sweeps (7) | 0.466 | 1 MB |
| 64^3 vector | inject + advect (2) | 0.322 | 4 MB |
| 64^3 reaction-diffusion | inject + Gray-Scott (2) | 0.138 | 2 MB |

Reading the numbers:

- **A 64^3 grid stepped every frame is a quarter of a millisecond.** Eight times the cells of
  32^3 costs 2.7x, not 8x: the small case never fills the machine.
- **Diffusion is the expensive stage** — five extra dispatches (a snapshot plus four Jacobi
  sweeps) add 0.2 ms, each sweep gathering six neighbours per component. Halve
  `diffuseIterations` before you halve the resolution.
- **Reaction-diffusion is the cheapest** per cell: two components, one gather of six neighbours
  each, no field sampling and no advection.
- **Injection and advection sample fields**, so their cost follows the field kind: the rows above
  use a `box` and a `direction` field (a few tens of ALU). A `curlNoise` velocity field costs
  what it costs everywhere else — six `fbm3Vec` evaluations per cell.
- **Memory** is the real limit, not time: the shared grid table is a fixed 8 MB
  (`spatial::kMaxGridTableFloats`), which is exactly one 128^3 scalar grid, one 64^3 vector grid
  or a 100^3 reaction-diffusion grid; `Simulation` adds two ping-pong buffers plus one snapshot
  buffer of the same size as the scene's grids.
