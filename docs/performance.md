# Performance

Measurement infrastructure exists from day one; nothing is optimised yet.

## What is measured

| Metric | Where | How |
|---|---|---|
| CPU frame time, FPS | `Application::runLive` → `ui::FrameStats` | steady_clock around the frame; FPS averaged every 0.5 s |
| GPU frame time | `gpu::GpuTimer` | timestamp queries at the start of the scene pass and end of the tone-map pass, 4-slot mapped ring, no stalls |
| Draw calls, triangles, entities | `rendering::RenderStats` | counted per frame |
| Analysis time per hop | `AnalysisRunner::averageHopMicros` | steady_clock per hop, EMA 0.1 |
| Modulation + scene update time | `EngineStats::modulationMicros` | steady_clock around `Engine::update` |

All of these are displayed in the Control window and logged every 120 frames at debug level.

## Milestone 0.1 numbers (Apple M2 Max, macOS 26.6, Debug build unless stated)

| Metric | Value |
|---|---|
| GPU frame (scene + tone map) at 1280x720, headless | 0.07-0.13 ms |
| GPU frame at 2880x1800 window (scene + tone map, UI excluded from the timer) | 0.39-0.59 ms |
| Live window, Release | 120 fps (ProMotion vsync), CPU work 1.4-1.8 ms/frame |
| Live window, Debug | 60 fps (vsync), CPU work ~3 ms/frame (the earlier 16.6 ms figure included the vsync wait) |
| Analysis per hop (N=2048, H=512) | ~100 µs Debug, ~17 µs Release (0.16% of the hop period) |
| Offline frame incl. synchronous readback, 1280x720 | ~6 ms Release (250 frames in 1.53 s wall), ~43 ms Debug |
| Decode 24 s stereo 48 kHz WAV | ~100-150 ms |
| Test suite | 146 tests, ~5 s Debug |

## Milestone 0.2 numbers (Apple M2 Max, Release unless stated)

| Metric | Value |
|---|---|
| DamagedHelmet load (3.8 MB, five 2048² PNGs) | 161 ms Release, 835 ms Debug (stb PNG decode) |
| Environment preprocessing, 1k HDRI (cube 256 + 9 mips, irradiance 32, prefiltered 128 x 6, BRDF 128) | 18 ms Release, 127 ms Debug |
| Helmet + IBL + skybox, 2880x1800 window | 120 fps (vsync), CPU work 0.3 ms, GPU 1.0-1.2 ms |
| Helmet headless 1280x720 incl. readback | ~6 ms per frame |

## Milestone 0.5 numbers (Apple M2 Max)

| Metric | Value |
|---|---|
| Orb scene + 131k-capacity sparks, 2880x1800 window | 120 fps, GPU 0.79 ms (was 0.4 ms without particles), CPU work 1.2 ms (Debug) |
| One-million-particle pool, ~1M alive, 1280x720 headless (`[.perf]` probe) | 3.4 ms GPU per frame (emit + simulate + indirect draw) |
| Particle uniform update per system | one 288-byte write; no readback |

## Milestone 0.6 numbers (Apple M2 Max, Debug window 2880x1800)

| Metric | Value |
|---|---|
| Orb scene + sparks + default post chain (bloom 6 levels + composite + tone map) | 120 fps, GPU 3.3 ms (2.5 ms is the post chain at full 2880x1800) |
| Post chain passes at 1280x720 with DoF + motion blur + bloom | 13 passes, ~13 transient textures reused every frame |

Bloom at native Retina resolution dominates; starting the chain at quarter resolution is the
obvious optimisation when the budget tightens.

Superseded for the post chain by the ADR-037/ADR-039 reordering: see
`docs/performance/image-formation.md` for 1080p and 4K measurements of exposure, bloom, halation,
anamorphic and depth of field on and off (bloom 0.3 ms at 1080p; depth of field is now the
expensive stage).

## Procedural geometry (ADR-023)

Instanced procedural objects with the GPU deformer stack: 1k/10k-instance probe in
`docs/performance/procedural-geometry.md` (10k instances of a 24-segment cylinder, 2.4 M
triangles, one draw: 3.6 ms undeformed, 4.5-5.6 ms with one to three deformers at 720p).

## Milestone 0.7 numbers (Apple M2 Max, Release)

| Metric | Value |
|---|---|
| Six-node composition (2x DamagedHelmet, orb, grid, BoxTextured, nested MetalRoughSpheres + particles, studio HDRI), 2880x1800 window | 120 fps (vsync), GPU 1.9 ms, CPU work 1.8-2.4 ms |
| Same composition headless 1280x720 | GPU 0.59 ms per frame |
| Scene file load (three glTF assets, one nested scene) | 163 ms Release, of which DamagedHelmet decode 153 ms; second helmet instance free (registry cache) |
| Composition rebuild (flatten) | structural changes only; per-frame cost is one TRS compose per entity |

