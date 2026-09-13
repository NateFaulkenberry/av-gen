# ADR-136: What the shading tiers are worth, and what that says about the 8.4 ms residual

**Status:** Accepted
**Date:** 2026-09-13

## Conditions

Everything below is one session, one process per arm, arms interleaved block by block (ADR-113,
ADR-117), `tools/gpu-lock.sh` held throughout, nothing else on the GPU.

```
scene      examples/world/glowmere-stylized.scene.json
size       1280x800, realtime tier
build      cmake --preset release, branch agent/mat at the ADR-135 commit
protocol   120 frames per block, first 12 discarded, GPU timestamp queries
machine    Apple M2 Max, Dawn/Metal
```

The session's own null A/B (`--ab none`, 3 pairs) reports **−0.93%, NOT A RESULT**, with baseline
blocks varying 0.93% GPU. That is the floor these numbers are read against; the 2% rule stands.

**This session's baseline is 14.02 ms GPU / 11.34–11.53 ms scene**, against the recorded
13.37 / 10.81. Session-to-session gaps of that size are exactly what §3.1's "same session or no
comparison" rule exists for, and no number below is a cross-session subtraction.

## The measurements

| arm | GPU median (base → arm) | Δ GPU | scene pass (base → arm) | pairs | baseline spread | verdict |
| --- | --- | --- | --- | --- | --- | --- |
| `none` (null) | 14.02 → 14.16 | −0.13 (−0.93%) | 11.34 → 11.53 | 3 | 0.93% | not a result |
| **`matflat`** | 14.09 → 8.32 | **+5.77 (+40.9%)** | 11.34–11.47 → 5.24–5.64 | 5 | 1.40% | **a result** |
| `matlights0` | 14.09 → 12.78 | +1.31 (+9.3%) | 11.47 → 10.03–10.16 | 3 | 0.93% | a result |
| **`matreduced`** | 13.96 → 13.17 | **+0.79 (+5.6%)** | 11.21–11.73 → 10.49–10.62 | 5 | 3.29% | **a result** |
| `contact` (re-taken) | 14.42 → 12.91 | +1.90 (+13.2%) | 11.47–12.26 → 9.31–10.49 | 3 | 5.91% | a result, noisily |

Per-pair deltas, because a median can hide a single carrying pair:

* `matflat`: +5.77, +6.03, +5.70, +5.83, **+1.11**
* `matreduced`: +0.92, +0.79, +1.11, +0.66, +0.59
* `matlights0`: +1.64, +1.31, +1.25
* `contact`: +0.72, +2.10, +1.90

**One arm block per `matflat` run reads about halfway between the two arms** — +1.11 here, +1.31 in
an earlier three-pair run of the same arm. It is a whole block, it appears once per run, and it
appears in a different position each time (pair 2 then pair 5), which is the shape of a machine
hiccup and not of an arm. The median over pairs is the robust statistic and the harness already
reports it; the outliers are listed rather than trimmed, because a discarded outlier that goes
unmentioned is how a noise floor becomes whatever the author needs it to be.

`contact` was re-taken in this session so that it could be subtracted from `matflat` without
crossing a session boundary. Its baseline spread is 5.91% — the machine was less settled by then —
so it is quoted as **1.9 ms in this session** and not offered as a correction to ADR-118's 2.36 ms.

## What this says about the 8.4 ms

§3.2.3 records "~8.4 ms of opaque shading that no arm removes". **The flat rung removes about 5.9 ms
of scene pass** — roughly 70% of it — so the residual is not opaque after all. It was invisible for
a structural reason ADR-118 already named: every arm before this one removed a *pass* or a
*subsystem*, and the work here is per-light terms inside `evaluateLight` and per-fragment ambient
work inside `shadeSurface`. Neither is a pass. A shading-tier arm is the first instrument in this
project that can point at them.

Decomposing the flat rung, same session, **overlapping rather than disjoint** (removing all local
lights also removes their contact march, so the first two rows double-count):

| removed by the flat rung | this session |
| --- | --- |
| the contact march, all lights | ~1.9 ms |
| the whole clustered local-light loop, 222 lights | ~1.4 ms |
| everything else: directional cascade lookup, PCSS blocker, mask read, the AO sample, painterly specular | the remainder, ~2.6–3.5 ms |

**The three corrections this makes to the record.**

1. **The local-light count is not the problem.** Glowmere carries 222 local lights and removing
   *every one of them* is worth 1.4 ms of an 11.4 ms scene pass. §3.6 and ADR-114 already said the
   cluster grid was not a cost; this says the loop it feeds is not one either. Anyone who arrives at
   this scene and reaches first for the light count will spend their week on 12% of the problem.
2. **Shadow *terms* are the largest thing the tiers can reach**, not shadow *passes*. The shadow
   pass is 0.33 ms and has been immovable through three separate investigations; the per-light
   shadow work inside the lit pass is several times that.
3. **The reduced rung is small and the flat rung is large, and the gap between them is the shadow
   terms.** `matreduced` (+0.79 ms) caps local lights at 6 against a measured mean froxel occupancy
   of 5.70, so the cap barely binds and almost all of its saving is the local contact march.
   Everything above that requires giving up a shadow, which is a visible change and is why the
   rungs are separate.

## The visual gate (§50)

The canonical Glowmere frame was captured at frame 119 under each arm and inspected at full size.

* **`matreduced`** is close to the baseline over most of the frame and **visibly changes the
  foreground**: the bioluminescent light pools on the ground — the scene's signature — shrink or
  disappear where a froxel held more than six lights, most obviously the purple pool at lower left.
* **`matflat`** is a different picture in the near field: every contact shadow is gone, the ferns
  lose their shading gradient and the ground glow pools disappear. **The distant hillside and its
  tree line are, by eye, unchanged.**
* **`matlights0`** removes the scene's identity entirely, as intended for a bound arm.

That last observation is the whole argument for Phase D and is why these arms are ceilings rather
than settings: the half of the image the flat rung damages is the near half, and the half it leaves
alone is exactly the half an importance rule would assign it to.

## Decision

**Both rungs are kept, as policy, off by default, with assignment unwired** (ADR-135). The flat rung
is justified by 5.77 ms; the reduced rung by 0.79 ms, which clears the 2% floor but only just, and
which the visual gate shows is not free in the foreground.

**Neither is enabled globally in any tier.** A frame-global flat tier saves 41% and fails §50 on the
near field, and §49 forbids a quality reduction that is not explicit and general. What ships is the
mechanism and the policy; what turns it on is assignment, and ADR-138 says what assignment can be
expected to realize before anyone builds it.
