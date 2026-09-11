# Renderer 2.0, Phase 0: audit

**Status: in progress.** This covers the measurements taken before any renderer change, and the
conclusions they support. It does not yet cover every subsystem the brief lists; the sections marked
*not yet audited* are honest gaps, not omissions of bad news.

Nothing in the renderer has been modified to produce this document.

## How these numbers were taken

Apple M2 Max, release build, headless, `--tier realtime`, 180 frames at a fixed 30 Hz simulation
clock, 168 warmed frames measured, no concurrent build or GPU work. Glowmere
(`examples/world/glowmere-stylized.json`) is the subject throughout, per §68.

`--fps 30` is the simulation cadence, not achieved rendering speed.

---

## 1. Where the time actually goes

At 1440×900, GPU frame median **24.25 ms**:

| Pass | ms | share |
|---|---:|---:|
| **scene** | **20.71** | **85%** |
| shadow | 0.92 | 4% |
| volume | 0.85 | 4% |
| cull | 0.33 | 1% |
| depth | 0.33 | 1% |
| ao | 0.33 | 1% |
| bloom / clusters / particles / composite / fxaa / tonemap | 0.07–0.13 each | ~2% total |

Pass medians sum to 23.92 against a 24.25 ms frame; independent medians need not sum exactly.

Wall clock over 168 steady frames, three consecutive runs: median **25.78 / 25.79 / 25.68 ms**,
p10 24.5, p90 **28.4**, min 23.8.

**Frame pacing at steady state is good.** An earlier 48-frame sample showed p90 49.95 ms against a
25.37 ms median, which looked like a serious pacing defect; over 168 warmed frames it does not
reproduce, and the spread is p10 24.5 to p90 28.4. The earlier figure included start-up transients.
The problem is the *absolute* frame time — 25.7 ms is 39 FPS against the brief's 16.67 ms target —
not variance.

## 2. The scene pass is geometry-bound, not fragment-bound

This is the single most important measured result, because it decides which phases matter.

| Resolution | pixels | GPU frame | scene pass |
|---|---:|---:|---:|
| 720×450 | 0.32 MP | 22.09 ms | 19.92 ms |
| 1440×900 | 1.30 MP | 24.25 ms | 20.71 ms |
| 2880×1800 | 5.18 MP | 47.97 ms | 40.96 ms |

**Quadrupling the pixels from 720×450 to 1440×900 moves the scene pass by 4%** (19.92 → 20.71 ms).
A fragment-bound pass would have moved by something close to 4×. From 1440×900 to 2880×1800 it
doubles, so fragment cost does become significant at that resolution — but at the resolution the
editor actually runs, the scene pass is paying for vertices, draws and state, not pixels.

Two consequences:

- **Dynamic resolution (§33) will not help at 1440×900.** It is a real feature and worth having for
  high-resolution output, but it cannot be the answer to the current frame time. Reducing render
  scale from 1440×900 to 720×450 would buy roughly 2 ms of a 24 ms frame.
- The phases that matter here are **visibility, LOD, HLOD and instancing** (§9–§16, brief phases
  2–4). That is what the brief predicted, and the measurement supports it rather than merely being
  consistent with it.

## 3. The geometry statistics do not measure what they appear to

**Superseded by Phase 1 (ADR-077). Kept because the reasoning is what motivated the phase, but
the numbers below describe the profiler as it stood before the fix, not as it stands now.**

Reported for Glowmere at 1440x900, *before* Phase 1:

```
draws=112 (indirect 154, empty 2, skipped 62)  shadowDraws=123  cascades=2/2
tris=15154902  instances=2316v/114296c
```

`tris` was `sourceTriangles x instances` -- computed before LOD selection and before culling, so
it was the geometry the world *contains*, not the geometry submitted. The submitted count was
measured nowhere. `draws=112` beside `indirect 154` in the same line was the same kind of error:
it read a `ProceduralStats` copy taken before any draw was recorded.

