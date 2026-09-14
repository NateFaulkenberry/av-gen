# ADR-172: An aesthetic score component is a band, never a maximum

**Status:** Accepted
**Date:** 2026-09-14
**Scope:** the procedural mushroom candidate search (`docs/glowmere-valley-2/02-research.md` §4)

## The problem this closes before it happens

Glowmere Valley 2's hero mushrooms are chosen by generating a few hundred procedural candidates,
scoring them, and curating six. The brief's addendum lists the failure modes it wants avoided, and
three of them are the same failure wearing different clothes:

- "do not blindly optimise toward *more edges* or *more contrast* — these are signals, not
  definitions of artistic quality";
- "do not automatically reward higher tessellation";
- reject candidates that are "too complex: no clear silhouette, excessive visual noise".

Each is a warning about a scorer being *maximised*. And a scorer will be maximised, because a
selection stage that ranks by score is by definition an optimiser over whatever the score measures.
Writing "do not maximise this" in a comment does not stop the ranking from doing it.

## The decision

**Every component of the candidate quality score is a trapezoidal band — `(lowEdge, lowPlateau,
highPlateau, highEdge)` — scoring 0 outside the edges, 1 on the plateau, and interpolating between.
No component is monotone increasing. The four numbers are configuration data, not code.**

Penalties are the stated exception and are correctly monotone: there is no such thing as too little
of an artifact.

## Why a band rather than a cap

A cap ("score edge density, but clamp at 0.7") has the same defect in a smaller region: everything
below the cap is still an instruction to increase, so the optimum sits *at* the cap and every
selected candidate piles up on one boundary. A band has an interior optimum, so candidates spread
around it, which is what a diversity stage downstream needs to have anything to work with.

It also makes the art direction *writable*. "Cap thickness between 8% and 20% of cap radius" is a
sentence an art director can say, disagree with, and change by editing two numbers. "Maximise
thickness, weight 0.3" is not a sentence about mushrooms at all.

## What it costs

**The bands have to be authored, and initially they will be wrong.** A monotone score needs no
calibration and this one needs eight sets of four numbers. That is the real price and it is accepted
because the alternative is a pipeline whose output is predictable in the bad way.

Two mechanisms make the calibration converge rather than thrash, both from the brief's addendum and
both adopted:

- **every component is stored per candidate, not just the total** — so when the pipeline proposes
  something ugly, the breakdown says which band lied;
- **a human's override of the ranking is recorded with its reason** — so the overrides accumulate
  into a specification for the next set of bands.

## Where the same idea is applied differently, and why

Colour is **not** scored at all. It is drawn from `world::PaletteRoles` — Glowmere's five authored
colours, with `reserveAccent` keeping the warm one for the hero. The addendum asks us to reject
"random rainbow coloration"; a band on saturation would do that, and constraining the generator does
it better, because a candidate that cannot be the wrong colour needs no score defending against it.

**The general rule that follows: prefer constraining the generator to penalising the scorer, and
band whatever is left.** A scorer only ever sees what the generator produced, so anything the
generator can be stopped from producing is one fewer thing the scorer can get wrong.

## Consequences

- The scoring config is a checked-in data file with 32 numbers in it and a changelog.
- A component's band can be inspected, plotted against the candidate population, and argued about
  without reading C++.
- `docs/glowmere-valley-2/02-research.md` §4.7's eight components are each specified as a band, and
  the performance component is banded on **projected pixels per triangle** rather than triangle
  count — because `src/scene/mesh_metrics.hpp` records that this renderer's fragment cost tracks
  triangle *size*, with the knee at the 2×2-quad threshold. "Fewer triangles" is the wrong axis, and
  it is also monotone, so it fails this ADR twice.

## Rejected alternatives

- **Monotone components with hand-tuned weights.** The weights cannot express an interior optimum.
  Any weighting of "more is better" still selects the most.
- **A single holistic score.** Cheapest to write and impossible to debug: when it picks badly there
  is nothing to look at. The addendum asks for stored components for exactly this reason.
- **A learned scorer.** No training data, no way to audit a rejection, and a determinism obligation
  with no benefit at a few hundred candidates.

## Revisit when

- A component is found that genuinely has no upper failure mode. None of the eight does, but the
  rule should be broken by evidence rather than by convenience.
- The bands stop moving between calibration rounds — at which point they are art direction and
  should be promoted into the documented art-direction profile rather than living in a tool's config.
