# The Song Analyzer

What happens between opening an audio file and having an editable film. Written against the code as
of 2026-09-16; the decisions behind it are ADR-004, ADR-063, ADR-206, ADR-215, ADR-216 and ADR-247.

Its companion is `docs/section-shot-language.md`, which describes the vocabulary this produces.

## The pipeline

```
  audio ──▶ Analyzer ──▶ AnalysisTrack ──▶ detectStructure ──▶ SongStructure
            (STFT, per hop)  (+ beat grid)   (background job)   (the report)
                                                                     │
                                                       timelineFromStructure
                                                                     ▼
                                                           song::SectionTimeline
                                                              (the film)
```

Three stages, three ADRs, and one property that runs through all of them: **every stage is a pure
function of the one before it.** There is no wall clock, no rng and no thread scheduling anywhere in
it, so the same audio produces the same sections on every machine and in every run. That is a
requirement rather than a nicety, because these boundaries get saved in a project and a person edits
them.

### Per-hop features (ADR-004)

`analysis::Analyzer` is a streaming STFT extractor: RMS, peak, per-band energy, spectral centroid,
half-wave-rectified spectral flux, and peak-picked onsets. Deterministic — identical input samples
produce identical frames regardless of how the input was chunked. No smoothing is applied; raw
features are published and shaped downstream.

### The beat grid

`OfflineBeats` (Ellis 2007) over the whole track. Everything structural is beat-synchronous, and not
for musicality — for tractability. A four-minute track is ~20,000 analysis hops, and a dense
self-similarity matrix over hops is 400 million cells. Over beats it is ~500 × 500.

The precise timestamps are kept throughout: a boundary is reported at the beat's **own** time in
seconds, never rounded.

### Structure detection (ADR-206)

```
  chroma + timbre, folded from the magnitude spectrum
    -> aggregated onto the beat grid (median, so one transient does not decide a beat)
    -> self-similarity matrices, one per feature family
    -> Foote novelty, at several kernel widths (16, 32 and 64 beats)
    -> peak-picked candidate boundaries, snapped to beats
    -> recurrence grouping: which segments are the same music
    -> functional labels from structural evidence
```

Structure from **repetition and change**, rather than from energy alone — which is what
`signals::MusicalStructure` (ADR-063) already did well for EDM-shaped dynamics and structurally could
not do for "bar 33 and bar 97 are the same music".

References, used to inform rather than to reproduce: Foote (2000), *Automatic Audio Segmentation
Using a Measure of Audio Novelty*; Müller, *FMP* C4S4; librosa's `segment.recurrence_matrix`; MIREX
Structural Segmentation. Essentia's `MusicExtractor` and `RhythmExtractor` were read for what they
compute and deliberately not adopted: everything the segmentation needs — chroma, a timbre proxy,
onset strength, a beat grid, band energies — the engine already computes per hop, and adding a second
feature pipeline would have meant two copies of the analysis that drift apart.

### Where and when it runs (ADR-216)

On the `JobSystem`, never per frame and never on the UI thread. It runs **automatically once**, when
audio arrives and the piece has no structure yet. A project saved with a structure is not re-analyzed
on open — that is what caching it is for.

The result carries the `audioRevision` it was started for and is discarded if the audio changed while
it ran, so opening two files quickly cannot put one song's sections on the other. The revision is
claimed when the job *starts*, not when it finishes.

Progress is the stage name and nothing else. `detectStructure` does not know how far through itself it
is, and a bar invented in the UI is indistinguishable from a measured one.

## Two questions, two confidences

**Where** a section boundary is and **what** the section is are separate questions with separate
answers, and nothing here returns a label without saying how much to trust it.

A boundary can be certain while its label is a guess. That is the normal case, not a degenerate one,
and it matters most for genre robustness: on an ambient piece the honest answer is "here are five
sections, A B A C A, and I do not know what to call them". A detector that reports Verse and Chorus
there is not more useful — it is wrong with confidence.

