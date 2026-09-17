# ADR-257: The eye ranked them the way the spatial measure did, and the control in the frame was not a control

**Status:** Accepted
**Date:** 2026-09-17
**Context:** Quality Lab Phase 6 — validation against human assessment, on a moving camera
**Follows:** ADR-243 (the detector and the eye disagree), ADR-253 (the residual knows where the pixel
came from), ADR-254 (a benchmark that cannot show the artifact), ADR-182 (a probe that cannot fail
proves nothing), ADR-170 (the lock does not establish exclusivity)
**Narrows:** ADR-253's claim that the temporal measure agrees on a moving camera
**Corrects:** ADR-254's description of the orb as a control, and
`examples/quality/aliasing*.scene.json`'s `_note` that says the same thing

## Context

Every phase of the Quality Lab was built under one sentence, written in the README and meant
literally: *"No human has looked at anything this instrument has measured."* ADR-243 is the reason
the sentence was there — a correct, non-vacuous, well-controlled temporal detector ranked two
anti-aliasing remedies **backwards** against a reviewer — and ADR-253 closed by saying that the
agreement it found between two instruments *"is between two instruments, not between an instrument
and an eye."*

Phase 6 is a person looking. Three arms of `examples/quality/aliasing-dolly` — a free camera
tracking 4.4 m laterally in two seconds while holding aim on a fence corridor — at 1280×720, 60
frames, offline tier, presented **blind** as A/B/C with the mapping withheld until the answers were
written:

| blind label | arm | sequence hash |
|---|---|---|
| **A** | FXAA off (`--disable fxaa`) | `d8b30dba9e336e9c` |
| **B** | baseline (`post/output/antialias: 0.75`) | `b221cb2a46054c37` |
| **C** | `--supersample 2.0` | `861886575d058683` |

Three distinct hashes: no arm was vacuous. **All three were re-rendered for this ADR and came back
with the same three hashes**, so the frames measured below are the frames the person watched, which
is not something the numbers alone could have established.

**The prediction was registered before the reviewer looked**, which is what makes this an experiment
rather than a story told afterwards:

| arm | `detail.spatialLaplacian` | vs baseline | `temporal.temporalAlternation` | vs baseline |
|---|---:|---:|---:|---:|
| B baseline | 9.0103 | — | 20.5141 | — |
| A FXAA off | 9.5991 | **+6.5%** worse | 20.6341 | +0.59% |
| C supersample | 7.4912 | **−16.9%** better | 20.4111 | −0.50% |

Every number in that table reproduced to four decimal places on the re-render.

## Finding 1: on a moving camera, the spatial measure is validated against the eye

The reviewer ranked **C > B > A**, unprompted and blind:

> *"C — Best looking. A and B look about level at 1x speed animation, however in the close up you
> can clearly see B is a progression from A"* … *"To me it clearly went A: Good B: Better C: Best."*

That is `spatialLaplacian`'s ordering exactly, in both directions from the baseline. They located it
in the places the scene was built to put it — *"the far fences are the first visible noticible
improvement"* (their spelling), *"less artifacting between diagonal lines and outer fences"* —
which is the fence crossing the sampling limit inside one frame, ADR-254 §"The scenes".

`metrics.md` §4.2 made ADR-243's three arms a mandatory gate on any metric set. **The gate now has a
human behind it on a second scene and a second kind of motion**, and the metric that passes it is the
same one that passed it on Glowmere.

### The measure's sensitivity has two data points, and they are a bracket, not a curve

The reviewer's Q1 is the most useful sentence in the review, because it is not a ranking:

> *"A and B look about level at 1x speed animation, however in the close up you can clearly see B is
> a progression from A"*

and against C, asked how long it took to be sure: *"Immediately obvious"*.

So a **6.5%** `spatialLaplacian` difference was **not visible at 1× playback** on this moving camera
and *was* visible in a frozen close-up; a **16.9%** difference was immediately obvious in motion.
That is one point in each direction on one shot with one reviewer, and it is recorded as a bracket
because it is the first calibration this instrument has ever had of *how much of its number a person
can see.* It is not a threshold and must not be quoted as one.

## Finding 2: the temporal measure did not invert — it went uninformative exactly where the eye was certain

ADR-243's failure was **inversion**: on wind-animated sub-pixel geometry under a *static* camera,
`temporalAlternation` ranked FXAA-off 42% *better* and supersampling 9% *worse*. On a moving camera
that does not reproduce. Nothing here is backwards.

But "flat" is not the right description either, and the difference matters. Two arms of one
experiment are rendered from the same camera at the same times, so their per-frame metrics are
**paired** — and a difference of means that does not survive the pairing is a difference nobody can
act on. `avgen_quality analyze --per-frame` was added to make that comparison possible at all:

| comparison | `spatialLaplacian` | `temporalAlternation` |
|---|---|---|
| A worse than B | **60 of 60 frames**, mean +6.98% | **58 of 58 frames**, mean +0.62% |
| C better than B | **60 of 60 frames**, mean −16.90% | **32 of 58 frames**, mean −0.74% |
| A worse than C | 60 of 60 frames, mean +28.81% | 36 of 58 frames, mean +1.44% |

