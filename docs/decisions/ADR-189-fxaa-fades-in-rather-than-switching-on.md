# ADR-189: FXAA fades in rather than switching on

**Status:** Accepted
**Date:** 2026-09-14

## The report, and the assessment that came with it

*"With slow camera movement, I can see some edge crawling/shimmering. The slowly swaying grass also
produces some minor crawling along the blade edges. The overall animation remains smooth and the
grass is still crisp. Real but low-severity — I would not justify a large temporal-rendering change
based on this alone."*

That assessment is the right one and it set the brief: find something small, and do not soften the
image or break offline determinism to get it.

## Confirming the cause before fixing it

Measured at a grass close-up chosen to match the report — `7,-1.35,6 → 3,-1.55,0`, fov 32, where
thin blades fill the foreground — with `tools/flicker_bench.py`:

| arm | flickering area | peak second difference |
|---|---:|---:|
| baseline | 4.196% | 132.6 |
| **FXAA off** | **3.148% (−25%)** | **174.0** |
| particles off | 4.039% (−4%) | 132.6 |

So FXAA is a quarter of it — more than the 21% it measured at the river view, which is consistent
with the report that grass close-ups are where it shows. **Three-quarters of the flicker survives
without FXAA**, so it is a contributor and not the cause; the rest is the blades themselves aliasing
as they sway, which nothing short of more samples or a temporal filter will reach.

The peak moving the *other* way is the useful detail. FXAA **lowers** the worst single-pixel swing
(174.0 → 132.6) while **raising** the number of pixels that flicker at all. It is spreading
instability over more pixels at lower amplitude — and a wide, low-amplitude, moving disturbance is
exactly what reads as "crawl".

## The mechanism

`fs_fxaa` opened with a hard early-out:

```wgsl
if (range < max(kFxaaAbsolute, lumaMax * kFxaaRelative)) { return rgbM; }
```

A **binary** decision, per pixel, per frame. The filtered result differs from the unfiltered one by a
lot, so a pixel whose local contrast sits near that threshold flips between the two as a blade sways
past it, and a row of such pixels flipping is the crawl. This is also why the earlier strength sweep
found most of the cost arriving the moment the pass switches on at all: the discontinuity is in the
*decision*, not in the amount.

## The fix

Fade the filter in across a band above the threshold instead of switching it on at it. The pass's
output is a resample at `uv + offset`, so scaling the offset by a confidence is the blend, and at
confidence zero the sample is the unfiltered pixel exactly:

```wgsl
let confidence = smoothstep(edgeThreshold, edgeThreshold * (1.0 + kFxaaRamp), range);
let finalOffset = max(pixelOffset, subPixelOffset) * confidence;
```

Continuous in `range`, therefore continuous in time. **A pure function of the current frame** — no
history, no previous-frame decision — which is what rules out the obvious alternative, a temporal
hysteresis on the edge test. That would have made the image depend on how the camera arrived, which
§5.9 forbids an offline render.

## What it measures

Same view, same sequence, sweeping the band width:

| `kFxaaRamp` | flickering area | vs today | gradient energy | vs today |
|---|---:|---:|---:|---:|
| 0 (the hard cut-off) | 4.196% | — | 13.399 | — |
| **1.0 (shipped)** | **4.001%** | **−4.6%** | **13.614** | **+1.6%** |
| 2.0 | 3.823% | −8.9% | 13.888 | +3.7% |
| 4.0 | 3.518% | −16.2% | 14.366 | +7.2% |
| FXAA off | 3.148% | −25.0% | 15.326 | +14.4% |

**It is not a trade-off in the range tested: flicker falls and detail rises together.** That makes
sense once stated — the pixels near the threshold are weak edges, mostly shading detail rather than
geometric silhouettes, and full FXAA on them both blurs the detail and flips frame to frame. Fading
it out there recovers the detail *and* removes the flip.

Shipped at **1.0** rather than at the best-scoring 4.0, deliberately. Gradient energy cannot tell
recovered detail from returned jaggies — both raise it — so the metric stops being a guide exactly
where the risk begins. At a ramp of 4 a pixel needs five times the threshold for full filtering, and
plenty of genuine foliage silhouettes on a night landscape do not reach that. 1.0 is the value that
is defensible on both numbers without leaning on the one that cannot distinguish them; the constant
is one line, and the sweep above says what raising it buys.

## Verification

`kFxaaRamp = 0` reproduces the pre-change sequence hash **byte for byte** (`682771e07e72a10e`), so
the old behaviour is exactly recoverable and the change is provably inert when switched off.

The test asserts the two properties that make it safe rather than the number: FXAA still softens a
hard diagonal silhouette (a fade that faded everything out would score beautifully on flicker and be
a filter that does nothing), and a second renderer with no history at all produces an identical
frame.

## What is left

Three-quarters of the crawl at this view is not FXAA, and this does not touch it. That is the blades
aliasing under wind, and reaching it means more samples or a temporal term — the large change the
report explicitly declined to justify. Recorded, not scheduled.
