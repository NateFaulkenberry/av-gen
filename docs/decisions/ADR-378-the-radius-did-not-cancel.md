# ADR-378: The radius did not cancel

- Status: Accepted (2026-09-19)
- Fixes the red `tests/rendering/test_wind_gpu.cpp` assertion. Extends ADR-055.

## Problem

"A stalk in a steady wind bends by exactly what the CPU model predicts" failed on main: measured
0.3057 m against a predicted 0.3333, 8.3% low where 6% is allowed.

## Three wrong answers before the right one

**Mine, reported last wave: ADR-372's AgX curve.** The threshold is an absolute `p[0]+p[1]+p[2] >
30`, which sits in the toe where the AgX correction is worth 9 code levels, so it was a good
hypothesis. The control kills it: the pre-ADR-372 `tonemap.wgsl` swapped in gives **0.305732489 in
both arms, byte for byte**. I should have run that control before naming another agent's change, and
the coordinator ran it instead of passing my attribution along.

**The per-instance random.** `windBend` uses `rnd.z` for amplitude and `rnd.x` as a flutter phase,
while the CPU prediction hand-picks `(0, 0, 0.5, 0)`. Forcing the shader to use the CPU's vector
gives the same value byte for byte — `amplitudeVariance` is 0 in this fixture, so it cannot matter.

**A CPU/shader flutter-phase divergence at t = 0**, the suspicion handed to me with the task. Also
no: the test renders at `renderTime = 0`, so the shader's `tFlutter` is 0 and matches the model, and
the byte-identity above rules out the phase offset independently.

## What it actually is

`rightmostLit` scans the **whole frame**. For the upright stalk the rightmost lit pixel is the
barrel's full radius, found at mid-height. For the bent stalk it is the **tip**, and a capped
stalk's apex is narrower than its barrel. Differencing the two therefore subtracts
(barrel radius − apex half-width) as well as measuring the travel.

The test's own comment says "the stalk's radius is constant, so the furthest-right pixel *is* the
tip, and the difference between the two images is the tip's travel with the radius cancelled out".
The first clause is true of the barrel and false of the cap, and the radius does not cancel because
the two measurements come from different parts of the silhouette.

That is a **systematic** shortfall of about 2 px, always downward, and it is why the failure looked
like a scale factor rather than noise.

### Why it was invisible until the ceiling started biting

The error is absolute — two thresholded edges, a pixel each way — and the tolerance was
**relative**. Run with the soft ceiling made inert the same scene measures 30 px against 31.4, and
1.4 px of error is 4.4% and passes. Run with the ceiling active the displacement is only 24 px, the
same error is 8.3%, and it fails. Nothing about the shader changed; the quantity the percentage was
taken of got smaller.

Two notes in passing. The fixture comments `bendLimit = 1.0f; // no ceiling`, and that is wrong —
at this amplitude the ceiling still scales the result by 1/1.2, from a raw 0.4 m to 0.3333. And
`predicted.x` being exactly 1/3 is that clamp, not a coincidence.

## Decision

Measure both images at the **same part of the silhouette**: `rightmostLitAtTop` takes the rightmost
lit column within six rows of *each image's own* topmost lit row, so the apex is compared with the
apex and the radius genuinely cancels. Each image finding its own top also absorbs the 2.18 px
length-preserving drop, which `predicted.y` independently confirms.

The tolerance becomes **2.5 px** rather than ±6%, because the error is in pixels and does not scale
with the displacement.

| | measured − predicted |
|---|---|
| unperturbed | **−1.17 px** (was −2.17) |
| shader amplitude ×1.08 | +0.83 px, passes |
| shader amplitude ×1.15 | **+2.83 px, fails** |
| shader amplitude ×0.92 | **−3.17 px, fails** |

**The old form was blind in one direction.** It passed a shader perturbed by +15%, because the 2 px
bias cancelled the error. A guard that only catches divergence one way is worse than a loose one,
and that — not the number — is what was wrong with it.

This is not the tolerance being widened to make a test pass. The bias is explained and removed, the
residual is quantisation, and the new guard trips at ±15% in both directions where the old one
tripped at −15% only.

## Consequences

- `rightmostLit` remains for the tests that want a whole-frame extreme; only the CPU-agreement test
  changed.
- Any test differencing two silhouettes should ask whether both extremes come from the same piece
  of geometry. This one had been asserting for months that they did.
