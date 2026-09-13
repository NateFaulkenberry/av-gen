# ADR-151: Impostors and HLOD proxies are not justified on this content, and the reason is measured

**Status:** Accepted
**Date:** 2026-09-13
**Decides:** C5 (impostors, 2–8 px), C6 (HLOD proxies, 8–40 px), C7 (HLOD invalidation)
**Instruments:** ADR-150
**Disposition:** tracked, not removed (`05-scope-contract.md`)

## The verdict first

**C5 is refused outright: its entire band, deleted, costs nothing measurable.**
**C6's band is worth real time, but not to a proxy: most of what is there is already reachable by
code that is written, calibrated and unit-tested, and the remainder does not pay for the machinery.**
**C7 therefore has nothing to invalidate.**

## What was measured

### The band, in milliseconds, on the real frame

`[.perf][representation]`, 1280×800, fixed t = 4.0 s, three interleaved runs in one process session
under `tools/gpu-lock.sh`. Every arm reproduced its triangle count exactly across all three runs.

| arm | frame ms | scene pass | vs baseline | triangles |
|---|---|---|---|---|
| baseline | 13.566 | 10.879 | — | 264,305 |
| everything below **8 px** radius deleted | 13.763 | 11.076 | **+1.8%** | 260,866 |
| everything below **40 px** radius deleted | 12.190 | 8.978 | **−17.5%** | 137,543 |
| everything below 200 px radius (scale check) | 12.386 | 8.061 | −25.9% | 99,820 |

The arms **delete** the geometry. No proxy can beat that: a proxy still draws a silhouette, and it
still casts the shadows the deleted geometry stopped casting. Each row is therefore a ceiling on
every possible implementation of the band above it.

### C5 is refused by its own ceiling

**Deleting C5's whole band — every instance below 8 px of projected radius — moves the scene pass by
+1.8%.** Inside the 2% floor, and the wrong sign. It removes 3,439 triangles and 0.4% of the frame's
coverage, and the captured frame is indistinguishable from the baseline at full size.

There is no way to read that as "build an impostor system for this band". An impostor is a *cheaper*
way to draw geometry whose *deletion* is free. The correct conclusion is not that impostors are a bad
technique — Epic's own case for them is sound — but that Glowmere's 2–8 px band contains 0.4% of the
frame's covered pixels, and 0.4% of anything is not a phase.

This is the same shape as ADR-126's central finding, reached independently and in milliseconds: 45%
of Glowmere's triangles are sub-pixel and they cover 2% of the frame. **Coverage is what costs.**

### C6's band is real, and the prize is smaller than the band

Deleting the 8–40 px band as well is worth **−17.5% of the scene pass (1.90 ms)** and −10.1% of the
frame (1.38 ms). That is comfortably above the floor and it is the only real number in this ADR.

But three things have to come off it before it is a case for a proxy system.

**First, the visual.** The 40 px capture, looked at at full size, has lost the entire mid-ground and
hillside ecology — every fern, bush, grass clump and distant tree between the foreground and the
ridge. It is a different shot. Everything a proxy would have to *put back* is the thing that was
costing the 1.90 ms.

**Second, what an ideal proxy actually recovers.** `[.analysis][bands]` costs, against §4.5's curve,
the same frame with those two bands drawn at the cheapest triangle size measured and the same
coverage — which no real proxy can beat, because it still covers those pixels. Whole frame:

| | share of the triangle-size-weighted cost |
|---|---|
| the C5+C6 bands **deleted** | −16.25% |
| the C5+C6 bands drawn by an **ideal, free proxy** | **−9.45%** |

An ideal proxy is 58% of deletion. Applied to the measured deletion, **a perfect, zero-cost proxy
system is worth about −10% of the scene pass, ≈1.1 ms, and ≈0.8 ms of the frame.**

**Third, and decisively — most of that is already available.** ADR-125 records that
`RepresentationSelector` is built, calibrated, unit-tested and *deliberately wired to nothing*. Run
on this frame's real ladders, choosing rungs by ADR-124's measured px/triangle rule instead of by the
shipped radius thresholds, it takes the frame from 207,727 triangles to 100,786 — **a 51% reduction
with no new machinery at all** — for −4.72% of the weighted cost, ≈0.55 ms of the scene pass.

So the marginal value of building impostors, an HLOD proxy system, a bake pipeline and an
invalidation protocol, over *connecting a component that already exists*, is:

