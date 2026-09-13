# ADR-132: Spread is the transition mitigation, and temporal AA is not a prerequisite

**Status:** Accepted
**Date:** 2026-09-13
**Answers:** C2 — transition quality without temporal AA
**Amends:** `04-target-architecture.md` risk table row 2, and §28's promotion to prerequisite

## The question

The target architecture lists representation popping as a **High** risk, notes that Nanite's seamless
LOD is *contingent* on temporal AA, observes that this engine has only FXAA, and concludes that
"cross-fade or dithered transition is **required, not optional**". On that basis §28 (temporal
rendering) was promoted from deferred to a **prerequisite for the whole of Phase C** — the largest
scope commitment in the plan.

That conclusion came from the literature. Nobody had measured what the engine already does.

## What the engine already does

`cull.wgsl` offsets every instance's ladder thresholds by a per-instance hash (`lodSpread`,
default **0.12**, clamped to 0.5). A stand of ferns therefore does not cross a threshold together:
each instance crosses at a slightly different distance. That is stochastic LOD distributed over
space, which is the standard answer to popping when there is no temporal filter to hide it.

## The measurement

`tests/rendering/test_culling_gpu.cpp`, "LOD spread turns one simultaneous pop into a smear".
400 instances at effectively one distance — the worst case on purpose, since naturally spread
content staggers on its own — dollied through all three thresholds over 130 steps. Popping is
measured **at its source**: how many instances change level on a single frame.

| spread | worst frame | frames moving | total switches |
|---|---|---|---|
| 0.00 (off) | **400 (100%)** | 3 | 1200 |
| 0.12 (default) | 104 (26%) | 28 | 1200 |
| 0.25 | 54 (13.5%) | 55 | 1200 |
| 0.50 (clamp) | **30 (7.5%)** | 99 | 1200 |

The total is identical in every arm — 1200 = 400 instances x 3 thresholds. The same work happens;
only its distribution changes. That is what makes this a mitigation rather than a reduction, and it
is why the total column is printed: an arm that changed the total would be changing the ladder, not
smearing it.

## Decision

**Temporal AA is not a prerequisite for Phase C, and §28 returns to deferred.** Cross-fade and
dithered transition are not required. The existing knob, already shipped and costing nothing, takes
the worst-case simultaneous switch from 100% of a stand to **7.5%** — a 13.3x reduction — and the
instances still switching are by definition at a size where the two levels are near
indistinguishable.

**The default stays at 0.12.** Raising it is a quality trade, not a free win: a larger spread means
some instances take a coarser level nearer the camera than the author asked for. The right place for
that choice is `RepresentationPolicy::forTier` (ADR-124 already moves radius bands per tier), with
the measured curve above as the evidence. Recommended when Phase C selects representations for real:
preview can afford 0.5, realtime 0.25.

## What this does not establish

It measures **level changes, not perceived difference**. How visible one switch is depends on how
unlike each other two rungs look, which is a property of the meshes meshoptimizer produces, not of
the ladder. A 7.5% worst frame of *radically* different meshes could still read as a shimmer.
The instrument to close that is a perceptual comparison of rung pairs, and it is not built here.

So the claim is bounded: **the scheduling problem is solved and the plan's stated reason for
requiring TAA does not hold.** Whether any individual transition is visible is a separate question
about mesh similarity, and it is the one to ask if popping is ever reported.

## Verified vs assumed

**Verified:** every row above, on a real device, twice (the second run added the 0.25 and 0.5 arms
and reproduced 0.00 and 0.12 exactly). Both arms provably cross the ladder — the test fails if
either total is zero, because two null results also compare equal.

**Assumed:** that 30 of 400 instances switching on one frame is below perception on this content.
Not measured; stated so it can be challenged.
