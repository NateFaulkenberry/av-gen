# Level 3 gap closure — Phase 1 assessment

**Status:** in progress. Assessment, plus the parity fixes it turned up (ADR-186).
**Updated:** 2026-09-14, after the offline detail-limits work.
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

## Priority 1, re-measured: the harness is now in the repository, and the answer changed

The re-measurement the particle correction owed has been done, and the first thing it produced is
not a number. It is `tools/flicker_bench.py` — a harness that renders every arm through `--render`
(the path that produces deliverables, so a subsystem inert in it is a defect and not an artifact),
discards a warm-up so a filling particle pool is not counted as instability, **proves every arm
changes the analysed frames before reporting its number**, and writes the camera it used to a file.

That last point is why the original figures cannot be reproduced rather than merely disagreed with:
**the camera was never written down.** "A static camera over the river bank" does not name a view,
and water's contribution is a function of how much of the frame it occupies. Every number below
names its view, and the view ships with the result.

### The answer is different, and the largest source was not on the list

Glowmere, 960x540, 24 frames analysed after a 2 s warm-up, flicker threshold 6/255, offline tier.
Two views: **river** `4,1.6,6 → -1,-1.6,-38` fov 50, and **graze** `2,-1.2,2 → -1,-1.7,-45` fov 50.

| arm | river | graze |
|---|---:|---:|
| baseline | 4.453% | 4.158% |
| **post-process FXAA off** (`post.antialias = 0`) | **−21%** | **−19%** |
| **water ripple normals off** | **−14%** | **−19%** |
| particles off | −3% | — |
| bloom off | 0% | −0% |
| water specular off | −2% | — |
| water sparkle off | *vacuous* | — |
| water reflection off | +4% | — |

**The post-process antialiasing is the single largest identified source of temporal instability**, and
it was on nobody's candidate list. It is Lottes's FXAA (ADR-059), and this is its textbook failure:
a spatial filter that makes an independent per-frame edge decision flips that decision on pixels
near its threshold, and a flipping decision is exactly what a second difference is large for.

A strength sweep at the river view makes the causality hard to argue with, and also says what kind
of problem it is:

| `post.antialias` | 0 | 0.25 | 0.5 | **0.75 (authored)** | 1.0 |
|---|---:|---:|---:|---:|---:|
| share vs baseline | **−21%** | −5% | −5% | — | +5% |

Monotone in strength, but **most of the cost is incurred by switching the pass on at all** — the step
from 0 to 0.25 is four times the step from 0.25 to 1.0. So this is not a strength-tuning problem. It
is the pass's frame-independence, and tuning the dial will not reach it.

### Two arms that turned out to be badly formed, and what they cost

Both were in the original inventory, and both are the reason its numbers moved so far.

**`--disable water` does not remove flicker; it changes what is on screen.** Taking the surface away
reveals the riverbed, which has its own aliasing. At the river view the whole-water arm attributes
−5% while the *ripple normals* arm inside it attributes −14% — an arm cannot attribute less than a
term it contains, so the discrepancy is the riverbed arriving. The per-term arms are sound and the
subsystem arm is not, which is why the table above leads with terms.

**`--disable post` is not a per-stage arm either.** It removes the tone map with everything else, so
the frame's transfer function moves and every threshold in the detector moves with it. Its −21% at
the river view happens to equal the FXAA arm's, and that agreement is the only reason it is readable
at all; a per-stage arm was needed to know which stage it was.

This is ADR-182's rule one turn further: an arm can be *non-vacuous* — it demonstrably changes the
frames — and still attribute nothing, because what it changed was not one thing.

### What this does and does not overturn

It does not overturn "water is a significant source": ripple normals are 14–19% at both views, the
second-largest identified term, and the old finding named them too. What it overturns is the
*share*: at no view measured here does water reach anything like 57%, and the arm that produced that
number is now known to be confounded.

It also contradicts "bloom amplifies rather than originates". Bloom attributes **0%** at both views,
measured with the stage turned off in the scene rather than with the chain removed. The likeliest
explanation is that the old figure was the whole-post arm under another name.

Sparkle is confirmed exactly: **vacuous**, byte-identical frames, at both this view and the original.
Two independent harnesses agreeing on a null is worth more than either alone.

### The offline tier's lifted detail limits cost temporal stability

Same view, same arms, three configurations — this is a direct consequence of ADR-186 and it is a cost
rather than a benefit:

| configuration | baseline flicker | water arm | post arm |
|---|---:|---:|---:|
| realtime tier | 2.420% | −12% | −20% |
| offline tier, `--render-limits live` | 2.842% | −8% | −21% |
| **offline tier, limits lifted (the new default)** | **4.453%** | −5% | −21% |

**Lifting the distance limits increases flickering area by 57%** (2.842 → 4.453). That is what a
sharper far field costs: scatter held at rung 0 is high-frequency geometry where the ladder used to
substitute something smoother, and high-frequency geometry aliases. The offline shading tier alone
adds a further 17% over realtime (2.420 → 2.842) for the same reason — auxiliary passes at full
resolution resolve more detail to alias.

