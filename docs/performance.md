# Performance

## Measure a reference scene alongside, every time (2026-09-10)

The machine drifts. Mid-session, `examples/world/_skyonly.scene.json` at 1280x720 went from
**8.4 ms to 27.4 ms** with nothing touched, and the world scene read 97 ms where it had read 52.
Read on its own, that looks exactly like a catastrophic regression in whatever was committed last,
and the next hour goes on bisecting a change that was never at fault.

So measure the reference scene in the same session, immediately before or after the thing being
measured, and quote the pair. It costs one extra run and it is the difference between "this change
cost 45 ms" and "this laptop is thermally throttled by 3x right now". If the reference has moved,
throw the numbers away and wait -- interleaving does not save you here, because a drift that large
swamps the effect being measured in both arms.

## Per-pass timers are not per-pass costs (2026-09-10)

The `passes:` line the renderer logs is a set of timestamp deltas around each render pass, and a
pass that follows a heavy one absorbs the drain of the work still in flight ahead of it. On the
world scene the volumetric pass reported **39.4 ms of a 46 ms frame**. Turning volumetrics off
took the frame from 53.1 ms to 47.7: the pass costs **5.4 ms**, and the timer was reporting the
lit pass finishing behind it. Every other pass on that line has the same failure mode; they do
not sum to the frame and the largest number is usually just the pass that follows the expensive
one.

Attribute cost by turning one thing off and diffing the frame median, interleaved. Measured that
way on the world scene at 2880x1800 (baseline 52.6 ms):

| removed | frame | cost |
|---|---|---|
| material programs (fireflies, fronds) | 46.8 | 5.8 ms |
| ecology lights (ADR-053) | 47.2 | 5.4 ms |
| volumetrics | 47.7 | 5.4 ms |
| airborne spores | 52.3 | 0.3 ms |

### Where the non-GPU time goes

With the camera turned to face empty sky at 1280x720, the world scene reports `gpu=12-14 ms`
and a wall-clock median of about 21 ms. The scene rebuild is not the gap: `cpu(scene)` (new on
the `passes:` line) measures 0.98 ms, against 0.07 for a sky-only scene. So roughly 7 ms per
frame is spent between submitting the frame and getting it back -- the offline loop waits on the
queue rather than pipelining, which is P3 of the optimisation spec and is still open. Every
frame number in this document is measured through that path and therefore includes it.

The frame scales at roughly 5.2 ms per megapixel. What does *not* scale with resolution is
larger than expected: at 1280x720 a sky-only scene renders in 8.4 ms, the world in 30.8, and the
world with the camera turned to face empty sky still costs 20.7 -- so about 12 ms goes on
geometry that is not on screen, before any of it is shaded. That is the next thing worth
attacking, and it is where the shadow cascades and the draw submission live.

## The correction that matters most (2026-09-09)

**"Ecology costs ~1.5 ms per scatter object per frame" was a measurement artefact, and the world
renderer optimisation spec was written against it.**

`tools/bench_world.sh` timed the whole process with `/usr/bin/time` and divided by the frame count.
Eleven scatter layers take about **two seconds longer to load** than none -- eleven glTF decodes and
eleven scatter placements -- and over a hundred frames that is twenty milliseconds a frame of
one-time work charged to the frames. Linear in layer count. Independent of resolution, triangle
count and instances drawn, *because it happens before any of them are involved*. Every property that
made it look like a submission bottleneck follows from it being scene build.

The renderer now times its own frames and reports a median after warm-up. The same runs:

| | process wall / frames | the renderer's own median |
|---|---|---|
| 1 scatter layer | 51.8 | 38.3 |
| 11 scatter layers | 68.9 | 42.1 |
| no ecology | 40.0 | 33.9 |
| **implied cost of 11 layers** | **28.9 ms** | **8.3 ms** |
| **implied slope** | **1.7 ms/layer** | **~0.4 ms/layer** |

So the spec's P1 target -- take the slope from 1.5 to 0.3 -- had already been met before it was
written, by a renderer that was never doing the thing it was accused of.

**What survives.** Any comparison between two variants that build the *same* scene is unaffected,
because the build time cancels: the shadow numbers, the volumetric numbers, the resolution sweep,
the shadow-distance and cascade A/Bs, and the material interpreter work (which the material agent
measured in-process anyway). **What does not survive** is anything comparing a scene with ecology
against one without, which is where the 21 ms ecology figure came from.