## Milestone 0.8 numbers (Apple M2 Max, Release)

| Metric | Value |
|---|---|
| Orb scene + 3 timeline tracks + 2 cues, 2880x1800 window | 120 fps, GPU 1.4 ms, CPU work ~1 ms |
| Timeline evaluation | binary search per track per frame; negligible next to modulation |

## Milestone 1.0 numbers (Apple M2 Max, Release): deterministic particle compaction

| Metric | Value |
|---|---|
| One-million-particle pool, ~1M alive, 1280x720 headless (`[.perf]` probe), atomic dead/alive lists (before) | 2.29 ms GPU per frame |
| Same probe with stable prefix-sum compaction (after: emit + simulate + reduce + top scan + scatter + indirect draw) | 2.61-2.73 ms GPU per frame (+15%, two runs) |
| Orb scene headless 1280x720, 240 frames at 30 fps, run twice | 240/240 identical per-frame hashes, 0 GPU errors |

The extra cost is two passes over the 4 MB flag buffer plus a 4 KB block-sum scan; curl-noise
simulation still dominates. The earlier 3.4 ms figure above was a different build of the same
probe; the before/after pair here was measured back to back on the same binary configuration.

## Milestone 1.0 offline rendering numbers (Apple M2 Max, Release)

| Metric | Value |
|---|---|
| Six-node composition + HDRI + timeline, 1920x1080 PNG sequence, 8 encoder threads | 50 fps (60 frames in 1.2 s incl. readback and PNG encode) |
| Orb scene + sparks, 1280x720 PNG sequence | 102 fps (300 frames in 2.9 s) |
| Readback | synchronous per frame; the GPU idles while the CPU maps (a staging ring is the next step) |

## Budget and revisit triggers

- Analysis: switch FFT backend (pffft/vDSP) if hop time exceeds 10% of the hop period.
- Render: MSAA and post-processing will add cost; the frame graph in 0.6 introduces a transient
  pool so intermediate targets are not reallocated.
- Offline: replace the synchronous readback with a 3-deep staging ring when the render-job runner
  arrives (1.0).
- Memory: `AnalysisTrack` ≈ 0.8 MB per second of audio; whole-file decode ≈ 0.4 MB per second
  of stereo 48 kHz.

## Where a world frame goes (2026-09-09)

**The first version of this section was wrong, and how it was wrong is the most useful thing in
it.** It is kept below the corrected numbers because the mistake is a standing hazard.

`--size` was ignored in headless mode: `runHeadless` rendered a hard-coded 1280x720 whatever was
asked for. Every headless measurement that varied resolution therefore compared 1280x720 against
1280x720, three times, and concluded that the frame was not fragment-bound. It is. With the flag
honoured, and with the per-frame determinism hash (a full scan of the image, a fifth of the wall
time at 2880x1800) computed only when something reads it:

| resolution | ms/frame |
|---|---|
| 720x450 | 42.8 |
| 1440x900 | 52.6 |
| 2880x1800 | 110.2 |

### Baseline at the resolution the app actually runs

`tools/bench_world.sh 2880x1800 120`, wall clock, Glowmere with terrain, water and eleven scatter
layers:

| variant | ms/frame | delta |
|---|---|---|
| full | 87.0 | |
| ecology off | 65.8 | **ecology = 21.2 ms** |
| shadows off | 71.3 | shadows = 15.7 ms |
| volumetrics off | 80.8 | volumetrics = 6.2 ms |
| 1 scatter layer | 71.6 | |
| 3 scatter layers | 75.0 | |
| 6 scatter layers | 80.5 | ~1.6 ms per layer |

The per-layer slope survives the correction: ecology still costs about 1.6 ms per scatter object per
frame and is roughly resolution-independent (18 ms at 720p, 21 ms at 5 Mpixels), which is what a
submission-bound cost looks like. What did not survive is the claim that the *frame* is not
fragment-bound. It is: two thirds of it scales with pixels.

### The pass timers measure a stall, not the pass

`volumeMs` reports 51 ms of an 87 ms frame. Turning volumetrics off saves 6.2 ms. Both numbers are
repeatable. A timer that brackets a pass costing six milliseconds and reports fifty-one is bracketing
something else -- on this backend the GPU is idle inside that pass waiting on work queued before it,
and removing the pass moves the wait rather than removing it. The same applies to `gpuFrameMs`.

They are not, however, inert: a 64x change in volumetric steps moves `volumeMs` by 20% and wall clock
by 16%, in step. So they respond to workload while misattributing where it happened. **Use them to
detect that something changed; use wall-clock differences between scene variants to say what.** An
earlier version of this note declared them simply INVALID on the strength of a 3x workload change
that moved neither -- too small a change to see past a pass that is not dominated by it.

