# Performance

> **This document is the GPU's frame.** The main thread -- the UI, the event loop, the engine
> update, threading, allocation behaviour and input latency -- is
> [docs/application-performance.md](application-performance.md) (ADR-084). Keeping them apart is
> deliberate: "why is the editor slow" has two very different answers and they should not be
> searched for in the same place.
>
> One number from there belongs here, because every benchmark below is affected by it: **the editor
> does not render the world at the window size.** It renders it into the dock tree's centre region
> at the display's backing scale, which on this machine is 3.36 Mpx in a 1440x900-point window and
> 4.36 Mpx in a 1920x1200 one -- 2.6x and 3.4x the 1.30 Mpx that "1440x900" means below. A pass
> costing 25.7 ms here costs considerably more in the editor as actually used. Every run now logs
> the canvas's real size once; `--canvas-scale` sets it.

## The ecology is drawn instances, not submitted draws (2026-09-10, P1/P2)

**The optimisation spec's P1 and P2 were aimed at the wrong thing, and this section is the
evidence.** Submitting the scatter layers' indirect draws is about **1% of the frame**. What the
ecology costs is rasterising the instances that survive the cull, and that is not a submission
problem.

### Use the minimum, not the median, over a long run

Every number below is `min` over 300 headless frames at 1440x900, not the median. Over 120 frames
the median was usable; over 300 it is not. The same three arms, run twice in a row:

| arm | median run 1 | median run 2 | median run 3 | min |
|---|---|---|---|---|
| sky only | 3.97 | 6.44 | 7.56 | 3.7 |
| world | 53.29 | 25.06 | 75.44 | 22.3 |
| world, no ecology | 10.02 | 15.74 | 10.23 | 9.7 |

The medians differ by 3x between identical runs; the minima agree to a tenth of a millisecond.
A longer run does not average the contention out, it *collects* more of it, and the median walks
with it. Contention is never negative, so the minimum is the statistic (the same conclusion
ADR-050's material work reached, for the same reason). Calibration for everything below:
`examples/world/_skyonly.scene.json` at **min 3.6-4.9 ms**.

### Where the ecology goes

Five arms of `examples/world/terrain.scene.json` at 1440x900, interleaved, minima:

| arm | min ms | what it isolates |
|---|---|---|
| full | **22.3** | |
| no ecology (`scatter: []`) | **9.9** | the ecology is **12.4 ms**, 56% of the frame |
| every record culled, all 160 draws still recorded | **11.9** | drawing the survivors is **10.4 ms** |
| cull compute chain not encoded at all | **11.75** | the cull chain is **0.15 ms** |
| full + 1920 extra recorded empty indirect draws | 25.1 | **1.46 us** per recorded draw |

So the 12.4 ms divides as:

| | ms | share of the ecology |
|---|---|---|
| rasterising the 1281 surviving instances over five passes | **10.4** | **84%** |
| per-layer setup and the ADR-053 ecology light field | ~1.6 | 13% |
| the 160 indirect draws, with all their state setting | **0.23** | **2%** |
| the four-dispatch cull chain over 36k records | **0.15** | **1%** |

The per-draw price is measured directly, by recording extra draws that are provably empty (a
zeroed slot of the shared indirect buffer) with the full pipeline / bind group / vertex / index
state in front of each: 1920 of them cost 2.8 ms, or **1.46 microseconds each**. A frame that
records 120 spends **0.24 ms** doing it. There is no draw-consolidation win here to be had: even
collapsing every scatter layer to a single draw could not return a quarter of a millisecond.

The GPU's own cull timer agrees, and so does the throughput test in
`tests/rendering/test_culling_gpu.cpp`: **0.08 ms to cull 100k boxes**.

### "It scales with what the GPU culls, not what it draws" is wrong

That claim was the premise of the P1/P2 brief. It does not survive an A/B in which the record
count is held fixed and the drawn count moves. Every layer's `minScreenRadius` x4 (drawn falls,
records do not), and every layer's `viewDistance` x0.5:

| arm | records | drawn | min ms |
|---|---|---|---|
| full | 35,969 | 1281 | 22.3 |
| `minScreenRadius` x4 | 36,592 | 658 | **19.4** |
| `viewDistance` x0.5 | 36,879 | 371 | **16.9** |

The record count is flat to within 2.5% across all three; the frame moves by 5.4 ms. Both arms lie
on the same line: **about 5 microseconds per instance actually drawn**, per frame, across the depth
prepass, the lit pass and three shadow cascades. Raising `minScreenRadius` *did* help -- 2.9 ms --
which is the specific observation the earlier claim was built on reading the other way.

