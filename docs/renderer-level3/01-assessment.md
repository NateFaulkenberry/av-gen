# Level 3 gap closure — Phase 1 assessment

**Status:** in progress. Assessment only; no rendering behaviour changed.
**Date:** 2026-09-14

## The start condition

The mandate blocks itself until "all currently active work is complete" and names that work as the
**rendering/scalability** effort. That effort is complete: the four-wave renderer upgrade landed, its
backlog's "known and unfixed" list is empty, and the post-processing investigation that followed it
merged at `f41f66e`. Two scene projects (Glowmere Valley 2, Tree of Life) were commissioned *after*
the mandate and are running on their own branches; they are not what the start condition names.

**CURRENT WORK COMPLETE (rendering/scalability) — BEGINNING LEVEL 3 GAP CLOSURE, PHASE 1.**

Implementation is held until the scene projects land. This phase is §1 only: research and forensics.

## Correction (same day): the protocol exists and was not being followed

The section below concluded that this project cannot certify a single-run timing and that the first
task was to build a protocol. **That was half wrong, and the missing half is the useful part.**

A third instance of the same confounder arrived hours later, and it came with the answer. Measuring
whether six hero mushrooms cost anything, a two-invocation A/B returned the causally impossible
result that *hiding* them made the frame slower. Three runs of a **byte-identical** scene, every one
holding `gpu-lock.sh` and passing a `pgrep` check on both sides:

| run | frame ms | triangles |
|---|---:|---:|
| 1 | 10.945 | 252,996 |
| 2 | 11.272 | 252,996 |
| 3 | 13.697 | 252,996 |

**25% spread with the scene identical.** So a two-invocation comparison on this machine carries a
noise floor of roughly 3 ms, and every cross-invocation number below that is unreadable.

The protocol that works already exists and is the one ADR-150 has used all along: **arms must be
interleaved inside one process**, which is what `--ab` does and why the register says cross-session
comparison "is not offered, on purpose". Interleaved properly, those six heroes cost +0.20 ms and
+0.66 ms — real, and a third of what the two-invocation run reported.

So the gap is not a missing instrument. It is that **nothing enforces interleaving**, and absolute
frame times get quoted to three significant figures in documents where they were never that precise.
ADR-170's contention clause is a floor, not a protocol.

Two consequences, adopted:

* **Every frame time in this project's documents should be read with a ±3 ms band unless it says it
  was interleaved.** That includes the 15.93 vs 13.57 ms pair below, which is now explained rather
  than mysterious, and the renderer upgrade's own headline figures, which were five locked runs per
  figure and are therefore better than that but not immune.
* **A causally impossible result is a free diagnostic.** Hiding geometry cannot make a frame slower;
  the value of the arm that produced it is that it indicts the method immediately, at no cost. Worth
  reaching for deliberately — an arm whose sign is known in advance is a protocol test.

Thermal state remains uninstrumented and may still contribute. It is no longer the leading
explanation, and no work is scheduled against it.

## The finding that gates everything else — as originally written, now corrected above

**This project cannot currently certify a single-run timing, and two independent measurements this
week say so.**

1. The same unmodified Glowmere scene measured **15.93 ms** in one quiet run and **13.57 ms** in
   another. Both passed a `pgrep` check for contending processes on *both* sides of the run. 17%
   apart, with identical triangle counts.
2. ADR-151's 40 px deletion arm measured **−0.5%** of the scene pass in a run where the ADR itself
   recorded **−17.5%** and built its argument to refuse impostors on that number.

ADR-170 added the rule that `tools/gpu-lock.sh` serialises *agents*, not the device, so an
interactive session contending for the GPU is invisible to it. That rule is **necessary and not
sufficient**: a 17% confounder survives it. Thermal state is the obvious uninstrumented candidate and
is not currently measured.

Every priority in this mandate is judged on before/after performance. §23 says so explicitly, and
also says *"do not repeat the earlier mistake of treating noisy timing as a precise measurement"*.
So the first implementation task of this effort is not a rendering feature. It is a protocol that can
certify a number, and it needs a quiet machine — which this one is not while two agents hold the GPU.

Neither result overturns anything on its own: cross-session timings are not comparable and the rules
forbid treating them as such. What they establish is that the *variance is not characterised*, and
§23 requires characterising it before making strong claims.

## A. Already production-grade — do not rebuild

Verified by reading the code and the tests, not inferred from names.

