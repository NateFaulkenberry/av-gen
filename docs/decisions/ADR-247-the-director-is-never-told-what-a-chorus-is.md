# ADR-247 — The director is never told what a chorus is

**Status:** accepted · 2026-09-16
**Builds on:** ADR-206 (structure from repetition), ADR-215 (a section somebody decided outranks a
section somebody detected), ADR-216 (the sequencer is where the song lives), ADR-210 (the director is
a decision layer), ADR-225 (a setting nobody keeps)

## Context

A brief asking for a song-aware shot language: import a song, analyze it, get sections, give every
section an appropriate cinematic treatment, let the Auto-director execute that treatment across the
available cameras. It must work for Verse and Chorus **and** for "Ocean Ambience", "Dream Sequence"
and "Character Reveal", and it states the failure it is trying to avoid in one line:

> The system should not think `Chorus = Camera 3`.

The repository already had four fifths of the pipeline. ADR-206 built a real structure detector.
ADR-215 made its output editable, persistent, and safe to re-run. ADR-216 put it in the sequencer and
made analysis a background job. ADR-245 built multi-camera. `app::cinematic` has had a shot vocabulary
since ADR-062. What was missing was the layer in the middle: something between "this passage is a
chorus" and "the camera orbits at twelve metres".

Three things made that gap real rather than cosmetic:

1. **`analysis::SectionFunction` is closed.** It is an enum of twelve musical functions plus `Other`.
   "Ocean Ambience" cannot be one, and adding it would be a category error — it is not a claim about
   the music, and a detector must never be in a position to produce or overwrite it.
2. **`signals::MusicalSection` is also closed**, and it is what the shot planner switches on. Every
   route from a section to a camera therefore ran through a fifteen-value musical enum, which is
   `Chorus = Camera 3` with more steps.
3. **Provenance was per section.** ADR-215 says so and says it is deliberately over-protective. That
   is right for a model with only boundaries and labels. It is too blunt once a section also has a
   type and a treatment: renaming a passage would freeze its boundaries for ever.

## Decision

A new layer, `src/song/`, namespace `avgen::song`. Three layers, stated:

```
  audio ──▶ analysis::SongStructure ──▶ song::SectionTimeline ──▶ song::SectionCue ──▶ director
            (evidence, recomputed)      (the film, authored)      (no musical labels)
```

### 1. The analysis and the film are not the same object

`analysis::SongStructure` goes back to being what its name says: the detector's report about the
audio, with confidences, replaced whenever somebody presses Analyze. `song::SectionTimeline` is the
film: what a person says each passage is and how it should be treated.

The brief is explicit that these must not be collapsed, and the reason becomes visible the moment
somebody types "Ocean Ambience". That is not a musical claim, it is a decision about a film, and the
two need different lifetimes because only one of them is safe to recompute.

Both live on `seq::Sequence` and both go in the project, in `sequence.sectionTimeline` and
`sequence.shotLanguage`. Written only when non-empty, so a piece that never analyzed a song and never
defined a type serializes exactly the file it had.

### 2. A section type is a string, not an enumerator

`SectionTypeId` is a stable lowercase id. Sixty built-ins ship — the brief's four lists, implemented
as written, near-synonyms included. A person may define their own, and a custom type takes **the
identical path through every function in the namespace**. There is no `if (builtIn)` anywhere.

That claim is not a promise; it is `tests/unit/test_shot_language.cpp`, where the assertions the
built-ins are held to are factored into `assertTypeIsUsable()` and run a second time against three
types that did not exist when the engine was compiled. If anything ever special-cases the structural
vocabulary, the custom half fails and the built-in half does not.

**Repeats are occurrences, not types.** `Verse`, `Verse 2`, `Verse 3` are one type and three
occurrences; `Section::occurrence` is derived from the timeline's order and never stored.

**"Final" is a property of position, not of the vocabulary.** `analysis::SectionFunction::FinalChorus`
maps to the plain `chorus` type, and `SectionCue::finalOfKind` is computed. `signals::MusicalSection`
needed `FinalBuild`, `FinalDrop` *and* `FinalChorus` as separate enumerators because its vocabulary is
closed; a boolean derived from position costs one line and works for the last Ocean Ambience, which a
fourth enumerator never could.

**`Other` becomes `phrase`, once, in one place.** The detector is allowed to answer "I do not know"
and a film is not. `song::sectionTypeForFunction` has no `default:`, so a new analyzer function is a
compile error at the site of the decision rather than a silent `phrase`.

### 3. A shot intent is semantic, and it is the *only* thing a director sees

`ShotIntent` is a named treatment carrying eight preference dials, a subject focus, a framing band, a
camera count and an arc. Not `Camera 2, 50mm, 8 seconds`. Thirty-one ship.

A type points at an intent; it does not contain one. The brief asks whether a type should also carry
energy, movement, camera distance and cut frequency. The answer is no, and it is an answer rather
than an omission: those dials already exist once, on the intent, and a type carrying a second copy
would give every question two places to look. A person who wants different dials wants a different
*treatment*, and `defineIntent` is as available as `defineType`. **The cost is that a custom look is
two steps rather than one**, and it is recorded here rather than discovered later.

### 4. `Arc` is why this is not a rule engine

The brief asks for "Build → progressively increase movement", "Riser → movement accumulates toward
the boundary", "Drop → a strong transition event", "Pause → hold or suspend". Four musical rules.

