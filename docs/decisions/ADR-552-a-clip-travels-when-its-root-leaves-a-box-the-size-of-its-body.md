# ADR-552: A clip travels when its root leaves a box the size of its body

**Status:** Accepted
**Date:** 2026-09-20
**Related:** ADR-337 (which joint carries travel), ADR-385 (a stated reason is not evidence),
ADR-540 (the motion database does not exist; every locomotion clip is in place), ADR-546 (a planted
foot is low and still, not stationary), ADR-549 (BVH is how motion gets in)
**Implemented by:** `src/scene/motion_analysis.{hpp,cpp}` — `measureRoot`, `ClipAnalysis::travels`,
`ClipAnalysis::travelAmbiguous`
**Tests:** `tests/unit/test_motion_analysis.cpp` — "a clip that ends where it began still travels",
"a foot that shuffles without lifting is still stance and swing", "the whole pack is in place"

---

## Context

ADR-546 makes a foot's contact test conditional: a clip that **travels** gets the textbook test
(a planted foot is stationary in both components), and a clip authored **in place** gets a
vertical-only test, because an in-place cycle's stance foot sweeps backwards under the hips by
design and a horizontal test would find the swing foot and call it the plant.

The switch was therefore load-bearing from the day it was written, and it was written against
content that could not exercise it. ADR-540 had already established that **every** locomotion clip
in this repository is authored in place, so the "travels" branch had never run.

## The three readings

The first implementation asked for **net displacement over duration**: where the root ended up,
minus where it started, divided by how long it took. Threshold 0.05 m/s.

That is correct for a clip that goes one way, and wrong for almost every motion-capture take ever
recorded, because a subject in a capture volume turns round and comes back. Measured on 100STYLE's
`Neutral_FW` — **131 seconds of continuous forward walking**:

| reading | value |
|---|---|
| net displacement | **0.508 m** |
| implied speed | **0.004 m/s** |
| path actually covered | **93.030 m** |

Against the 0.05 m/s threshold that is standing still. The largest shippable locomotion corpus in
existence classified as in-place, file after file, and the horizontal arm never ran on the only
data that needed it. The cost, measured as worst foot slide within a contact span, same slide
definition on both sides:

| clip | net-displacement rule | extent rule |
|---|---|---|
| `Neutral_FR` | 0.662 m | **0.052 m** |
| `Neutral_FW` | 0.370 m | **0.062 m** |
| `Neutral_TR1` | 0.232 m | **0.059 m** |
| `Neutral_BW` | 0.169 m | **0.070 m** |
| `Neutral_SW` | 0.134 m | **0.044 m** |
| `Neutral_ID` | 0.070 m | **0.043 m** |

**Path length over duration** is the obvious repair and it breaks the other half. An in-place
cycle's root does not sit still: it sways, bobs and leans, and the sway has a length. Measured on
this repository's own clips, all of which ADR-540 established are in place: `Running` **0.330 m/s**,
`Fight_leg_kick_1` **0.535 m/s**, `Fight_leg_kick_2` **0.387 m/s**. All three read as travelling.

Neither reading is a measurement error. They are two different questions, and neither is the
question the switch is asking.

## Decision

**A clip travels when the diagonal of its root's horizontal bounding box exceeds the skeleton's own
rest height.**

* **Extent, not displacement or path.** An in-place cycle's root is *bounded*: it can sway for an
  hour and never leave a box the size of the body. A travelling take's root is not. That is the
  property the switch is about, and it is the only one of the three that is invariant to how long
  the clip is and to which way the subject was facing when it ended.
* **Against the body's own rest height, not a distance in metres.** The same rule then judges a
  1.66 m alien and a 1.79 m human, and no number in this file has to be revisited when a character
  changes scale.
* **One implementation.** `measureRoot` is the only place that answers it; `detectContacts` and
  `analyseClip` both call it. Two answers to "where did the body go" is how ADR-260 started.
* **The travel joint is ADR-337's, not the skeleton root.** On the alien, `rig` is an armature
  wrapper no clip animates.

### And it says when it is guessing

`ClipAnalysis::travelAmbiguous` is true when the ratio lands between **0.7** and **1.4**. Measured,
that band is empty: this repository's in-place clips top out at **0.568** (`Dying_forward`, because
a body falling forward really does move a metre) and 100STYLE's least mobile file, a 35-second
idle, comes in at **1.80**. A clip in the band resembles neither population.

The classification is still made — a contact detector cannot abstain — but a survey or a pack build
can report that the answer is a guess rather than presenting it as a reading. A diagnostic that is
confidently wrong is worse than one that says it does not know, because a wrong answer looks like a
data problem for as long as it takes someone to doubt it.

## Consequences

* ADR-546's conditional test now runs both arms on real content, and the in-place arm keeps the
  behaviour ADR-540's content depends on.
* `ClipAnalysis` reports `rootTravel`, `rootPathLength`, `rootExtent`, `restHeight`, `groundSpeed`,
  `travels` and `travelAmbiguous`. `groundSpeed` is now path over duration and is **reported, not
  thresholded** — it is how fast the body moved, which is a useful number and not this decision.
* The assertion that ADR-540's pack is in place is now on `travels`, where the claim is, rather
  than on a speed that happens to be small.
* **Revisit if** a corpus turns up whose takes are short enough that a genuine walk stays inside one
  body height, or a character whose in-place idle wanders more than one. Both would land in the
  ambiguous band first, which is what the band is for.
