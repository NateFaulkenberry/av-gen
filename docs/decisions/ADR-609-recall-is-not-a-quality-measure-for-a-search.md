# ADR-609: Recall is not a quality measure for a search, and the two-stage win came from the stride rather than the prefix

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-559 (a monotone metric is a direction, not a quality measure), ADR-607 (synthetic
data may time a scan; only real data may judge a first stage), ADR-603 (record the forecast),
Phase C §16, §17
**Implemented by:** `src/scene/motion_database.{hpp,cpp}` (`MotionSearchPlan`, `searchMotionStaged`)
**Tests:** `tests/unit/test_motion_database_scale.cpp`

---

## Context — and the disclaimer, which belongs before the justification

**On this repository's actual content a linear scan is comfortably correct.** The real Glowmere
database is 1,738 samples; a full search costs 8.88 µs at best, 9.25 µs on average, 17.46 µs at
worst — **955 characters per frame at 60 Hz on the worst case.** Nothing in this tree needs a
two-stage search.

What needs it is the scale §6 and §17 ask about: at a million samples one query is **36.5 ms**, more
than two whole frames, for one character, and the scan is honestly linear (101x for 100x the
samples). §16 is therefore built to specification for a database two orders of magnitude larger than
any that exists here.

**A future reader finding a two-stage search in this tree should not conclude the linear scan was
inadequate. It was not.** Both numbers are stated together, in the ADR and in the header, because an
optimisation defended by a benchmark that never needed it becomes permanent without having been
justified.

## Decision

**Judge a first stage by the severity of its mistakes, not by how often it makes them.**

> **The stride-8 plan's worst miss is 1.15× the gap between a good match and a typical one. In its
> failure mode it is not a degraded search — it is worse than not searching at all. And it earns
> 94.4% recall while doing it.**

That is the sharpest statement of this family the phase produced, and it is only visible once
severity is measured against a scale that is a property of the data.

Four plans, swept on **real** Glowmere motion (ADR-607: a scan's cost is distribution-independent,
a first stage's recall is not), 249 queries drawn from inside the distribution and perturbed so the
answer is not trivially the seed sample:

| plan | recall | worst excess cost | **× the typical gap** | samples fully scored |
|---|---|---|---|---|
| stride 8, prefix 12, top 32 | 94.4% | 60.60 | **1.15×** | 2320 |
| stride 8, prefix 12, top 128 | 99.6% | 48.93 | **0.93×** | 1873 |
| stride 8, full prefix, top 32 | 96.8% | 59.49 | **1.13×** | 2278 |
| **stride 4, full prefix, top 32** | **99.6%** | **2.19** | **0.04×** | 84 |

(Reported as a multiple rather than a percentage: a worst case is not bounded by a typical case, so
a value above one is legitimate, and a multiple says so without needing a footnote. These figures
are against the **weighted** spread of 52.57; they were first published against an unweighted 69.72
and every one of them was too low.)

On this database a typical candidate scores **52.57** worse than the best one, and that spread is
the denominator that makes an excess mean anything.

**94.4% recall hides that the 5.6% of misses are near-worst-case picks** — 86.9% of the whole gap
between a good match and a random one. A matcher wrong that way does not choose a slightly different
frame of comparable motion; it chooses different motion. And the 99.6%-recall plan at stride 8 is
*still* 70.2% when it is wrong: **raising recall did not make the failures benign.**

This is ADR-559's family arriving in a new place. Recall is monotone in effort and contains no term
for how bad a miss is, so optimising it alone recommends whatever agrees most often regardless of
what it does when it disagrees. The opposing quantity is the **severity** of a disagreement, and it
has to be measured against the database's own cost spread.

### Getting the denominator right took two attempts, and both wrong ones were instructive

- First: `staged.cost / full.cost`, which reported **2957x**. A query drawn from the database sits
  almost on top of a sample, so the best cost is near zero and any absolute difference over near
  zero is enormous. **A ratio metric whose denominator can approach zero measures the denominator.**
- Second: the absolute excess against the *mean best cost*, which reported 83.7x for the best plan.
  Same defect, one step removed — the mean best cost is also near zero.
- Third and correct: the excess against **the measured gap between a good match and a typical one**,
  which is the only scale on which "how bad is this miss" has an answer.

### The rule, stated generally, because §11 and §14 both have this trap in them

> **A ratio whose denominator can approach zero measures the denominator.** An error metric needs a
> scale that is a property of the data, not of how close this particular query happened to land.

Every quantity of the form "how much worse is A than B" needs an explicit answer to *worse relative
to what*, and the answer is almost never B itself. Here the scale is the measured gap between a good
match and a typical one; in §41's foot slide it was the character's rest height; in §45's limb
deviation it was the rest bone length. **Pick the scale before taking the measurement**, because
once a number exists there is a strong pull to divide it by whatever is nearest to hand.

### The same discipline at the other end of a measurement

The rule above is about the scale you **divide by**. Its twin is about the cut-off you **compare
against**, and it cost three numbers in this phase before it was stated:

> **A threshold I chose is a result about me.**

§22's coverage gaps (8 bins), the §21/§22 "convergence" (26 empty cells at those bins), and §23's
duplicate rate (7.42% at a radius of 0.05) were each overturned by deriving the constant instead of
picking it. The derived versions: bin width from the speed change the matcher can act on; duplicate
radius from the feature distance at which the search returns a different sample. Both carry ADR-389's
property — they move when the weights move — and both changed the conclusion, not just the digits.
The 0.05 became **1.3945** and 7.42% became **57.02%**, which turned a deletion argument into its
opposite.

### And the cheap prefix did not work

The textbook two-stage design ranks coarsely on a cheap prefix of the feature vector. Here the
prefix bought nothing: at stride 8 the full-prefix plan (96.8%, 85.3%) was barely better than the
12-dimension one (94.4%, 86.9%). **The entire win came from the stride**, and the only safe plan is
stride 4 with the *full* prefix.

**The standard design was tried, measured, and lost**, and that is recorded here so nobody re-adds
the prefix stage later believing it was merely overlooked. The plan this data wants is the one the
textbook treats as the degenerate case. This is the second time in this programme that a canonical
design has lost to a simpler one on real data, which is the entire argument for §15's insistence — which fully scores 84 samples per query against 1,738 exhaustive,
a 20x reduction in work at 99.6% recall and a 3.1% worst case.

## Consequences

- The default `MotionSearchPlan` is exhaustive (`stride = 1`), and an exhaustive plan **delegates to
  `searchMotion`** rather than reimplementing it — so the plan is a tuning surface, not a second
  cost function to keep in step. That identity is asserted before any recall number is trusted.
- `neighbourhood` re-expands around each shortlisted sample, so striding trades work for a *chance*
  of missing rather than a guarantee: at stride 4 with neighbourhood 4, every sample is reachable.
- The recorded bound is **a tenth of the database's own cost spread**, so it scales with the content
  instead of being a number chosen to pass.
- `coarseConsidered` and `fullyScored` are reported separately from `considered`, so the saving is
  visible rather than inferred.
