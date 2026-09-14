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

### Second refinement: interleaving is necessary and not sufficient

The same rule caught the same person again, one phase later, and the second failure is more
instructive than the first. An interleaved A/B that co-locates its arms but runs them in **fixed
order** — "no heroes" always second — reported that hiding six mushrooms made the frame **2.3 ms
slower**. Same impossible sign as the two-invocation version, but from **bias rather than noise**: an
arm that always runs second always pays for whatever drift accumulates during the run.

> **Interleaving must be counterbalanced.** Alternate arm order over an even number of runs.

Counterbalanced over four runs, the valley axis gives +0.85 ms, consistent with the +0.66 ms measured
independently. **The opening view still returns an impossible −3.5 ms.** So the honest statement is
not a delta at all: *the six heroes cost less than this method can resolve.* Plausible readings
cluster at +0.2 to +0.9 ms and the implausible ones put the noise above that. **A number below the
instrument's floor is not a number.**

### The first implementation task, now specified

Counterbalancing *averages over* drift. It does not detect it, which is why a counterbalanced run can
still return an impossible sign and give no warning that it has.

**A control arm, re-measured at the end of the run.** Measure arm A, run every arm, measure A again.
If the two measurements of A differ by more than the effect being claimed, **the run is void** — the
machine moved under the experiment and nothing measured during it can be trusted at that resolution.
This is a drift *detector* rather than a drift *averager*, and it converts "the sign came out
impossible" from a lucky catch into a routine check that fires before anyone reads the result.

This is the first thing this effort should build. It changes no rendering behaviour — §1 permits
diagnostic instrumentation — and every subsequent priority is judged on numbers it would certify.
It needs a quiet machine to validate, which is the only reason it is not built yet.

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

## Two instrument gaps found by using the instrument

**`--ab` cannot measure a scene that is not a project file.** It takes a project path and quality
arms, so a scene assembled in C++ — which is how both new showcase scenes are built during
development — cannot be measured by the one harness whose method is trusted. The Tree of Life work
had to hand-roll a counterbalanced ABBA loop inside a `[.perf]` test to measure its atmosphere at
all. That measurement was sound and its result is the best available argument for the method: across
two invocations the **absolute** frame time moved 1.8 ms (6.94 → 5.10) while the **difference between
the arms moved 0.07 ms**. Anyone quoting the absolute from one run would have been quoting noise
nearly three times the size of the effect.

The gap is that the trustworthy method is reachable only through a file format. Either `--ab` should
accept a scene the way `--composition` does, or the counterbalanced loop should be a helper rather
than something each investigation rebuilds.

**Timing-sensitive tests fail under contention and nobody can say which.** Two separate flakes today:
a MIDI hotplug test that polls two seconds for a virtual source, and one unidentified CPU assertion
that failed once after a GPU A/B and did not reproduce in five subsequent runs. Both are the class
the repo already tags `[.perf]`, and neither is a defect in the code under test. Worth a pass that
identifies them and either tags them or makes them robust, because a suite that occasionally fails
for environmental reasons trains people to re-run rather than read.

## Priority 1: the temporal inventory, first measurements

The detector is `tools/temporal_stats.py`, and its whole design is one choice. A sequence taken from
a **static camera** needs no motion vectors and no reprojection: with the view held still, every
frame-to-frame difference is the scene. What remains is separating animation from instability, and
that is the **second difference in time**, `|x(t+1) - 2x(t) + x(t-1)|`. A pixel that animates moves
smoothly and has a near-zero second difference however *fast* it moves; a pixel that shimmers
alternates, and alternation is exactly what a second difference is large for. First differences
cannot tell them apart — which is why "the frame changed" has never been evidence of instability.

Glowmere, static camera over the river bank, 24 frames at 960x540, flicker threshold 6/255:

| arm | pixels that ever flicker | share of baseline |
|---|---:|---:|
| everything on | 3.116% | — |
| bloom off | 2.595% | −17% |
| **water off** | **1.332%** | **−57%** |
| volumetrics off | 3.218% | +3% |

**Water is the dominant source of temporal instability in this view**, by a wide margin. Bloom
amplifies rather than originates — it spreads flicker across more pixels while capping the peak.
Volumetrics contribute nothing measurable, which is a useful negative given how often fog is blamed.

Peak second difference is **315 of 255** — a pixel swinging past the full display range and back
between adjacent frames. That is not a subtle artifact.

Three caveats that the numbers do not carry. This is one camera on one scene, and the water's own
sparkle is *band-passed in screen space*, so its contribution is a function of camera distance and
resolution together — a different view is a different experiment. The arms became trustworthy only
after ADR-182, and every number above was re-measured afterwards. And the flicker threshold of 6/255
is a choice, not a constant; the ranking is stable across plausible thresholds but the percentages
are not.

### Correction: the particle arm was measuring their absence

The attribution table above says volumetrics contribute nothing and does not list particles, because
the particles arm produced a byte-identical frame and was recorded as "a content fact rather than a
harness fact" — this camera simply had none in view.

**That reading was wrong, and for a reason no camera choice could have fixed.** Three harness bugs
stood between a working emitter and a single rendered particle: a clock constructed inside the render
loop makes every frame the first frame, so `dt` is always zero; resizing the target resets the
particle pools; and seeking backwards resets them again. While any of the three held, an emitter at
4,000 particles a second changed not one pixel.