**Acted on** (ADR-191): the LOD half of that was a mistake and is reverted. Three of ADR-186's four
reductions hide something a viewer would otherwise see -- scatter that vanishes, a character stepping
at 20 Hz, a frozen herd -- and lifting those is right. The ladder is not one of those: it picks a
representation by projected screen size and is this renderer's only prefilter for geometry smaller
than the sampling grid. The offline default now keeps it, which puts the flickering area at 2.761%
against live playback's 2.691% -- **+2.6% instead of +57%** -- with the other three still lifted.
`--render-limits unlimited` still lifts all four.

The remaining figure below is still the argument that **the far field needs to be resolved, not
simplified**, and they move a temporal-AA or supersampling question from "refused
on measurement" (ADR-132, which refused TAA as a *prerequisite for LOD stability*) to a different
question it was never asked: what resolves an offline far field that is now drawing its real
geometry. Worth noting the earlier refutation still stands on its own terms — supersampling made
flicker *worse* at 1920x1080 in the original inventory, and that experiment should be re-run on this
harness before anybody leans on either result.

### Resolution still does not average it away

The original inventory refuted supersampling by finding that 1920x1080 *raised* the flickering
fraction rather than lowering it, and that experiment predated both the particle fix and the
per-term arms. Re-run on this harness, same river view, same arms:

| | 960x540 | 1920x1080 |
|---|---:|---:|
| baseline | 4.453% | 4.717% |
| FXAA off | −21% | −16% |
| ripple normals off | −14% | −20% |

**Four times the pixels gave a slightly *larger* flickering fraction**, not a smaller one. The
direction of the original refutation holds; its magnitude does not — it recorded 3.116% → 4.274%
(+37%) where this measures +6%. The conclusion that matters is unchanged and now rests on a harness
anybody can re-run: whatever this is, more samples of it do not cancel.

It is also consistent with the FXAA finding. A per-frame edge decision does not become stable
because the edge is sampled more finely; there are simply more edges near the threshold.

### How to re-run any of it

    tools/gpu-lock.sh python3 tools/flicker_bench.py \
      --scene examples/world/glowmere-stylized.scene.json \
      --camera "4,1.6,6:-1,-1.6,-38:50" \
      --arms particles,volume \
      --scene-arm "aa=post.antialias=0" \
      --scene-arm "ripple=valley.terrain.water.ripple=0" \
      --out /tmp/flicker

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

## Realtime/offline parity (§15): three defects, and what the third says about the other two

ADR-146 (hysteresis in the GPU cull ladder read the authored value at every tier) and ADR-147 (the
render job never called `setQuality`, so a batch frame was byte-identical to an interactive one) were
found before this document. A third arrived with ADR-186 and it is the same shape a third time:

**The Offline tier's promise of "no representation shortcut" was only ever about shading.** Every
field `forTier` sets for Offline — `materialTiers`, `forcedMaterialTier`, `renderScale`,
`volumeResolutionScale`, `lodHysteresisAllowed` — is about how a pixel is shaded or how finely a
shared pass is sampled. Four *geometric and temporal* reductions were never covered, because each
lives in a subsystem that decides for itself: the procedural cull ladder's distance and screen-radius
tests, its LOD rungs, `updateRigs`' pose rate, and the entity world's behaviour bands. A finished
render still had billboards in its far field and a motionless herd on the far hillside.

Fixed as `scene::DetailLimits` plus one render setting (`tier` | `live` | `unlimited`). Shown to
change the output per ADR-182 — 0.59% of pixels, max delta 137, in a box over the distant scatter.

Two things this says about the audit that remains:

* **A tier field is not a parity guarantee; it is a guarantee about the subsystem that reads it.**
  The audit should enumerate every subsystem that reduces work and ask which policy object it reads,
  rather than reading the tier table and assuming coverage.
* **The same pass found `--tier` never reaching `RenderSettings::tier`**, so `--render --tier realtime`
  rendered at offline. Three of the four parity defects found so far are "a policy set on the
  interactive side only". That is now a search pattern, not an anecdote.

## The §15/§16 audit, run: three more fields that no path reads

The search pattern above says to enumerate every subsystem that reduces work and ask which policy
object it reads, rather than reading the tier table and assuming coverage. Run against
`QualitySettings` — grep every field for a reader outside its own header — it produced two hits
immediately, and chasing the second produced a third finding that is larger than either.

**`sdfShadowSteps` was read by nothing.** All four tiers set it (16, 24, 32, 48) and the shadow
march derived its own budget as `maxSteps / 4` in `shaders/sdf_raymarch.wgsl`. Now wired through the
free `info.w` lane, capped by the object's own march so a cheap object cannot get an expensive
shadow.

