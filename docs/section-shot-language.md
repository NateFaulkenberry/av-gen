# The Section and Shot Language

What a section is, what a shot intent is, why they are two things, and how a person adds their own.
Written against the code as of 2026-09-16; the decision behind it is ADR-247, which builds on
ADR-206, ADR-215 and ADR-216.

## The shape of it

```
  analysis::SongStructure  ──▶  song::SectionTimeline  ──▶  song::SectionCue  ──▶  the director
  what the audio contains       what the film is made of     what the camera is told
  (evidence, recomputed)        (authored, persisted)        (no musical labels)
```

Four kinds of object, and the whole design is in keeping them apart:

| | is | may be recomputed | knows about cameras |
|---|---|---|---|
| **Song analysis** | the detector's report about the audio | yes, on every Analyze | no |
| **Section** | one passage of the film, with a type and a span | no | no |
| **Section type** | what a person says a passage *is* | no | no |
| **Shot intent** | the cinematic treatment a passage gets | no | **no** |
| **Director** | which camera does what, and when | n/a | yes |

The last row of the third column is the load-bearing one. A shot intent describes a treatment
without naming a camera, a lens, a position or a duration, which is what allows the same song to be
cut twice and get two different films.

## A section

```cpp
struct Section {
    SectionTypeId type;                     // "chorus", "ocean_ambience"
    std::string label;                      // a person's own name; empty means "compose one"
    double startSeconds, endSeconds;        // never rounded, at any point
    std::optional<ShotIntentId> shotIntent; // empty means "this type's default"
    bool authored; SectionField edited;     // provenance, per field
    float energy, density;                  // measured from the audio under this span
    float labelConfidence, startConfidence, endConfidence;
    int repetitionGroup, occurrence;
};
```

**A section is not a musical structure.** It is a general audiovisual timeline semantic. Verse and
Chorus are in the vocabulary, and so are Ocean Ambience, Dream Sequence, City Flyover and Credits.
The analyzer generally produces musical categories; a person replaces them with whatever they like,
and nothing downstream behaves differently.

**Repeats are occurrences, not types.** `Verse`, `Verse 2` and `Verse 3` are one type and three
occurrences of it. `occurrence` is derived from the timeline's order, rebuilt after every edit, and
never stored — so splitting a verse renumbers the rest without anybody writing code to do it.

**`label` is a person's name, and only ever a person's name.** The detector's own guess is *not*
copied into it, because a label a person did not type is not a person's name for the passage — and
copying it would mark every section as renamed and freeze the whole timeline against the next
re-analysis.

## A section type

```cpp
struct SectionType {
    SectionTypeId id;                 // stable, lowercase, snake_case, in the project file
    std::string name, description;
    SectionCategory category;         // Structural | Energy | Texture | Cinematic | Custom
    ShotIntentId defaultShotIntent;   // the treatment a section of this type gets by default
    bool builtIn;
};
```

An id is a string rather than an enumerator, for two reasons that are both about the same thing: a
user-defined type cannot be an enumerator, and a project saved today must still name the same type in
a year. The built-in ids are frozen and pinned by a literal list in `tests/unit/test_shot_language.cpp`,
so renaming one is a test failure rather than a silent breakage of every project that used it.

**A type points at a treatment; it does not contain one.** A type does not carry its own energy,
movement or cut-frequency dials — those live once, on the shot intent. A person who wants different
dials defines a different intent. The cost is that a custom look is two steps rather than one, and
that cost is deliberate: a type with nine dials of its own would give every question two places to
look and two ways to disagree.

### The built-in vocabulary — sixty types

**Structural** — Intro, Verse, Pre-Chorus, Chorus, Post-Chorus, Refrain, Bridge, Middle 8, Outro,
Interlude, Instrumental, Hook, Tag, Phrase.

**Energy / arrangement** — Build, Build-Up, Rise, Riser, Drop, Impact, Peak, Climax, Release,
Transition, Pause, Stop, Break, Breakdown, Swell, Crescendo, Decrescendo.

**Texture / musical character** — Ambient, Atmospheric, Drone, Soundscape, Groove, Beat, Percussion,
Vocal, Vocal Break, Solo, Acapella, Spoken Word, Rap, Ad-Lib.

**Visual / cinematic** — Dream Sequence, Montage, Exploration, Reflection, Suspense, Calm, Tension,
Celebration, Finale, Scene Change, Character Introduction, Character Reveal, Establishing, Action,
Credits. These are never produced by the analyzer and are entirely valid to author.

