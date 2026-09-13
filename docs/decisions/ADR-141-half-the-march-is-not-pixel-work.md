# ADR-141: Half the march is not pixel work, and Constellation's noise floor is not one number

**Status:** Accepted
**Date:** 2026-09-13
**Answers:** Phase F — what the scalability axes of ADR-139 actually cost
**Related:** ADR-113 (a measurement carries its conditions), ADR-117, ADR-140

## Two problems, and the second one had to be solved first

Constellation is the scene volumetrics matter on, and ADR-113 established that its whole-frame GPU
median is not a usable statistic: 36.97% within-session spread, a null A/B that a fixed 2% threshold
would have certified as a 16% improvement.

**Measured again today, over four separate null and paired baselines in one session, Constellation's
floor was 13.16%, 21.77%, 3.82% and 34.96% GPU.** It is not one number. It is not even one number
*within a session*: it is whatever the four baseline blocks of that particular A/B happened to
spread by. The harness is right to compute it per-run rather than carry a constant, and a reader of
any Constellation figure has to be told which floor it was measured against.

**The statistic that works is the paired `volume.march` median.** ADR-140 made the march a label of
its own; within one A/B pair, the arm's march median against its own baseline block's march median
is far tighter than either frame median. Three properties earn it: it excludes every pass the change
does not touch, it is paired so session drift is charged to both arms, and in practice the arm
blocks repeated *exactly* — `volumepreview` reported 1.835 ms in all three of its blocks while its
baselines ranged 6.750–7.012.

This is not offered as a replacement for ADR-113's protocol. It is the same protocol — one session,
interleaved, paired, ≥3 pairs — with a narrower quantity in place of the frame median, and every
figure below is quoted with both.

## What the axes cost

1280×800, M2 Max, Dawn/Metal, release, `tools/gpu-lock.sh`, 120 frames (12 discarded), revision
`bfed449`/`ae3248c`, interleaved A/B, 3–5 pairs each. "march ratio" is arm ÷ baseline within a pair.

| arm | scene | march ratio, per pair | whole-frame GPU | floor | verdict |
|---|---|---|---|---|---|
| `volumefull` (1.0, 4× pixels) | Constellation | 2.45, 1.57, 1.91 | **−55.8%** | 21.8% | a result: it costs |
| `volumequarter` (0.25, 1/16 pixels) | Constellation | 0.35, 0.43, 0.61, 0.51 | **+40.0%** | 13.0% | **a result** |
| `volumesteps` (0.5× steps) | Constellation | 0.59, 0.58, 0.64, 0.64, 0.64 | +26.8% | 35.0% | **not a result** |
| `volumepreview` (both) | Constellation | 0.27, 0.26, 0.26 | **+51.0%** | 3.8% | **a result** |
| `volumefull` | Glowmere | 3.00, 3.00, 2.75 | −3.1% | 6.7% | not a result |
| `volumequarter` | Glowmere | 0.60, 0.50, 0.55 | +2.7% | 2.0% | **not trusted — see below** |
| `volumesteps` | Glowmere | 0.60, 0.50, 0.60 | +3.4% | 2.9% | **not trusted** |

**Glowmere's own null A/B in that batch reported "A RESULT" at −2.39% against a 2.00% floor.** By
ADR-113 §5 that is a broken measurement whatever it says about any real arm, so the two Glowmere
rows marked "not trusted" are withdrawn as whole-frame results even though the harness certified
them. The cause is visible: the floor was the fixed 2% constant, and the session's baseline spread
landed exactly on it. **The march-ratio column for Glowmere stands**, because it is a different
statistic that the null did not invalidate — and it agrees with the frame numbers being tiny, since
Glowmere's whole march is 0.655 ms of a 13.4 ms frame.

## The finding

**The march is strongly sublinear in resolution. A sixteenth of the pixels costs about half the
time.** Constellation 0.35–0.61, Glowmere 0.50–0.60, and in the other direction four times the
pixels costs 1.6–2.5× (Constellation) or 2.75–3.0× (Glowmere), never four.

So **roughly half of what the `volume.march` interval measures is not proportional to pixel count**:
pass setup, the render target's clear and store on a tile-based GPU, pipeline and barrier cost, and
whatever fixed latency the pass pays before its first wave retires. The pixel-proportional half is
all that any pixel-reducing technique can address, and that is the single most decision-relevant
number Phase F produced (it is what ADR-143 rejects temporal reprojection on).

**Steps are the weaker axis, and by less than the resolution axis.** Halving the step count costs
0.58–0.64 of the march on both scenes, so the loop body is genuinely about 40% of the march's
pixel-proportional work — but it is bounded below by the same fixed half, which is why halving the
steps can never halve the pass.

**The axes roughly multiply.** 0.47 (quarter resolution) × 0.61 (half steps) = 0.29 predicted for
`volumepreview`, measured 0.26–0.27 on Constellation and 0.25–0.27 on Glowmere. Two scenes, both
axes, within 10% of the product — which is the best evidence available that the two parameters are
in fact independent and that ADR-139 was right to keep them apart.

## Consequences

Constellation is usable for volumetric decisions **through the paired per-pass march median**, and
only that way. Every future Phase F figure must quote the floor its own run computed, not a floor
from this document.

The `--ab` harness's fixed 2% GPU constant is too low to protect Glowmere in a session whose
baseline spread reaches it. **Found, not fixed** — the floor logic is `render_stats.cpp`, which this
phase does not own, and the finding is recorded here for whoever does.
