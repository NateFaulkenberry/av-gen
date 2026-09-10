# ADR-051: Measuring a frame, and one shared indirect buffer

## Status
Accepted, 2026-09-09.

## Context

The world optimisation spec's P1 was to consolidate scatter draws. Its premise was the strongest
measurement in `docs/performance.md`:

> ecology frame cost is linear in the number of scatter OBJECTS and nearly independent of what they
> draw -- about 1.5 ms per scatter object per frame, fixed.

That number was arrived at honestly, from repeated, interleaved runs, and it survived several
attempts to break it: it did not move with resolution, with triangle count, with instances drawn or
with terrain chunk count. A cost that ignores everything about the work is a submission cost, and
the conclusion drawn from it -- that the ceiling on a world is how many distinct scatter layers it
has -- followed.

**It was an artefact of the instrument.** The benchmark measured the whole process under
`/usr/bin/time` and divided by the frame count. Eleven scatter layers take about two seconds longer
to *load* than none: eleven glTF decodes, eleven scatter placements, eleven mesh budgets. Over a
100-frame run that is 20 ms a frame of one-time work charged to the frames, and it is linear in the
number of layers, and it is independent of resolution and triangle count and everything else about
what is drawn -- because it happens before any of that.

Measured per frame instead, the slope over 1, 3, 6 and 11 layers is **-0.03 ms per layer**: zero.

This is the same class of error as the `--size` bug recorded in `docs/performance.md`, and it went
undetected for the same reason: the instrument was never asked to measure something whose answer was
already known.

## Decision

### The benchmark measures frames, not processes
`avgen --headless` times each frame itself and reports the median over the run after a warm-up,
alongside p10/p90/min. A median is used rather than a mean because on a shared machine a handful of
frames are stolen outright -- a typical run's p90 is twice its median -- and an average of a
hundred frames is an average of that theft. Nothing that happens once can enter the number.

`tools/bench_ab.sh` runs every configuration once per round and repeats the rounds, reporting the
median over rounds. Sequential A-then-B has been measured 11 ms apart on identical binaries.

Together these took the noise floor from **±1.5 ms to about ±0.3 ms**, which is what makes a
one-millisecond change legible at all.

### A binary is pinned to its shaders when two binaries are compared
Shaders are read from the working tree at run time, so an old binary run today runs today's shaders.
A baseline binary silently paired with the new shader produced an image missing two thirds of its
ground cover, which read exactly like a pre-existing rendering bug and was chased as one.
`bench_ab.sh` takes `binary@shaderdir` and every comparison pins both sides.

### One indirect buffer for all procedural objects
Each object used to own a `kMaxLodLevels x drawIndexedIndirect` buffer. They are now slots in a
single buffer indexed by the object's slot -- the same slot it already writes its cull stats to, so
no new bookkeeping -- and each draw addresses its own slice by offset.

Nothing about the draws changes: the same number of `DrawIndexedIndirect` calls with the same
arguments, and the rendered frame is bit-identical. What changes is that they name one buffer
instead of eleven.

## Rationale

With the instrument fixed, the ecology's real cost decomposes (2880x1800, 11 layers, ~37,000
instances, ~900 surviving):

| | ms/frame |
|---|---|
| the procedural renderer doing nothing at all | 43.4 |
| every state change recorded, no draws | 44.0 |
| normal | 51.7 |
| normal, but every level drawn with the cheapest LOD mesh | 45.6 |

Recording the state costs **0.6 ms**; the draws cost **7.7 ms**; and **6.1 ms of those 7.7 is
geometry**, since drawing the same 72 draws with the smallest mesh recovers it. The submission is
not the cost. The work is.

The one part that *is* submission is the indirect buffers. Replacing every `DrawIndexedIndirect`
with a direct `DrawIndexed` of a CPU-supplied count saved 1.4 ms; pointing all 72 indirect draws at
a single shared buffer saved the same 1.56 ms. Once there is one buffer, an indirect draw costs what
a direct one costs. So the whole indirect overhead was per-buffer, and it is the only part of the
frame that scaled with the number of layers.

That is worth taking, and it is the only thing in P1's brief that the evidence supports.

## Consequences

At 2880x1800, 11 layers: **55.5 -> 54.0 ms**, medians of six interleaved rounds each with every
round inside 0.5 ms of its median. The saving reproduced in three separate sessions -- -1.24, -1.27
and -1.46 ms -- whose absolute levels differed by 6 ms. A delta that holds while the level moves is
the shape of a real one.

At one layer, where there was only ever one buffer, it saves nothing (52.32 -> 52.64), which is the
mechanism confirming itself.

| | 1 layer | 11 layers | slope |
|---|---|---|---|
| before | 52.32 | 55.48 | 0.32 ms/layer |
| after | 52.64 | 54.02 | **0.14 ms/layer** |

The rendered frame is bit-identical: 0 of 921,600 pixels differ.

The buffer is 20 KB (256 objects x 4 levels x 20 bytes) allocated once, replacing one small buffer
per object allocated on demand.

## Rejected alternatives

**Merging layers into one draw** -- a shared instance buffer with a per-instance `speciesId`, one
mesh selected per instance, texture arrays so a material family shares a pipeline. This is what P1
asked for, and every version of it trades submissions for vertex work: a single indexed draw cannot
vary its index count per instance, so the meshes must be padded to a common size (1,400 triangles
against a mean of 445 here) or drawn through vertex pulling. The measurements above say submissions
are worth about a millisecond in total and geometry is worth six. It would be paying in the
expensive currency to save in the cheap one.

**Per-cascade culling (the spec's P4)** was already measured and rejected in
`docs/performance.md` for its own reasons; nothing here changes that.

**Keeping the whole-process benchmark and subtracting a measured load time.** Two-point
differencing (run at 5 frames and at 100, take the slope) does remove the scene build, and it was
used to find this. But it needs both legs to be equally contended and they are not, so it inherits
the drift it was meant to remove: three frame counts of the same scene gave 62.7, 78.7 and 27.5
ms/frame. Timing the frames is strictly better and costs a dozen lines.

## Revisit triggers

- A scene with many hundreds of procedural objects: `kMaxProceduralObjects` (256) now also bounds
  the indirect buffer, and the per-buffer cost this removed would return as something else.
- Ecology geometry becoming the frame's largest item. The lever is the LOD ladder and the mesh
  budgets -- the cheapest-mesh probe puts a 6 ms ceiling on what is available there -- not the
  number of draws.
- WebGPU gaining multi-draw indirect. That changes the arithmetic behind the rejected alternative,
  because it removes the padding that made merging cost more than it saved.
