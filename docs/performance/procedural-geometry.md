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