Per-pass shares of that 12.4 ms, by turning one thing off: ecology shadow casting is **1.5 ms**
(`castsShadow: false` on all twelve layers: 23.0 -> 21.5). The rest is the prepass and the lit
pass, where the bushes -- the layer that covers the most screen -- are 4.2 ms on their own. It is
fragment work on alpha-masked foliage with a material program, and it belongs to the overdraw and
material-cost items of the spec, not to submission.

### What P1/P2 could still be made to do, and what it bought

Two things were changed, both of which are correct independently of what they save, and neither of
which saves anything measurable on this scene:

- **Whole-object rejection.** `objectFullyCulled` proves on the CPU, from the object's cached
  record bounds, that `cull.wgsl` must reject every record; that object's cull dispatches and all
  of its indirect draws in every pass are then never encoded. It is sound by construction and
  tested against the per-instance reference over a sweep of cameras and limits.
- **Redundant state.** An object's LOD levels share a material and usually a pipeline, and
  consecutive layers often share a pipeline; only what changes is now set. The four per-LOD uniform
  writes became one.

Frame before/after, interleaved, three reps each against a 4.2-6.3 ms sky calibration: **22.29 /
22.69 before, 22.49 / 22.59 after.** The world capture is bit-identical, 0 of 1,440,000 pixels
differing.

The whole-object rejection does not fire on this scene at all, and the reason is worth writing
down: **a scatter's bounding box is the whole terrain, and the camera stands inside it.** No
frustum plane can reject a box that contains the viewer, however few of its instances are visible.
It fires for compact objects -- a hero organism, a boulder field, anything the camera can get
outside of -- and never for ground cover. Even in `_skyonly`, where all 37,146 records are culled
and all 36 recorded draws are empty, one box per layer cannot prove it. Making it fire there would
need per-cell bounds over spatially sorted records, and at 1.46 us a draw it would be buying
**0.05 ms**.

**The standing conclusion: stop optimising the submission path.** It is a quarter of a millisecond.
The ecology's cost is 1281 instances of alpha-masked foliage rasterised five times, and the levers
that move it are overdraw, the fragment cost of the foliage material, and how many instances the
LOD ladder and the cascades ask for -- not how many draws carry them.

## What the ecology actually costs, and why the obvious levers do nothing (2026-09-10)

Five measurements, each repeatable to about 0.05 ms, all at a stable `_skyonly` calibration of
2.6-2.9 ms. Together they rule out every explanation I started with.

| what was varied | result |
|---|---|
| framebuffer 0.32 MP -> 5.18 MP (16x) | ecology 16.9 -> 19.4 ms. **Not fill-bound.** |
| triangle budget, 15.7M -> 5.3M submitted (-66%) | 21.6 -> 23.4 ms, i.e. slightly *worse*. **Not vertex-throughput-bound.** |
| drawn instances, records held fixed (`minScreenRadius` 2.5 -> 200) | 21.7 -> 5.7 ms. **Linear in instances drawn.** |
| view distance x0.5 at fixed 720x450 | 21.6 -> 9.8 ms, consistent with the above |
| shadows + AO + volumetrics + post all disabled | 21.7 -> 19.5 ms. Everything outside the lit pass is 2.2 ms. |

So the cost is **per drawn instance**, and almost nothing else: not pixels, not triangles, not
the passes around it. About 6 us per instance.

Then the lever that should have followed from that made it worse. Pushing the LOD ladder so most
instances draw as camera-facing billboards instead of meshes -- same 1281 instances drawn, split
29/178/831/243 across levels instead of 338/746/191/6 -- cost **+11.6 ms** (21.0 -> 32.6).

The hypothesis that fits all six results is tile binning. This is a tile-based deferred GPU:
geometry is binned into tiles before any shading, and that work scales with primitives and with
*how many tiles each primitive touches*, largely independently of framebuffer resolution and of
shading. A billboard circumscribes the source's bounding sphere, so it covers far more tiles than
the plant it replaces -- which is exactly the wrong trade here, and explains why fewer, larger
primitives lost to more, smaller ones.

Treat that as the leading explanation rather than a proven one: it is consistent with all five
rows plus the billboard result, but it has not been confirmed against a GPU trace.

