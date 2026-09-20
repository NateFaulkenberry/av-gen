# ADR-480: The editor chooses its own resolution, and a resize stops being a seek

**Status:** Accepted
**Date:** 2026-09-20
**Supersedes the deferral in:** ADR-137 (render scale is a tier parameter, and is not applied yet)
**Related:** ADR-084 (`canvasRenderScale`), ADR-212 (an offline render may spend pixels),
ADR-246 (the preview is not a picture of the render), ADR-360 (the determinism contract),
ADR-170 (minima over repeats), ADR-182 (a probe that cannot fail proves nothing)

## Problem

Playing `examples/world/glowmere-valley-2-multicam.json` in the editor ran at single-digit frames
per second. It was not a regression, it was not accumulation, and it was not the recently merged
work — all three were measured and ruled out before this started.

It was fill cost, and the thing that decides the fill is the canvas. The editor renders the world
into the dock tree's centre region **at the display's backing scale**, so a maximised window on a
Retina display asks for several times the pixel count any benchmark quotes.
`docs/application-performance.md` §7 has said so since ADR-084 and every run logs it at frame 60.

The lever that fixes it has existed for ten days. ADR-084 added `canvasRenderScale`, a slider in
Settings, default 1.0, range 0.25–1.0. **Nobody has ever moved it.** The same diagnosis was filed
twice, on two different scenes, and both times the finding was "the lever exists, awaiting the
owner". A quality lever that has to be found is not a quality lever; it is a defect with a knob,
which is the phrase ADR-137 used about a different half of this same mechanism.

## What was measured

`tools/resolution_sweep.sh` runs the multicam film headless at one aspect ratio and six linear
scales, three repeats, interleaved by round, under `tools/gpu-lock.sh`, reported as minima over
repeats (ADR-170). `tools/resolution_sweep_report.py` prints it.

| target | Mpx | GPU min | spread | wall min | local slope |
|---|---:|---:|---:|---:|---:|
| 2068×1326 | 2.74 | 27.79 | 0.46 | 34.44 | — |
| 1758×1126 | 1.98 | 21.43 | 0.13 | 27.66 | 0.80 |
| 1468×940 | 1.38 | 16.12 | 0.13 | 22.77 | 0.79 |
| 1198×768 | 0.92 | 12.65 | 0.59 | 19.42 | 0.60 |
| 1034×662 | 0.68 | 10.29 | 2.49 | 17.08 | 0.70 |
| 724×464 | 0.34 | 11.21 | 0.98 | 18.65 | −0.12 |

The GPU frame is **affine in pixel count**, and remarkably so:

> **GPU ms = 4.63 + 8.45 × Mpx** — Apple M2 Max, release, maximum residual **0.24 ms** over the
> five points from 2.74 down to 0.69 Mpx, which is smaller than the spread between repeats at any
> one of them.

### ADR-137's prior is wrong in the range the editor lives in, and right where it was taken

ADR-137 deferred dynamic resolution behind the measurement that this renderer's scene pass is
"only 44% resolution-dependent" — 640×400 being 0.26× the pixels of 1280×800 and 0.66× the scene
time. Between 2.74 and 1.38 Mpx the local log-log slope is **0.80**, not 0.44.

Both numbers are correct. The affine law explains them: below about 1 Mpx the 4.63 ms fixed term
dominates and the slope collapses towards zero, which is exactly the region 640×400 sits in. **The
deferral was decided on a measurement taken in the part of the curve an editor canvas never
visits.** This ADR does not overturn ADR-137's caution; it moves the measurement to where the
question is.

The same law is why the ladder's floor is 0.5 and not ADR-084's 0.25: past the point where the
fixed term dominates, a smaller scene spends image quality and buys nothing.

## Decision

### 1. `app::InteractiveResolution` picks a rung from the frame time

Five rungs — 1.00, 0.85, 0.71, 0.58, 0.50 linear, so 100%, 72%, 50%, 34%, 25% of the pixels —
applied as `QualitySettings::renderScale` (ADR-137's mechanism: the scene renders into a smaller
HDR target and the tonemap filters it up into the output the canvas asked for).

A short ladder rather than a continuous knob because every change reallocates the render targets
and invalidates the screen-space history, so the set of distinct costs the application can pay
should be small and nameable.

Four properties of the control law, each a test in `tests/unit/test_interactive_resolution.cpp`:

* **It holds rung 0 unless the GPU is over budget.** A frame that fits changes nothing, and an
  editor that keeps up is byte-identical to one built before this existed.
* **It declines to act when the main thread is what the frame is waiting for.** §2 again: the
  profiling outranks the proposed solution, and the proposed solution is a GPU lever. The measured
  Tree of Life shape — wall 18.42 ms against a GPU 14.68 — does not even reach the guard, because
  14.68 is inside the budget.
* **It predicts pessimistically.** The next rung's cost is estimated with the naive proportional
  model, which the affine law says always *understates* the saving. So the controller never drops
  further than the evidence supports; when one step is not enough it takes another. Two rungs down
  and one up per dwell, because a person at 5 fps should not watch the ladder walk.
* **It settles.** 2,000 frames of a closed loop against the measured cost law produce at most five
  decisions. A hunting controller would produce hundreds, and each one costs a reallocation.

Off is the default for the *type*; the live editor turns it on. `runHeadless` never constructs a
decision and `RenderJob` sizes its own targets at the Offline tier.

### 2. It is presentation, and nothing else (§33/§34)

The scene, its parameters, the timeline and the flattened `scene::Scene` are untouched — the
renderer reads them and does not write back. The Offline tier pins `renderScale = 1.0` and
`QualityPolicy::assertOfflineIsUncompromised()` is the test that says so. ADR-360's contract is
that *two renders of the same range may not differ from each other*, and nothing here is reachable
from a render at all.

