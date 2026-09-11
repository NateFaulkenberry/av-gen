---
id: performance/instruments
title: The Profiler and Its Instruments
category: Performance
summary: Which numbers are real GPU timestamps, which are CPU wall clock, and how to get at the per-pass breakdown.
order: 72
audience: expert
tags: profiler, timestamps, gpu, cpu, phases, csv
keywords: how do i profile; per pass timing; profile-cpu; gpu timing; where does the frame go; frame profiler
related: performance/diagnosis, performance/what-costs-what, reference/command-line
features: subsystem.performance
---

# The Profiler and Its Instruments

AV Gen has two instruments and they measure different things. Confusing them wastes time.

## The GPU frame timeline — real timestamps

One timestamp query set per frame, written at the end of every pass. A pass's cost is the
difference between its own end timestamp and the previous one, so **the passes partition the frame
and sum to it exactly**.

Pass labels, as they appear in the breakdown:

```
ao  auxdebug  background  clusters  composition  cull  debug  depth
effectors  particles  scene  sdf  shaderlayer  shadow  shadowmask
sim  tonemap  volume
```

plus the post chain, which labels itself per stage: `post/meter`, `post/exposure`, `post/dof`,
`post/motionblur`, `post/lens`, `post/bloom`, `post/halation`, `post/anamorphic`, `post/composite`,
`post/fxaa`, `post/sharpen`.

Several passes deliberately share a label — two shadow cascades, a bloom pyramid, the scene pass
split around the SDF raymarch — and are summed.

### Its known failure modes, which it reports

**An empty render pass gets no timestamp.** Metal does not write the end-of-pass timestamp for a
pass that issues no draws; the slot reads as a literal zero. AV Gen only believes a slot that is
non-zero and not before the previous one, charges an unbelieved pass nothing, and folds its cost
into the next pass. It names which passes this happened to. Left unhandled this once made the depth
prepass report 228,832,448 ms.

**The timestamp quantum on this hardware is about 0.066 ms**, so a pass cheaper than that reads
0.00. That is a floor on one pass in one frame, not on the frame.

**A stall lands on the first pass.** The frame origin is the start of the first marked pass, so
external contention is charged to whichever pass runs first — usually `clusters`, `particles` or
`cull`. A `cull` pass reporting 15 ms in a scene with nothing to cull means the machine is
contended, not that culling got slow.

**If the adapter has no timestamp support**, the status bar shows `gpu n/a` and there is no
breakdown at all.

## The CPU phase profiler — wall clock only

The main thread's own phases, over the last 2048 frames, reported as min, median, mean, P95, P99
and max. **No number in it is GPU time**, and two of its phases are named as waits — `gpu.acquire
WAIT` and `gpu.present WAIT` — precisely so that a number which grows when the GPU is the
bottleneck is not read as the CPU getting slower.

The phases, in order:

```
ui.script  events.poll  engine.update  ui.build  gpu.acquire WAIT  debug.geometry
render.record  imgui.record  gpu.submit  gpu.present WAIT  viewport.pick
outputs+share  render.job  gpu.processEvents  canvas.resize  input->present ms
```

`--profile-cpu` prints the table to standard error on exit — deliberately not to the log, so it is
not interleaved with timestamps and can be pasted as it stands. `--profile-csv <file>` writes one
row per frame with every phase, and implies `--profile-cpu`.

The report ends with a spike count at 16.7, 33.3 and 50 ms, and an `unaccounted` row: the median
frame minus the median of every phase.

## Where to see what

| You want | Look at |
|---|---|
| frame rate, frame time, CPU work, GPU frame | the status bar |
| the same plus per-subsystem costs | **Control ▸ Performance**, at the foot of the panel |
| the per-pass GPU breakdown | a `--headless` run's log, every thirtieth frame |
| the main thread's phase distribution | `--profile-cpu` |
| a per-frame trace | `--profile-csv` |

> [!NOTE]
> There is no per-pass GPU breakdown in the editor's interface today. The numbers exist and are
> collected every frame; they are reported through the headless log and the environment variables
> below.

## Environment variables

| Variable | Does |
|---|---|
| `AVGEN_TIMELINE_RAW` | dump each frame's raw timestamps to standard error |
| `AVGEN_FRAME_COUNTERS` | log the submission split every thirtieth frame |
| `AVGEN_CPU_STAGES` | log the CPU stage split every thirtieth frame |
| `AVGEN_SLOW_PHASE_MS` | log what was being written on any frame whose phase exceeded this |
| `AVGEN_SLIDER_FILTER` | restrict `--ui-script sliders` to parameters under a path prefix |

The last two together are how "changing a property costs 14 ms" becomes the name of the property.

## What the Control panel reports

Beneath the transport: frame rate and times, canvas size, draws and triangles, analysis
microseconds per hop, modulation microseconds, and then one line each for procedural geometry,
fields, culling and LOD, SDF and particles — each with its own GPU pass time. Every one of those
pass times is a real timestamp.

> [!WARNING]
> The `N tris` figure in the status bar is the legacy total and includes fullscreen triangles from
> post passes. The submitted-geometry counters in the headless log are the ones to use for
> geometry work; they also distinguish *estimated* draws (whose instance count came from a cull
> readback one to three frames old) and *unmeasured* indirect draws, which contribute nothing at
> all — so a non-zero unmeasured count means the triangle figure is a floor.