**As of Phase 1 both report what their names say.** The same scene now reads:

```
draws=143 (indirect 154, empty 6, skipped 62)  tris=636397  instances=2340v/114272c/116612
submitted: camera 636397 tris / 2479 inst / 140 draws; shadow 1176964 / 1615 / 197
```

Submitted geometry is 4.2% of logical. `tris` is therefore resolution-dependent now, which it was
not before -- that is correct behaviour for a submitted count and is the point of the change.

The split made something visible that was not: **shadows submit 1.85x the camera's geometry.**

## 4. The CPU is not the bottleneck

`cpu(scene)=0.77 ms` and `cpu(proc)=0.14 ms` against a 25.7 ms frame. Whatever is wrong, it is not
CPU scene traversal — so the brief's §17 progression toward GPU-driven submission should stay at its
early stages until profiling says otherwise, exactly as §17 itself instructs.

Note this is a *sampled* per-update figure, not whole-frame CPU latency. Phase 1 needs a real CPU
frame breakdown (§6) before this can be treated as settled.

## 5. The volumetric measurement is broadly sound

The brief (§31) suspects the volumetric timings, on the grounds that they looked invariant to
resolution and step count. Measured:

| Resolution | volume pass |
|---|---:|
| 720×450 | 0.39 ms |
| 1440×900 | 0.85 ms |
| 2880×1800 | 2.88 ms |

It scales with pixels, roughly as a screen-space effect should. A removal test is less flattering:
`--disable volume` at 1440×900 gives a 22.81 ms GPU frame against 24.25 ms baseline — **1.44 ms
saved against 0.85 ms reported**, a 0.6 ms discrepancy. Removing a pass also removes its resource
transitions and bandwidth, which the pass's own timestamps do not attribute to it, so the direction
of the error is expected. The reported number is a floor, not the full cost.

**Conclusion: the volumetric timing is not fabricated and not invariant.** It under-reports by
roughly 40% on a removal basis. That is worth fixing in Phase 1 but it is not the scandal the brief
allowed for, and volumetrics are 4% of the frame either way.

## 6. Subsystem map

13,000 lines across `src/rendering`, with the mass concentrated in two files:

| File | lines | role |
|---|---:|---|
| `scene_renderer.cpp` | 2,369 | the frame: targets, passes, uniforms, entity draws, orchestration |
| `procedural_renderer.cpp` | 1,910 | the ecology: instance generation, LOD, GPU cull dispatch |
| `particle_renderer.cpp` | 702 | particles |
| `post_processor.cpp` | 653 | bloom, exposure, tonemap, FXAA, composite |
| `sdf_renderer.cpp` | 603 | raymarched SDFs |
| `simulation.cpp` | 513 | GPU simulation |
| `volume_renderer.cpp` | 416 | volumetrics |
| `environment.cpp` | 416 | sky, IBL |
| `ao_renderer.cpp` | 363 | screen-space AO |
| `light_data.cpp` | 339 | light packing, froxel grid, LTC |
| `shadow_renderer.cpp` | 282 | cascaded shadow maps |

`scene_renderer.cpp` at 2,369 lines is both the orchestration layer and a pass implementation. That
is the file the brief's §40 FrameGraph would decompose, and it is the main structural obstacle to
parallel work on the renderer.

## 7. What already exists and must not be rebuilt

Per §2 and §80, these are working systems to extend rather than replace:

- **A GPU cull pass exists** (`cull=0.33 ms`, `cullDispatches`), producing `visibleInstances` and
  `culledInstances`. Instance culling is therefore already GPU-driven in part — the brief's §17
  Stage 4 is partially present for procedural ecology.
- **Indirect draws exist** (`indirect 154`).
- **A 4-level LOD system exists** for procedural instances (`lod=485/1453/373/5`).
- **Cascaded shadows exist** (`cascades=2/2`), with per-band `castsShadow` policy already set by the
  world composer.