**Debug targets (§14).** Twelve auxiliary views: normal, roughness, velocity, emission, ids,
occlusion, depth, linear depth, depth edges, object depth, overdraw, fragment density. Every one is
exercised, and the guarding test is exactly what §14 asks for: it renders each view and asserts they
all **hash differently**, on the stated grounds that *"two views that hash alike are either the same
buffer shown twice or two empty frames, and both of those are a diagnostic that lies."* A second test
drives every view through three awkward resolutions and a selection change. This requirement is met;
building it again would be waste.

**The measurement lab.** Per-scene noise floors reported as four components (ADR-148), A/B arms
interleaved within one process because cross-session comparison is deliberately not offered, and
`--bench-json` carrying percentiles, counters, conditions and a session id. The instrument is better
than the protocol around it — see above.

**Quality tiers (§16).** Four tiers (Preview, Realtime, High, Offline) as one policy object rather
than scattered flags, with per-rung material tiers (ADR-155), hysteresis policy as tier data
(ADR-146), and render scale. `QualitySettings` carries 25 knobs; the tiers are compositions of them,
not independent code paths.

**The refusals.** Impostors (ADR-151/153), HLOD (ADR-154), TAA as a prerequisite (ADR-132), temporal
reprojection of the volume (ADR-143), a frame graph (ADR-119), clustered-light optimisation. Each
carries the number that would reverse it. §20 says respect these unless new measurements invalidate
them, and nothing found so far does.

## B. Incomplete, with the gap named

**AOV export to disk.** The renderer already produces normal+roughness, velocity, emission and
object/material IDs in HDR and can display them. Nothing writes them beside the beauty pass, and
depth is not an exportable target. Note the standing oddity: **normal+roughness is written every
fragment and read by nothing** on the normal path — measured at zero cost, but an export is the thing
that would give it a consumer.

**Debug views the mandate lists that do not exist:** albedo, metallic, material ID (the `Ids` view is
object identity), shadow, volumetric contribution, bloom contribution, exposure. Each is cheap
individually. None should be added without saying what question it answers — the project's own rule
is that a target must correspond to meaningful renderer state, and seven new views nobody uses is the
failure mode §14 warns about from the other direction.

**Performance dashboard (§13).** The data is machine-readable and the panel does not exist. This is
the one item that genuinely needs a human to certify, because an agent cannot honestly call an ImGui
surface working by reading its source.

## C, D, F. Deferred, with the reason

**What is technically correct but artistically inadequate (C)**, **what is expensive without
producing visual value (D)**, and **temporal instability (F)** all require rendering and measuring,
and C in particular requires looking. They are the substance of Priorities 1 and 2 and cannot be
answered from source. They are deferred to the measurement pass, not dropped.

One input already exists and should be carried into F rather than rediscovered: the post-processing
investigation established that the anamorphic tier's artifact was **spatial, not temporal** — an
undersampled gaussian printing a comb — and that there is **no temporal history anywhere in the post
chain** except auto-exposure's 1×1 readback. Whatever temporal instability exists in this renderer,
the post chain is not carrying it frame to frame.

## E. Content vs renderer

A live example, from this week, in the project's own scenes: Glowmere's wanderer stalling for sixteen
minutes at a time was reported as a character bug, diagnosed through four wrong hypotheses, and
turned out to be navigation — a body off the navigable set that no amount of correct re-planning
could recover (ADR-162). Nothing in the renderer was involved. The mandate's §E warning — *do not
"fix" a content-authoring problem by making the renderer more complicated* — has teeth here.

Similarly: **Glowmere's two distance ladders disagreed** (ADR-160) and **rate matching was wired and
never switched on in any scene** (ADR-161). Both read as engine defects and both were authoring gaps
the engine failed to point at. The pattern worth carrying into this effort is that the fix in each
case was **a diagnostic that names the two numbers**, not a mechanism.

## Maintainability (§18)

`SceneRenderer::render` is **1,485 lines** inside a 3,639-line file. Before treating that as a
finding: it is a linear pipeline with **31 labelled sections and 11 render/compute passes**, nearly
every one carrying the ADR that explains it. It is large *and* coherent, which is the case §18
explicitly says not to split for size alone.

The real cost is not length, it is that eleven passes share one function scope, so state established
for one pass is visible to all the others and a leak between them is invisible. That is the same
shape as the defect the Glowmere agent hit in its own forensics harness this week, where one arm
cleared a material program and the next five silently re-measured the first. **Not yet a
recommendation** — it is the thing to look for when C/D/F measurements start touching this function.

## What Phase 1 still owes

- The temporal artifact inventory (§4), which needs the representative suite rendered and looked at.
- The variance protocol above, which needs a quiet machine.
- The realtime/offline parity audit (§15), beyond the two parity defects already fixed (ADR-146,
  ADR-147).
- Scene authoring ergonomics (§17, §G).
