# ADR-159: No tap may step further than the texel of the texture it reads

**Status:** Accepted
**Date:** 2026-09-14
**Corrects:** `fs_wide` in `shaders/post.wgsl`, which took eight taps a side whatever the reach

## What happened

With `post/anamorphic/enabled` on over Glowmere's sparkling river, the frame grew a lattice of dots
and dotted bands that followed the water but did not look like water. Three fixes were shipped and
reverted before this one: a luminance-weighted (Karis) bloom prefilter, raising the streak's tap
count from 8 to 48 a side, and selecting a coarser pyramid level for the wide tier plus a 3x3 tent
on the ghosts. All three were plausible; none was localised. One appeared to do nothing, one changed
the look, and none of them settled which stage was responsible.

They could not settle it, because all three were judged on the final frame. Bloom, the wide tier and
the grade all land on the same pixels, so a change anywhere upstream moves the final image and every
change therefore "does something". A post chain judged on its output cannot be debugged.

## What the measurement said

`PostProcessor::armCapture` keeps a handle on every intermediate the chain renders, and an impulse —
one texel at 40.0 in an otherwise black frame — is the honest test of a filter, because a filter's
response to an impulse is the filter:

```
  scene-hdr        peak 40.0000  lag  0 score 0.000   bloom/up4..up0  lag 2 score <= 0.44
  bloom/prefilter  peak  9.0000  lag  0 score 0.000   wide            lag 11 score 0.253
  bloom/down1..5                 lag  2 score <= 0.14
```

Every stage up to and including the finished pyramid carries no periodic structure. The wide tier
returns a row of copies, and a blur cannot do that: an autocorrelation peak away from zero means the
output contains shifted copies of its input.

The arithmetic then names the mechanism instead of suggesting it. `fs_wide` stepped its taps by
`post.texelSize.x * stretch`, so the period had to equal `stretch` in wide texels exactly and track
it linearly. It did, over a fivefold sweep: stretch 4, 6, 8, 10.386, 14, 20 gave lags 4, 6, 8, 10,
14, 20. A compact bright rectangle through the same chain acquired no comb at all, so the pass only
misbehaved where the input's energy sat in isolated texels — which is exactly what the water writes
to the emission target. And with the ghosts off entirely the comb was still there, which acquits
them: they add structure of their own on top, they were never the cause.

## The rule

**No tap may step further than the texel of the texture it reads** — and the margin is Nyquist's, so
"not further" means half a texel, not one. A filter sampled more coarsely than its source's detail
is not a blur. It is a comb, and it prints one copy of every isolated highlight per tap.

Two numbers follow from the rule, and neither may be a constant while the other is a parameter:

- **taps**: enough that the spacing is inside a texel of the level below, bounded by a budget
  (48 a side) so a long streak costs samples linearly rather than without bound;
- **source**: the finest pyramid level whose texel is at least twice the tap spacing, so whatever
  the budget leaves unresolved has already been filtered away rather than aliased.

The budget and the level trade against each other, and the thing they trade is *anisotropy*. A
coarser level softens the streak in both axes, and an anamorphic streak soft in both axes is a blob
— which is a different artifact and not a fix. So the budget is as high as it is to buy the
anisotropy back, and the tests measure elongation as well as the comb.

## Evidence

The streak's shape is unchanged by construction: the reach stays `8 * stretch` texels and the
gaussian's sigma stays `3.2 * stretch` texels, expressed in the new tap units as `0.4 * taps`, which
is exactly 3.2 at the old eight.

On the wide target, QA river with Glowmere's water and post: peak 0.3943 -> 0.0235, mean 0.001595 ->
0.001588, isolated peaks 420 -> 0. The mean is the one that matters as much as the count — the
streak carries the same total light to within half a percent, so this removes the teeth and not the
effect. Every bloom target upstream is bit-identical across the change.

On the user's own project at 1920x1080 with the authored settings, measuring the effect's own
contribution (the same frame with the effect on and off, subtracted) — `tools/post_artifact_stats.py`:

| | before | after |
|---|---|---|
| comb prominence at 42 px (= 4 x stretch) | +0.2218 | +0.0056 |
| prominence at 126 px (third harmonic) | +0.1711 | -0.0419 |
| contribution max | 142.7 | 52.7 |
| contribution mean | 1.7477 | 1.9395 |

The contribution's *mean rose 11 %*: the effect is not weaker, the peaks fell because the teeth were
the peaks. With `post/anamorphic/enabled` off, the before and after builds render the same project
to the same sequence hash, which is the evidence that nothing outside this stage moved.

Cost, same protocol both sides with the first run discarded as warm-up: the anamorphic tier goes
from 0.107 to 0.116 ms at 1080p and from 0.214 to 0.502 ms at 4K. Six times the taps for nine
microseconds at 1080p, because the coarser source is a far smaller texture than the half-resolution
one it replaced and the old 10-texel stride was cache-hostile.

Regression coverage is `tests/rendering/test_post_artifact_forensics_gpu.cpp`, which asserts the
comb's prominence at the lag the defect predicts (before: +0.52 to +0.94 across the sweep; after:
-0.09 to +0.012; threshold 0.02), the isolated-peak count in the wide target, and the elongation
that stops a comb being traded for a blob.