They are one generic parameter. An intent declares `Rising`, `Falling`, `Suspended`, `Burst` or
`Steady`, and `ShotIntent::atProgress(t)` applies it to movement, energy, variation and cut frequency
— never to framing, focus, density or camera count, because those describe what a passage *is* and do
not travel. A director reading `Rising` never learns that risers exist, and a custom type called
"Ocean Swell" gets the behaviour for free.

### 5. The director is handed a projection with no section type in it

This is the decision the ADR is named for, and it is the one that was easy to agree to and would have
been hard to keep. The section object is right there, and switching on its type is always the
shortest path to a result.

So the director is not given the section object. `song::SectionCue` has **no `SectionTypeId` field,
no `SectionType` pointer and no musical enum**:

```cpp
struct SectionCue {
    int index; double startSeconds, endSeconds;
    ShotIntent intent;                 // resolved, by value
    float energy, density;             // measured, 0..1
    int occurrence; bool finalOfKind;
    std::string displayName;           // FOR DISPLAY AND LOGS. Never switch on this.
    float progressAt(double) const; ShotIntent intentAt(double) const;
};
```

"The Auto-director must not depend on musical labels like Chorus" is therefore a property of the type
rather than a promise somebody has to keep. A director written against `song/section_cue.hpp` and
`song/shot_intent.hpp` **cannot** special-case the structural vocabulary, and therefore treats
`ocean_ambience` exactly as it treats `chorus`.

The intent is carried **by value**. A pointer into `ShotLanguage`'s own vector of custom intents
would have been cheaper and is a use-after-free waiting to happen: defining one more intent
reallocates that vector and dangles every cue already handed out. A cue sheet is tens of entries of
about a hundred bytes, and the copy buys a lifetime rule nobody has to remember.

`displayName` is the one concession and it is for overlays and logs. It is a person's arbitrary
string; on a custom type it is whatever they typed.

### 6. Provenance is per field

`Section::edited` is a mask of `Type | Label | ShotIntent | Start | End`, and `origin()` derives
ADR-215's `Detected | Refined | Authored` from it rather than storing a fourth thing.

Re-analysis replaces every field not in the mask and keeps every field that is. A section retyped to
"Ocean Ambience" with untouched boundaries **keeps its type and takes the fresh boundaries**, which is
exactly what the person asked for and what ADR-215's per-section freeze would have prevented.

One place it stays blunt on purpose: **pinning either boundary pins the whole span.** A section is a
span, and half of one is not something a fresh detection can be carved around. Stated, and tested.

### 7. The re-analysis policy

`analysis result + user overrides = current authored structure`, implemented literally.

1. A fresh detection with **no** sections changes nothing and reports nothing. ADR-215's rule kept:
   refusing to act on empty information beats emptying somebody's timeline.
2. Frozen sections keep their span and every field; fresh sections are trimmed around them, and one a
   frozen section covers completely is dropped.
3. Everything else is replaced, except the fields a person set, which are carried onto the fresh
   section they overlap most.
4. **At most one current section may hand its fields to any one fresh section**, greedily by overlap,
   ties broken by index so the result is a pure function of the inputs. Without that cap, two edited
   sections that a coarser detection merges into one would both write to it and the second would
   silently win — data loss wearing the costume of a successful merge. The loser is counted in
   `discarded` rather than vanishing.

`ReanalysisPolicy::Replace` throws the timeline away. It is never the default, and
`previewReanalysis` is *literally the same call against a copy*, so the confirmation a UI shows
cannot come to disagree with what the button does.

## What this cost, in defects found

**An inverted framing band shipped in the intent table.** `character_introduction` declared
`Medium..Close` — tightest wider than widest. It was caught by the loop in "Every built-in section
type has a valid default shot intent", which validates all sixty rather than spot-checking. A
spot-checking test would have passed, and the intent would have silently failed `validate()` for
anything that ever asked.

**A round-trip test that was asserting the wrong design.** Comparing a whole `Section` across JSON
fails for an *edited* section, because ADR-215's rule — confidences are written only while they still
mean something — is deliberately asymmetric. The test now compares the persisted fields, and the
asymmetry is documented at the assertion rather than papered over with a looser comparison.

## Consequences

- One new directory, `src/song/`, and one word added to the core source glob.
- `seq::Sequence` gains two fields, two JSON blocks and one line in `validate()`.
- `Sequence::fromJson` derives a timeline from `structure` when a project has an analysis and no
  film. That is the only place the two models touch, it is migration, and it cannot lose anything
  because there was nothing authored to lose. A project that *has* a timeline is never re-derived:
  that would be a silent re-analysis on load, which is the one thing this model exists to prevent.
- `SequencePanel::pollStructureAnalysis` updates both layers from one detection, under their two
  policies. The logic is `song::applyAnalysis`, a pure function, so the diff in the shared UI file is
  a handful of lines.

## Not done, and deliberately

**The two editing APIs have not been converged.** `seq/song_structure.hpp`'s `moveBoundary`,
`setSectionFunction`, `splitSection` and `reanalyze` still edit `analysis::SongStructure`, and the
section lane in the Sequencer still drives them. The authored equivalents now exist on
`SectionTimeline` with a richer vocabulary and better provenance. Converging them means moving the
section lane onto the new model, which is a UI change owned elsewhere. **Until that happens a person
editing the section lane is editing the analysis layer, and those edits do not reach the film.** That
is the single most important thing to know about this change, and it is a wart, not a design.

**No `SectionTimeline → signals::MusicalStructure` bridge was written**, though it would have been
four lines. It would have let the existing shot planner consume an edited timeline immediately, and it
would have done so by mapping section types back onto musical labels — undoing the entire point.
Song Mode consumes cues. Continuous Shot and Edited Sequence are untouched and take the path they
always did.
