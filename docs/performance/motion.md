# Motion quality performance (ADR-040)

Probe: `avgen_render_tests "[.perf][motion]"` (Release build;
`tests/rendering/test_motion_gpu.cpp`). Apple M2 Max, macOS 26.6.2, headless **1920x1080**.

Each row renders 20 warm-up frames and 60 measured frames at a fixed 1/60 s step with an advancing
frame index, and reports the **median** of the whole-frame GPU timer (the first pass through tone
mapping). Two idle runs are listed; a third run taken immediately after a full test suite is quoted
at the bottom, because the whole-frame figure on this machine moves by several milliseconds when
the GPU is warm. The device reports timestamps in ~65 us ticks, which is why several figures land
on the same value.

The particle scenes are a single sphere emitter on black: no lights, no shadows, no bloom. The
frame is essentially "simulate, compact, draw, tone map", so the deltas are the feature's own cost.

## Particles

| Configuration | run 1 | run 2 | trail memory |
|---|---|---|---|
| 256 k round billboards (baseline) | 1.835 ms | 1.901 ms | 0 |
| 256 k **stretched** billboards | 2.032 ms | 1.835 ms | 0 |
| 256 k stretched + motion blur | 3.342 ms | 3.473 ms | 0 |
| 32 k billboards (ribbon baseline) | 1.049 ms | 1.049 ms | 0 |
| 32 k **ribbons**, 32 points | 3.080 ms | 3.146 ms | **15.5 MiB** |

## Motion blur

A single moving emissive box, so the velocity target is non-trivial and every tile does real work.
16 taps, 20 px tiles.

| Configuration | run 1 | run 2 |
|---|---|---|
| no motion blur | 1.180 ms | 1.311 ms |
| tile max + neighbour max + reconstruction | 2.097 ms | 2.228 ms |
| **delta** | **0.92 ms** | **0.92 ms** |

## Reading the numbers

**Stretching is free.** The two runs bracket the baseline (2.03 ms one way, 1.84 ms the other,
against a 1.84-1.90 ms baseline), which is to say the difference is inside the timer's noise. It is
one branch and a basis rotation in the vertex shader, no extra memory, no extra draw, and the only
real term is that a streak covers more pixels than a dot. It is the right default for large counts,
and it is what turns a cloud of pasted-on dots into sparks.

**Ribbons cost memory and fill, and both are real.** Say it plainly:

- **Memory** is `capacity * (trailLength - 1) * 16` bytes, allocated once per pool and untouched
  when trails are off. 32 k particles x 32 points is **15.5 MiB**. The same trail on a
  million-particle system would be 496 MiB, which is why `validateParticleSystem()` refuses
  anything over **64 MiB per system**. That budget is the feature's honest boundary: ribbons are
  for hero emitters, and everything else gets a stretched billboard.
- **Time** is about **2 ms** for 32 k ribbons at 1080p against the same 32 k drawn as billboards —
  a *tripling* of that frame. The ribbon draw is 31 quads per particle instead of one, so it is
  31x the vertices, and long thin additive strips overdraw heavily. A 32 k ribbon system is
  affordable inside a 16 ms budget; a 250 k one is not, and the memory budget stops you before the
  time does.

**Motion blur is 0.92 ms at 1080p**, reproduced exactly across both idle runs, for three fullscreen
passes: the tile max reads 20x20 velocity texels per output texel (one read per screen pixel in
total), the neighbour max is 9 reads over a 96x54 target, and the reconstruction is 16 taps of
colour plus depth and velocity each. The reconstruction dominates and scales linearly with
`post/motionBlur/samples`. Where nothing moves the pass still runs, but every pixel takes the
early-out below half a pixel of tile motion, so a static frame costs the three dispatches and
nothing else. On the 256 k particle scene it costs more (1.3-1.6 ms) because the velocity target is
dense and busy rather than mostly empty.

**On thermal noise.** A run taken immediately after the full render suite read 4.39 / 4.26 / 6.23 /
1.84 / 6.23 ms for the particle rows and 1.84 -> 3.80 ms for the blur pass: every figure roughly
doubles, and the *ordering* survives. These are whole-frame numbers on a laptop GPU with its own
opinions about clocks; read the within-run deltas, not the absolute values.