**≈0.56 ms of an 10.88 ms scene pass. ≈0.40 ms of a 13.57 ms frame. About 3%.**

And that is the margin over a *perfect* proxy: free to build, free to draw, never stale.

## Decision

**C5, C6 and C7 are not scheduled.** Not "rejected as techniques" and not removed from the spec —
they move to `05-scope-contract.md`'s *tracked, not removed* table with the number above as the
reason, and with the measurement that would reverse it named below.

§56 is the governing rule and it is not close: a High-severity stale-derived-copy risk (register row
3), a new asset bake pipeline (row 11), and a proxy generation and invalidation protocol, for an
unbeatable ceiling of four tenths of a millisecond over work already done.

## What is justified instead, and is not this task's to do

The measurements point somewhere, and it is not at new machinery:

1. **Wire `RepresentationSelector`, or recalibrate the GPU ladder's thresholds from ADR-124's target.**
   −4.72% of the weighted cost and half the frame's triangles, from code that is already written. The
   open question is entirely quality, not cost: the selector picks *much* coarser rungs than the
   shipped 28 / 11 / 4 px thresholds do, and whether those rungs look acceptable is not measured by
   anything here. That is the next experiment, and it is a §50 experiment, not a §4.5 one.
2. **ADR-152**, the cull ladder's missing source transform. A correctness defect that mis-sizes every
   scatter layer, by up to 8×, in both directions.
3. **Phase B.** ADR-126 already concluded that per-pixel work is at least as valuable as
   representation for this scene. Nothing here contradicts it and the 1.8% null strengthens it.

## Rejected alternatives, and the evidence for each

- **Build a prototype impostor system and measure it**, as Deliverable 9 asks. Rejected *as the first
  step*: the deletion ceiling bounds every implementation at once and cost no new code, and it came
  back at +1.8% for C5. A prototype returning "small" cannot distinguish an unjustified phase from a
  bad prototype.
- **Reach the impostor rung the engine already has.** Measured and it is a *regression* — ADR-153.
- **Merge scatter instances into HLOD proxies.** Structurally redundant here — ADR-154.
- **Accept ADR-126's 1.52× and proceed**, which is what the wave plan expected. Rejected: 1.52× is a
  multiplier on the triangle-size-sensitive component against a normalised curve, and it cannot be
  turned into a budget. Turned into milliseconds by deleting the geometry, the same content says the
  reachable part is under a millisecond.
- **Cut the bands differently** so C5 covers more. The bands come from
  `RepresentationPolicy::forTier` and are read from it rather than restated; moving them to make a
  phase look justified is the §49 failure mode this whole exercise exists to avoid. The 8 px arm is
  reported at the shipped threshold and the 40 px arm brackets it from above.

## Revisit when

- **A scene exists whose 2–40 px band carries a large share of coverage** — a dense forest seen from
  above, an open vista, a crowd. §45 already schedules Dense Forest and Open Vista as stress scenes,
  and they are the honest test of this decision: this verdict is about Glowmere's content and says so.
  Re-run `[.analysis][bands]` and `[.perf][representation]` against them; both are committed and
  take one afternoon.
- **The already-built selector is wired and the remaining gap is measured against the new baseline.**
  If wiring it recovers less than the 0.55 ms projected here, the projection was wrong and the
  ceiling arithmetic above should be redone rather than trusted.
- **The overdraw counting pass covers procedural instances**, replacing the CPU estimate that the
  proxy/deletion ratio rests on with a counter.

## Verified vs assumed

**Verified:** every row of the timing table — three interleaved runs, one session, GPU lock held,
baseline triangle count identical across all three (asserted, not eyeballed). Every row of the band
table, from a committed walk that reproduces ADR-126's totals. The visual state of all four captures,
looked at at full size.

**Assumed, and each could move the conclusion:**
- **That the ideal-proxy / deletion ratio (58%) transfers from the CPU cost model to the measured
  milliseconds.** Both interventions' savings are assumed to scale the same way with that model. This
  is the load-bearing extrapolation in the ADR and it is the one to attack first.
- **That an ideal proxy costs nothing to draw beyond its coverage.** It does not; every real one is
  worse than the 1.1 ms figure, so the assumption is conservative in the decision's favour.
- **That the frame timer's smaller delta (−10.1%) than the scene pass's (−17.5%) is other passes
  taking the time back rather than measurement error.** Not investigated. It does not change the
  verdict — both are far above what the marginal case needs — but it is unexplained.