- **`gpu::FrameTimeline`** provides per-pass GPU timestamps that partition the frame. Timestamps are
  taken at each pass *end*, so intervals partition rather than nest.
- **Aux targets already exist**: normal+roughness, velocity, emission, object+material IDs, linear
  depth. Motion vectors (§53) therefore have somewhere to come from already.
- **`--disable shadows,ao,volume,post`** for cost attribution by removal.

## 7a. The popping has a specific cause, and it is not the one the brief assumes

The brief's §11 asks to "move from distance LOD to screen-space LOD" and calls it mandatory.
**Screen-space LOD already exists**, on both sides:

- CPU, `procedural_renderer.cpp`: `screenRadius = radius / distance * camera.projScale`, and
  `lodByScreenSize` selects by `screenRadius <= threshold`.
- GPU, `shaders/cull.wgsl`: the same projected radius, the same comparison, plus frustum planes,
  a max distance and a minimum screen radius.

So §11 is largely done. What is missing is narrower and is the actual cause of what the user sees:

```wgsl
let minRadius = cullParams.limits.y;
if (minRadius > 0.0 && screenRadius < minRadius) { culled = true; }
...
if (byScreen) { take = screenRadius <= t / detail; }
```

**Every one of these is a hard binary threshold, and there is no hysteresis anywhere in the
renderer.** Searched: no `hysteresis`, no enter/leave radii, no fade-in or fade-out, in
`src/rendering/` or in any shader. Nor is there any LOD crossfade — no dither, no stochastic
transition, no temporal blend. An instance whose projected radius sits near `minScreenRadius`
flickers in and out as the camera breathes; an instance crossing an LOD threshold swaps mesh
between one frame and the next.

That is exactly the brief's §10 and §13, and it means Phase 2's work is smaller and more targeted
than "build screen-space LOD": it is **hysteresis on thresholds that already exist**, plus a
transition so a swap is not instantaneous.

Two things already present that Phase 2 should build on rather than replace:

- The composition's depth bands already thin density and shift the LOD ladder per band
  (`depthBand(dist)` returning a density and a detail multiplier), which is a ready-made place to
  put per-band hysteresis margins.
- Instances are already hashed for stable stochastic thinning (`instanceHash(i) >= band.x`), which
  is the same mechanism a stochastic LOD crossfade needs.

## 8. Not yet audited

Stated rather than quietly skipped:

- CPU frame breakdown beyond the two sampled counters.
- Resource lifetime, buffer churn and per-frame allocation (§44, §45).
- Pipeline/bind-group/render-target switch counts (§6 asks for these; they are not reported).
- Shader and pipeline compilation timing, and whether hitches occur on first encounter (§39).
- Texture and buffer memory (§6).
- Whether shadow cascade splits are stable, and the cause of the reported shadow popping (§21).
- Terrain chunk culling behaviour (§48).
- Occlusion culling — there appears to be none (§47).
- Whether the *terrain* participates in the same screen-space LOD path as procedural instances, or
  has its own chunk LOD with its own thresholds (§48).
- Colour management audit (§56).
- The offline render path's divergence from the realtime path.

## 9. Conclusions that should drive the phase order

1. **Phase 1 is not optional and is not merely instrumentation.** The headline triangle statistic
   measures pre-cull geometry, submitted primitives are unmeasured, and pass timings under-report by
   ~40% on a removal basis. Nothing downstream can be evaluated until this is fixed.
2. **Phases 2–4 (visibility, LOD/HLOD, instancing) are where the frame time is.** 85% of the GPU
   frame is one geometry-bound pass.
3. **Dynamic resolution and fragment-side work are not the lever at editor resolution.** Worth
   building for high-resolution output; not an answer to 25.7 ms.
4. **GPU-driven submission is already partly present** and the CPU is not the bottleneck, so §17's
   later stages stay parked until measurement justifies them.
5. **Volumetrics are 4% of the frame.** Whatever their measurement error, they are not the problem.
