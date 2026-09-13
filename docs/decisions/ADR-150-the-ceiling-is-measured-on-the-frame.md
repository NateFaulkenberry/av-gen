# ADR-150: The ceiling of a representation system is measured on the frame, not on the curve

**Status:** Accepted
**Date:** 2026-09-13
**Answers:** the question C5/C6/C7 were told to settle first — is a proxy system justified on this content
**Builds on:** ADR-122 (importance), ADR-124 (the band), ADR-126 (Glowmere's share), ADR-131 (what the curve cannot resolve)

## The problem with the measurement that was asked for

ADR-124 flagged its own weakest point and named the fix: a four-arm sweep of the synthetic plane
between 500 and 512,000 px/triangle. ADR-131 ran it. Every interval overlaps every other, spreads
reach 120%, and the conclusion is that **the cheap half of the fragment-cost curve is not measurable
by that instrument** — while the expensive half, below ~8 px/triangle, reproduces to the microsecond.

So the synthetic sweep cannot be asked for more. And the thing it was going to be asked for was
whether the remaining excess in Glowmere is worth a proxy system, which is a question about *this
content*, not about a plane.

## What replaces it

Two instruments, both committed, neither of which touches the unresolvable region.

### 1. Where the excess lives, by the band that would recover it

`tests/unit/test_representation_band_analysis.cpp`, `[.analysis][bands]`.

ADR-126 bucketed Glowmere's estimated excess by pixels per **triangle**. That says how much excess
exists. It does not say whether the proposed machinery can reach it, because ADR-123 decided that
*kind* of representation is selected by projected **radius** and only the *rung* by px/triangle — and
the two measures are independent. A fern eight metres away and the same fern eighty metres away can
share a px/triangle bucket and belong to different representation bands.

So this walk cross-tabulates the excess against `RepresentationPolicy::forTier(Realtime)`'s own bands,
read from the policy rather than restated, and costs three hypotheticals and two ceilings against
§4.5's curve. It also prints each layer's ladder in triangles, which is how ADR-153 was found, and
each layer's transforms, which is how ADR-152 was found.

### 2. The ceiling, in milliseconds, on the real frame

`tests/rendering/test_representation_ceiling_perf.cpp`, `[.perf][representation]`.

The intervention is `LodSettings::minScreenRadius`, which is shipped, general, policy-exposed and in
exactly the right units — `cull.wgsl` culls an instance whose `radius / distance × projScale` falls
below it, and that is the same projected radius the representation bands are cut on. Raising it to 8
deletes C5's whole band; to 40, C5's and C6's together.

**Deleting is the point.** No representation system can beat deletion: a proxy still draws something,
and it still casts the shadows the deleted geometry stopped casting. A band whose *deletion* is
inside the noise floor cannot be worth a proxy, whatever the proxy is. That turns a question about a
mechanism nobody has built into a measurement of geometry that is already there.

## Why this design and not the obvious alternatives

- **Re-run the synthetic sweep with more arms.** Rejected by ADR-131, which is that experiment.
- **Prototype an impostor system and measure it.** This is what the plan's Deliverable 9 asks for
  ("needs a prototype before commitment"). Rejected as the *first* step, not as a step: a prototype
  measures one implementation, and if it comes back small you cannot tell an unjustified phase from a
  bad prototype. The deletion ceiling bounds every implementation at once and costs no new code.
- **Extend the overdraw counting pass to the procedural path**, which ADR-126 names as the change
  that would replace its estimate with a counter. Still the right thing and still out of scope here;
  it measures the mechanism, where this measures the prize.
- **Trust ADR-126's 1.52× and proceed.** Rejected: 1.52× is a share of the triangle-size-sensitive
  component of the frame, expressed against a curve whose absolute milliseconds explicitly do not
  transfer. It cannot be converted into a budget, and a phase cannot be sized by it.

## Conditions every number from these carries

- 1280×800, the baseline's resolution (§3.1). The canonical Glowmere camera for the CPU walk; a
  fixed timeline second (t = 4.0 s) for the GPU probe, so every arm renders the same geometry.
- GPU arms interleaved inside one process session, three runs each, under `tools/gpu-lock.sh`.
- The CPU walk's excess is an **estimate against a shape**: §4.5's curve is normalised, its shader is
  simpler than Glowmere's, and its milliseconds do not transfer. It is never quoted as a time.
- The GPU probe's deltas are milliseconds and are quoted as such, with the 2% GPU floor stated in
  the output so a reader cannot miss which side of it a number is on.

## What the instruments had to be taught about themselves

Both were wrong first, in ways that produced confident output:

- The band walk omitted `sourceTransform` from the world-space radius, exactly as ADR-126's walk does
  — see ADR-152. Corrected, band membership moves a great deal and the aggregate barely moves.
- The ceiling probe raised `minScreenRadius` with `std::max` and never put it back, because
  `Composition::update` does not rewrite it. After the 200 px arm, two full runs reported the same
  99,820 triangles for every arm **including the baseline**: eight measurements of one arm under four
  labels, in a summary table that looked like a result. It now assigns from a snapshot taken once,
  and asserts the baseline's triangle count is identical across runs.
- The ceiling probe's first arm set `lod.cull = true` on every procedural, switching culling on for
  hand-placed hero geometry that never had it, and deleted the elder mushroom's cap from the 40 px
  capture. That was found by looking at the frame (§50), not at the timer, and its −20.9% was partly
  measuring the hero going missing.

All three are recorded rather than quietly fixed, because each of them produced a number a reader
would have believed.

## Verified vs assumed

**Verified:** both instruments are committed and re-runnable; every figure they print is reproduced
from the scene rather than transcribed. The band walk reproduces ADR-126's totals exactly (207,727
triangles, 3.29× coverage, 1.52× / 2.02×) before its own correction, which is what establishes that
the two walks are measuring the same thing.

**Assumed:** that deletion bounds a proxy from above in *time* as well as in work. A proxy that
replaced a stand of ferns with one merged mesh could in principle beat deletion by removing draw
calls too — but not here, where the scatter path already issues one indirect draw per layer per rung
whatever the instance count, so there are no draws for a merge to remove (ADR-154).
