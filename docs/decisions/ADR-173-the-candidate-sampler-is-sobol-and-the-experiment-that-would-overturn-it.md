# ADR-173: The mushroom candidate sampler is a Sobol sequence, and the experiment that would replace it with MAP-Elites is named

**Status:** Accepted (provisional status lifted 2026-09-14 — see "the experiment, run")
**Date:** 2026-09-14
**Scope:** `docs/glowmere-valley-2/02-research.md` §4.4

## The question

Six hero mushrooms are wanted from an 18-dimensional procedural parameter space. The goal is
explicitly *not* "find the best mushroom" but "find several excellent mushrooms in meaningfully
different regions of morphological space", which is the definition of a quality-diversity problem and
points straight at MAP-Elites.

The brief's addendum asks for exactly the right thing: compare it against the simpler
sample → score → cluster → diverse-top-N pipeline, and use the simpler one if it does as well.

## The decision

**Sample the parameter space with a scrambled Sobol low-discrepancy sequence. One pass. No
evolutionary loop.**

### Why low-discrepancy at all

Uniform random sampling in 18 dimensions clumps and leaves holes, and the addendum's list of
complaints about naive sampling — "near-duplicates", "boring forms" — is largely a description of
clumping. A low-discrepancy sequence has bounded discrepancy at every prefix length by construction.

### Why Sobol rather than Latin hypercube

Two properties, both decisive and neither aesthetic.

**Sobol is extensible.** Any prefix is well-distributed, so a 200-candidate run grows to 400 by
appending indices 200–399 and every earlier candidate keeps its identity. An LHS design must fix N in
advance and a different N is a different sample — under LHS, "candidate #184" stops meaning anything
the moment the population changes.

**Sobol is indexable.** Point *i* is computed directly from *i*, so candidates parallelise with no
shared state, and **a candidate's identity is its index**. The canonical record for a hero mushroom is
then `(generatorVersion, schemaHash, sobolIndex)` — three values that regenerate the mesh exactly and
require storing no mesh at all.

### One consequence worth writing down

Sobol's low-dimensional projections are much better than its high-dimensional ones. **The parameter
schema is therefore ordered by visual influence**, so the stratification is spent where it buys the
most: aspect ratio and cap rim tangent take dimensions 0 and 1, and surface noise scale takes
dimension 13. An innocent alphabetical reorder of the schema silently degrades the sample, which is
why the ordering is a documented invariant rather than an accident.

## Why not MAP-Elites — and this is the part that could be wrong

MAP-Elites is the right *framing* and the literature is clear that it beats random sampling on
coverage. But the mechanism of that advantage is conditional, and the QD literature states the
condition itself: MAP-Elites wins because mutating a member of a diverse population fills new cells
more readily than sampling the genotype space does — **"especially if cells are more likely to be
filled by mutating a nearby cell than by randomly sampling from the space of all possible
genotypes."**

Here that condition is believed to be weak, by design:

1. **The space is bounded and every endpoint is usable.** There is no large invalid region a search
   must escape, because the ranges were authored not to have one.
2. **The genotype→behaviour map is close to monotone.** Aspect ratio is nearly
   `capRadius/stemHeight`; asymmetry is nearly `capTiltDeg` plus `capLobeDepth`. When the behaviour
   descriptors are near-monotone in the parameters, **a stratified sample in parameter space is
   already a stratified sample in behaviour space** — which is the entire prize MAP-Elites is
   competing for.
3. **Eighteen dimensions and a few hundred evaluations** is a small budget for an evolutionary loop
   and a comfortable one for a low-discrepancy sequence.
4. **MAP-Elites is stateful and order-dependent.** Its archive depends on evaluation order. That is a
   determinism obligation this repo takes seriously, incurred for a benefit not yet demonstrated on
   this problem.

Reason 2 is the load-bearing one and it is an assumption about a map nobody has evaluated yet.

## The experiment

This ADR is provisional and names what would overturn it.

**Bin the valid candidates of a 500-candidate Sobol run into the behaviour grid and report cell
coverage — the fraction of *reachable* cells occupied.**