**`pcssBlockerTaps` is read by nothing, and is still not wired.** The blocker search takes the same
tap count as the PCF filter — `shadowPcfTaps`, through `ShadowUniforms::info.z` — and every lane of
`info` and `splits` is already taken, so wiring it is a uniform-layout change rather than a line.
Left in place with the truth attached at its declaration rather than deleted, because the tiers do
want a separate budget for it: ADR-111 measured the blocker search as the largest single contributor
to the shadow mask's residual. Only the High tier sets the two differently today (20 PCF, 16
blocker), so wiring it moves one tier's picture and no other.

### And the reason the first fix could not be tested: a raymarched SDF casts no shadow

The test written to prove `sdfShadowSteps` reaches the picture failed, and kept failing as the arms
got more extreme — 8 steps against 1024 produced a byte-identical frame. The probe that settles it
is one layer down: **switching the key light's shadow off, with the SDF still in the scene, changes
the frame by zero bytes.** The object casts no shadow at all, so no step budget could have shown up.

`SdfRenderer::update` computes each raymarched object's screen-space quad — `sdf.rect`, in NDC —
from the **camera's** view-projection. `drawRaymarchDepth(..., reducedSteps = true)` then reuses that
rect when the shadow pass draws the object into a shadow map whose projection is the **light's**. The
ray the shader reconstructs is the light's, because the frame block is; the quad it reconstructs the
ray over is the camera's. So the march runs over the wrong region of the shadow map.

ADR-034 says raymarched SDFs "appear in the depth prepass and in the shadow maps". The prepass half
is true, and that is exactly why this hid for so long: the prepass shares the camera's projection, so
the one pass that works is the one that cannot expose the bug.

The fix is a per-view rect, or the full-screen fallback the shader already has, in the shadow pass —
and it carries an unmeasured cost: a full shadow-map quad marched per SDF per cascade. That is a
decision with a number attached, so it is recorded here rather than guessed at. Kept as a probe,
`avgen_render_tests "[.probe][sdf]"`, which passes when the defect is gone.

Worth noting what this does *not* affect: no shipped example scene uses a raymarched SDF, which is
why nothing looked wrong. It is a defect in a feature nothing currently leans on.

## What Phase 1 still owes

Ordered by what blocks the most.

1. ~~Re-measure the Priority 1 attribution.~~ **Done** — see above. The harness is
   `tools/flicker_bench.py` and the answer changed: FXAA is the largest identified source at 19-21%,
   water's ripple normals second at 14-19%, bloom zero, and the old 57% came from a confounded arm.
2. **What FXAA's 20% should be done about.** It is not a strength-tuning problem — the dose-response
   says most of the cost arrives when the pass is switched on at all. The options are a temporal
   term, a threshold hysteresis, or not running FXAA at the offline tier at all and resolving the
   edge some other way. None of them should be chosen before §4's inventory says whether the
   *artifact* a viewer sees is edge crawl; a 20% share of a detector's metric is not by itself a
   reason to change a shipping picture.
3. **The remaining unattributed flicker.** The identified terms sum to roughly 40% at the river view;
   the rest is not reached by any authored parameter. The candidates are the moon glint, the sky
   reflection and the depth-derived shoreline, and separating them needs arms inside `water.wgsl`.
   Still deliberately not started: editing that shader on a hypothesis is what cost three rounds on
   the anamorphic comb, and the per-term scene arms are not exhausted yet.
4. ~~Re-run the supersampling refutation on this harness.~~ **Done** — the direction holds (4.453% →
   4.717% for four times the pixels), the magnitude was much smaller than recorded. The refusal
   stands and now rests on something re-runnable.
5. **The variance protocol**, which needs a quiet machine.
6. **The temporal artifact inventory (§4)**, which needs the representative suite rendered and
   *looked at*.
7. **§7's cinematic lighting evaluation** — depth, separation, focal hierarchy. This is what remains
   of Priority 2 now that the HDR/emissive plumbing has been verified sound, and it requires looking.
8. **The performance dashboard (§13)** — the one item that genuinely needs a human to certify.
9. **AOV export (§B)**, and the debug views the mandate lists that do not exist — neither should be
   built without saying what question each answers.
10. **Wire `pcssBlockerTaps`**, which needs a lane in `ShadowUniforms`, and **fix the raymarched
    SDF's shadow-map quad** — both found by the §15 audit above, both left recorded rather than
    rushed. The SDF one wants its cost measured first.
11. **The rest of the realtime/offline parity audit (§15)** beyond `QualitySettings` — the same
    enumeration against the scene's own reduction policies, the particle budgets and the terrain's
    view distances.
12. **Scene authoring ergonomics (§17, §G).**
13. **Maintainability (§18)** — flagged, still not a recommendation; the thing to look for when the
    C/D/F measurements start touching `SceneRenderer::render`.

Also outstanding and not blocked by any of the above: the counterbalanced A/B for ten heroes and ten
emitters, designed and never run because the GPU was contended.