So **every capture this project has ever written contained no particles at all** — Glowmere's river
motes and the visitor's beam included — and every frame looked plausible without them. Once they
simulate, the flicker baseline at a hero camera moves **27x**, 0.347% to 9.363%.

The consequence for this document is specific and worth stating rather than quietly re-running: the
water/bloom/volumetrics attribution was taken on frames with no particles in them. Water being 57% of
the flicker is a statement about a scene that was missing a subsystem. The *ranking* may well survive
— water is a large continuous surface and particles are sparse — but the percentages are not
comparable with anything measured afterwards, and re-measuring is owed.

It is the same failure as ADR-182's, one layer further down: an arm whose null result was
indistinguishable from a broken instrument. The identity check caught the *arm*; it could not catch a
subsystem that was inert in every arm, including the baseline.

### Inside the water: what is and is not the cause

Arms on the water's own authored parameters, same camera and sequence, each checked for
non-vacuity first:

| arm | flickering pixels | attributable |
|---|---:|---:|
| baseline | 3.116% | — |
| ripple normals off | 2.429% | **22 points of the 57** |
| foam off | 3.081% | ~1 point |
| glow off | 3.115% | **none** |
| sparkle off | 3.116% | **none — the arm is vacuous here** |

The tile map localises it beyond doubt. The tiles peaking near **300** are the river; with water off
they fall to 50-130, while tiles containing no water are unchanged to the decimal — which is also the
cleanest evidence that the arm perturbs only what it claims to.

Three things this refutes, each on the mandate's own candidate list for water shimmer:

* **Insufficient resolution is not the cause and supersampling is not the fix.** Rendering the same
  sequence at 1920x1080 instead of 960x540 *raised* the flickering fraction from 3.116% to **4.274%**
  — four times the pixels produced 5.5 times the flickering area. Whatever this is, more samples do
  not average it away.
* **The sparkle is innocent here.** Zeroing it produces a byte-identical frame at *both* resolutions,
  because it is band-passed in screen space and contributes nothing at this camera distance. Worth
  stating loudly, because it is the term whose name most invites the blame — and the term a previous
  investigation spent three attempts on.
* **The subsurface glow contributes nothing**, despite changing the image.

**Ripple normals are the largest identified single cause at about 22 of the 57 points**, which is the
classic specular-aliasing story: high-frequency procedural normals under a tight specular lobe. It is
also only a third of water's share.

**About 34 points remain unattributed**, and no authored parameter reaches them. The candidates are
the moon glint, the sky reflection, and the depth-derived shoreline — and separating those needs arms
inside `water.wgsl` rather than in the scene file. That is the next experiment and it is deliberately
not being run yet: editing that shader on a hypothesis is exactly what cost three rounds on the
anamorphic comb, and the discipline that eventually worked there was an impulse through the
production chain rather than a plausible change.

## Priority 2: the emissive path is not the gap the mandate expects

The mandate's worry is that *"a glowing mushroom should actually look luminous, not merely have a
bright-coloured surface"*, and asks for the whole HDR path to be verified rather than assumed. It
was, and it holds:

* **Float targets throughout.** Scene colour, normal+roughness and every post target are
  `RGBA16Float`. There is **no clamp on emission anywhere in the shaders** — grepped, not assumed.
* **The ladder is authored as absolute rungs, not fractions**, precisely so a later "brightness"
  control cannot flatten it: `inert` 0, `silhouette` 0.035, `groundCover` 0.0615, `noticeable` 0.295,
  then a deliberate gap to `special` 3.94 and up to `brightest` 6.89. The 13x gap between the
  brightest ordinary vegetation and the first rung across the gap is **validated in code**, because a
  profile edited toward "a bit more glow everywhere" closes it without anybody noticing the effect
  has been removed.
* **Emissive content becomes actual illumination.** Glowmere's ecology light field turns emissive
  scatter into local lights, and the visual contribution is large: shading the same frame with the
  local-light count forced to zero changes **28.96% of pixels**, mean delta 7.62/255, peak 233.

Set against the renderer upgrade's measurement that removing *every* local light is worth 1.4 ms of
an 11.4 ms scene pass, the emissive strategy costs about **12% of the scene pass and pays for 29% of
the frame**. That is a good trade by any standard, and it is the mandate's own proposed hierarchy —
hero emissives to local lighting, distant ones to emission and bloom — already implemented via a
field rather than per-object lights.

**So this is an "already production-grade" answer, not a gap.** The remaining Priority 2 work is
lighting *quality* — §7's cinematic evaluation of depth, separation and focal hierarchy — rather than
the HDR/emissive plumbing, which is sound.

One caveat carried from the scene work: Glowmere Valley 2 recorded that mask-modulated emission on
its hero mushrooms reads *less punchy* than the flat version it replaced, even at intensity 6.0. That
is a material-authoring trade rather than a path defect, and it is the kind of thing §7 is for.

## What Phase 1 still owes

- The temporal artifact inventory (§4), which needs the representative suite rendered and looked at.
- The variance protocol above, which needs a quiet machine.
- The realtime/offline parity audit (§15), beyond the two parity defects already fixed (ADR-146,
  ADR-147).
- Scene authoring ergonomics (§17, §G).