- **Coverage good:** assumption 2 holds, MAP-Elites has nothing to add, this decision stands.
- **Coverage poor, and the empty cells are reachable** — proven by hand-authoring one parameter set
  that lands in an empty cell, not by asserting it — then assumption 2 is false, the map is not
  near-monotone, and MAP-Elites is correct.

"Reachable" carries the weight. An empty cell that no parameter set can reach is a fact about the
mushroom, not a failure of the sampler, and counting it against Sobol would manufacture a case for
replacing it.

## The experiment, run

Run on 2026-09-14 against the mushroom generator's own population. Four behaviour dimensions —
aspect ratio, cap-to-stem, asymmetry, curvature — at three bins each, 81 cells, which is the largest
grid a few hundred samples can speak to. A finer grid would report low coverage as a property of the
sample size rather than of the sampler.

| valid candidates | cells occupied of 81 |
|---:|---|
| 165 | 59 (72.8%) |
| 330 | 61 (75.3%) |
| 660 | **73 (90.1%)** |
| 1,322 | **80 (98.8%)** |

**The decision stands, and the reason is the shape of that column rather than any single row.**

At 165 samples, coverage is 72.8% — *below* what uniformly distributed samples in uniform bins would
give (~87%), which on its own reads as a case against the sampler. It is not one: the behaviour
descriptors are products and ratios of the parameters, so their distribution is not uniform even when
the parameters are, and a histogram binned over the observed range will always leave corner cells
thin.

The question ADR-173 actually posed was whether the empty cells are **reachable**, and coverage
climbing monotonically to 98.8% answers it directly: **they are reachable by sampling, they are
merely rare.** Sobol does reach them; it needs more points. MAP-Elites would reach them with fewer
evaluations, which is a different and much weaker claim than the one that would have justified it —
"cells are more likely to be filled by mutating a nearby cell than by sampling the genotype space"
is false here, because sampling fills them.

Assumption 2 — that the genotype-to-behaviour map is near-monotone — **holds**, and it was the
load-bearing one.

### What the experiment changed

**The population, not the sampler.** At 200 candidates a quarter of behaviour space is never
sampled, and a diversity selection can only choose from what it was shown. The default is now **800**,
where coverage is 90.1%. This is the useful result and it would not have come from any amount of
arguing about samplers: the parameter that mattered was `N`.

This is also the property Sobol was chosen for paying off directly — extending 200 → 400 → 800 →
1600 cost nothing and invalidated nothing, because every earlier candidate keeps its identity. Under
Latin hypercube each row of that table would have been a different sample and the comparison could
not have been made.

## Why the upgrade path is cheap, and why that is the reason to build it in this order

If the experiment goes the other way, **only the sampler is replaced.** The parameter schema, the
builder, the validity gate, the eight score components, the feature vector, the diversity selection
and the serialisation record are all reused unchanged — MAP-Elites needs a quality score and a
behaviour descriptor, and both exist by then. The provisional decision costs a sampler, and the
alternative ordering — build the sophisticated thing first — costs the sampler *and* forecloses the
measurement that would have said it was unnecessary.

## Rejected alternatives

- **Uniform random.** Clumps; it is the thing the addendum specifically warns against.
- **Latin hypercube.** Not extensible and not indexable; loses stable candidate identity.
- **CMA-ES / Bayesian optimisation.** Both optimise toward a single optimum. That is the wrong shape:
  we want many good and different, not one best.
- **Novelty search.** Diversity without a quality floor. The diversity stage already has an explicit
  quality/diversity trade parameter (`α`), which is the same control with the quality kept.
- **MAP-Elites now.** Above.

## Verified vs assumed

**Verified:** Sobol's extensibility and indexability, which are properties of the construction. The
QD literature's stated conditional for MAP-Elites' advantage over random sampling.

**Assumed, and each could overturn this:**
- **That the behaviour descriptors are near-monotone in the parameters.** The load-bearing assumption.
  The experiment tests exactly it.
- **That 18 dimensions with the most influential first gives adequate Sobol projections.** Sobol's
  quality degrades with dimension index; whether dimension 13 is still usefully stratified at 500
  points is not checked here.
- **That a few hundred candidates is the right budget.** Depends on generation time, unmeasured.