### Submission accounting

At 2880x1800, per frame: **220 indirect draws, 110 of them empty.** That is 11 scatter objects x 4
LOD levels x 5 passes (depth prepass, main, three shadow cascades). The LOD distribution is
733/161/10/0 instances, so LOD 3 is empty for every object and LOD 2 for nearly all of them -- the
CPU records those draws anyway, because the instance count is written by the GPU cull pass and is
not knowable when the draw is recorded.

Those counters read zero when they were first added, because `stats_.procedural` is snapshotted
during `update()`, before a single draw is recorded. Anything counted while recording was being
thrown away. It is now re-read after the passes.



### Superseded: the original note, kept for the mistake

Measured on an M2 Max, 200 headless frames at what was believed to be 1440x900 but was in fact
1280x720 for all of them. **The built-in per-pass GPU timers were called untrustworthy**: `volumeMs` reported a constant 14.5 ms at every resolution from 720x450 to 2880x1800
*and* at every step count from 18 down to 6. A pass whose measured cost is invariant to both its
pixel count and its loop count is not being measured. `gpuFrameMs` is flat across the same 16x pixel
range for the same reason. Everything below is wall-clock difference between scene variants, which
is the only instrument here that has held up.

| variant | ms/frame | delta |
|---|---|---|
| full (terrain + ecology + shadows + volumetrics) | 41.5 | |
| volumetrics off | 40.0 | volumetrics = 1.5 ms |
| shadows off | 34.4 | shadows = 6.5 ms *with* ecology |
| ecology off | 23.5 | **ecology = 18 ms** |
| neither ecology nor shadows | 22.2 | shadows = 1.3 ms *without* ecology |

Then the surprise. The ecology cost is **linear in the number of scatter objects and independent of
what they draw**:

| scatter layers | ms/frame |
|---|---|
| 0 | 23.5 |
| 1 | 28.8 |
| 3 | 32.0 |
| 6 | 35.1 |
| 11 | 39.8 |

About **1.5 ms per procedural object per frame**, fixed. Things that did *not* change it:

- **Resolution.** Flat from 720x450 to 2880x1800. The frame is not fragment-bound.
- **Triangles.** Switching on the LOD ladder (it defaults to one level, so every surviving instance
  was drawing its full-resolution mesh) saved 1.1 ms.
- **Instances drawn.** Per-layer view distances, cutting grass from 520 m to 70 m, saved nothing.
- **Terrain chunk count.** 256 chunks or 25, no difference.
- **Volumetric steps or reach.** 18 steps over 320 m or 6 over 120, no difference.

Culling itself is working: 6,400 instances survive and 31,200 are culled each frame.

So the ceiling on a world is not its instance count, its triangle count or its resolution -- it is
**how many distinct scatter layers it has**. The cull work is already batched into a single compute
pass, so the overhead is in the draw path: each object issues up to four indirect draws in each of
the depth prepass, the main pass and every shadow cascade -- around 264 indirect draws for eleven
layers -- and those indirect buffers were written by a compute pass in the same command buffer.
That is the shape of a compute-to-indirect-draw hazard serialising the render passes, and it is
consistent with the CPU sample, which found 73% of the main thread blocked in `waitForQueue`.

Confirming that needs a Metal frame capture rather than the in-engine timers. The two fixes it
points at, in order: draw a layer's LOD levels through fewer indirect draws (or skip empty levels
without submitting), and cull per shadow cascade instead of reusing the camera's visible set --
cascade 0 covers about forty metres and is currently drawing everything the camera can see.

## The material program interpreter is priced per op, per pixel (2026-09-09)

Bisecting the base frame at 2880x1800, with ecology, volumetrics and bloom all off and only 86 draws
in the frame, found 64.7 ms. An empty scene is 9 ms. So the terrain -- 86 draws of one material --
was 55 ms.

Removing the generated ground material program entirely (a world with no biomes gets none) took the
frame to 33 ms. **The program cost 31.7 ms of an 87 ms frame**: a third of the frame spent painting
the ground. Cutting it from twenty ops to eight took that to 14.6 ms.

| | ops | ms at 2880x1800 |
|---|---|---|
| no program at all | 0 | 35.7 |
| eight-op palette | 8 | 48.1 |
| eight ops plus mottling | 12 | 50.3 |
| the original | 20 | 64.7 |

That is roughly **1.6 ms per op over a full-screen surface at five megapixels**, and it is linear in
op count, so the interpreter's loop does exit early. What it does not do is exit cheaply: the shape
of the cost is a per-pixel read of the program's own op records out of a storage buffer, which at
this size is gigabytes of buffer traffic per frame. The depth prepass and the shadow passes do not
run it -- `fs_depth` only does alpha masking -- so this is the main pass alone.