## Current baseline (2026-09-09, honest instrument)

`tools/bench_world.sh 2880x1800 100`, the renderer's own per-frame median:

| variant | ms/frame | delta |
|---|---|---|
| full | 42.8 | |
| shadows off | 27.4 | **shadows = 15.4 ms** |
| ecology off | 33.8 | ecology = 9.0 ms |
| volumetrics off | 38.2 | volumetrics = 4.7 ms |
| 1 / 3 / 6 layers | 38.1 / 35.0 / 37.1 | no measurable slope |

**Shadows are now the largest single item**, and neither of the two things that ought to explain them
does: cutting casters by 72% bought ~1 ms, and dropping a cascade bought ~1 ms. That remains open.

## How much of a difference is a difference

Six identical runs of the benchmark -- same scene, same 100 frames, same 2880x1800 -- came out at
77.0, 78.6, 79.4, 80.6, 80.2, 79.1 ms per frame. **The noise floor was about +-1.5 ms, so a claimed
saving under about 3 ms needed interleaved repeats before it meant anything.** With the renderer
timing its own frames and reporting a median after warm-up, and `tools/bench_ab.sh` interleaving,
that floor is now about +-0.3 ms.

That was learned the hard way. Running cascade counts sequentially gave 3 cascades 84.9 ms and 2
cascades 73.8 -- an eleven millisecond saving, and very nearly written down as one. Interleaved,
three reps each, it is 78.4 against 77.3: **1.1 ms.** The eleven was two runs of a drifting machine.

Applying that threshold to everything below:

| change | measured | verdict |
|---|---|---|
| removing the ground material program | 31.7 ms | far above the floor, and repeatable |
| that program from twenty ops to eight | 14.4 ms | above the floor |
| eliminating empty indirect draws | 6.5 ms | above the floor, measured once each way |
| terrain shadow distance | 1.1 ms | at the floor |
| ground cover not casting | ~1 ms | at the floor |
| three shadow cascades to two | 1.1 ms | at the floor |

The three at the floor are all kept, because each is *correct* independently of what it saves -- a
chunk half a kilometre away has no business in a cascade covering forty metres -- but none of them
should be described as an optimisation.

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

The per-layer slope survives *this* correction, but not the next one (see the last section of this
file): it is the scene build, not the frame. As measured here ecology looks like about 1.6 ms per
scatter object per frame, roughly resolution-independent (18 ms at 720p, 21 ms at 5 Mpixels), which is what a
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

About **1.5 ms per procedural object per frame**, fixed -- or so this said. It is scene-build time;
see the last section. Things that did *not* change it:

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

## Superseded: the material interpreter's cost was diagnosed wrongly here

The measurements in this section are right and the explanation offered for them is wrong. ADR-050
has the proved mechanism: not the per-pixel fetch of op records, but the interpreter's loop body --
a dynamically indexed register array that could not be register-allocated, and a Field evaluator
inlined into the loop that set the occupancy for every program whether it used fields or not. Per-op
cost is now 0.26 ms rather than 0.78, and a program of no ops at all got 2.5x cheaper.

Op count still matters, and the advice below still holds. The reason given for it did not.

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
op count, so the interpreter's loop does exit early. What it does not do is exit cheaply. The reason
given here -- a per-pixel read of the program's own op records out of a storage buffer, gigabytes of
traffic per frame -- **is wrong, and the next section takes it apart.** The fetch is about a fifth
of it. The depth prepass and the shadow passes do not
run it -- `fs_depth` only does alpha masking -- so this is the main pass alone.

The practical consequence for anything authoring a material program: **op count is the cost, and it
is not a small cost.** A full-screen material can afford single-figure ops. The palette went from two
three-stop ramps crossed over in the middle to one three-stop ramp, which for an ordered biome set
means its second and fourth entries stop being distinct stops and become the interpolations between
the ones that remain. That is what an ordered set is for, and on screen the difference is not
visible.

## The interpreter, taken apart (2026-09-09, ADR-050)

**The section above is right about the number and wrong about the reason.** It is not the fetch.

A compute probe of 5.18 M invocations, one `evaluateMaterialProgram` each
(`tests/rendering/test_material_perf.cpp`, `[.perf][material]`), reproduced the cost at 0.78 ms per
op in a bare dispatch and then bisected it:

