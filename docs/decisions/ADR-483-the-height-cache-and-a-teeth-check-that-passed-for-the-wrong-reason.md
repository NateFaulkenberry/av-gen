# ADR-483: The height cache, and a teeth-check that passed for the wrong reason

**Status:** Accepted
**Date:** 2026-09-20
**Builds on:** ADR-482 (the scrub is a polyline scan), ADR-182 (a probe that cannot fail proves
nothing), ADR-170 (minima over repeats, and state the contention)

## Problem

After ADR-482's cutoff, a scrub on the owner's film is still about 2.4 seconds, and the leaf profile
says **93% of it is inside `WorldMap::height`** — `closestOnPath` and `valueNoise` are both reached
from there. `height` is a pure function of (x, z) for a fixed map. "Memoise it" is the obvious next
move, and *"it is a pure function"* is a correctness argument of exactly the kind ADR-482 was
written about.

## What was measured before anything was built

`AVGEN_HEIGHT_CENSUS=1` counts calls and distinct coordinates, and simulates direct-mapped tables of
three sizes. Counts only, no timing, so contention cannot move it.

| phase | calls | distinct | perfect | 4K | 64K | 1M | threads |
|---|---:|---:|---:|---:|---:|---:|---:|
| load: world build, nav bake, scatter | 1.82M | 1.76M | 3.0% | **0.2%** | 1.2% | 2.1% | **12** |
| one uncapped seek to t = 90 s | 2.83M | 2.02M | 28.7% | **26.5%** | 28.5% | 28.7% | **1** |

Three facts, none of them guessable, and each decided part of the design.

* **The seek is single-threaded.** The twelve threads are the load's job workers. The path that
  benefits needs no lock.
* **4,096 entries take 26.5 of the 28.7 available points** — 92% of the ceiling, from 32 KB. The
  repeats are *temporally local*: a walker re-samples its own neighbourhood, so a small table holds
  them and a large one adds almost nothing.
* **The load hits 0.2%**, a lattice swept once. A cache is worth nothing there and must at least be
  harmless, which `thread_local` achieves — the workers get their own tables and never contend.

### The prediction that was wrong, recorded because it was reasonable

Going in, the expectation was that the hit rate would be **near zero**, on a specific and checkable
mechanism: `pathClear` samples along a segment whose origin is the walker's position, the walker
moves about five centimetres per step, and an exact-key cache hits on bit equality — so consecutive
steps should sample *nearly* identical and never *bit* identical points.

The measurement says 28.7%. The model was not silly and it was wrong, and the interesting half is
why: it accounted for motion between steps and not for the fan of candidate directions *within* a
step, which re-asks about the same neighbourhood many times from the same origin. The busiest single
coordinate was asked about **1,102 times** in one replay.

A wrong prediction with a measured correction is worth more to the next reader than a right one, so
it is here rather than quietly dropped.

## Decision

A `thread_local` direct-mapped table of 4,096 entries in front of `WorldMap::height`. Each entry
carries the map's identity **and** build (`cacheId_`, reassigned by every `prepare()` from a counter
that never reuses a value), so neither a different map nor a rebuilt one can be answered out of an
old entry. `heightUncached` is **public**, so the slow path is something a shipping build still
runs — ADR-482's own rule applied to this ADR's code.

**Measured, interleaved in one session, minima (ADR-170):**

    cache off   5892.1  6928.2  6101.2   ->  min 5892.1
    cache on    4804.5  4591.0  4852.8   ->  min 4591.0      -22.1%

The cached arm is also four times steadier — a spread of 262 ms against 1036.

## The teeth-check that passed for the wrong reason

The stale-map arm exists because a bit-exactness test cannot see a stale cache on its own: both arms
would share the staleness. So the test warms the cache, edits the features, calls `prepare()`, and
compares again, with a control requiring the edit to have moved the heights at all (it moved 3,996
of 4,225).

Then the arm was checked for teeth by removing the identity from the entry's tag. **It passed.** It
was one sentence away from being reported as verified.

It passed because the identity is folded into the **hash index** as well as the tag, so a rebuilt
map sends the same coordinate to a *different slot* and the stale entry is never consulted. Only
when the identity came out of the index too did the arm fail — **174 stale heights of 4,225**,
exit 42.

**The general form, which is worse than the rule ADR-182 states.** ADR-182 says a probe that cannot
fail proves nothing. This is a probe that *can* fail and *does* fail — but not for the reason being
tested, so it certifies a mechanism that is not the one carrying the safety. When a guard is
enforced by two independent mechanisms, defeating either alone is masked by the other, and the
teeth-check reports success. **Defeat every mechanism that could mask the fault, together, not one
at a time.**

Here the index was carrying most of the safety and the tag is what makes it exact. A later reader
who removes the index hashing as a simplification, trusting the tag, gets a green suite and a cache
that returns confidently wrong heights.

## Two near-misses, both caught and both worth the line

* **A pooled 62.4% that was not a hit rate.** The first census averaged the load phase and five
  capped re-seeks together. Those have opposite access patterns — a lattice swept once, and a
  trajectory walked repeatedly — and pooling them produced a number twice the truth. The figure for
  one scrub is 28.7%.
* **A first timing that said the cache was 2x *slower*.** It was the load average at fifty and
  nothing else. It was caught only because the arms were then run interleaved inside one session
  rather than against a number taken an hour earlier, which is ADR-170's rule and the reason it
  exists.

## Consequences

* A scrub is cheaper again, on top of ADR-482. The absolute figure is deliberately **not quoted
  here**: every number in this document that could be quoted as an absolute was taken while three
  other agents were on the machine, and the honest ones above are ratios measured inside a single
  interleaved session. The end-to-end figure belongs in a quiet window.
* `AVGEN_HEIGHT_CACHE=0` removes the cache without removing the build, so the comparison can always
  be re-run as an A/B rather than against a remembered number.
* **Not done:** the load phase's 1.8 million height calls at a 3% repeat rate are untouched, and a
  cache is the wrong tool for them. If that phase matters — it is part of time-to-first-usable-frame
  — the lever is to ask for fewer points, not to remember more of them.