The practical consequence for anything authoring a material program: **op count is the cost, and it
is not a small cost.** A full-screen material can afford single-figure ops. The palette went from two
three-stop ramps crossed over in the middle to one three-stop ramp, which for an ordered biome set
means its second and fourth entries stop being distinct stops and become the interpolations between
the ones that remain. That is what an ordered set is for, and on screen the difference is not
visible.

## P2: empty indirect draws (2026-09-09)

**CHANGE.** A LOD level that has had no instances for three consecutive frames stops being recorded.

**WHY.** Half of every frame's indirect draws were empty: 220 recorded, 110 with a zero instance
count. The LOD distribution is 733/161/10/0, so level 3 is empty for every object and level 2 for
nearly all of them. The CPU cannot know a level is empty when it records the draw -- the count is
written by the GPU cull pass -- but it can know the level has been empty for a while, which for the
far levels of a scatter is almost always true and almost never about to stop being true.

Level 0 is always recorded. It is the level an object enters when it comes into view, and one frame
of it missing is an object popping in. An instance arriving a frame late in level 2 or 3 is a
distant billboard.

| | before | after |
|---|---|---|
| indirect draws | 220 | 110 |
| empty draws submitted | 110 | **0** |
| frame at 2880x1800 | 85.8 ms | **79.3 ms** |
| 6 layers | 80.5 ms | 71.9 ms |
| 3 layers | 75.0 ms | 66.9 ms |

**VISUAL IMPACT.** None: the rendered frame is bit-identical, 0 of 921,600 pixels differing.

**What it did not do** is change the slope. Cost per scatter layer is 1.55 ms after and was 1.35 ms
before -- the whole curve moved down by about 0.6 ms per object rather than tilting, because the
draws removed are a fixed two per object rather than a share of its work. Decoupling logical
diversity from submission count is still P1's job.

## Where the frame stands (2026-09-09, after P0/P2 and the material fix)

At 2880x1800, 120 frames, `tools/bench_world.sh`:

| variant | ms/frame | delta |
|---|---|---|
| full | 79.8 | (was 93.9) |
| shadows off | 65.2 | **shadows = 14.6 ms** |
| ecology off | 53.6 | ecology = 26.2 ms |
| volumetrics off | 75.1 | volumetrics = 4.7 ms |

Ground cover no longer casts shadows -- grass, ferns, flowers and pebbles cast shadows smaller than
a shadow-map texel at the sizes they are drawn. **That bought about 1 ms, not the several it looked
like it should**, because those layers already have short view distances (70-120 m) and little of
them reached a cascade in the first place. The casters that remain are the ones with a 500 m reach:
trees, and the bushes and fungi that are numerous.

So the shadow cost is not "too many small things casting"; it is that every camera-visible caster is
drawn into every cascade, including cascade 0, which covers forty metres. That is P4, and it needs
the cull pass to run per view rather than once for the camera. It is now the largest remaining
single item after ecology.

`volumeMs` continues to report ~44 ms for a pass whose removal saves 4.7. Read it as a stall
indicator, not as volumetric cost.

## P4/P5: the shadow cost is not the casters (2026-09-09)

**CHANGE.** `Entity::castsShadow` and `ProceduralGeometry::castsShadow`, a `shadowDistance` on the
terrain, and a `castsShadow` on each scatter layer. The shadow passes skip what does not cast.

**WHY.** Shadows measured 15.6 ms of an 81.5 ms frame at 2880x1800, and 9.2 ms of that was the
*terrain*, not the ecology. The cascades are fitted from the scene's radius, so on a 640 m world
they reach past a kilometre and every chunk the camera can see is a caster in every cascade,
including cascade 0, which covers forty metres.

**RESULT.** Terrain shadow entity draws fell from **255 to 72** -- a 72% cut in casters. Frame time
fell by **1.1 ms**, A/B'd twice at 120 frames (76.2/75.1 against 76.7/76.8). Ground cover opting out
of casting bought about another 1 ms.

**So the shadow cost is not the number of casters.** Removing seven casters in ten bought seven per
cent of the shadow cost. Whatever the other 13 ms is, it is in the passes themselves -- their setup,
their depth targets, the fixed cost of three cascades -- and not in the geometry submitted to them.

That is a result about the spec's P4 more than about this change: **per-cascade culling is unlikely
to repay its complexity.** It is a more precise way of doing the thing that has just been shown to
be worth about a millisecond. The lever that is left is the number and resolution of the cascades,
which is a quality-tier decision, not a culling one.

The change is kept because it is *correct* -- a chunk half a kilometre away has no business in a
cascade covering forty metres -- and because the counters it added are what made the negative result
legible. 95 pixels of 921,600 differ, all at the far shadow boundary.
