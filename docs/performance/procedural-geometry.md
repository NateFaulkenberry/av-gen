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
