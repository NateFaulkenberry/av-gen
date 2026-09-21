# ADR-607: A requirement whose cost lives in another section will be read as a preference

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-604 (every character carries its own copy of the pack), ADR-606 (re-read the
specification), ADR-603 (record the forecast so it can be wrong), Phase C §4, §6, §15, §17, §19
**Implemented by:** `src/entity/match_motion_provider.hpp` (`database()`)
**Tests:** `tests/unit/test_motion_database_scale.cpp`

---

## Context

Phase C §4 says the motion database "should be versioned, serializable, inspectable,
deterministic, **shared between character instances**". Read on its own, that last clause is
housekeeping — the kind of line an implementer notes and does not act on, because nothing in the
sentence says what it costs to ignore.

§6, four sections away, says to measure memory at 10,000 / 100,000 / 1,000,000 samples. Measured:
**152 bytes per sample exactly**, so 144.96 MB at a million. Which makes §4's clause read
completely differently: **a hundred characters each holding one is 14.5 GB.**

The clause did not change. The number arrived from somewhere else and made it enforceable.

This is a third distinct failure mode, and it is worth separating from the two already recorded:

- **ADR-606** is an audit citing a true fact that answers a *neighbouring* question.
- **ADR-182** is a probe that cannot fail.
- **This** is a requirement that is *correct, unambiguous and read as optional*, because the
  section stating it contains no reason to act and the section supplying the reason is elsewhere.

It is also ADR-604's mistake one tier up and two orders of magnitude worse. `SkinnedRig` holds its
`Skeleton` and every `AnimationClip` by value; 78% of the Glowmere scene's rig memory is a
byte-identical second copy. Neither instance would have come from a profiler: **a loop that never
reads the duplicates does not slow down.** One was found by reading a header, the other by pricing
a clause.

## Decision

**When a measurement lands, go back and re-read the clauses that were cheap to ignore without it.**

A number is not only an answer to the question that produced it. It is a licence to re-read every
requirement whose cost was previously unstated, and the re-read is a deliberate step rather than
something that happens on its own.

`MatchMotionProvider` holds a `const MotionDatabase*`, so §4 is satisfied today. A hundred
providers are now asserted to share one `features.data()` — comparing the address of the bytes, not
of the struct, so a shallow copy would still fail — because **"it is a pointer today" is not a
property anything checks.**

### The same rule applied to work one hour old

Re-reading §15 and §17 after writing the §6 benchmark caught that benchmark:

- §15: "the earlier research already demonstrated that **synthetic benchmark results can mislead**.
  Therefore: use real AV Gen motion distributions as the primary benchmark." Mine was synthetic.
- §17: "benchmark **1,700** / 10,000 / 100,000 / 1,000,000 frames … query latency, **average**
  latency, **worst-case** latency, candidate count, memory, database load time, **build time**."
  Mine started at 10,000 and reported only a minimum.

**Three misses, in work one hour old, by the author who had just written ADR-606 about exactly this
failure.** That sentence is in the record deliberately, because the alternative reading — that the
first author was careless — is available to every future reader and is wrong. The benchmark was
written carefully. Awareness of a failure mode decays inside the hour, and the only thing that
survives the decay is a re-read of the text or a test that fails.

**What survives the correction is the useful part.** A linear scan touches every sample whatever the
values are, so its cost is distribution-independent and the synthetic timing is a valid measurement
*of a linear scan*. What is not distribution-independent is everything §14 and §16 are about: how
much candidate filtering removes, and whether a cheap first stage keeps the sample the full cost
would have chosen. So the rule for the rest of Phase C is narrow and firm:

> **Synthetic data may time the scan. Only real data may judge a filter or a first stage.**

On minima versus averages: this repository's standing rule is minima over repeats, never means, and
§17 asks for average and worst case. That is two questions, not a contradiction. A minimum answers
*how fast can this code go*, which is a property of the code. A worst case answers *will this drop a
frame*, which is the only thing a 60 Hz budget cares about, because a 36 ms spike drops a frame
however good the average was.

## Consequences

- **The real Glowmere database is 1,738 samples**, which is §17's "1,700 frames" almost exactly.
  That is a *second kind of textual evidence*, distinct from the clause-pricing above: a
  specification quoting a number that turns out to be your own corpus is evidence about **intent**.
  Real-data-first was the reading and the synthetic ladder of round numbers was the paraphrase. When
  a spec's example matches the repository's actual content, the example is not an example.
- **Minima and worst cases are not in conflict, and this is the line that says so.** The standing
  rule — minima over repeats, never means — was always about *comparing arms on a contended
  machine*, where a mean measures whatever else the machine was doing. It was never an argument
  that a worst case is uninteresting. A minimum answers *how fast can this code go*; a worst case
  answers *will this drop a frame*, and a 36 ms spike drops a frame however good the average was.
  Report both; use the minimum to compare and the worst case to budget.
- Measured on it: 26 clips, dimension 33, **0.252 MB at 152.0 bytes/sample** — the same per-sample
  arithmetic as the synthetic table, which cross-checks both. Build is 32.7 ms for the pack plus
  77.2 ms for the database. Query latency over 500 queries seeded from inside the real distribution:
  **best 8.88 µs, average 9.25 µs, worst 17.46 µs** — a worst case about twice the average, and
  **955 characters per frame at 60 Hz on the worst case.**
- **Therefore §16's two-stage search is justified by the million-sample case and by nothing in the
  repository today.** At present content a linear scan is comfortably correct, and saying so is
  what stops the two-stage search being built for the wrong reason and then defended with a
  benchmark that never needed it.
- The benchmark asserts it actually searched: a query seeded with sample 579 returns sample 579 at
  cost 0.000000. Without that, every latency number above could have been the cost of a fast
  refusal — the same failure as a parity test over a field that is zero everywhere.