So `analysis::SectionFunction` has a value called `Other`, and it is **not** a failure or a fallback
for bugs. It is the correct answer for music that does not have verses and choruses. A section
labelled `Other` with a good boundary and a repetition group is more useful than a confident wrong
label.

### A confidence is a claim about a guess

Which means it stops meaning anything the moment a person overrules it. `song::labelConfidenceIsMeaningful`
and `song::boundaryConfidenceIsMeaningful` are what a UI asks before displaying one, and they are
answered **per field**: retyping a passage retires the claim about its label and leaves the claim
about its position alone, because those were two separate claims.

The numbers are not cleared — deleting the detector's record would be a second kind of data loss —
and they are not written to the project once they have stopped meaning anything, so the next reader
cannot display a number about nothing.

There is deliberately no `tension` field. It was asked for, and there is no derivation for it that is
not a formula dressed up as a measurement, so it is absent rather than fabricated.

## From an analysis to a film

The detector produces a report. `song::timelineFromStructure` turns it into a `SectionTimeline`, and
that is where the analysis stops and the film starts.

Every section comes out **Detected**, with no fields edited, carrying its measurements and its
confidences, and with **no shot-intent override** — so every one resolves through its type's default
and the person immediately has a complete first-pass treatment for the whole piece:

```
  INTRO        00:00-00:18   Atmospheric Establishing
  VERSE        00:18-00:43   Hero / Character Coverage
  PRE-CHORUS   00:43-00:57   Building Tension
  CHORUS       00:57-01:23   Dynamic Hero Coverage
  VERSE 2      01:23-01:48   Hero / Character Coverage
  BRIDGE       01:48-02:12   Visual Departure
  CHORUS 2     02:12-02:46   Dynamic Hero Coverage
  OUTRO        02:46-03:10   Slow Pullback / Resolution
```

Shot boundaries are section boundaries, exactly — the same `double`, not a rounded copy.

Two things about the mapping are worth stating because they are easy to get wrong:

**`FinalChorus` maps to the plain `chorus` type.** "Final" is a property of *position*, not of the
music, and `SectionCue::finalOfKind` derives it. A `final_chorus` type would mean a piece with two
choruses and a piece with five needed different vocabularies, and it would leave the last Ocean
Ambience with no way to be the last one.

**The detector's label is not copied into `Section::label`.** A label a person did not type is not a
person's name for the passage. Copying it would mark every section as renamed and freeze the entire
timeline against the next re-analysis.

## Manual refinement

Everything the analysis produced is a starting point. All of it can be changed, and each operation
records exactly what it touched:

| Operation | Marks |
|---|---|
| `moveBoundary` | `End` on the earlier section, `Start` on the later one |
| `splitSection` | `End` on the earlier half; the later half becomes **Authored** |
| `mergeSectionWithPrevious` | `End` on the survivor |
| `removeSection` | the boundary field on whichever neighbour took the span |
| `insertSection` | the new section is **Authored**; neighbours are *not* marked |
| `setSectionType` | `Type` |
| `setSectionLabel` | `Label` — and *un*-marks it when renamed back to nothing |
| `setSectionShotIntent` | `ShotIntent` |
| `clearSectionShotIntent` | *un*-marks `ShotIntent` |

Boundary times are stored exactly as given. A boundary snapped to a beat takes that beat's own value,
bit for bit, because snapping assigns the beat's value rather than computing a nearby one. Nothing in
the model, the editing operations or the JSON rounds a time at any point — and the tests compare them
bit for bit rather than within a millisecond, because a tolerance would pass against a serializer that
quietly rounded to three decimals and a timeline forty milliseconds out everywhere still looks
perfectly reasonable in a UI.

## Re-analysis: what happens when you press Analyze again

The equation, implemented literally:

```
  analysis result  +  user overrides  =  current authored song structure
```

`song::reanalyze(current, fresh)` folds a fresh detection into an authored timeline.

