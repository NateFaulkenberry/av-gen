# ADR-077: What the frame profiler is allowed to claim

Status: accepted
Date: 2026-09-10

## Context

This is phase 1 of the renderer 2.0 work, and its governing instruction is *do not optimise until
this works*. Everything the later phases do — visibility, LOD, HLOD, instancing — is evaluated by
diffing numbers this instrument produces. If the numbers are wrong, the phases after this one
cannot be evaluated at all, and a change that made the frame worse would be indistinguishable from
one that made it better.

The phase 0 audit (`docs/renderer-2-architecture.md`) found three specific problems, and measuring
them turned up two more.

**The headline geometry number answered a different question.** Glowmere reported `tris=15154902`.
That is `ProceduralStats::logicalTriangles` — source triangles times instance records — computed
before LOD selection and before the GPU cull pass rejects anything. It is the geometry the world
*contains*. Nothing anywhere measured the geometry the frame *submitted*, so a culling or LOD
change could not be shown to have done anything to the one geometry statistic anybody reads.

**Pass timings under-reported on a removal basis.** `--disable volume` saved 1.44 ms while the
volume pass reported 0.85. The audit's hypothesis was that a pass's timestamps do not capture its
resource transitions or the bandwidth its targets cost.

**There was no CPU frame breakdown.** Two sampled counters, `cpu(proc)=0.14ms` and
`cpu(scene)=0.77ms`, against a 25.7 ms frame, and a conclusion drawn from them that the CPU is not
the bottleneck. It probably is not. Two samples of a seventeen-stage frame is not how anyone should
find that out.

And, found while measuring:

**The timestamp counter is quantised at 65,536 ns.** Every interval the driver reports on this
machine is an exact multiple of 65,536 ns — 0.065536 ms. The greatest common divisor of 279
consecutive intervals is exactly that. So the audit's "bloom / clusters / particles / composite /
fxaa / tonemap 0.07–0.13 ms each" are one and two ticks of a counter, and a 0.33 ms pass is five
ticks give or take one.

**`draws=112 (indirect 154)`.** The frame reported fewer draws than indirect draws, in the same
line. `SceneRenderer` folded in the procedural draw count from a copy of `ProceduralStats` taken
during `update()`, which happens before a single draw is recorded, so the counter it read was
always zero and the `std::max` beside it fell through to one-draw-per-object for a renderer that
issues up to four indirect draws per object.

## Decision

### Every statistic says which question it answers, in its own name

`rendering::render_stats.hpp` is the home for this. `logicalTriangles` and `logicalInstances` are
the world's content, before LOD and before culling. `SubmittedGeometry` is what a class of pass was
handed, accumulated while the draws are recorded. The two are never added together and never share
a name.

`SubmittedGeometry` is kept per class of pass — `camera`, `depth`, `shadow` — because a camera-side
triangle and a shadow-side triangle cost different amounts, and folding them into one number lets a
shadow change read as a scene change.

`RenderStats::triangles` keeps its name, because the editor status line and the profile capture
read it by that name and neither is this phase's file to change, but it now carries
`geometry.camera.triangles`: what the lit scene pass submitted. Particles, raymarched SDFs, the
skybox and the fullscreen passes are deliberately excluded — they are one to two triangles of
fragment work each, and a geometry budget that moves when the resolution changes is not a geometry
budget. Their draws are still counted in `drawCalls`.

### A number that cannot be gathered honestly is flagged, not invented

The instance counts behind indirect draws are written by the cull pass, on the GPU, after the draw
is recorded. The CPU cannot have this frame's without stalling the frame it is measuring. So:

- a draw whose instance count came from the last *completed* cull readback is counted in
  `estimatedDraws` as well as `draws` — exact in a still scene, one to three frames behind under a
  moving camera, and visibly so either way;
- a draw with no readback behind it at all is counted in `unmeasuredDraws` and contributes nothing
  to `triangles` or `instances`, so those two are a floor and say so.

The alternative — charging such a draw the object's whole record count — puts geometry in the frame
that no pass drew. That is precisely the mistake `logicalTriangles` made at the top of this
document, and it is not repeated one field down.

The same rule governs what is *not* here. The brief's §6 asks for pipeline, bind-group and
render-target switch counts. `SceneRenderer` and `ProceduralRenderer` count theirs; the particle,
SDF, post, AO, volume and shadow renderers are not this phase's files and record their own binds
uncounted. So `StateChangeCounters` carries a documented scope and is a floor, not a total — except
the pass counts, which are complete because they come from `FrameTimeline::mark()`, which every
pass in the frame already calls. A caller that says which kind of pass it is marking is counted as
render or compute; one that does not is counted as **unclassified**, and the three always sum to
the frame's pass count. A statistic with a hole in it that admits to the hole is usable. One that
quietly fills the hole is not.

### The CPU stages partition `render()` the way the timeline partitions the frame

`CpuFrameBreakdown` has seventeen stages and a rolling boundary: every interval between two marks
is charged to the stage the mark names, in submission order. They sum to `render()`, plus — on the
offline path only — the `Finish`, `Submit` and queue wait that follow it, which the live path does
not pay in the same place. `unattributedMs()` is what falls after the last mark, and it measures a
few nanoseconds.

### The removal discrepancy is a bookkeeping question and is answered as one

The timeline's intervals partition the frame exactly: each is the span between two consecutive pass
ends on one query set. A phase that is switched off therefore *cannot* take cost out of the frame
without that cost being visible in the per-pass numbers — if the removed pass is not charged it,
some other pass is. `rendering::attributeRemoval()` takes the per-label medians of both arms of an
A/B and returns the per-label deltas, which sum to the frame delta by construction.

So the audit's 0.59 ms is not missing and never was. See `docs/renderer-2-benchmark.md` for where
it actually goes.

### Timing tests compare paired arms, never two independently-taken medians

Every GPU timing test in `tests/rendering/test_frame_profiler_gpu.cpp` interleaves its two arms in
short rounds and takes the median of the per-round *ratios*. Written the obvious way first — all of
arm A, then all of arm B, then divide the medians — the suite reported a sixteen-times-the-pixels
arm as costing half the time of the one-times arm, because a neighbouring process started between
the two arms. Pairing divides that out. Absolute bars were replaced by relative ones for the same
reason: an "empty frame costs under 4 ms" assertion measured 3.7 ms alone and 10.4 ms with one
other process on the GPU.

## Consequences

- The headline `tris=` figure changes meaning. It was 15.1 M for Glowmere and is now the submitted
  count. `docs/renderer-2-architecture.md` quotes the old meaning and predates this.
- `draws=` rises, because it now counts indirect draws per LOD level rather than one per object.
  It was under-reporting; it is not a regression.
- Nothing about what is rendered changes. The Glowmere sequence hash is `dc0860f8b2db9cf7` before
  and after.
- Per-pass numbers below about 0.2 ms are three ticks of a quantised counter and should not be
  A/B'd. This is a property of the hardware's timestamp counter, not of the instrument, and no
  amount of averaging within a frame removes it — only averaging across frames does.
- `StateChangeCounters` will need extending as the other renderers become fair game in later
  phases. Until then its scope comment is load-bearing.
