# ADR-215 — A section somebody decided outranks a section somebody detected

**Status:** accepted · 2026-09-15
**Builds on:** ADR-206 (structure from repetition), ADR-063 (moments folded into sections)

## Context

ADR-206 landed a real MSA detector and, deliberately, stopped there. It computes; it does not
remember. `SongSection` already carried `SectionOrigin` — `Detected | Refined | Authored` — and
nothing in the codebase honoured it, because nothing stored a structure for long enough to have an
origin worth honouring. `Sequence::setSectionMarkers` *replaced* the Section markers from a fold, so:

* a boundary dragged from 1:02.4 to 1:03.1 survived nothing;
* re-running analysis destroyed refinements silently;
* nothing distinguished the analyser's guess from a person's decision.

ADR-206 also left one thing explicitly open: `signals::MusicalSection` and
`analysis::SectionFunction` overlap, and "the two need a stated relationship rather than a silent
overlap".

## Decision

### The structure belongs to the sequence

`seq::Sequence` carries an `analysis::SongStructure`, and it goes into the project inside the
sequence's own JSON. It is **not derived**: the markers are derived from it (`refreshSectionMarkers`)
and the beat markers are still derived from the track, but the structure itself is authored data the
moment anybody touches it.

`beatTimes` and `novelty` are *not* written. They are a copy of the analysis, they are large, and
they go stale the instant the audio changes — the same argument that already stops `toJson` writing
thousands of Beat markers. They come back from the track.

The confidences are written **only for `Detected` sections**. On a refined one they are not a smaller
claim, they are not a claim at all, so persisting them would invite the next reader to display a
number about nothing. The in-memory values are left alone — deleting the detector's record would be a
second kind of data loss — and `confidenceIsMeaningful()` is what a UI asks before showing one.

### The re-analysis policy

`seq::reanalyse(current, fresh)`: **`Detected` sections are replaced; `Refined` and `Authored` ones
are kept exactly, and the fresh detection is cut around them.** A fresh section that straddles a kept
one is trimmed rather than dropped, which is what keeps the result ordered, gapless and covering.
Where a trimmed section and a kept one disagree about where they meet, the kept one wins.

A fresh detection with **no** sections leaves `current` completely alone. Refusing to act on empty
information is much better than emptying somebody's structure because the detector had a bad day.

It returns a `ReanalysisReport` — what was replaced, what was kept, and the kept spans in clock time
— because "we kept your edits" is a claim, and a claim about data loss should be reported rather than
assumed.

#### The consequence of section-level provenance, stated rather than hidden

Provenance is per *section*, which is ADR-206's model. So dragging one boundary marks the two
sections it separates, and that protects their **outer** boundaries too — three boundaries frozen by
one drag. This is over-protective, and it is the deliberate direction to err in: the failure it
prevents is losing work, and the failure it causes is a re-analysis that changes less than it could.
Per-boundary provenance would be exact and is the upgrade if this ever gets annoying.

### The two vocabularies

Stated, finally, and one-way:

| | `analysis::SectionFunction` | `signals::MusicalSection` |
|---|---|---|
| describes | the music | what the camera should do |
| may decline | **yes** — `Other` is the correct answer for a piece with no verses | **no** — a shot has to be *some* shot |
| carries confidence | yes | no |
| parsed from a detector | yes | never |

`seq::sectionKindFor` maps analysis → director, totally, and **never back**: going back would invent
a musical claim out of a directorial one, and `FinalBuild`, `FinalDrop` and `Phrase` have nothing to
invent from. `Other → Phrase` is the load-bearing line: "an ordinary passage" is the honest image of
"I do not know", and nothing else maps to `Phrase`, so the two stay distinguishable.

`sectionKindsFor(structure)` adds the one thing a lone section cannot know — where in the piece it is
— and promotes the last Build/Drop/Chorus past 55% to the Final variant, because the last drop is a
different shot from the first and the same music.

### Six more director sections, and four switches answered

`MusicalSection` gains `PreChorus`, `Chorus`, `Break`, `Bridge`, `Instrumental`, `FinalChorus`: the
pop/rock half of a vocabulary that was EDM-shaped. Extending the enum was the cheap part. The risk
was entirely downstream, in the four tables that switch on it, where a new kind falling through a
`default` produces a silently bland shot — a failure that looks like nothing at all.

So all four were answered one kind at a time:

* `shotKindForSection` — PreChorus→Approach, Chorus→HeroReveal, Break→Drift, Bridge→Transition,
  Instrumental→Orbit, FinalChorus→Reveal.
* `isDropSectionKind` — **Chorus and FinalChorus join Drop and FinalDrop.** In a song with words the
  chorus *is* the payoff, and the whole reason to read the structure is that the cut lands where the
  music lands. A track has drops or it has choruses, so they are one tier rather than two. The cost
  is accepted and visible: four choruses means four hard cuts.
* `mayBeSplitSection` — Bridge and Instrumental are passages and may be more than one shot; PreChorus
  is a run-up and is not cut into, like a build; Break and Chorus must be held.
* `emphasisForSection` — Chorus takes the Drop weights and FinalChorus the FinalDrop weights exactly.
  A second, weaker tier would be a claim that a chorus matters less than a drop *in the piece it is
  actually in*. `PreChorus` is zero because plain `Build` is zero: only the *final* run-up carries
  the hero.

Two of the four are now exhaustive switches with no `default`, so `-Wswitch` makes a new enumerator a
compile-time problem at the site of the bug. The other two cannot be — `Establish` and an emphasis of
zero are legitimate answers *and* the values a fall-through produces — so the tests pin exactly which
kinds hold them, which is what makes a fall-through visible. `kSectionNames` is `static_assert`ed
against the last enumerator, so a kind cannot be added without a name.

## What the tests assert, and what they refuse to

Following ADR-206: structural invariants and constructed fixtures, never the detector's opinion.

The §17 test is the point of the exercise and is written to fail if it regresses: a person moves a
boundary, the detector returns four completely different sections, and afterwards the moved boundary
is *exactly* where the person put it while a boundary with detected material on both sides has been
replaced by the fresh one. Both halves, because "kept everything" and "replaced everything" each pass
half of it.

Boundary times are compared **bit for bit**, not within a millisecond. A tolerance would pass against
a serialiser that quietly rounded to three decimals, which is precisely the bug worth catching.

One honest limit found while writing them: `signals::StructureSection` stores a start and a
*duration*, so its computed `endSeconds()` is `start + duration` and need not reproduce the authored
end in the last bit (~7e-15 s). The start is copied verbatim, and the start is what a section trigger
fires on and what the project stores, so that is the value asserted exactly and the other is asserted
to a margin — recorded here rather than papered over.

## Consequences

Everything built on `signals::MusicalStructure` — the shot planner above all — reads an *edited*
structure with no changes of its own, through `seq::toMusicalStructure`. That is the seam the
generation half of the brief plugs into (ADR-216).