**1. A fresh detection with no sections changes nothing**, and reports nothing. Refusing to act on
empty information is much better than emptying somebody's timeline because the detector had a bad day.

**2. Frozen sections keep their whole span and all their fields.** A section is frozen when it was
authored from nothing, or when either of its boundaries was moved. The fresh detection is cut around
it: a fresh section that straddles a frozen one becomes two pieces either side, and one the frozen
section covers completely is dropped. Where a trimmed section and a frozen one disagree about where
they meet, the frozen one wins.

Pinning *one* boundary freezes the *whole* span. That is deliberately conservative — a section is a
span, and half of one is not something a fresh detection can be carved around.

**3. Everything else is replaced, except the fields a person set.** A section whose type, name or
treatment was chosen — but whose boundaries were left alone — hands those fields to the fresh section
it overlaps most, and the fresh boundaries win.

This is where ADR-247 goes past ADR-215. ADR-215's provenance is per section: touch anything and the
whole section freezes, boundaries included. That is right for a model with only boundaries and labels,
and too blunt once a section also has a type and a treatment. Renaming a passage to "Ocean Ambience"
is not a claim about where the passage starts, and freezing its boundary because of it would mean a
re-analysis could never improve a boundary on any section anybody ever renamed.

**4. At most one current section may hand its fields to any one fresh section.** Greedily by overlap,
largest first, ties broken by index so the result is a pure function of its inputs. Without that cap,
two edited sections that a coarser fresh detection merges into one would both write to it and the
second would silently win — data loss wearing the costume of a successful merge. The loser is counted
in the report's `discarded` rather than vanishing.

**Pressing it twice is safe.** The second run has nothing left to change, because the first run's
result already *is* the fresh detection with the edits folded in.

### The report

```cpp
struct ReanalysisReport {
    int sectionsReplaced, spansFrozen, fieldsCarried, sectionsCarried;
    int freshTrimmed, freshDropped, discarded;
    std::vector<std::string> kept;   // "1:02.40-1:31.80 Ocean Ambience (type, shot)"
    std::string summary() const;
};
```

This exists because "we kept your edits" is a *claim*, and a claim about data loss should be reported
rather than assumed. If a re-run silently dropped a refined chorus, nothing in a UI would say so.

### Starting again on purpose

`ReanalysisPolicy::Replace` discards the authored timeline and takes the fresh one. It is never the
default. `previewReanalysis(current, fresh, policy)` returns the report **before** anything is
touched, so a confirmation dialogue can say how much is about to go — and because the preview is
literally the same call run against a copy, it cannot come to disagree with what the button does.

## Two layers, and a wart

A project carries both `sequence.structure` (the analyzer's report) and `sequence.sectionTimeline`
(the film). They are not the same object on purpose: only one of them is safe to recompute.

**They currently have two editing APIs, and only one of them is wired to the UI.** The Sequencer's
section lane drives `seq::moveBoundary` and friends, which edit `analysis::SongStructure` under
ADR-215's per-section policy. The authored equivalents exist on `SectionTimeline` with a richer
vocabulary and per-field provenance, and the section lane has not been moved onto them yet — that is a
UI change owned elsewhere. Until it happens, **a person editing the section lane is editing the
analysis layer, and those edits do not reach the film.**

Pressing Analyze updates both, under their two policies, via `song::applyAnalysis`.

A project saved before the shot language existed has an analysis and no film; opening it derives one.
A project that already has a timeline is never re-derived on load — that would be a silent
re-analysis, which is the one thing this whole model exists to prevent.

## What is not attempted

Following the brief's own list, and worth stating so nobody looks for it: no genre classification, no
lyric understanding, no full harmonic analysis, no machine-learned cinematography, and no automatic
semantic understanding of an arbitrary custom section description. "Ocean Ambience" gets whatever
treatment the person pointed it at; the engine does not read the words.

The detector also remains imperfect, deliberately. The architectural goal was that its output become
useful enough to drive a visual sequence and correctable by hand — not that it become right.