**Consequences for optimisation.** The levers that work are the ones that reduce the number of
instances submitted, or the tiles they touch. The levers that do not work, and are now measured
not to: draw consolidation (all indirect draws together are 0.23 ms), triangle reduction,
resolution reduction, and impostors as currently built.

## Corrected model: geometry, not instances (2026-09-10, later)

An earlier section here concluded the cost was "per drawn instance, and almost nothing else".
That was the right observation and the wrong model, and the difference matters because it points
at a different fix.

Controlled scenes (spheres on a grid, `bench_many` / `bench_few`), 1440x900, calibration 3.1-5.5 ms:

| instances | tris each | total tris | frame min |
|---|---|---|---|
| 4096 | 960 | 3.93M | 10.4-12.2 ms |
| 256 | 16128 | 4.13M | 12.5-13.7 ms |
| 4096 | 960 | 3.93M | 10.42 ms |
| 4096 | 240 | 0.92M | 6.50 ms |
| 4096 | 60 | 0.20M | 5.68 ms |

Holding total triangles fixed and collapsing 4096 instances into 256 changes nothing. Holding
instances fixed and cutting triangles 20x takes 10.4 ms to 5.7. So the frame is **linear in
triangles actually drawn** (about 0.75 Gtri/s here) plus a small per-instance floor near 1.3 us.
Culling instances looked like the driver only because culling an instance also removes its
geometry.

Three consequences, all of which kill an optimisation that looked obvious:

- **Merging many small instances into fewer large meshes buys nothing.** Same geometry, same cost.
  Worth knowing before building a vegetation-clustering system.
- **Billboard impostors make it worse.** Pushing the ladder so most instances draw as camera-facing
  quads cost +11.6 ms at an identical drawn count: they trade geometry for fill, and a quad
  circumscribing the source's bounding sphere covers far more pixels than the plant it replaces.
- **Moving instances from LOD0 to LOD1 is nearly free either way** (338 -> 79 -> 29 -> 10 at LOD0
  moves the frame 21.3 -> 20.9 -> 21.0 -> 21.6). The half-resolution mesh is not much cheaper than
  the full one for meshes this small.

Where the 21 ms goes at 1440x900: terrain and everything else about 10 ms, ground cover 7.0,
trees 3.7, rocks free. Shadows, AO, volumetrics and post together are 2.2. Draw submission is 0.23.

**And the CPU is not waiting.** Wall-clock min 20.76 against a GPU frame of 20.97 at 1440x900,
44.30 against 42.60 at 2880x1800. P3 (queue pipelining) has nothing to recover; the earlier 7 ms
gap was measured with the camera facing empty sky, where the GPU is idle enough for fixed costs
to show.

So there is no large algorithmic win hiding here. The renderer draws roughly the geometry it is
asked to draw, at a sane rate. Getting the frame down means drawing less vegetation, or finding a
distant representation that is cheaper in *both* geometry and fill -- which billboards, as built,
are not. That is an art-direction decision as much as an engineering one.

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

## Per-pass timers were not per-pass costs -- fixed (2026-09-10)

**The old instrument.** Each pass carried its own begin/end timestamp pair. A pass's begin
timestamp is written when the pass is *reached*, not when its own work starts, so a pass behind a
heavy one absorbed the drain of everything still in flight ahead of it. On the world scene the
volumetric pass reported **39.4 ms of a 46 ms frame**; turning volumetrics off took the frame from
53.1 ms to 47.7, so the pass costs **5.4 ms** and the timer was reporting the lit pass finishing
behind it. The tell was that the number did not respond to its own workload: halving the march
steps, disabling its noise and halving its max distance each moved it by under 2 ms. Every pass on
that line had the same failure mode; they did not sum to the frame, and the largest number was
usually just the pass that followed the expensive one.

**What replaced it.** `gpu::FrameTimeline` (src/gpu/frame_timeline.hpp): one query set for the
whole frame, one timestamp per pass written *at the pass's end*, in submission order. A pass costs
`end[i] - end[i-1]` -- the interval between two consecutive "the GPU has finished everything up to
here" markers. Consecutive intervals are contiguous, so the passes partition the frame and sum to
it exactly; a pass cannot be charged another's work because a millisecond spent between two
markers belongs to exactly one interval. Every pass in the frame marks itself, including the ones
inside the post chain, the AO resolve and each shadow cascade. `--headless` prints the per-pass
medians next to the frame median.

