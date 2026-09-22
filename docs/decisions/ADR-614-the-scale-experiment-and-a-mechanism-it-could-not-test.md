# ADR-614: §20 at scale — the stride plan survives, the mechanism that predicted otherwise is untested rather than refuted, and recall is the wrong instrument

**Status:** Accepted — **first version retracted the same day. It reported the mechanism as
refuted; that was an artefact of measuring at 21 dimensions.** See "Retraction" below.
**Date:** 2026-09-21
**Related:** ADR-542 (the pack carries its own licence), ADR-549 (the BVH reader), ADR-553 (a
rotation retarget cannot animate a rig that is not a hierarchy), ADR-540 (clips are authored in
place), ADR-607 (a scan's cost is distribution-independent, a first stage's recall is not),
ADR-611 (a rate metric cannot tell "nothing bad" from "nothing"), ADR-613 (§32's residual),
Phase C §16, §20, §22, §23
**Implemented by:** `avgen_motion quality` (`tools/motion_build.cpp`)
**Corpus:** `assets/100STYLE-ATTRIBUTION.md` — terms, the exact file list, the digests and the
commands. **Every number below is conditioned on the subset named beside it.**

**Amended 2026-09-21:** re-taken after four corrections to the feature values (extraction version 4). Stride-4 still survives on FW, but by less: 94.0% recall, with a miss at 0.07× of the typical gap. FW is more resolvable: 87.0% duplicates, radius ÷ NN 1.47. Only the *sweep* cost spread still transfers. The figures are in `docs/design/procedural-character-motion.md`, "Feature values corrected". The body below is the original record. **Second amendment, same day:** MIXED re-taken. Stride-4 recall falls to 86.9% (was 96.4%); a miss still costs about an eighth of the typical gap. §30 on TR: contacts cost −3.2% per transition and +24.2% over the run (was −16.2% / +7.5%), so the direction is unchanged. Wall time was measured for the first time and the strided plan is *slower* than the linear scan on real data (the scan's early out wins); see "§50–§55" in the phase log.

---

## Retraction: "the mechanism is refuted" was wrong, and the dimensionality is why

**The first version of this ADR was measured on a 21-dimension database — feature joints and root
velocity, no trajectory block — while every earlier Phase C density figure was taken at 33.** In
that system stride-4 recall fell 99.0% → 95.2% while duplicates rose, the two quantities moved in
opposite senses, and I reported the proposed mechanism as refuted in the cleanest possible way.

**Re-run at 33 dimensions, the recall does not fall at all: 98.3% → 98.4%.** The opposite-senses
observation does not exist in the system the rest of the phase lives in. The correct statement is
weaker and more useful:

> **The mechanism is untested, not refuted.** It predicted that *a corpus the matcher can resolve*
> would break the stride plan. 100STYLE is **further under** the matcher's resolution limit than
> Glowmere — duplicate radius ÷ mean nearest neighbour goes **0.95 → 1.71** — so it is not that
> corpus, and the experiment the prediction called for has still not been run. Everything observed
> here is consistent with the mechanism; none of it tests it.
>
> **Testing it needs a different weighting, not a bigger corpus.** The resolution limit is a
> property of the weights, and adding data moves the corpus the wrong way.

The 33-dimension control reproduces the published figures exactly — **61.68% duplicates at radius
1.3945** — which is what establishes that the two eras were one number system all along and that
the 21-dimension run was the odd one out. **The earlier figures are not superseded by anything
here.** All tables below are 33-dimension.

**The lesson is the one already on the record and I walked into it anyway:** a comparison between
arms of different dimensionality is not protected by a constant measurement artefact. I applied
that rule to reject comparing my control against the published number, and then failed to notice
that the fix was to re-run rather than to caveat.

## The corpus, because none of these numbers mean anything without it

**100STYLE, CC BY 4.0** (Ian Mason, Sebastian Starke, Taku Komura) — licence per
`docs/dependencies.md` and `CREDITS.md`, which are the authority. Two subsets, both gitignored:

| | files | frames | database samples | what it is for |
|---|---|---|---|---|
| **FW** `assets/100style/`, digest `7746de4c21a26194` | 100 (`*_FW.bvh`, every style) | 868,806 @ 60 Hz | **434,478** | the style axis |
| **MIXED** `assets/100style-mixed/`, digest `7f2e0d2707aa5611` | 96 (12 styles × 8 movement types) | — | **279,735** | the movement axis, to kill a confound |
| **Glowmere control** (`alien-scout.glb`) | 26 clips | — | **1,738** | the same instrument on the old corpus |

FW is **250× the Glowmere corpus**. The survey is per file, not sampled: 100 of 100 files declare
rotation order **YXZ**, one distinct skeleton (`b42573623c8456bb`), 28 joints, 60 Hz, 0 unreadable.
The rest-pose extent at `--scale 0.01` is x 1.896, y 1.790, z 0.242 — a 1.79 m human in a T-pose,
which is what **confirms the centimetre assumption empirically** rather than by assertion.

**MIXED exists because FW alone could not answer question 3 honestly.** A cross-clip distance
measured over 100 forward walks might be small only because every clip is a forward walk. It is
reported beside FW everywhere, and it agrees — so the confound was real to worry about and turned
out not to bite.

## The control reproduces the published figures, so the two eras are one system

Running this instrument on Glowmere at 33 dimensions gives **duplicates 61.68% at derived radius
1.3945** — the figures on the record, to four decimals. Cost spread is 49.50 by the sweep's
definition (n=290) against the recorded 52.57, and mean nearest neighbour 1.4659 against 1.8493;
both are computed over a different number of seeds, and the relationship that carries the argument —
**the radius sits below the mean nearest neighbour** — holds in both.

**Nothing in this ADR supersedes an earlier Phase C number.** The earlier figures were correct for
the 33-dimension database and remain so; this adds two more corpora measured the same way.

## 1. The stride-4 plan survives; the cheap prefix does not

All arms 33-dimension, 250 seeds spread evenly over each corpus.

| plan | Glowmere (1,738) | FW (434,478) | MIXED (279,735) |
|---|---|---|---|
| stride 8, prefix 12, top 32 | 94.8% | **60.2%** | **62.9%** |
| stride 8, prefix 12, top 128 | 99.0% | 75.3% | 74.5% |
| stride 8, FULL prefix, top 32 | 94.5% | 83.3% | 81.7% |
| **stride 4, FULL prefix, top 32** | **98.3%** | **98.4%** | **96.4%** |

**Stride-4 with the full prefix survives 250× the corpus**: 98.3% → 98.4% on FW, 96.4% on MIXED.
And the cost of a miss falls, read against each corpus's own good-to-typical gap:

| stride 4, FULL prefix | worst excess | × typical gap |
|---|---|---|
| Glowmere | 18.8211 | 0.38× |
| FW | 0.9267 | **0.02×** |
| MIXED | 6.8359 | 0.14× |

**The cheap prefix is the result that does not transfer, and it is the sharpest thing in this ADR.**
On Glowmere the 12-dimension prefix stage was free — 94.8% against full-prefix's 94.5%, so it
scored *better* than reading every dimension, which is how "the cheap prefix bought nothing" came
to be stated with confidence. At scale it costs **23.1 recall points** (60.2% against 83.3%) on FW
and **18.8** on MIXED. A first stage's recall is not distribution-independent (ADR-607), and this is
that sentence with a number on it.

**Recall is still the wrong instrument to read alone**, and it matters even where it barely moves.
It is exact-argmin agreement — one correct answer — and on a corpus that is 92% duplicates by its
own matcher's resolution the argmin is nearly arbitrary. The pairing is what carries meaning: at
stride 4 the plan disagrees with the exhaustive search about 2% of the time and the sample it
prefers is within **0.02×** of the gap between a good match and a typical one. That is the
identifiability failure leave-one-out was retired for, and it was visible only because the paired
quantity was printed beside the rate (ADR-611).

## 2. The density figures moved, and the corpus got *less* resolvable

| | duplicate radius | mean NN | radius ÷ mean NN | duplicates |
|---|---|---|---|---|
| Glowmere | 1.3945 | 1.4659 | **0.95** | 61.68% |
| MIXED | 1.6463 | 1.1426 | **1.44** | 82.36% |
| FW | 1.5517 | 0.9099 | **1.71** | **91.66%** |

**More data made the corpus harder for this matcher to tell apart, not easier.** The samples pack
closer together (mean nearest neighbour 1.47 → 0.91) while the distance the search needs before it
changes its answer barely moves (1.39 → 1.55). That is the sense in which the resolution limit is a
property of **the weighting** rather than of the corpus, and it is why a bigger corpus cannot test
the mechanism in the retraction above. §22 had already pointed at the same thing from the other
side: root velocity is 3 of 33 dimensions and swamped.

**At 92% the duplicate percentage is not a deletion argument.** Deleting nine tenths of a corpus
because the current weight vector cannot resolve it would be destroying content to flatter an
instrument — the same reading the Glowmere figure got when deriving the radius moved it from 7.42%
to 57%.

Two more, both against expectation:

- **The cost spread is the figure that transferred.** 49.50 (Glowmere) → 49.88 (FW) → 50.36
  (MIXED) by the sweep's definition; 45.73 → 47.59 → 51.43 at build time. **A 250× corpus has the
  same good-to-typical gap**, so it is a property of the cost function rather than of the content,
  and every severity number expressed against it carries further than its "on this corpus" caveat
  suggested.
- **Matcher speed resolution got worse**: 0.876 m/s (Glowmere) → 1.168 (FW) → 1.742 (MIXED), while
  speed × turn coverage is fully occupied everywhere. The corpus covers the space and the matcher
  reads it more coarsely — the same finding as the duplicates, through a different instrument.

## 3. The cross-clip coverage bound improves, and it is the most useful of the three

For each seed: the distance to the next frame of its own clip, against the nearest sample in a
**different** clip. It bounds what leaving a clip can cost before any feature or weight is chosen.

| | median ratio | p90 | ratio of means |
|---|---|---|---|
| Glowmere | **3.53×** | 18.29× | 2.33× |
| FW | **1.90×** | 3.72× | 1.81× |
| MIXED | **1.92×** | 3.40× | 1.71× |

**Leaving a clip costs about 1.9 adjacent-frame steps on 100STYLE against 3.5 on Glowmere**, and
MIXED says that is not an artefact of every clip in FW being a forward walk.

**This bounds ADR-613's residual.** §32's blend leaves 0.4112 m — 8.06× the acceptance bar — on a
*forced* cross-clip transition, and ADR-613 concluded the fix is better selection rather than better
blending. Here is that in numbers: the residual is a consequence of Glowmere's 3.5× gap, and on a
corpus whose gap is 1.9× the same blend has roughly half as much to absorb.

**An instrument retired here, and it would have been the headline.** The mean of the per-seed ratios
reads **2142.95×** on Glowmere, worst **294,645.80×**. Those describe the smallest denominator in
the set — a near-static frame whose next frame is nearly identical — and not the corpus. The median
and the ratio of means agree with each other and with the figure on the record.

> **The tell, stated so it can be checked rather than remembered: three orders of magnitude between
> a mean and a median means the denominator approaches zero somewhere in the set.** This is the
> third ratio in Phase C to fail this way, after the 2957× staged/full cost and the unweighted cost
> spread. See ADR-609.

## 4. Contacts are NOT inert on the content §30 was written for — and the two measures disagree

§30's verdict (*contacts net −2.0 points, genuinely inert*) was scoped to the Glowmere corpus from
the day it was taken, because that corpus has no starts, stops or directional changes. 100STYLE's
`TR1` files do: 100 styles of a body changing what it is doing, **420,432 samples**, packed by the
command in `assets/100STYLE-ATTRIBUTION.md`.

**The first run was degenerate, and the tell was in the table.** At §30's fixed +0.05 perturbation
the two arms agreed to four decimals — mean 1.5743 m, worst 4.8504 m, **and the same 30 switches**
— because the search returned the seed sample on **600 of 600** steps. A query built from a
sample's own features and nudged by a fixed amount identifies its source uniquely among 420,432
samples, so no feature can change the answer. The number being compared was the corpus's clip
layout, not the matcher's choices. On 1,738 Glowmere samples the same nudge left the choice
genuinely open, which is why the instrument worked there and not here.

**Ask what the metric can see before asking what the feature carries** — the third time that
pre-test has been the one that mattered, and the first time it was caught from a table rather than
after a wrong conclusion. Three identical columns including the *count* is the signature.

**The fix is a perturbation in the matcher's own units**: the derived duplicate radius is the
distance at which this matcher stops telling two samples apart, so a query displaced by exactly
that is ambiguous by construction at any corpus size. Here 1.6757 over 33 dimensions = 0.2917 per
dimension, **5.8× the fixed value**. Seed-return falls to 194/600 and 250/600, and the arms can
finally disagree:

| | switches | mean per transition | total over the run |
|---|---|---|---|
| contacts OFF | 99 | 1.1248 m | 111.4 m |
| contacts ON | 127 | **0.9428 m (−16.2%)** | **119.7 m (+7.5%)** |

**Both rows are printed because choosing one would be choosing the answer.** Contacts change how
*often* the matcher switches as well as how far each switch moves a foot — **+28% more switches** —
so the arms do not share a denominator. Per transition the plant discontinuity is 16.2% better;
across the run there is 7.5% more of it.

**What is established, and it is the part that matters:** on the content §30 was written for,
**contacts are not inert.** On Glowmere they moved the mean 6.3% the wrong way and nothing else;
here they change the matcher's behaviour substantially. The inert verdict was a property of that
corpus, exactly as it was scoped to be.

**What is not established: whether they help.** A −16.2% that comes with +28% more transitions is
not a win by inspection, and this instrument cannot separate "each transition is better" from "it
takes more, smaller steps". The worst case is **identical in both arms** (4.2572 m), as it was on
Glowmere — so neither arm fixes the tail. Deciding this needs a measure that holds the switch rate
fixed, which is a §28/§29 hysteresis question rather than a contact question.

**The dimensional hurdle does not transfer to this measure.** The −2.0 points was a pose-proximity
score, where two contact dimensions cost ~0.6 points at unit weight. Metres of foot jump have no
such per-dimension tax, so the two verdicts are not on the same scale and neither supersedes the
other.

## What this does not answer

- **Whether contacts are worth carrying.** Section 4 establishes they are not inert on transition
  content and leaves the sign unresolved, because the two available measures disagree and the
  confound is the switch rate.
- **The mechanism in the retraction** — it needs a weighting that resolves the corpus, not a bigger
  corpus, and no such weighting has been built.
- **The pose-proximity instrument on 100STYLE.** Section 4 re-ran §30's *transition* experiment,
  which is what §30's stated purpose asks for. The net-points instrument is a separate ~500-line
  fixture built around the Glowmere rig and was not ported.

## Consequences

- **§20 is answered for its three queued items**, and the first of them came back as "the
  experiment has not been run" rather than as an answer — which is worth more than the wrong answer
  this ADR carried for an afternoon.
- **`avgen_motion quality` is the instrument**, and it is a command rather than a test on purpose:
  every figure is a property of a 612 MB gitignored corpus, so a test would skip wherever it ran.
  What makes the numbers checkable is that the command line sits beside them and the instruments are
  the library functions the Glowmere tests already use.
- **Results carrying "on this corpus" that now transfer:** the cost spread, and therefore the
  severity figures expressed against it; and the stride-4 plan. **Results that do not:** "the cheap
  prefix bought nothing", which is a Glowmere property and costs 23.1 recall points at scale, and
  any reading of the duplicate percentage as an argument for deleting samples — at 91.66% it is an
  argument about the weighting.
- **Run every arm of a comparison in the number system the rest of the phase uses, or re-run the
  control.** Caveating a dimensionality difference is not a substitute for removing it: the caveat
  was accurate, the reader would still have compared the tables, and one of the conclusions was
  wrong underneath it.
- **Nothing derived from 100STYLE is published from this repository.** CC BY 4.0 permits
  redistribution with attribution, and the pack records that; permission is not a plan.