Near-synonyms are all present — Build and Build-Up, Rise and Riser, Peak and Climax, Swell and
Crescendo — because a person reaching for the word "Riser" should find it rather than be told the
correct word is "Rise". They share a default treatment, and that is the whole of what being a synonym
costs.

**Phrase** is the one that is not in the brief's lists, and it is the most important. The detector is
allowed to answer "I do not know" (`analysis::SectionFunction::Other`) and a film is not. `phrase` —
"an ordinary passage" — is what "I do not know" becomes once something has to point a camera. Nothing
else maps to it, so a `phrase` section always means exactly that.

## A shot intent

```cpp
struct ShotIntent {
    ShotIntentId id; std::string name, description;
    SubjectFocus focus;      // Hero | Ensemble | Environment | Mixed
    float focusStrength;     // 0: the subject is a suggestion. 1: never leave them.
    FramingRange framing;    // a band of shot sizes, not one size
    float movement;          // 0: locked off. 1: always travelling.
    float energy;            // how hard the treatment pushes
    float variation;         // 0: every shot alike. 1: every shot different.
    float cutFrequency;      // 0: one shot for the section. 1: as fast as the cadence allows.
    float visualDensity;     // 0: one thing in frame. 1: a full frame.
    CameraCount cameras;     // fewest..most; most = 0 means "as many as there are"
    Arc arc;                 // how all of the above travel across the section
};
```

Every float is 0..1 and every one is a *preference*. A director is free to ignore any of them; what
it cannot do is go looking for the section type behind them, because it is never given one.

**Framing is a band, not a size.** "Intro is wide" is exactly the kind of semantic rule this design
refuses. An intent says which shot sizes read correctly for a passage and the director picks one —
and a person who wants an extreme close-up on their intro can simply have it.

**Out-of-range values are refused, not clamped.** ADR-225's rule: a clamped 1.7 is indistinguishable
from an authored 1.0 for the rest of the project's life.

### Arc — why this is not a collection of musical rules

"Build → progressively increase movement", "Riser → movement accumulates toward the boundary",
"Drop → a strong transition event", "Pause → hold or suspend" are four musical rules. Here they are
one generic parameter:

| Arc | What it does | Used by |
|---|---|---|
| `Steady` | the dials mean what they say, throughout | most treatments |
| `Rising` | they arrive at their stated values at the end | Build, Riser, Swell, Pre-Chorus |
| `Falling` | they hold at the start and decay | Outro, Release, Decrescendo |
| `Suspended` | held, with movement and cutting pinned down | Pause, Stop |
| `Burst` | the stated values land on the first frame and settle | Drop, Impact, Transition |

`ShotIntent::atProgress(t)` applies the arc to `movement`, `energy`, `variation` and `cutFrequency`.
It never touches framing, focus, density or camera count, because those describe what a passage *is*
and do not travel.

A director reading `Rising` never learns that risers exist. A custom type called "Ocean Swell" that
points at a `Rising` treatment gets the behaviour for free, which a hard-coded rule about risers could
never have given it.

### The thirty-one built-in treatments

The brief's table, and the ones its type list implies:

| Section | Default treatment |
|---|---|
| Intro, Establishing | Atmospheric Establishing |
| Verse | Hero / Character Coverage |
| Pre-Chorus | Building Tension |
| Chorus, Hook | Dynamic Hero Coverage |
| Post-Chorus | Dynamic Alternate Coverage |
| Bridge, Middle 8 | Visual Departure |
| Outro, Release, Tag, Decrescendo | Slow Pullback / Resolution |
| Build, Build-Up, Swell, Crescendo | Increasing Movement |
| Rise, Riser | Rising / Reveal |
| Drop, Character Reveal | Dramatic Reveal / Impact |
| Break, Breakdown, Reflection | Intimate / Restrained |
| Ambient, Atmospheric, Soundscape | Slow Environmental Exploration |
| Instrumental, Interlude | Environmental / Performance Exploration |
| Solo, Refrain | Hero Performance |
| Acapella, Vocal Break | Intimate Close-Up |
| Peak, Climax, Finale | Large-Scale Dynamic Coverage |
| Impact | Immediate Dramatic Framing |
| Pause, Stop | Suspended / Locked-Off |
| Montage | Rapid Multi-Shot Coverage |
| Dream Sequence | Floating / Unconventional Exploration |
| Exploration | Free-Roaming Environment |
| Celebration | Dynamic Multi-Subject Coverage |
| Transition, Scene Change | Hard Transition |
| Suspense, Tension | Held Tension |
| Calm, Drone | Calm / Stillness |
| Character Introduction | Character Introduction |
| Groove, Beat, Percussion | Groove Coverage |
| Vocal, Spoken Word, Rap, Ad-Lib | Vocal Focus |
| Action | Action Coverage |
| Credits | Detached Observation |
| Phrase | Steady Coverage |

