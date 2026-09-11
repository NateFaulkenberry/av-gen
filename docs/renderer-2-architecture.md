# Renderer 2.0, Phase 0: audit

**Status: in progress.** This covers the measurements taken before any renderer change, and the
conclusions they support. It does not yet cover every subsystem the brief lists; the sections marked
*not yet audited* are honest gaps, not omissions of bad news.

Nothing in the renderer had been modified when the document was first written. Sections 2 and 3 have
since been superseded by later phases and say so where they stand; the superseded text is kept
because a wrong conclusion that set a phase order is worth being able to read.

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

## 2. The scene pass is fragment-bound, and its fragments are counted by triangles

**Superseded by Phase 3. The section as written said the opposite, and the measurement it rested on
was confounded.** Kept in the same form as section 3: the reasoning is what the phase order was
built on, and it has to be readable to see where it went wrong.

### What it used to say

| Resolution | pixels | GPU frame | scene pass |
|---|---:|---:|---:|
| 720×450 | 0.32 MP | 22.09 ms | 19.92 ms |
| 1440×900 | 1.30 MP | 24.25 ms | 20.71 ms |
| 2880×1800 | 5.18 MP | 47.97 ms | 40.96 ms |

Quadrupling the pixels moves the scene pass by 4%, so — the argument ran — the pass is paying for
vertices, draws and state rather than for pixels, and the phases that matter are visibility, LOD,
HLOD and instancing.

### The confound

`projScale` is `viewportHeight / (2 tan(fovY/2))`, and screen-space LOD selects against it, so a
taller render target selects *more* geometry. The sweep varied pixels and triangles together:

| resolution | pixels | submitted camera triangles | scene pass |
|---|---:|---:|---:|
| 720×450 | 0.32 MP | 436,176 | 21.50 ms |
| 1440×900 | 1.30 MP | 635,754 | 23.53 ms |
| 2880×1800 | 5.18 MP | 809,459 | 48.04 ms |

Neither a "slope in ms per megapixel" nor a "resolution-independent intercept" fitted to those
points means what its name says: the three rows are three different workloads.

### What is actually true

**The pass is fragment-bound.** The depth prepass submits *the same geometry* — the counters say so:
`camera 436176 tris / 2353 inst / 145 draws; depth 436176 / 2353 / 145` — through the same vertex
stage, and costs **0.20 ms**. The lit pass costs **21.30 ms** at 720×450. Vertices, draws and state
are the 0.20; everything else is the fragment stage. Cutting the fragment shader's work confirms it
and says where it goes (720×450, Glowmere, shader-only A/B through `AVGEN_SHADER_DIR`, min of 3
interleaved runs):

| fragment shader arm | scene | delta |
|---|---:|---:|
| baseline | 21.43 | — |
| `directLighting` returns zero | 4.98 | **−16.45** |
| `shadowFactor` returns 1 (keeps the contact march) | 11.14 | −10.29 |
| contact march skipped (keeps the shadow maps) | 14.81 | −6.62 |
| the clustered local lights skipped | 18.02 | −3.41 |
| the screen-space AO fetch skipped | 20.51 | −0.92 |

Three quarters of the pass is `directLighting`: two directional lights and a practical, each doing a
12-step screen-space contact march, plus the key light's PCSS cascade lookup, plus up to 32
bioluminescent cluster lights.

**But the number of fragments is set by the triangle count, not by the pixel count.** That is what
made the pass look geometry-bound, and it is why both readings of the old table were half right. The
ecology submits more triangles than the frame has pixels — 436,176 against 324,000 at 720×450 — and
a triangle smaller than a quad still costs a quad. So below roughly 1.3 MP the invocation count is a
floor set by geometry and barely moves with resolution, and above it screen coverage takes over:

| Glowmere, `directLighting`'s own cost | 360×225 | 720×450 | 1440×900 | 2880×1800 |
|---|---:|---:|---:|---:|
| ms | 12.00 | 16.06 | 14.28 | 32.83 |
| pixels | 0.081 MP | 0.324 MP | 1.30 MP | 5.18 MP |

Sixteen times the pixels between the first and third column, and the lighting costs 19% more.

