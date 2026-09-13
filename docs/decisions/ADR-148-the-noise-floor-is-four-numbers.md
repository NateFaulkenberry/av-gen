# ADR-148: The noise floor is four numbers, and the one that binds is usually the pairs disagreeing

**Status:** Accepted
**Date:** 2026-09-13
**Fixes a defect in:** ADR-113 §4 (whose rule was right and whose implementation saw half of it)

## The defect, reported and then verified

The Phase F agent hit this and withdrew two of its own Glowmere rows over it: **a null A/B — both
arms the same code — was certified as "A RESULT" at −2.39% against the 2.00% floor.** ADR-113 §5 is
explicit that a null reporting a result means a broken harness, whatever it says about any real arm.

The cause, confirmed by reading `pairDeltas` and then by unit test rather than taken on report: the
session floor was derived from **`baseUsed` only** — the baseline blocks. The quantity being
certified is a difference between *two* arms. In a null A/B the two arms are the same code and are
equally noisy, so whenever the baseline's blocks happened to land tight and the arm's happened to
wobble, the arm's variance never entered the floor and the wobble was certified as a difference.

The regression test reproduces the reported shape directly: a baseline flat to 0.2% against an arm
spanning 3%, differencing to −2.4%. Under the old rule it is a result. Under the new one it is not.

## Decision

`PairedDelta::noiseFloorPercent` is the largest of **four** components, each of which catches a case
the others cannot, and all four are reported.

| component | what it catches | why the others cannot |
|---|---|---|
| `calibratedFloorPercent` | the reference machine's own floor, 2% GPU / 4% wall | a session that happened to be quiet is not licence to certify below what the hardware has been measured to produce |
| `baselineSpreadPercent` | a noisy session | ADR-113 §4's original rule, and what correctly rejects a Constellation null |
| `armSpreadPercent` | a noisy session **on the side the baseline cannot see** | the defect above, exactly |
| `deltaSpreadPercent` | the pairs disagreeing **with each other** | both arms can be individually steady block to block while the pairing is not, and neither arm's own spread can reveal it |

The fourth is the coordinator's suggestion and it turns out to be the important one. It is the
peak-to-peak of the per-pair deltas as a percentage of the baseline median — the variability of the
number actually being certified. ADR-113 printed the per-pair deltas for precisely this case and
then certified their median anyway.

`AbSummary::gpuSpreadPercent` still means the *baseline's* spread. It is published under that name
and quietly changing what a named field means is how a reader ends up comparing two different
quantities.

## The fourth component is not theoretical: it binds

Four null A/Bs on Glowmere, 1280×800, four interleaved pairs of 120 frames each, one after another
under `tools/gpu-lock.sh` this afternoon:

| null | delta | calibrated | baseline blocks | arm blocks | **per-pair deltas** | floor applied |
|---|---:|---:|---:|---:|---:|---:|
| 1 | +0.00% | 2.00% | 1.48% | 0.49% | **1.48%** | 2.00% |
| 2 | +0.97% | 2.00% | 1.46% | 1.95% | **2.91%** | 2.91% |
| 3 | +0.48% | 2.00% | 10.10% | 13.57% | **13.94%** | 13.94% |
| 4 | +1.33% | 2.00% | 12.00% | 11.40% | **16.89%** | 16.89% |

**The per-pair delta spread is the largest component in all four**, and in three of them it is
larger than either arm's own spread. All four nulls are correctly not results.

Two further things fall out of that table and neither is comfortable:

- **Glowmere's within-session GPU spread reached 12–13.6% today**, against the 1.0% §3.3 measured
  over five consecutive runs. The 2% constant does not describe this machine as it currently is. It
  stays as a floor because a floor should not fall; it is no longer the number doing the work.
- Nulls 3 and 4 would have needed a **17% real effect** to certify anything. That is the honest cost
  of a shared machine, and it is now visible in the log line rather than discovered afterwards.

## Which past claims this invalidates

Stated plainly, and one of them is left unresolved rather than argued into safety.

**ADR-120 (`--ab shadowrange`, −6.90%) survives, and can be shown to.** It published its three
per-pair deltas — −0.92, −1.18, −0.79 — so the new component is recomputable from the record: 0.39 ms
peak-to-peak on a 13.30 ms baseline is **2.93%**. Its baseline spread was 1.48% and its arm's scene
pass moved 11.67–11.73 across blocks, so no component reaches 6.90%. It survives *because it
published its per-pair deltas*, which is the argument for reporting all four components.

**ADR-119 (`--ab auxstore`, −0.99%, not a result) is unaffected.** All three deltas were identical
at −0.13 ms, so the new component is zero and the verdict does not move.

**§3.5's `--ab shadowmask` result (−4.06 ms, −20.9%, two pairs) cannot be verified from the record
and its status is now unknown.** Its per-pair deltas were not published, and two pairs give a
one-difference estimate of the component. −20.9% is large, but nulls 3 and 4 above show this scene
producing floors of 14% and 17% on the same machine, so it is not comfortably clear. **It needs
re-running before it is quoted again.** It is the only A/B claim in the upgrade's documents this
change leaves in doubt, and the doubt is not resolved here because resolving it by argument is what
the whole instrument exists to prevent.

**The Phase F agent's two withdrawn Glowmere rows stay withdrawn.** They were withdrawn correctly.

Nothing else is touched: §3.2.1's −21% LOD0 result, §3.2.2 and §3.2.3 were separate runs or reported
state, not paired A/Bs, and ADR-131's sweep never used this path.

## Consequences

- The floor is now per-scene and per-session **by construction** rather than by a caveat somebody
  has to remember. Three of its four components are measured from the run itself; the constant is
  only a floor beneath them.
- The console prints the components on the line beside the verdict, and `--bench-json` carries them
  under `noiseFloorComponents`. Which component bound is part of the result: a difference rejected
  by the calibrated constant and one rejected because this session's arm wobbled are different
  findings with different next steps.
- Some arms that used to certify will stop. That is the point and it is not to be worked around.

## Alternatives considered

**Only add the arm's spread** (the direct fix for the reported case). Rejected on the evidence
above: it would have fixed all four nulls, but in three of them the per-pair spread is larger, so
the floor would still have been understated in the general case.

**Replace the baseline's spread with the per-pair spread** rather than taking the maximum. Tempting,
and it has a real argument — pairing exists to survive baseline drift, so charging a drift that
pairing already cancelled to the floor is arguably double-counting. Rejected for now: ADR-113 §4's
rule is what correctly refuses to certify anything on Constellation, whose baseline blocks span 37%,
and removing it to make a cleaner story would re-open a hole the audit paid to find. If someone
later measures that pairing genuinely removes the drift on such a scene, this is the component to
reconsider.

**A statistical test on the pairs.** Rejected for ADR-113's reason, unchanged: three or four pairs is
not a sample a test has anything to work with, and the observed peak-to-peak is a direct measurement
of the quantity a test would be estimating.

## Verified vs assumed

**Verified:** the defect, by unit test reproducing the reported shape; the fix, by the same test and
by the four live nulls above under `tools/gpu-lock.sh`; that the pre-existing A/B unit tests all
still pass unchanged, including the one asserting that a noisy baseline raises the floor; ADR-120's
survival, recomputed from the figures it published.

**Assumed:** that the reported −2.39% had this cause specifically. It was not reproduced live — four
nulls today all reported +0.00% to +1.33% on the GPU clock — so what is established is that the
mechanism exists, that it produces exactly the reported symptom on the reported shape of data, and
that it is now closed. A different cause producing the same symptom is not excluded by this.