**Validation.** `examples/world/terrain.scene.json` at 1440x900, three interleaved rounds per arm,
against a `_skyonly` calibration of 5.57 ms before and 6.19 ms after. Each phase switched off with
`--disable`, which logs what it turned off:

| phase off | pass timer says | A/B on the frame (wall) | A/B on the frame (GPU) |
|---|---|---|---|
| volumetrics | 1.05 ms | +0.89 ms | +0.86 ms |
| shadow cascades | 0.39 ms | +0.32 ms | +0.14 ms |
| GTAO | 0.26 ms | +0.43 ms | +0.40 ms |
| post chain | 0.20 ms | -0.03 ms | +0.00 ms |

Every phase agrees with its A/B to within a few tenths of a millisecond -- which is between three
and six ticks of the counter. The one that does not look exact is instructive: turning AO off saves
0.43 ms against a pass that costs 0.26, and the timeline says where the rest went, because `scene`
drops from 18.02 to 17.69 in the same run. The lit pass stops sampling the AO texture. The old
instrument could not have told you that.

The decisive test is whether a number moves with its *own* workload, which is exactly what the old
one failed:

| knob | pass timer | frame (wall min) |
|---|---|---|
| `volumeSteps` 18 -> 144 (8x the march) | 1.05 -> 7.67 ms (+6.62) | 20.71 -> 27.38 (+6.67) |
| `shadowCascades` 1 -> 4 | 0.26 -> 0.59 ms (+0.33) | 20.66 -> 21.23 (+0.57) |

Eight times the marching work moves the volume pass by 6.62 ms and moves the frame by 6.67. Under
the old instrument that pass claimed 39.4 ms and halving its steps moved it by under 2.

**What `--headless` now prints per frame.** Three lines: the per-pass GPU breakdown sorted by
cost, then the submission counters, then the workload each measured phase was given.

```
gpu 20.45 ms over 26 passes (sum 20.45): scene=18.02 volume=1.05 shadow=0.39 ao=0.26 cull=0.26
  depth=0.13 post/bloom=0.13 clusters=0.07 tonemap=0.07 particles=0.00 background=0.00
draws=100 (indirect 120, empty 6, skipped 40) shadowDraws=48 cascades=2/2 spots=0 dispatches=3
  tris=15690069 instances=1281v/35969c lod=338/746/191/6 particles=1sys/10240cap/1emit
  cpu(proc)=0.06ms cpu(scene)=0.15ms
workload: volumeSteps=18 cascades=2 shadowRes=2048 aoTarget=720x450 aoSlices=3x6 postPasses=12
  bloomLevels=6 sdf=0ray/0mesh simGrids=0 transient=12
```

The third line exists because of the `scatter` incident below: an A/B that edits a scene has to
prove its two arms differ, and "no effect" reads exactly like a run whose edit never applied. The
same run prints per-pass *medians* over the steady frames at the end, next to the frame median,
which is what to quote -- one frame's sample of a 0.066 ms counter says very little.

`tris` is what is *submitted*: source triangles times instances, before the GPU cull rejects any
of them. The counts the cull writes are never read back for the frame that used them, so the
honest reading of what survived is `instances=Nv/Mc` and the per-LOD split beside it.

**Three things to know before trusting a number.**

- *Resolution is 0.066 ms.* Every timestamp this device returns is a multiple of 65,536 ns, so a
  pass costing less than that reads 0.00. It is a floor on one pass in one frame, not on the
  frame: over a hundred frames the medians still resolve tenths of a millisecond.
- *A stall lands on the first pass of the frame.* The frame origin is the start of the first
  marked pass, so if the GPU was busy with something else -- another agent's benchmark, a
  compositor -- the wait is charged to whichever pass runs first, usually `clusters`, `particles`
  or `cull`. A `cull` pass reporting 15 ms in a scene with nothing to cull means the machine is
  contended, not that culling got slow.
- *An empty render pass reports 0 and folds into the next one.* Metal does not write the
  end-of-pass timestamp of a pass that issues no draws; the slot resolves as a literal zero. The
  timeline detects that (`FrameTimeline::unwritten()`) and leaves the boundary where it was, so
  the following pass's interval covers both. Left unhandled this turned the depth prepass into
  228,832,448 ms.

A/B is still the ground truth, and `--disable shadows,ao,volume,post` switches a phase off without
editing a scene, logging what it turned off so the two arms of a comparison can never be confused
after the fact. Measured that way on the world scene at 2880x1800 (baseline 52.6 ms):