| what the probe runs, 20 iterations | ms per op |
|---|---|
| 20 x `Constant` -- the interpreter, no arithmetic at all | 0.781 |
| 20 x `Noise` -- the interpreter, a full fbm3 per op | 0.943 |
| only the 112-byte storage fetch of an op record | 0.145 |
| only a dynamically indexed `array<vec4<f32>, 8>` read-modify-write | 0.271 |
| the fetch **plus the whole 31-branch `materialEvalOp` body** | 0.110 |

A `Constant` op does no arithmetic and an fbm3 does a hundred ops' worth, and they are 21% apart:
**it is not the arithmetic.** The fetch is a fifth of the cost, and fetching a record and running
the entire op switch over it is *cheaper* than fetching it and adding up its fields, because the
compiler sinks each load into the branch that wants it: **it is not the fetch either.**

It was two things, both about the *loop body* rather than any op:

1. **The register file lived in memory.** A dynamically indexed local array cannot be held in
   registers, so `regs[dst] = f(regs[srcA], ...)` was a loop-carried dependency through
   thread-private indexable memory, with the addresses themselves arriving from another load.
2. **A Field op inlined the whole of `fields.wgsl`** -- `combineScalar`'s four-child loop over
   `basicScalar`, each a grid fetch, a falloff and a noise -- into the op loop's body. The body's
   size sets the shader's register allocation and so its occupancy, so **every material program paid
   for that, including programs with no Field op and including a program of zero ops.**

The second is the larger of the two and the one no amount of reasoning about buffer traffic would
have found. Deleting that one branch took a program of *no ops at all* from 1.39 ms to 0.47.

Both fixed (ADR-050): the register file is eight named vec4s reached through a select tree, and
every distinct field is sampled once before the interpreter runs -- exact, because a Field op's
value depends only on its slot and the world position, neither of which changes while a program
runs. Interleaved A/B with both interpreters compiled into one process:

| program | before | after |
|---|---|---|
| 20 x `Constant` | 0.781 ms/op | **0.263** |
| 20 x `Mix` | 0.782 | **0.282** |
| 20 x `Noise` | 0.943 | **0.684** |
| terrain-shaped, 12 ops | 1.150 | **0.424** |
| a program of zero ops (the fixed cost of having one) | 1.75 ms | **0.70 ms** |

Glowmere at 2880x1800, 60 frames, two binaries interleaved four times each, minimum:

| variant | before | after | saved |
|---|---|---|---|
| full | 60.3 ms/frame | **45.7** | 14.7 ms |
| ecology off | 57.2 | **37.5** | 19.7 ms |

`examples/world/terrain.scene.json` is bit-identical before and after.

**The measurement method is the other result.** Two other agents were building and benchmarking
throughout, and the same probe read 1.44 ms and 2.77 ms for the identical workload twenty minutes
apart. Nothing here is a difference between two runs. Every number is an interleaved A/B inside one
process -- for the shader, two `ShaderLibrary` search paths so both versions compile into the same
binary; for the frame, two `avgen` binaries alternated, the older one pointed at the older shaders
through `AVGEN_SHADER_DIR` -- and the statistic is the **minimum**, not the median, because
contention is never negative.

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
fell by about **1.1 ms**, which is the noise floor. Ground cover opting out of casting bought about
another 1 ms, also at the floor. Cutting the cascades from three to two, which drops the caster
count again and is visually indistinguishable here, bought a further 1.1 ms. All three are at the
floor.

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


## The benchmark was measuring the scene build (2026-09-09, ADR-051)

**The 1.5 ms per scatter layer was not a frame cost.** `tools/bench_world.sh` timed the whole
process and divided by the frame count. Eleven scatter layers take about two seconds longer to load
than none -- eleven glTF decodes, eleven scatter placements, eleven sets of LOD meshes -- and over a
100-frame run that is 20 ms a frame of one-time work charged to the frames.

| scatter layers | process load, s | old ms/frame (process / frames) | actual ms/frame |
|---|---|---|---|
| 0 | 0.79 | 54.1 | 43.7 |
| 1 | 1.08 | 61.2 | 52.2 |
| 3 | 1.80 | 68.2 | 54.1 |
| 6 | 2.29 | 73.2 | 51.4 |
| 11 | 2.78 | 79.4 | 52.7 |