**The marginal cost per pixel does not have a cliff in it.** Measured on the scene pass's own
geometry-free arms: terrain alone runs 8.30 → 3.39 → 2.88 ns/pixel from 0.32 to 5.18 MP (falling, as
a fixed floor is amortised), and a fixed-geometry scene — the authored procedural nodes only, single
instances with no ladder, so `projScale` cannot change what is submitted — is flat within the
timestamp's own quantisation across the same range. The scene pass writes five colour attachments
plus depth, and it was worth asking whether the tiler splits past some resolution and re-bins; these
two say it does not. The steepening in the old table is the geometry growing with the viewport.

### What it should have concluded

- **The phases that matter are still LOD and instancing** — but for a different reason, and with a
  different success criterion. The lever is *triangles*, because triangles buy fragment invocations,
  not because the vertex stage is expensive. A LOD change that halves submitted triangles and leaves
  the mesh count alone is a win; a draw-call merge that leaves the triangles alone is not.
- **Fragment-side work is not "not the lever".** It is three quarters of the pass, and it is the
  whole of it at the resolution the editor actually runs — see the canvas note below.
- **Dynamic resolution was dismissed on a bad number.** At 0.32–1.3 MP it buys little because the
  invocation count is geometry-floored. Above that the pass is genuinely per-pixel at roughly
  2–3 ms/MP, and the editor canvas is 3.4–4.4 MP.

### The resolution this is all quoted at is not the one the editor uses

The world is rendered into the dock centre at the display's backing scale: a 1440×900-point window
is a **2880×1166 = 3.36 MP** canvas, and 1920×1200 points is **2466×1766 = 4.36 MP**, against the
1.30 MP that "1440×900" has meant in every benchmark in this document. `--canvas-scale` and a
once-per-run log of the canvas's true size exist now; `docs/performance.md` carries it. Both
operating points are measured in section 2a.

## 2a. Where Glowmere's scene pass goes, by resolution

Min of 3 interleaved runs per cell, `--tier realtime`, headless, machine shared with other agents.
"before" is `df06bb8`; "after" is the LOD chain of ADR-084.

| canvas | pixels | submitted tris before → after | scene before → after |
|---|---:|---:|---:|
| 720×450 | 0.32 MP | 436,176 → 201,172 | 21.36 → **12.65** |
| 1440×900 | 1.30 MP | 635,754 → 396,493 | 20.97 → **18.15** |
| 2880×1166 (editor, 1440×900 pt) | 3.36 MP | 1,014,963 → 694,975 | 40.57 → **37.62** |
| 2466×1766 (editor, 1920×1200 pt) | 4.36 MP | 711,065 → 621,121 | 36.37 → **35.39** |
| 2880×1800 | 5.18 MP | 809,459 → 707,017 | 43.58 → **42.80** |

The shape of that table *is* the finding: cutting triangles takes 41% off the pass at the benchmark
resolution and 7% off it at the resolution the editor renders, because the two ends of the range are
bound by different things. The editor canvas is past the crossover, and what it is short of is
fragment throughput.

Note also that the 3.36 MP canvas is *slower* than the 4.36 MP one. It is wider (2.47:1 against
1.40:1), so its frustum holds more of the world — 1.01 M triangles against 0.71 M. Aspect ratio
moves this budget as much as pixel count does.

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
   frame is one pass. *(Phase 3: still true, but read section 2 for why — the pass is fragment-bound
   and the geometry is what buys the fragments.)*
3. ~~**Dynamic resolution and fragment-side work are not the lever at editor resolution.** Worth
   building for high-resolution output; not an answer to 25.7 ms.~~ **Wrong on both counts, and
   wrong because the editor does not render at 1440×900.** It renders at 3.4–4.4 MP, which is past
   the point where the pass becomes per-pixel. Fragment-side work is three quarters of the pass, and
   render scale is a real lever there. Section 2.
4. **GPU-driven submission is already partly present** and the CPU is not the bottleneck, so §17's
   later stages stay parked until measurement justifies them.
5. **Volumetrics are 4% of the frame.** Whatever their measurement error, they are not the problem.
