# ADR-612: The provider seam does not blend, which is why `proceduralMotion` is off in Glowmere — and it costs 0.36 m per transition

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-556 (a motion provider advances and draws in two calls), ADR-540 (clips are
authored in place), Phase B's Glowmere integration, Phase C §30 and §32
**Implemented by:** nothing — this records a measured product defect and its blocked consumer
**Tests:** `tests/unit/test_cross_clip_matching.cpp` — "§30 measured against its own purpose"

---

## Context

**If you are here because you are asking "why is `proceduralMotion` off on the Glowmere aliens",
this is the answer.**

The provider seam works. `ClipMotionProvider` and `MatchMotionProvider` both pose the rig, and the
Glowmere integration was built, rendered and shown to be byte-identical with the feature default-off.
`proceduralMotion` was nevertheless left **off on all five aliens**, for one stated reason: the
providers have parity with the shipping `AnimationPlayer` in steady state but **implement no
transitions and no inertialization**, and Glowmere's aliens change gait constantly. Switching them
on would have traded a blending player for a non-blending one — an architecture win that reads on
screen as a regression.

That caveat was recorded without a magnitude. It now has one.

## The defect in one sentence

> **When the matcher switches motion, a foot moves seven times further in that single step than it
> ever moves between two frames of the walk it is interrupting.**

That comparison needs no character height, no scene scale and no unit. It compares the defect to the
thing it interrupts, which is the only comparison a viewer actually makes. The metres and the
body-height fractions are below; this is the line to quote.

## The measurement, taken by accident

Phase C §30 built a transition loop to test whether contact features reduce foot-plant
discontinuity. They do not — but **both arms agreed on a number nobody had asked for**:

| | switches | mean foot jump at a transition | worst |
|---|---|---|---|
| contacts off | 32 | **0.3566 m** | 1.6437 m |
| contacts on | 30 | **0.3792 m** | 1.6437 m |

**Stated as fractions of the character, because metres mean nothing without the scene's scale** and
a ratio survives a change of units or of character:

| | metres | of body height (1.733 m) | × a normal frame step |
|---|---|---|---|
| mean transition jump | 0.3566 | **21%** | **7.0×** |
| worst transition jump | 1.6437 | **95%** | 32× |

**A foot teleports a fifth of a body height on average when the matcher switches motion, and once
very nearly a whole body height.** The worst case is **identical in both arms**, so it comes from a
specific reproducible pair of clips rather than a distributional tail.

The third column is the sharpest of the three: during ordinary walking this foot moves a median
**0.0510 m** between two frames. **A transition moves it seven times further than the motion itself
ever does.**

### The acceptance threshold, fixed now — before any fix exists

The before-figure is trustworthy because it was taken by accident. **The after-figure will be taken
by someone who wants it to be smaller**, so the threshold is set while nothing exists to flatter,
and derived from the content rather than picked:

> **A transition may not move a foot further than walking does: ≤ 0.0510 m.**

Derived from the same quantity the defect is stated against, which closes the loop — the criterion
is self-evidently the right bar rather than a number someone negotiated.

Below that it is indistinguishable from normal locomotion; above it, something happened that the
motion itself would not have done. **A fix that reduces 0.3566 m to 0.30 m is not a fix.** The
threshold is asserted in `test_cross_clip_matching.cpp` so it cannot be quietly relaxed when the fix
is measured against it.

**This is the shipping path, not the harness.** `MatchMotionProvider::advance` records
`transitionStart`, and it is read *only* for the search interval and the continuation lock. Nothing
in the provider blends between the outgoing and incoming samples. The test loop jumps because the
provider jumps.

## Decision

**Record it as a product defect with a named beneficiary, and treat Phase C §32 (root-motion
continuity) as the fix rather than as a routine section.**

- The defect is findable from the symptom, which is the point of this ADR existing separately from
  Phase C's design log: someone investigating Glowmere's disabled flag should not have to read a
  motion-matching phase log to find the cause.
- **0.3566 m mean / 1.6437 m worst, over ~30 real switches on the real corpus, is the before
  figure.** It is unusually well-conditioned: it was taken *before anyone intended to fix it*, as a
  by-product of a different experiment, so nothing was tuned in anticipation of it.
- The consumer is explicit. Fixing this is what allows `proceduralMotion` to be switched on for the
  five Glowmere aliens, which is the last item outstanding from the Phase B integration.

## Consequences

- §32 has a defect, a number, a named beneficiary and an honest before/after. That is the
  best-conditioned measurement available in this programme.
- The contact experiment that produced it found nothing about contacts and something important
  about transitions, which is the fourth time in Phase C that **printing the distribution before the
  ratio** has outperformed the ratio. The general form, and the cheapest of the pre-tests:
  **when the ratio is null, the shared baseline is where the result is.** A null difference means
  the arms agree, and agreeing on a bad number is still agreeing.
- Until it is fixed, any claim that the provider seam is production-ready is true only for steady
  state, and this ADR is the qualifier.