Read the second row. On the arm the reviewer called *"immediately obvious"*, *"Best"* and
*"shippable"*, the temporal measure is **a coin flip** — it calls supersampling worse on 26 of 58
frames. On the arm where the reviewer hesitated at 1× speed, it is perfectly consistent and 0.62%
wide. **The two instruments' confidences are ordered opposite to each other**, even though their
means happen to agree, and that is a worse failure for the use the measure would be put to than a
number that is simply small.

For scale: within the baseline arm, both metrics vary **2.9×** from frame to frame. Between-arm
means are only readable at all because the frames are paired.

### And the agreement ADR-253 found was a property of its resolution

ADR-253 measured these same three arms at **640×360, 30 frames** and reported the temporal measure
agreeing at +2.7% / −5.4%, concluding *"the temporal measure agrees, which is not what ADR-243
found."* That result reproduces exactly. It does not survive the resolution change, and the
experiment that isolates it is one variable wide — the same 2-second window, the same 60 frames, only
the render size:

| | spatial A vs B | spatial C vs B | temporal A vs B | **temporal C vs B** |
|---|---|---|---|---|
| 640×360 | +8.40%, 60/60 | −17.65%, 0/60 | +2.31%, 58/58 | −2.97%, **14/58 wrong-direction** |
| 1280×720 | +6.98%, 60/60 | −16.90%, 0/60 | +0.62%, 58/58 | −0.74%, **26/58 wrong-direction** |

The spatial measure holds its size and its unanimity at both. The temporal measure's separation of
the supersampled arm shrinks 4× and degrades to chance. **Halving the resolution puts more of the
content below the sampling rate, so supersampling changes more pixels — the detector's effect size
is a property of the render size, not of the renderer.** The person watched 720p.

**Rule, and it is general:** an instrument's effect size is only evidence at the resolution it was
measured at, and the resolution that counts is the delivery one. A metric validated at preview size
has been validated for preview size.

## Finding 3: the control was not a control, the reviewer caught it, and they were right

The review's fourth question was a control: the large smooth orb *"should look identical in all
three"*, say so loudly if it does not. The answer was:

> *"No — ball looks smoother in C — less jagged"*

**They are right, and the control was specified wrongly.** A sphere's *interior shading* is
unaffected by anti-aliasing. Its *silhouette* is a curved edge like every other edge in the frame and
is affected exactly as much as the fence is. Calling the whole orb a control was an error in the
review's design — and in ADR-254, and in the scene's own `_note`, which both say *"it cannot alias,
so a metric that moves on this scene's arms must not be moving here."* Half of that sentence is true
and the other half licenses a wrong conclusion.

This repository prefers a measurement to an argument, so the claim was measured. `avgen_quality
control` splits an object out of the **identifier AOV** — exact, R32Uint — into an eroded
**interior** and a **silhouette band**, builds the mask from one arm and applies it unchanged to all
of them, and reports how far each arm's luma moves from the baseline's over each region:

| region of the orb (60 frames, band 2 px) | FXAA off vs baseline | supersample vs baseline |
|---|---:|---:|
| **interior** mean \|Δluma\| | **0.0000** | 0.1486 |
| **interior** max \|Δluma\| | **0.0000** | 2.7152 |
| **silhouette band** mean | 2.6981 | 5.3941 |
| **silhouette band** max | 122.26 | 140.63 |

The orb's interior is **bit-identical** with FXAA switched off — at band 1, 2, 4 and 8 px, so FXAA
touches this object only within *one pixel* of its silhouette. Supersampling moves the interior by
0.15 of a luma step, which is the resample of a smooth shading gradient and nothing else. The
silhouette moves by 36× that, with peaks over 120 steps — half the range.

**Two controls make those numbers mean something** (ADR-182: a probe that cannot fail proves
nothing):

* **The interior is a region where things can change.** The same interior mask applied to
  *consecutive frames of the baseline* moves **2.90** mean and **144** max — 20× the difference
  between arms. A zero between arms there is a statement about the arms, not about a dead region.
* **The interior measure is not structurally zero.** The same split run on the scene's other objects
  gives interiors of 2.75 (the diagonal blades, FXAA off) and 6.69 (the right fence, supersampled).
  Sub-pixel geometry has no invariant interior, because a 2-pixel erosion of a structure thinner than
  a pixel still contains edges. The orb is the only object in the frame whose interior holds still.

The geometry of the two objects says why in one line: **the orb is 94% interior and its silhouette
band is 11% of its area; the right-hand fence is 21% interior and its silhouette band is larger than
the fence itself.**

### Does this weaken the ranking result? No — and it is worth saying why not

It would have weakened it if the reviewer had reported the orb as identical. Then the review would
carry an answer that is *wrong on the physics*, and every other answer would have to be re-read as
possible agreement with a suggestion. What happened is the opposite: handed a region the instructions
said could not differ, the reviewer reported the difference that is actually there, on the object
where it is hardest to see, and described it with an edge word — *"less jagged"*. That is an
independent check that they were resolving silhouette quality and not reporting an impression.