Least squares over 1, 3, 6 and 11 layers: the old slope is **1.73 ms per layer**, the real one is
**-0.03**. Zero. The predicted phantom is 1.99 s of extra load over ten layers, over 100 frames:
2.0 ms per layer, which is the whole of it.

It survived scrutiny because it behaved exactly like a submission cost. It did not move with
resolution, triangle count, instances drawn or chunk count -- because it happens before any of them.
**A cost that is invariant to everything about the work may not be a cost of the work at all; it may
not be in the frame.**

### The instrument

`avgen --headless` now times each frame and reports `median / p10 / p90 / min` over the run after a
twelve-frame warm-up. The median matters as much as the per-frame timing: a typical run's p90 is
twice its median, because a concurrent build steals whole frames outright, and an average of a
hundred frames is an average of that theft. `tools/bench_ab.sh` interleaves configurations round by
round and takes the median over rounds.

**The noise floor is now about ±0.3 ms, not ±1.5.** Repeat runs of one binary land within 0.3 ms of
each other (48.44, 48.53, 48.61, 48.61, 48.62, 48.64, 48.72, 50.86). The earlier ±1.5 ms was mostly
the instrument, not the machine.

One trap it does not remove: **shaders are read from the working tree at run time**, so a stashed
baseline binary run today runs today's shaders. Pairing an old binary with a new `cull.wgsl` here
produced a frame missing two thirds of its ground cover, which is indistinguishable from a real
rendering bug and was chased as one for half an hour. `bench_ab.sh` takes `binary@shaderdir`; pin
both sides of every comparison.

### Where the ecology actually goes

At 2880x1800, 11 layers, ~37,000 instances of which ~900 survive culling, by disabling one stage at
a time in the same binary (so the scene build is identical on both sides):

| | ms/frame | |
|---|---|---|
| procedural renderer entirely inert | 43.4 | = a world with no ecology at all (43.7) |
| every state change recorded, no draws | 44.0 | recording state: **0.6 ms** |
| normal | 51.7 | the draws: **7.7 ms** |
| normal, cheapest LOD mesh at every level | 45.6 | of which geometry: **6.1 ms** |

So the ecology costs about 8 ms, not the 26 the old table said, and it is **geometry, not
submission**. Setting the pipeline, the bind groups and the vertex and index buffers 72 times a
frame -- five passes' worth -- costs six tenths of a millisecond.

### One shared indirect buffer

The exception, and the only part that scaled with the number of layers, was the indirect buffers
themselves: one per object. Replacing every `DrawIndexedIndirect` with a direct `DrawIndexed` of a
CPU-supplied count saved 1.4 ms; keeping the indirect draws but pointing all 72 at a single buffer
saved 1.56 ms. Once there is one buffer an indirect draw costs what a direct one costs, so the
overhead was per buffer, not per draw.

They are now slots in one 20 KB buffer, indexed by the slot each object already uses for its cull
stats.

| | before | after |
|---|---|---|
| indirect buffers per frame | 11 | **1** |
| indirect draws | 72 | 72 (unchanged) |
| 11 layers at 2880x1800 | 55.48 ms | **54.02 ms** |
| 1 layer | 52.32 | 52.64 (nothing, correctly) |
| slope, 1 -> 11 layers | 0.32 ms/layer | **0.14 ms/layer** |

Medians of six interleaved rounds each, every round within 0.5 ms of its median. The saving at
eleven layers reproduced in three separate sessions -- **-1.24, -1.27 and -1.46 ms** -- whose
absolute levels differed by 6 ms, which is what a machine that drifts and a delta that does not
look like. **Visual impact: none** -- 0 of 921,600 pixels differ.

At one layer it saves nothing, because there was one buffer either way. That is the mechanism
confirming itself, and it is the reason to believe the 1.3 ms at eleven.

### What this says about the rest of P1

The spec's remaining directions -- a shared instance buffer with a per-instance `speciesId`, merging
LOD levels into one draw, texture arrays so a material family shares a pipeline -- all trade
submissions for vertex work, because a single indexed draw cannot vary its index count per instance
and the meshes must be padded to a common size (1,400 triangles against a mean of 445) or drawn
through vertex pulling. Submissions here are worth about a millisecond in total; geometry is worth
six. **There is no per-layer submission cost left to remove.** The lever on the remaining 6 ms is
the LOD ladder and the mesh budgets.
