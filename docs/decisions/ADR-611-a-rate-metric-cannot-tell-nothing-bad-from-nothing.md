# ADR-611: A rate metric that counts bad events cannot distinguish "nothing bad happened" from "nothing happened"

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-182 (a probe that cannot fail proves nothing), ADR-559 (a monotone metric is a
direction), ADR-609 (recall is not a quality measure), ADR-610 (graded continuity was measured and
lost), `docs/testing.md`
**Implemented by:** nothing — this is a measurement rule
**Tests:** `tests/unit/test_motion_database_scale.cpp`

---

## Context

Three sections of Phase C produced the same failure in three different quantities, inside a few
hours:

- **§16**: recall. 94.4% agreement with the linear scan reads as fine and is silent on the fact
  that the 5.6% of misses land 86.9% of the way to a random sample.
- **§11**: jump rate. Graded continuity reported an identical jump rate to the binary version — and
  stalled the matching loop on 13% of steps. **A frozen matcher reports zero jumps, which is the
  best possible score on the only number anyone would have thought to print.**
- **§17**: rejected candidates. A tag filter that had quietly emptied the candidate set would
  produce an extremely fast query and a meaningless benchmark.

## Decision

> **A rate metric that counts bad events cannot distinguish "nothing bad happened" from "nothing
> happened". Every such metric needs a companion that counts that something happened at all.**

The companion is always cheap and is almost never written without being asked for:

| bad-event rate | the companion it needs |
|---|---|
| clip jumps per second | mean index step; fraction of steps that stalled |
| search disagreement rate | severity of a disagreement, against the data's own spread |
| candidates rejected | candidates *scored* |
| contact-error frames | frames in which a contact existed at all |
| layers that failed to resolve | layers that were asked to do anything |

This is the same illness as ADR-559's monotone metric and ADR-609's recall-without-severity, but it
is worth its own record because the tell is different. A monotone metric recommends an extreme; this
one **awards a perfect score to a dead system**, and the dead system is often the worst available
outcome rather than a neutral one. A character that freezes is worse than a character that
occasionally cuts to the wrong clip.

## The stronger form of ADR-182, which this session earned the right to state

ADR-182 says a probe that cannot fail proves nothing, and the remedy it implies is to discard the
probe's output and build a better probe.

§11's fixture went further than that. It queried the matching loop with the features of
`sampleNext[current]` — the natural continuation — so continuity was never asked to decide anything,
and it printed **0.00 jumps per second for every configuration**: a perfect score, from a probe that
could not have printed anything else.

**But its output already contained the defect.** Alongside the meaningless zero it reported a mean
index step of **0.15 against the binary's 1.82** — the freeze, in plain digits, in the same table. So:

> **A probe that cannot fail does not merely prove nothing. It can hold the proof of a real fault
> and hand it to you labelled as a pass.**

That is strictly stronger than ADR-182 and it changes what to do about it. 182 says throw the
result away; this says **read the rest of the row before you do**, because the number that was not
the headline may be the one that matters. The three dead-probe findings in this programme all had a
non-headline column that was already wrong.

## It applies to the harness, not only to the tests

The command this branch waited two hours on was:

```sh
tools/gpu-lock.sh ./build/release/tests/avgen_render_tests 2>&1 | tail -4; echo "render exit: $?"
```

**Both of its signals are incapable of carrying a failure.** `$?` after a pipeline is the status of
the *last* command in it, and with no `pipefail` that is `tail`'s — which exits 0 essentially
always. So `render exit: 0` prints whether the suite passed, failed or crashed. And `tail -4`
removes the rest of the row: a clean Catch2 run prints `All tests passed` and no `^test cases:`
line, a crash prints no verdict line at all, and the last four lines of a crashed run are whatever
the crash happened to emit.

This is contamination guard #3 — *take the exit code from the binary, never from a pipeline* —
violated by the author of this ADR while writing it. It is also the ADR's own subject: a probe that
cannot fail, with the one mitigation (read the rest of the row) removed by the `tail`.

**The rule is therefore about the harness as much as the assertions.** A suite full of careful
controls, run through a pipeline that discards the status, is a suite with no verdict.

## Consequences

- The §11 test asserts `stalledFraction` alongside the jump rate. The §16 sweep reports severity
  beside recall. The §17 benchmark asserts `considered == sampleCount` and `rejected == 0`. None of
  those three can now award a perfect score to a system that did nothing.
- **The habit that caught all three is worth stating plainly: an exact zero is a reading to distrust
  before believing.** Zero jumps in §11; zero variance in Phase B's reach probe; three tags carried
  by zero samples in §14. Same tell, three subsystems.
- A corollary found while checking §12: **a penalty that works is often invisible in the winner's
  breakdown**, because its effect was on the candidate that lost. `MotionCostBreakdown` answers
  "what did this cost", not "what changed the decision". Anyone debugging a choice through §50's
  read-out needs to know a term can be decisive and read as zero.
