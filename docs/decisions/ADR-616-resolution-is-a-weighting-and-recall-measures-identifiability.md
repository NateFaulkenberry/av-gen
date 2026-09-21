# ADR-616: Resolution is a property of the weighting — and the recall instrument measures identifiability, which is why the mechanism still cannot be tested

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-614 (§20 at scale, and its retraction), ADR-609 (recall is not a quality measure),
ADR-611, ADR-558 (a control that does nothing), Phase C §16, §20, §22, §23
**Implemented by:** `deriveDuplicateRadius` (space fix), `avgen_motion quality` (`--root-vel` and
friends, derived plan perturbation, seed-return column)
**Corpus:** `assets/100STYLE-ATTRIBUTION.md`. All figures 33-dimension, FW subset (434,478 samples),
Glowmere control (1,738), 200–250 seeds as stated per row.

---

## Three corrections before any result

**1. The duplicate radius was compared against a distance in a different space.**
`meanNearestNeighbour` is a **weighted** L2 distance. `deriveDuplicateRadius` displaced the query by
`radius/sqrt(D)` per dimension and returned the **unweighted** length of that displacement — and the
duplicate count tests `weightedDistance < radius`. A comment above the nearest-neighbour loop
asserted the two already shared a space. They did not. It is the weighted/unweighted mismatch this
phase has now paid for in five places, and it was hiding behind a comment claiming the opposite.

Fixed by one factor, `sqrt(mean(w))`, which is 0.863 at the default weights. **Glowmere: radius
1.3945 → 1.2040, duplicates 61.68% → 57.71%.**

**The corpus-to-corpus comparisons taken before this fix survive it**, because at fixed weights the
factor is constant across corpora — the artefact was constant across the thing being varied. **A
comparison across *weightings* would not have survived it**, and that is exactly the experiment
below, so the fix had to come first.

**2. The weight sweep's first run was a control that did nothing.** `--root-vel` was assigned to
`options.config` *after* `buildMotionDatabase` had already copied it. A 64× sweep produced four
byte-identical arms while faithfully printing the weight it had been handed. Caught only because
the arms were printed side by side — ADR-558's shape, reproduced by the person who had spent the
day cataloguing it.

**3. The plan sweep's perturbation is now derived**, for the reason §30's contact experiment found:
a fixed nudge stops making a query ambiguous once the corpus is large enough. It is now the
duplicate radius spread over the vector, and **the seed-return rate is printed beside every recall
number** so a degenerate run cannot be read as a good one.

## Resolution is a property of the weighting, and root velocity moves it a long way

FW, 200 seeds, `rootVelocityWeight` swept. §22 named this dial: root velocity is 3 of 33 dimensions
and swamped.

| `--root-vel` | duplicates | radius | mean NN | radius ÷ mean NN |
|---|---|---|---|---|
| 1 (default) | 85.95% | 1.3398 | 0.9099 | **1.47** |
| 4 | 80.16% | 1.3235 | 1.0081 | 1.31 |
| 16 | **58.61%** | 1.1642 | 1.2015 | **0.97** |
| 64 | 59.90% | 1.4736 | 1.5229 | 0.97 |

**One dial takes the corpus from far under the matcher's resolution limit to just under it** —
duplicates 86% → 59%, comparable to Glowmere's 57.71%. ADR-614 said testing the mechanism needs a
different weighting rather than more data. That part is confirmed: the weighting is where
resolution lives.

## And the mechanism is *still* untested, for a reason that is now diagnosed

| `--root-vel` | radius ÷ mean NN | **seed-returns** | stride-4 recall | stride-8 full-prefix |
|---|---|---|---|---|
| 1 | 1.47 | 100.0% | 100.0% | 71.6% |
| 16 | 0.97 | 96.5% | 100.0% | 64.7% |
| 64 | 0.97 | **19.9%** | **83.6%** | 54.2% |

**Read the third column first.** Where the exhaustive search returns the query's own seed sample on
essentially every trial, "recall" asks only whether the staged plan can find the one obvious
answer. That is a real question about a plan and it is **not** a question about match quality.

Between `root-vel` 16 and 64 the **resolution does not change** — the ratio is 0.97 in both — and
recall falls from 100% to 83.6%. What changed is the seed-return rate, 96.5% → 19.9%. **So the
thing that moved recall is a property of the probe, not of the corpus.**

> **The recall instrument's discriminating power and the corpus's resolvability are driven by the
> same perturbation, so this design cannot vary one while holding the other fixed.** The mechanism —
> *a corpus the matcher can resolve should break the stride plan* — remains untested, and this is
> the first account of **why** it is hard to test rather than a report that it was not tested.

Testing it needs an instrument whose queries do not come from the database at all: a held-out
trajectory, or synthesised intent. That is the next experiment, and it is a different instrument
rather than a different corpus or a different weighting.

## What is robust across everything measured

**The cheap prefix does not survive scale.** It is the one result that holds in every arm, at every
weighting, under both perturbations, and in both directions of the metric:

| | Glowmere | FW (root-vel 1) | FW (16) | FW (64) |
|---|---|---|---|---|
| stride 8, **prefix 12**, top 32 | 94.5% | 54.7% | — | — |
| stride 8, **FULL** prefix, top 32 | 94.5% | 71.6% | 64.7% | 54.2% |

On Glowmere the 12-dimension prefix stage scored **exactly what reading every dimension scored**,
which is how "the cheap prefix bought nothing" came to be stated with confidence. At scale it costs
**17 recall points**. A conclusion that reversed between corpora is the strongest argument §20 has
produced.

**And "stride-4 survives" needs its condition attached.** Glowmere and FW were both measured at
~100% seed-return, so the comparison is like-for-like and the transfer claim holds *in the regime
the original §16 result was also taken in*. It does **not** license a claim about realistic queries:
at `root-vel` 64, where the probe stops being trivially identifiable, stride-4 loses 16 points.

## Consequences

- **Every recall figure in this phase now carries a seed-return rate**, and one without it should be
  read as unverified rather than as a result.
- **The §16 stride-4 default is unchanged.** Nothing here shows it is wrong; what is shown is that
  the evidence for it was taken on in-database queries, which is a narrower claim than it has been
  used for.
- **Three instrument defects were found by running the instrument against itself** — a space
  mismatch, a dead control and an identifiability confound — and none of them was found by a test.
  Every one was found by printing arms side by side and reading the columns that were supposed to
  differ.
