# ADR-206 — Structure from repetition, not from energy

**Status:** accepted · 2026-09-14
**Complements:** ADR-063 (musical moments folded into sections)

## Context

ADR-063 folds musical *moments* — builds, drops, energy trends — into `signals::MusicalStructure`.
It is good at the thing it was built for, which is EDM-shaped dynamics, and it has one structural
blind spot: **it has no way to notice that bar 33 and bar 97 are the same music.** Verse 1 and
Verse 2 are unrelated to it. Repeated choruses are unrelated to each other.

The requirement is an editable song structure laid onto the Sequencer: sections a user can drag,
rename and refine, generated from a first-pass reading of the audio. An energy fold cannot produce
that, because most of what makes a section a section is repetition.

## Decision

A second, complementary detector — `src/analysis/structure.{hpp,cpp}` — built on established Music
Structure Analysis rather than on thresholds. Foote (2000) for the novelty kernel, FMP C4S4 for
novelty-based segmentation, librosa's `recurrence_matrix` for the repetition half.

```
per-frame features (existing Analyzer)
  -> chroma + timbre, folded from the magnitude spectrum
  -> aggregated onto the beat grid (existing Ellis beat tracker)
  -> self-similarity matrices, one per feature family
  -> Foote novelty, several kernel widths
  -> peak-picked candidate boundaries, on beats
  -> recurrence grouping: which segments are the same music
  -> functional labels from structural evidence
```

### Almost none of the DSP is new

The decisive finding, from reading `src/analysis/` before designing anything:

* `AnalysisFrame::magnitude` already carries linear bin magnitudes per frame, so chroma and a timbre
  vector fold out with no new FFT infrastructure.
* **Ellis (2007) dynamic-programming beat tracking is already implemented** (`trackBeatsOffline`),
  producing whole-track beat times.

That second one decides the architecture. A four-minute track is ~20,000 analysis hops, and a dense
self-similarity matrix over hops is 400 million cells. Over beats it is ~500 × 500. Beat-synchronous
aggregation is what makes the whole approach affordable, and the grid was already there.

A track without a beat grid is **refused**, not analysed at hop resolution: that would be a
different algorithm returning results under this one's name, and nothing downstream could tell which
it had been given.

### Boundaries and labels are separate claims

`SongSection` carries `startConfidence`, `endConfidence` **and** `labelConfidence`, because a
boundary can be certain while its label is a guess — that is the normal case, not a degenerate one.
On the Glowmere track the detector reports a `verse` at label confidence 0.92 whose opening boundary
is at 0.09: it is fairly sure what that section is and not at all sure where it starts. Collapsing
those into one number would have thrown away the more useful half.

`SectionFunction::Other` carries a confidence of **zero**, and that is not a low score — it is the
absence of a claim. On an ambient or through-composed piece the honest output is good boundaries
with no verse/chorus labels, and the detector takes that branch explicitly when it finds no repeated
material anywhere.

### Two metrics, and one deliberately absent

`energy` and `density` are measured, then rescaled against the track's own distribution, because
"loud" only means anything next to the rest of the piece.

**There is no `tension` field.** It was asked for, and there is no derivation available here that is
not a formula dressed up as a measurement. An absent metric is better than an invented one; this
project has already paid for the opposite choice.

### Provenance, because re-analysis must not destroy work

`SectionOrigin` is `Detected`, `Refined` or `Authored`. Re-running analysis may replace what the
analyser guessed and must leave alone what a person decided, and that is not expressible unless each
section remembers which it is.

## Two bugs the tests found

**The first section started at the first beat.** A beat grid begins at the first beat the tracker is
confident about — 0.49 s into the test fixture — and the audio before it is music somebody wrote.
`validate()` could not catch this: the structure was ordered, gapless, non-overlapping and
internally consistent, and simply started in the wrong place.

**Peak-picking returned nothing on a curve with obvious peaks.** The threshold is median plus a
multiple of the median absolute deviation, which is the right statistic for a real novelty curve.
A MAD of zero means *more than half the samples are identical*, which is not the same as "flat" — a
constant curve with three sharp spikes has a MAD of zero and three obvious boundaries. It now falls
back to the range above the median, and a genuinely flat curve still returns nothing.

## Testing something that has no ground truth

There is no annotated corpus in this repository, and a test asserting the detector finds a chorus at
1:02.4 is a test of its current opinion. It would pass today, fail on the first improvement, and be
"fixed" by writing down the new opinion — a ratchet, not a test.

So, two kinds of assertion and no others:

**Structural invariants** that hold for any input: ordered, gapless, non-overlapping, covering the
track, deterministic, and — the precision requirement — every boundary is *exactly* a beat time,
bit for bit.

**Synthetic fixtures whose construction is known.** A signal built as `A B A B C A` at a fixed
tempo, with distinct harmonic and timbral material per letter and a click on every beat, has
boundaries at times the test placed and a repetition structure the test built.

The precision test is worth recording because its first draft was wrong in an instructive way. It
asserted "some boundary is not a whole number of seconds", and failed against perfectly correct
output: at 120 bpm a beat is exactly 0.5 s and a 16-second section is exactly 32 beats, so every
honest boundary *is* a whole number. It was testing the fixture's tempo. The invariant is that a
boundary is a beat time — which holds at every tempo, and is the property rounding would break.

Detection quality on real music is **reported, not asserted**: `"Structure of the Glowmere track"`
is a hidden `[.report]` test that prints what the detector thinks so a person can listen and judge.
Its current reading of the 90-second Glowmere track: 122.6 bpm, three sections, the first and last
grouped as the same repeated material.

## Consequences

`signals::MusicalStructure` is unchanged and still drives the camera director. This detector answers
a different question and feeds the Sequencer's editable regions; when those land, the two need a
stated relationship rather than a silent overlap.

The labelling heuristics are the weakest part and are known to be. The Glowmere reading opens on
`chorus` where a listener would probably say `intro`, because the opening material is loud and
recurs. That is a labelling-quality question, it is visible in the report, and per the rule above it
is not something to pin down with an assertion.