| removed | frame | cost |
|---|---|---|
| material programs (fireflies, fronds) | 46.8 | 5.8 ms |
| ecology lights (ADR-053) | 47.2 | 5.4 ms |
| volumetrics | 47.7 | 5.4 ms |
| airborne spores | 52.3 | 0.3 ms |

### Correction (2026-09-10): the ecology *is* the cost

An earlier revision of this document said that removing the ecology changed nothing, and used
that to argue the frame was spent on terrain and submission. That measurement was a silent
no-op: `scatter` is a list on the terrain node, not an object with a `layers` key, so the edit
deleted nothing and both arms of the A/B rendered the same scene.

Redone at 1440x900 with edits that apply, against a sky-only calibration of 7.5 ms: baseline
36.3 ms, ecology removed 12.6 ms. **The ecology is 23.7 ms, about two thirds of the frame.**
Per layer, each interleaved against a fresh baseline: bushes 10.0 ms, ferns 3.9, fungi 1.3,
grass 1.1, pebbles within noise.

The lesson is not about JSON. It is that an A/B whose two arms are identical reports "no
effect" in exactly the same voice as a real null result. Print something from inside the edit
-- an instance count, a layer list -- and check it changed.

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

## The shadow-map lookup is the editor canvas's largest single term (2026-09-11, ADR-086)

Phase 3 left the scene pass fragment-bound and named the fragments: at the editor's 2880x1166
canvas, `directLighting` is 81% of the pass, the key light's cascaded PCSS lookup is 34% of it and
the screen-space contact marches 19%. Phase 4 moved the lookup into a half-resolution pass
(`shaders/shadow_mask.wgsl`) and upsamples it bilaterally in the lit pass.

Min of 5 interleaved runs, `--tier realtime`, headless, Glowmere, machine shared with other agents.
The arms are `--disable shadowmask` against the default, round-robin, never all of one then all of
the other.

| canvas | pixels | scene pass off -> on | mask pass | GPU frame off -> on |
|---|---:|---:|---:|---:|
| 720x450 | 0.32 MP | 13.04 -> **11.27** (-13.6%) | 0.20 | 14.75 -> 13.17 (-10.7%) |
| 1440x900 | 1.30 MP | 19.40 -> **14.94** (-23.0%) | 0.59 | 22.35 -> 18.48 (-17.3%) |
| 2880x1166 (editor, 1440x900 pt) | 3.36 MP | 42.80 -> **31.39** (-26.7%) | 1.25 | 47.91 -> 37.68 (-21.4%) |
| 2466x1766 (editor, 1920x1200 pt) | 4.36 MP | 39.58 -> **28.44** (-28.1%) | 1.44 | 45.42 -> 35.72 (-21.4%) |

That is the mirror image of ADR-085's table, which bought 41% at 720x450 and 7% at the editor's
canvas. Below the crossover the invocation count is geometry-floored and a per-pixel saving has
little to work on; above it, it is the whole story.

The absolute level drifted about 10% between measurement sessions on this machine (the 2880x1166
"off" arm read 38.27 in one session and 42.80 in another); the ratios moved by two points. Trust the
ratios.

### The contact march does not survive half resolution, and that is measured

Masking the contact march as well was the plan and is rejected on the image, not on the clock. At
2880x1166, against the unmasked frame:

| what the mask carries | pixels differing |
|---|---:|
| the shadow-map term only | **8.4%** |
| the shadow-map term and the contact march | 30.2% |

Glowmere's ground is a field of grass and reed cards; a march evaluated once per 2x2 applies what
it hit to all four pixels, so every contact shadow doubles in width and dense ground cover goes
black. A penumbra survives half resolution; a contact shadow is exactly the signal that does not.

### Render scale: a real lever that saturates

The editor canvas at a fixed aspect, scaled down. Min of 3 interleaved runs. Submitted triangles
fall too, because screen-space LOD selects against `projScale` and `projScale` follows the viewport
height -- which is what a user of a render-scale slider actually gets, so it is reported together.

| scale | size | Mpx | submitted tris | scene | GPU frame | vs full |
|---:|---|---:|---:|---:|---:|---:|
| 1.00 | 2880x1166 | 3.36 | 694,977 | 29.23 | 35.52 | — |
| 0.90 | 2592x1048 | 2.72 | 635,963 | 25.76 | 31.26 | -12.0% |
| 0.80 | 2304x932 | 2.15 | 594,261 | 22.35 | 27.13 | -23.6% |
| 0.71 | 2036x824 | 1.68 | 533,217 | 20.12 | 24.25 | -31.7% |
| 0.60 | 1728x700 | 1.21 | 472,276 | 17.37 | 20.91 | -41.1% |
| 0.50 | 1440x582 | 0.84 | 396,524 | 16.19 | 19.20 | -45.9% |