**These are defaults, not rules.** Every one of them is overridable on any single section, and the
type-level default is overridable for a whole project.

## Two kinds of override

**Per section.** `Section::shotIntent` — "this chorus is an intimate close-up". Set it with
`setSectionShotIntent`, drop it with `clearSectionShotIntent`.

**Per project.** Define a custom type with a built-in's id and it shadows the built-in for that
project — "every Intro in this piece is an extreme close-up", rather than choosing it eleven times.
Removing the custom restores the built-in, so a shadow is reversible and destroys nothing.

A section that stores *no* intent resolves through whatever type it currently has. That is why
"changing the type updates the default, unless I chose a treatment" needs no rule implementing it:
the absence is what is stored, so the answer follows the type automatically and an override survives
a retype.

## Making your own

```cpp
ShotIntent tidal;
tidal.id = "tidal_drift";              // song::makeId("Tidal Drift") does this from a display name
tidal.name = "Tidal Drift";
tidal.focus = SubjectFocus::Environment;
tidal.movement = 0.2f; tidal.cutFrequency = 0.05f; tidal.arc = Arc::Steady;
language.defineIntent(tidal);

language.defineType(SectionType{"ocean_ambience", "Ocean Ambience",
                                "Slow underwater environment passage",
                                SectionCategory::Custom, "tidal_drift", false});
```

`defineType` **refuses** a type whose default treatment is not defined. A type pointing at nothing
would produce a section with no treatment and no error anywhere, which looks exactly like the feature
not working.

Custom definitions live in `sequence.shotLanguage` and go in the project file. Only customs are
written: the built-in library is code, so a piece that defined nothing serializes nothing.

## What the director gets

```cpp
struct SectionCue {
    int index; double startSeconds, endSeconds;
    ShotIntent intent;          // resolved by value: override, else type default, else neutral
    float energy, density;      // measured, 0..1, against the track's own range
    int occurrence;
    bool finalOfKind;           // the last section of this type in the piece
    std::string displayName;    // FOR DISPLAY AND LOGS. Never switch on this.
    float progressAt(double seconds) const;
    ShotIntent intentAt(double seconds) const;   // the intent with its arc applied
};

std::vector<SectionCue> cueSheet(const SectionTimeline&, const ShotLanguage&);
```

**There is no section type in it.** Not as an id, not as a pointer, not as an enum. That is the point:
"the director must not depend on musical labels like Chorus" becomes a property of the type rather
than a promise somebody has to keep, and a director written against `song/section_cue.hpp` treats
`ocean_ambience` exactly as it treats `chorus` because it structurally cannot tell them apart.

`finalOfKind` is how "the last chorus is a different shot from the first" survives without a
`FinalChorus` enumerator. `signals::MusicalSection` needed three such enumerators — FinalBuild,
FinalDrop, FinalChorus — because its vocabulary is closed. A boolean derived from position works for
the last Ocean Ambience too.

The intent is carried **by value**, not as a pointer into the registry. A pointer would have been
cheaper and would dangle the moment anybody defined one more intent — so a cue is self-contained and
a director may hold one across a frame, a job boundary or an edit to the language.

`cueSheet` is pure: no clock, no randomness, no I/O. The same timeline and language give the same
sheet on every machine and every run, which is what lets a render reproduce a preview.

## Where the code is

| File | What is in it |
|---|---|
| `src/song/shot_intent.hpp` | `ShotIntent`, the dials, `Arc`, the 31 built-ins. **Depends on nothing.** |
| `src/song/section_type.hpp` | `SectionType`, `SectionCategory`, the 60 built-ins |
| `src/song/section_timeline.hpp` | `Section`, `SectionTimeline`, provenance, and every editing operation |
| `src/song/shot_language.hpp` | `ShotLanguage` — the registry, custom definitions, resolution |
| `src/song/section_cue.hpp` | `SectionCue`, `cueSheet` — the director's whole view |
| `src/song/from_analysis.hpp` | the analysis bridge, and `applyAnalysis` |
| `src/song/reanalysis.hpp` | the re-analysis policy — see `docs/song-analyzer.md` |

Nothing in `song/` includes anything from `app/`, `gpu/`, `rendering/` or `scene/`. Only
`from_analysis` includes `analysis/`. The director's two includes are `shot_intent.hpp` and
`section_cue.hpp`, and neither of them can reach a section type.
