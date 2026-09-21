# ADR-614: §20 at scale — the stride plan survives, the mechanism predicting its failure does not, and recall is the wrong instrument

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-542 (the pack carries its own licence), ADR-549 (the BVH reader), ADR-553 (a
rotation retarget cannot animate a rig that is not a hierarchy), ADR-540 (clips are authored in
place), ADR-607 (a scan's cost is distribution-independent, a first stage's recall is not),
ADR-611 (a rate metric cannot tell "nothing bad" from "nothing"), ADR-613 (§32's residual),
Phase C §16, §20, §22, §23
**Implemented by:** `avgen_motion quality` (`tools/motion_build.cpp`)
**Corpus:** `assets/100STYLE-ATTRIBUTION.md` — terms, the exact file list, the digests and the
commands. **Every number below is conditioned on the subset named beside it.**

---

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

## Read this before the table: the control does not reproduce the published absolutes

Running the same instrument on Glowmere gives **cost spread 32.76, duplicate radius 0.8758, mean
nearest neighbour 0.9694, duplicates 63.18%**. The figures on the record are **52.57, 1.3945,
1.8493 and 57.02%/61.68%**. The *relationships* reproduce — duplicates around 60%, the radius below
the mean nearest neighbour, a cross-clip ratio near 2.4× — but the absolutes do not, which means the
published numbers came from a database built differently (a different feature configuration or
sample rate; this instrument runs 21 dimensions with no trajectory block).

**So every comparison in this ADR is against my own control, taken through the identical command,
and none of them is against the published number.** Comparing 21 dimensions with whatever produced
1.3945 would be exactly the different-dimensionality comparison that no measurement artefact
protects (ADR-607's corollary). The published figures are not wrong; they are not this instrument's.

## 1. The stride-4 plan survives. The mechanism predicting it would break is refuted.

| plan | Glowmere recall | FW recall | MIXED recall |
|---|---|---|---|
| stride 8, prefix 12, top 32 | 94.8% | **60.2%** | **62.9%** |
| stride 8, prefix 12, top 128 | 99.0% | 75.3% | 74.5% |
| stride 8, FULL prefix, top 32 | 95.2% | 76.9% | 74.5% |
| **stride 4, FULL prefix, top 32** | **99.0%** | **95.2%** | **92.8%** |

Recall falls, as predicted. **And the worst excess falls with it** — the quantity that says how
*bad* a miss is, read against the corpus's own typical good-to-typical gap:

| stride 4, FULL prefix | worst excess | × typical gap |
|---|---|---|
| Glowmere | 8.9092 | 0.27× |
| FW | 1.3394 | **0.04×** |
| MIXED | 6.2784 | 0.19× |

**The two disagree, and recall is the one that rewards identifiability.** Recall is exact-argmin
agreement — a metric admitting exactly one correct answer — and on a corpus that is 80–96%
duplicates by its own matcher's resolution, the argmin is nearly arbitrary. The staged search picks
a different sample more often *and the different sample is as good or better*. **This is the failure
mode leave-one-out retrieval was retired for**, met again in a new place, and the pre-test caught it
because the paired quantity was printed beside the rate (ADR-611).

**What did break is the cheap prefix, and that is the real §16 result here.** On Glowmere the prefix
stage was free — 94.8% against 95.2% full-prefix, indistinguishable. On 100STYLE it costs **16.7
recall points** (60.2% against 76.9%) and **11.6** on MIXED. *"The cheap prefix bought nothing"* was
a property of the Glowmere corpus, and it does not survive.

**The mechanism, stated as a prediction and now refuted.** The reasoning was: Glowmere's derived
duplicate radius sits *below* its mean nearest-neighbour distance, so the corpus is at or under the
matcher's resolution limit almost everywhere, which is why subsampling lost nothing — and a corpus
with real resolution should break it. **100STYLE is further under the limit, not less:**

| | duplicate radius | mean NN | radius ÷ mean NN | duplicates |
|---|---|---|---|---|
| Glowmere | 0.8758 | 0.9694 | **0.90** | 63.18% |
| MIXED | 1.3529 | 0.9539 | **1.42** | 79.94% |
| FW | 1.6571 | 0.7710 | **2.15** | **96.50%** |

More data made the corpus **less** resolvable by this matcher, not more. So the stride result and
the duplicate figure are **not one property seen through two instruments** — under the predicted
mechanism they should have moved together, and they moved in opposite senses. The resolution limit
is a property of **the matcher's weighting, not of the corpus**, and no amount of data repairs it.
That is where §22 already pointed: root velocity is 3 of 33 dimensions and swamped.

**The refutation is worth more than the confirmation would have been**, and it was the stated reason
for running this: a corpus that broke stride-4 for the predicted reason would have confirmed a
story that is wrong.

## 2. The density figures moved, in the opposite direction

The table above is the answer. Two more:

- **Cost spread barely moved**: 32.76 (Glowmere) → 31.82 (FW) → 32.26 (MIXED) by the sweep's
  definition; 34.86 → 28.47 → 31.51 at build time. **A 250× corpus has the same good-to-typical
  gap**, which is the one figure that turned out to be a property of the cost function rather than
  of the corpus. Every severity number expressed against it therefore transfers better than
  expected.
- **Matcher speed resolution got worse**: 0.475 m/s (Glowmere) → 0.776 (FW) → 1.063 (MIXED). Speed ×
  turn coverage is fully occupied on both 100STYLE subsets (16/16, 9/9) against 24 of 36 on
  Glowmere — the corpus covers the space, and the matcher resolves it more coarsely.

## 3. The cross-clip coverage bound improves, and it is the most useful of the three

For each seed: the distance to the next frame of its own clip, against the nearest sample in a
**different** clip. It bounds what leaving a clip can cost, before any feature or weight is chosen.

| | median ratio | p90 | ratio of means |
|---|---|---|---|
| Glowmere | **4.39×** | 24.08× | 2.44× |
| FW | **1.68×** | 3.86× | 1.65× |
| MIXED | **1.66×** | 3.37× | 1.51× |

**Leaving a clip costs about 1.7 adjacent-frame steps on 100STYLE against 4.4 on Glowmere**, and
the MIXED arm says that is not an artefact of every clip being a forward walk.

**This bounds ADR-613's residual.** §32's blend leaves 0.4112 m — 8.06× the acceptance bar — on a
*forced* cross-clip transition, and ADR-613 concluded the fix is better selection rather than better
blending. This is the quantitative form of that: the residual is a consequence of Glowmere's 4.4×
gap, and on a corpus whose gap is 1.7× the same blend has a quarter as much to absorb.

**An instrument fix found here, and it would have produced a headline.** The mean of the per-seed
ratios reads **2144.98×** on Glowmere, worst **294,645.80×**. Those are descriptions of the smallest
denominator in the set — a near-static frame whose next frame is nearly identical — and not of the
corpus. The median and the ratio of means agree with each other and with the figure on the record.
**A ratio distribution with a denominator that can approach zero has no meaningful mean**, and the
tell was that the mean sat three orders of magnitude from the median.

## What this does not answer

**The contact verdict is still untested against the content it was written for.** §30's finding —
contacts net −2.0 points and genuinely inert — was explicitly scoped to the Glowmere corpus because
that corpus has no starts, stops or directional changes. 100STYLE has them, in the `TR*` transition
files, and **neither subset here includes one**: FW is forward walk only and MIXED takes the eight
steady movement types. So the inert verdict is **unchanged and still unconfirmed**, and the next
step is named rather than implied: fetch `*_TR1.bvh`, and re-run §30's transition experiment on a
database built from it.

## Consequences

- **§20 is answered for its three queued items**, and one of them came back against the hypothesis
  it was queued to test.
- **`avgen_motion quality` is the instrument**, and it is a command rather than a test on purpose:
  every figure is a property of a 612 MB gitignored corpus, so a test would skip wherever it ran.
  What makes the numbers checkable is that the command line sits beside them and the instruments are
  the library functions the Glowmere tests already use.
- **Results carrying "on this corpus" that now transfer:** the cost spread, and therefore the
  severity figures expressed against it. **Results that do not:** "the cheap prefix bought nothing",
  and any reading of the duplicate percentage as an argument for deleting samples — at 96.5% it is
  an argument about the weighting.
- **Nothing derived from 100STYLE is published from this repository.** CC BY 4.0 permits
  redistribution with attribution, and the pack records that; permission is not a plan.