**It saturates, and it saturates above the target.** Halving the linear scale -- a quarter of the
pixels, and a visibly softer picture -- leaves the frame at 19.20 ms, still short of 16.67. Between
0.60 and 0.50 the frame moves 1.71 ms for another 0.37 MP, because by then the invocation count is
geometry-floored again (section 2 of `renderer-2-architecture.md`).

**So dynamic resolution is not worth building yet.** A scaler targeting 16.67 ms on this world would
sit at its floor permanently and still miss, which is a static setting plus machinery plus a softer
picture the user did not ask for. The static `--canvas-scale` stays the right shape of control, and
0.9 is 12% for a difference that is hard to see on a still frame. Revisit when the frame is close
enough to the target that a scaler would spend most of its time at 1.0 -- that is the only regime
where it buys headroom rather than quietly spending quality.

### Two directional lights march without a map, and a rig can now say not to

`skyfill` and `elder-practical` are `castsShadow: false` and still run a twelve-step contact march
per fragment, because `PunctualLight::contactShadow` defaults to true and no rig field set it.
`RigLight::contactShadow` now exists (default true, so nothing changes on its own). Turning both off
in Glowmere's rig, min of 4 interleaved runs with the shadow mask already in place:

| canvas | scene | GPU frame |
|---|---:|---:|
| 720x450 | 11.21 -> 10.03 (-1.18) | 12.98 -> 11.80 (-1.18) |
| 2880x1166 | 29.23 -> **25.95** (-3.28) | 35.52 -> 32.31 (-3.21) |

Phase 3 measured 2.42 ms for the same removal at 720x450 as a shader-only A/B before the mask
existed; 1.18 is the same removal after it, which is the number that matters now. It changes the
image, ADR-034 gave the march to every light deliberately, and the control is therefore a rig's to
use and not the renderer's to apply.

### The LOD ladder was cheaper than it claimed (2026-09-11, ADR-086)

`meshopt_simplifySloppy` quantises onto a grid, so the triangle count it returns is a step function
of the grid it picked and one call lands wherever the steps fall: asked for 35% of `CommonTree_1` it
returned 7.6%. `LodChainSettings::sloppyIterations` now bisects the request. Glowmere's canopy ladder
goes from 8% / 8% / 2% of the source to 34% / 13% / 2% against a 35 / 12 / 4 target.

| canvas | submitted tris | scene pass | GPU frame |
|---|---:|---:|---:|
| 720x450 | 201,174 -> 235,761 (+17.2%) | 11.21 -> 11.34 (+0.13) | 12.98 -> 13.17 (+0.19) |
| 2880x1166 | 694,977 -> 767,961 (+10.5%) | 29.23 -> **31.06** (+1.83) | 35.59 -> 37.62 (+2.03) |

Min of 4 interleaved runs, two binaries, identical shaders. **A deliberate regression**: the
millisecond was being bought by drawing a thinner world than the ladder specified while every
counter reported the ladder working, which is §72 with the numbers on its side. 2.8% of pixels
change, all in the mid-ground band, and the change is canopies with their authored foliage mass.

## Where the phase leaves the frame

Both changes together against the Phase 3 renderer, min of 4 interleaved runs, same shaders on
disk in both arms so this isolates the two changes rather than the whole diff:

| canvas | submitted tris | scene pass | GPU frame | wall clock |
|---|---:|---:|---:|---:|
| 720x450 | 201,174 -> 235,761 | 13.11 -> **11.53** (-12.1%) | 14.61 -> 13.37 (-8.5%) | 17.09 -> 16.07 |
| 1440x900 | 396,495 -> 448,038 | 18.55 -> **15.47** (-16.6%) | 21.36 -> 19.01 (-11.0%) | 24.13 -> 21.77 |
| 2880x1166 | 694,977 -> 767,961 | 38.01 -> **31.00** (-18.4%) | 43.25 -> 37.49 (-13.3%) | 46.04 -> 40.42 |

13% off the editor canvas's GPU frame while submitting 10.5% *more* geometry than before, because
one change bought a per-pixel term and the other spent part of it on the geometry the ladder had
been quietly withholding.