`canvasRenderScale` is untouched and stays manual. The two are different levers: ADR-084's reduces
the **canvas target**, which the person chooses and the application must never override; this one
reduces the **scene target below the canvas**, which is the renderer's own business. ADR-246's rule
that `canvasRenderScale` does not compound with the output preview's quality rung is unaffected.

### 3. A resize is not a seek

`SceneRenderer::resize` ended in `resetTemporalHistory()`, which resets the particle pools. So
**every splitter drag and every window resize in the editor threw away the particle simulation** —
and on Glowmere the motes take 30 to 50 seconds of playback to reach the vortex (ADR-380). Two
existing tests work around it by warming at the capture resolution and say so in their comments.

The pools are world state. Nothing in them is indexed by a screen pixel, and a new render-target
extent has no more claim on them than a new window title does. `resetScreenHistory()` is the
narrower reset — prev view-projection, prev model matrices, the AO history, ADR-410's temporal ring
— and `resize` now calls that. `resetTemporalHistory()` keeps its meaning and its callers: a seek
moves the world's clock, and the pools are the world's state at that clock.

This is a defect fix on its own terms, and it is also what makes rung changes legal: a
presentation change that discards thirty seconds of simulation is not a presentation change.

### 4. `setQualitySettings` re-resolves the render scale

`renderScale` is consumed by `resize()` and by nothing else, so assigning it to a renderer whose
targets already exist was a **silent no-op**. ADR-212 records hitting exactly this and fixing it at
one call site by ordering two statements. The benchmark harness sets a quality arm per block,
*after* the targets are sized, and did not get that fix — so the four new `scale*` quality arms
would have measured nothing and reported a null result as a real one.

The setter now re-sizes when the value moved and there is something to re-size, which makes it mean
what it says for every caller.

## What each number would have been if the change were wrong

**The end-to-end arm.** 300 frames of the live editor on the multicam film, a 2452×1624 canvas
(3.98 Mpx) inside a 3840×2400 window:

| | GPU median | FRAME median | `gpu.acquire WAIT` median |
|---|---:|---:|---:|
| `--adaptive-scale off` | 42.01 ms | 42.78 ms | 35.42 ms |
| `--adaptive-scale on` | **15.79 ms** | **18.69 ms** | **11.34 ms** |

23 fps to 53 fps. The main thread's own work is ~7 ms in both arms and does not move; every
millisecond of the difference comes out of the wait. Three rung changes over the 300 frames — 0 to
2 to 3 to 4 — and then it settles, which is `# render-scale moves` reading a mean of 0.010.

**The component arm.** `--ab scale71`, four counterbalanced pairs at 2068×1326, headless:

    A/B gpu : baseline 29.49 ms, arm 17.37 ms, delta +12.12 ms (+41.11%)   A RESULT
    A/B wall: baseline 36.49 ms, arm 24.40 ms, delta +12.09 ms (+33.13%)   A RESULT
    drift: +1.78% GPU over the run -- the machine held still
    per-pair gpu delta ms: pair1=+11.93 pair2=+12.12 pair3=+11.93 pair4=+13.11

On minima (`tools/ab_minima.py`): baseline 28.05, arm 16.52, **delta 11.53 ms**, worst spread
within an arm 0.39 ms. Had `setQualitySettings` not been fixed, every one of those deltas would
have read 0.00 and this arm would have been a null result presented as evidence of nothing.

**The resize probe.** `changing the render target extent does not empty the particle pools`, in
`tests/rendering/test_particle_determinism_gpu.cpp`, with a same-extent control arm beside it
(ADR-182). Before the fix it read **101 alive out of 2,912** where the control read 2,912.

And it read 101 *again* after `particles_->resetAll()` was removed from `resize`'s path, which is
the whole reason the probe exists: `resetScreenHistory()` was also clearing `temporalScene_`, so
the next frame concluded the scene had been swapped under it and reset the pools by that route
instead. The second defect was invisible to reading and obvious to the test.

## Consequences

**The editor is unchanged until it cannot keep up.** At rung 0 no call is made, and the whole
feature is one comparison per frame.

**The Performance panel says what it rendered at.** When the two extents differ it prints `world
drawn at 1226x812 (1.00 of 3.98 Mpx) and sharpened up`, because a per-pixel figure on that panel
means nothing without it, and because "why does this look softer than it did" deserves an answer
in the interface rather than in a log.

**Two instruments arrive with it.** `tools/resolution_sweep.sh` plus its report script, and four
`scale*` quality arms that put the same lever inside the existing `--ab` harness — so a number
taken with one describes the other.

**What this does not do, named rather than implied.**

* *It does not make a scrub faster.* The dominant scrub cost is `EntityWorld::seek` re-simulating
  90 seconds of entity behaviour per click, which no resolution can touch.
* *It does not make opening a project faster.* That is 2.1 seconds of texture upload over 58
  uploads, which is a different ADR.
* *The rungs are discrete and a change costs a reallocation plus a screen-history reset.* A
  sub-rectangle of a fixed-size target would make a resolution change free and is the shape real
  dynamic resolution takes; it needs every auxiliary target, the froxel grid, the AO, the shadow
  mask, the volume and the temporal ring to learn "current extent ≤ allocated extent", which is
  much more of the renderer than this pass should touch. Recorded as the follow-up, with its
  reason.
* *Nobody has seen it.* The Settings checkbox, the budget slider and the Performance panel line are
  asserted by construction and by the settings round-trip test, and by nothing else. The frame
  times are measured on the device and do not depend on any of it.