**What the review genuinely lacks is a negative control**, and the orb was never going to be one.
There is no object in an aliasing scene that a person can look at and find invariant, because
anything visible has a silhouette. The control for a human A/B review is not a region of the frame:

> **The control for a human comparison is a duplicated arm.** Show one arm twice under two labels
> and ask whether they differ. A reviewer who reports a difference between a clip and itself has
> told you the review's resolution, which is the only thing that calibrates every other answer.

That is the review-level form of the rule the harness already runs on the render side — *two arms
that hash identically are void, not equal.* In a human review, two arms that hash identically are
precisely the arm you need, and it costs one extra clip of watching time.

## Decisions

### 1. `spatialLaplacian` is validated for ranking anti-aliasing arms within one view, and says so in its own output

Its limitation list gains the Phase 6 result and the sensitivity bracket, in the report itself rather
than only in a document. Everything `metrics.md` already forbids it — comparing across scenes, being
an absolute bar, separating aliasing from detail — is unchanged, because none of that was tested.

### 2. `temporalAlternation` is not demoted further, and its caveat is made accurate

It was already forbidden from deciding an anti-aliasing question alone, and forbidden from being
emitted without `spatialLaplacian` beside it (enforced by `pairingViolation()`, not by discipline).
Phase 6 does not change what it may be used for, so nothing is removed. What changes is that its
caveat **said the wrong thing**: it claimed anti-correlation "over moving geometry", which blurs the
two cases this pair of ADRs exists to separate. It now names ADR-243's case as a static camera over
wind-animated geometry, and records Phase 6's: on a moving camera it does not invert, and it is a
coin flip on the arm a person had no difficulty ranking.

### 3. The orb's role is corrected everywhere it is claimed, and narrowed to what was measured

`benchmark-scenes.md`, ADR-254 and both `aliasing*.scene.json` files say the orb is a control. They
now say what is true: **the orb's interior is the control; the orb is not.** The distinction is not
pedantic — it is the difference between "a metric that moves on these arms must not move here",
which is false and would have been used to dismiss a correct detector, and "a metric that moves on
these arms must not move in this object's *interior*", which is measured and holds to 0.15 luma
steps.

### 4. `avgen_quality control` and `analyze --per-frame` exist because both findings needed them

Neither is a metric. `control` is the region split above, with its two ADR-182 arms built in and its
coverage printed beside every row. `--per-frame` writes the series that pooling consumes, because the
pooled fields answer *"how bad is the worst frame"* and cannot answer *"is arm A worse than arm B on
**this** frame"*, which is the question that separated Finding 2 from a rounding error.

### 5. The human-review protocol is written down

Blind labels with the mapping withheld; arms that hash differently; a duplicated arm as the control;
questions that ask for a ranking, a magnitude, a location and a shipping decision separately; and the
instrument's prediction registered before the reviewer looks. `metrics.md` §4.4 now carries it, and
the README's open human decision — *"the human-validation protocol and its reviewer budget"* — is
closed.

## Consequences

**The shipping question is answered and it has two halves, both actionable.** Asked whether they
would ship these, the reviewer said: *"I would object to the worst clip (A) — I think a full scene of
small details would be very jagged looking. C is good enough to not worry about."* FXAA-off is
objected to, which is ADR-243's decision 1 re-confirmed by a different person on different content;
and supersampling is **sufficient**, which is the narrower question ADR-243 left open when it turned
on `supersample: 2.0` for the Glowmere deliverables and could not say whether it was enough. It is.
Note what the objection to A actually rests on — *"a full scene of small details would be"* — which
is an extrapolation from this scene, offered as one, and is recorded as one.

**Two documents were wrong before this and are wrong in different ways.** The README said no human
had looked at anything the instrument measured; that is now false and is rewritten. ADR-254 and the
scene files said the orb was a control; that was false when written, and it was found not by
re-reading it but by a person answering a question honestly and being disbelieved for about a minute.

**The cheapest instrument in this phase was the question that invited the reviewer to contradict the
instructions.** It cost one line of a review brief and it caught a mis-specified control that had
been copied into three files.

**What this does not say.** It does not say `spatialLaplacian` is validated *generally*. One scene,
one motion, one reviewer, one blind ranking, at one resolution. A **static camera on this same scene
was not reviewed**, and ADR-243's inversion was found on a static camera — so the case that produced
the original failure is still untested by this instrument. No other scene has been reviewed at all,
and nothing here validates `motionCompensatedResidual`, `disocclusionFraction`, `msSsim`, CAMBI or
VMAF against a person; ADR-253's warning stands unmodified for every one of them. It does not say the
temporal measure is useless — it says it did not rank these three arms in a way anyone could act on,
at the resolution that was watched, which is the second time it has failed to rank arms a human
ranked without difficulty. And it does not say the orb's interior is invariant under *any* change:
it is invariant under these two, measured, at this size.
